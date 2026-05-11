/**
 * PWM RGB strips react to tag colour codes. Duty cycle is proportional to channel
 * brightness: zero compare → dark; full-scale compare → bright (Cube PWM1, active-high).
 */

#include "valhalla_rgb_strip.h"

#include <string.h>

#if SHRINE_LED_VALHALLA_TAG_MAX < 4U
#error "valhalla_rgb_strip mapping uses tag indices 0..3; raise SHRINE_LED_VALHALLA_TAG_MAX or update strip_from_two_slots callers"
#endif

#ifndef VALHALLA_RGB_STRIP_PWM_FULL_SCALE
/** Must match `htim.Init.Period` in MX_TIMx_Init() (exclusive range 0..Period for compare). */
#define VALHALLA_RGB_STRIP_PWM_FULL_SCALE (999U)
#endif

#ifndef VALHALLA_RGB_STRIP_DUAL_PHASE_PERIOD_MS
/** When two tags compete for one strip, show each colour for this duration before alternating. */
#define VALHALLA_RGB_STRIP_DUAL_PHASE_PERIOD_MS (10000U)
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

static void strip_from_two_slots(const valhallaTag *tags,
                                 uint8_t idx_a,
                                 uint8_t idx_b,
                                 TIM_HandleTypeDef *htim,
                                 int is_tim1,
                                 uint32_t now_ms)
{
  uint8_t ra, ga, ba;
  uint8_t rb, gb, bb;
  int ok_a;
  int ok_b;

  ok_a = tag_colour_to_rgb(&tags[idx_a], &ra, &ga, &ba);
  ok_b = tag_colour_to_rgb(&tags[idx_b], &rb, &gb, &bb);

  if ((!ok_a) && (!ok_b))
  {
    strip_write_rgb(htim, is_tim1, 0U, 0U, 0U);
    return;
  }
  if (ok_a && (!ok_b))
  {
    strip_write_rgb(htim, is_tim1, ra, ga, ba);
    return;
  }
  if ((!ok_a) && ok_b)
  {
    strip_write_rgb(htim, is_tim1, rb, gb, bb);
    return;
  }

  {
    uint32_t phase = (now_ms / VALHALLA_RGB_STRIP_DUAL_PHASE_PERIOD_MS) & 1U;
    if (phase == 0U)
    {
      strip_write_rgb(htim, is_tim1, ra, ga, ba);
    }
    else
    {
      strip_write_rgb(htim, is_tim1, rb, gb, bb);
    }
  }
}

void valhalla_rgb_strip_apply(const valhallaTag *tags,
                              TIM_HandleTypeDef *htim1,
                              TIM_HandleTypeDef *htim2,
                              uint32_t now_ms)
{
  if ((tags == NULL) || (htim1 == NULL) || (htim2 == NULL))
  {
    return;
  }

  /* Indices 0,2 → TIM1; indices 1,3 → TIM2 (match SHRINE_LED_VALHALLA_TAG_MAX = 4 layout). */
  strip_from_two_slots(tags, 0U, 2U, htim1, 1, now_ms);
  strip_from_two_slots(tags, 1U, 3U, htim2, 0, now_ms);
}
