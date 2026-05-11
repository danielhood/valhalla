/**
 * PWM RGB strips react to tag colour codes. Duty cycle is proportional to channel
 * brightness: zero compare → dark; full-scale compare → bright (Cube PWM1, active-high).
 */

#include "valhalla_rgb_strip.h"

#include "valhalla_i2c_slave.h"

#include <string.h>

#if SHRINE_LED_VALHALLA_TAG_MAX < 4U
#error "valhalla_rgb_strip mapping uses tag indices 0..3; raise SHRINE_LED_VALHALLA_TAG_MAX or update strip_from_two_slots callers"
#endif

#ifndef VALHALLA_RGB_STRIP_PWM_FULL_SCALE
/** Must match `htim.Init.Period` in MX_TIMx_Init() (exclusive range 0..Period for compare). */
#define VALHALLA_RGB_STRIP_PWM_FULL_SCALE (999U)
#endif

#ifndef VALHALLA_RGB_STRIP_DUAL_TRANSITION_MS
/** When two tags share one strip, linearly crossfade A→B and B→A; each leg takes this many ms (full cycle = 2×). */
#define VALHALLA_RGB_STRIP_DUAL_TRANSITION_MS (10000U)
#endif

#ifndef VALHALLA_RGB_STRIP_FADE_MS
/** Fade duration for off↔solid and when leaving dual for a steady (non-dual) target; default matches dual leg. */
#define VALHALLA_RGB_STRIP_FADE_MS (VALHALLA_RGB_STRIP_DUAL_TRANSITION_MS)
#endif

typedef struct
{
  const char code[3];
  uint8_t r;
  uint8_t g;
  uint8_t b;
} color_entry_t;

static const color_entry_t s_colors[] = {
    {"RD", 255U, 0U, 0U},
    {"DR", 139U, 0U, 0U},
    {"OR", 255U, 128U, 0U},
    {"YL", 255U, 255U, 0U},
    {"GN", 0U, 255U, 0U},
    {"PR", 148U, 0U, 211U},
    {"BL", 0U, 0U, 255U},
};

static uint32_t rgb_byte_to_compare(uint8_t v)
{
  uint32_t c = ((uint32_t)v * VALHALLA_RGB_STRIP_PWM_FULL_SCALE) / 255U;
  if (c > VALHALLA_RGB_STRIP_PWM_FULL_SCALE)
  {
    c = VALHALLA_RGB_STRIP_PWM_FULL_SCALE;
  }
  return c;
}

static int tag_colour_to_rgb(const valhallaTag *t, uint8_t *pr, uint8_t *pg, uint8_t *pb)
{
  size_t k;

  if ((t == NULL) || (pr == NULL) || (pg == NULL) || (pb == NULL))
  {
    return 0;
  }
  if ((t->color[0] == '\0') || (t->color[1] == '\0'))
  {
    return 0;
  }
  if (t->color[2] != '\0')
  {
    return 0;
  }

  for (k = 0U; k < (sizeof(s_colors) / sizeof(s_colors[0])); k++)
  {
    if (memcmp(t->color, s_colors[k].code, 3) == 0)
    {
      *pr = s_colors[k].r;
      *pg = s_colors[k].g;
      *pb = s_colors[k].b;
      return 1;
    }
  }

  return 0;
}

/** Order dual-tag blend endpoints by packed RGB so slot/order noise does not swap colours frame-to-frame. */
static void sort_rgb_endpoints(uint8_t *ra,
                               uint8_t *ga,
                               uint8_t *ba,
                               uint8_t *rb,
                               uint8_t *gb,
                               uint8_t *bb)
{
  uint32_t pa = ((uint32_t)*ra << 16) | ((uint32_t)*ga << 8) | (uint32_t)*ba;
  uint32_t pb = ((uint32_t)*rb << 16) | ((uint32_t)*gb << 8) | (uint32_t)*bb;

  if (pa > pb)
  {
    uint8_t tr;
    uint8_t tg;
    uint8_t tb;

    tr = *ra;
    *ra = *rb;
    *rb = tr;
    tg = *ga;
    *ga = *gb;
    *gb = tg;
    tb = *ba;
    *ba = *bb;
    *bb = tb;
  }
}

static void strip_write_rgb(TIM_HandleTypeDef *htim, int is_tim1, uint8_t r, uint8_t g, uint8_t b)
{
  uint32_t cr = rgb_byte_to_compare(r);
  uint32_t cg = rgb_byte_to_compare(g);
  uint32_t cb = rgb_byte_to_compare(b);

  __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, cr);
  __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_2, cg);
  if (is_tim1)
  {
    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_3, cb);
  }
  else
  {
    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_4, cb);
  }
}

typedef struct
{
  uint8_t disp_r;
  uint8_t disp_g;
  uint8_t disp_b;
  uint8_t fade_fr;
  uint8_t fade_fg;
  uint8_t fade_fb;
  uint8_t goal_r;
  uint8_t goal_g;
  uint8_t goal_b;
  uint32_t fade_t0_ms;
  uint8_t was_dual;
  uint8_t dual_entry_active;
  uint32_t dual_entry_t0_ms;
} strip_anim_t;

static void strip_compute_instant(const valhallaTag *tags,
                                  uint8_t idx_a,
                                  uint8_t idx_b,
                                  uint32_t now_ms,
                                  uint8_t *out_r,
                                  uint8_t *out_g,
                                  uint8_t *out_b,
                                  int *out_dual)
{
  uint8_t ra, ga, ba;
  uint8_t rb, gb, bb;
  int ok_a;
  int ok_b;

  ok_a = tag_colour_to_rgb(&tags[idx_a], &ra, &ga, &ba);
  ok_b = tag_colour_to_rgb(&tags[idx_b], &rb, &gb, &bb);

  *out_dual = 0;

  if ((!ok_a) && (!ok_b))
  {
    *out_r = 0U;
    *out_g = 0U;
    *out_b = 0U;
    return;
  }
  if (ok_a && (!ok_b))
  {
    *out_r = ra;
    *out_g = ga;
    *out_b = ba;
    return;
  }
  if ((!ok_a) && ok_b)
  {
    *out_r = rb;
    *out_g = gb;
    *out_b = bb;
    return;
  }

  {
    uint32_t period = 2U * VALHALLA_RGB_STRIP_DUAL_TRANSITION_MS;
    uint32_t pos = now_ms % period;
    uint32_t w_a;
    uint32_t w_b;

    *out_dual = 1;
    sort_rgb_endpoints(&ra, &ga, &ba, &rb, &gb, &bb);

    if (pos < VALHALLA_RGB_STRIP_DUAL_TRANSITION_MS)
    {
      w_b = pos;
      w_a = VALHALLA_RGB_STRIP_DUAL_TRANSITION_MS - pos;
    }
    else
    {
      uint32_t pos2 = pos - VALHALLA_RGB_STRIP_DUAL_TRANSITION_MS;

      w_a = pos2;
      w_b = VALHALLA_RGB_STRIP_DUAL_TRANSITION_MS - pos2;
    }

    *out_r = (uint8_t)(((uint32_t)ra * w_a + (uint32_t)rb * w_b) / VALHALLA_RGB_STRIP_DUAL_TRANSITION_MS);
    *out_g = (uint8_t)(((uint32_t)ga * w_a + (uint32_t)gb * w_b) / VALHALLA_RGB_STRIP_DUAL_TRANSITION_MS);
    *out_b = (uint8_t)(((uint32_t)ba * w_a + (uint32_t)bb * w_b) / VALHALLA_RGB_STRIP_DUAL_TRANSITION_MS);
  }
}

static void strip_apply_faded(strip_anim_t *st,
                              TIM_HandleTypeDef *htim,
                              int is_tim1,
                              uint32_t now_ms,
                              uint8_t ins_r,
                              uint8_t ins_g,
                              uint8_t ins_b,
                              int is_dual)
{
  uint8_t out_r;
  uint8_t out_g;
  uint8_t out_b;
  uint32_t den;
  uint32_t elapsed;

  if (is_dual)
  {
    if ((!st->was_dual) && (st->disp_r == 0U) && (st->disp_g == 0U) && (st->disp_b == 0U))
    {
      st->dual_entry_active = 1U;
      st->dual_entry_t0_ms = now_ms;
    }

    if (st->dual_entry_active != 0U)
    {
      elapsed = now_ms - st->dual_entry_t0_ms;
      den = VALHALLA_RGB_STRIP_FADE_MS;
      if (elapsed >= den)
      {
        st->dual_entry_active = 0U;
        out_r = ins_r;
        out_g = ins_g;
        out_b = ins_b;
      }
      else
      {
        out_r = (uint8_t)(((uint32_t)ins_r * elapsed) / den);
        out_g = (uint8_t)(((uint32_t)ins_g * elapsed) / den);
        out_b = (uint8_t)(((uint32_t)ins_b * elapsed) / den);
      }
    }
    else
    {
      out_r = ins_r;
      out_g = ins_g;
      out_b = ins_b;
    }

    st->disp_r = out_r;
    st->disp_g = out_g;
    st->disp_b = out_b;
    strip_write_rgb(htim, is_tim1, out_r, out_g, out_b);
    st->was_dual = 1U;
    return;
  }

  st->dual_entry_active = 0U;

  if (st->was_dual != 0U)
  {
    st->fade_fr = st->disp_r;
    st->fade_fg = st->disp_g;
    st->fade_fb = st->disp_b;
    st->goal_r = ins_r;
    st->goal_g = ins_g;
    st->goal_b = ins_b;
    st->fade_t0_ms = now_ms;
    st->was_dual = 0U;
  }
  else if ((ins_r != st->goal_r) || (ins_g != st->goal_g) || (ins_b != st->goal_b))
  {
    st->fade_fr = st->disp_r;
    st->fade_fg = st->disp_g;
    st->fade_fb = st->disp_b;
    st->goal_r = ins_r;
    st->goal_g = ins_g;
    st->goal_b = ins_b;
    st->fade_t0_ms = now_ms;
  }

  den = VALHALLA_RGB_STRIP_FADE_MS;
  elapsed = now_ms - st->fade_t0_ms;
  if (elapsed >= den)
  {
    st->disp_r = st->goal_r;
    st->disp_g = st->goal_g;
    st->disp_b = st->goal_b;
  }
  else
  {
    st->disp_r =
        (uint8_t)(((uint32_t)st->fade_fr * (den - elapsed) + (uint32_t)st->goal_r * elapsed) / den);
    st->disp_g =
        (uint8_t)(((uint32_t)st->fade_fg * (den - elapsed) + (uint32_t)st->goal_g * elapsed) / den);
    st->disp_b =
        (uint8_t)(((uint32_t)st->fade_fb * (den - elapsed) + (uint32_t)st->goal_b * elapsed) / den);
  }

  strip_write_rgb(htim, is_tim1, st->disp_r, st->disp_g, st->disp_b);
}

static void strip_from_two_slots(strip_anim_t *anim,
                                 const valhallaTag *tags,
                                 uint8_t idx_a,
                                 uint8_t idx_b,
                                 TIM_HandleTypeDef *htim,
                                 int is_tim1,
                                 uint32_t now_ms)
{
  uint8_t ir;
  uint8_t ig;
  uint8_t ib;
  int dual;

  strip_compute_instant(tags, idx_a, idx_b, now_ms, &ir, &ig, &ib, &dual);
  strip_apply_faded(anim, htim, is_tim1, now_ms, ir, ig, ib, dual);
}

static strip_anim_t s_anim_tim1;
static strip_anim_t s_anim_tim2;

static TIM_HandleTypeDef *s_bind_htim1;
static TIM_HandleTypeDef *s_bind_htim2;

void valhalla_rgb_strip_bind_timers(TIM_HandleTypeDef *htim1, TIM_HandleTypeDef *htim2)
{
  s_bind_htim1 = htim1;
  s_bind_htim2 = htim2;
}

void valhalla_rgb_strip_systick_hook(void)
{
  if ((s_bind_htim1 == NULL) || (s_bind_htim2 == NULL))
  {
    return;
  }
  valhalla_rgb_strip_apply(valhalla_i2c_get_last_tags(), s_bind_htim1, s_bind_htim2, HAL_GetTick());
}

void valhalla_rgb_strip_apply(const valhallaTag *tags,
                              TIM_HandleTypeDef *htim1,
                              TIM_HandleTypeDef *htim2,
                              uint32_t now_ms)
{
  valhallaTag snap[SHRINE_LED_VALHALLA_TAG_MAX];

  if ((tags == NULL) || (htim1 == NULL) || (htim2 == NULL))
  {
    return;
  }

  __disable_irq();
  memcpy(snap, tags, sizeof(snap));
  __enable_irq();

  /* Indices 0,2 → TIM1; indices 1,3 → TIM2 (match SHRINE_LED_VALHALLA_TAG_MAX = 4 layout). */
  strip_from_two_slots(&s_anim_tim1, snap, 0U, 2U, htim1, 1, now_ms);
  strip_from_two_slots(&s_anim_tim2, snap, 1U, 3U, htim2, 0, now_ms);
}
