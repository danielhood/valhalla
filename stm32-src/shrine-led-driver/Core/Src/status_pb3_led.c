/**
 * PB3 LD3 behaviour aligned with shrine-rfid-reader: short heartbeat blink + burst on scans.
 */

#include "status_pb3_led.h"

#include "main.h"

#define STATUS_PB3_PORT    GPIOB
#define STATUS_PB3_PIN     GPIO_PIN_3

#define RX_FLASH_DURATION_MS       500U
#define RX_FLASH_TOGGLE_MS          40U

#define HB_PULSE_MS                100U
#define HB_INTERVAL_MS            1000U

static volatile uint32_t s_rx_flash_deadline_ms;
static volatile uint32_t s_rx_flash_next_toggle_ms;

static uint32_t s_hb_deadline_ms;
static uint32_t s_hb_phase;

static uint8_t valhalla_tag_well_formed(const valhallaTag *t)
{
  if (t == NULL)
  {
    return 0U;
  }
  if (t->type == '\0')
  {
    return 0U;
  }
  /* Reader `parse_valhalla_csv_text` insists on two-letter camp/color/rune + NUL in 3-byte fields. */
  if ((t->camp[0] == '\0') || (t->camp[1] == '\0') || (t->camp[2] != '\0'))
  {
    return 0U;
  }
  if ((t->color[0] == '\0') || (t->color[1] == '\0') || (t->color[2] != '\0'))
  {
    return 0U;
  }
  if ((t->rune[0] == '\0') || (t->rune[1] == '\0') || (t->rune[2] != '\0'))
  {
    return 0U;
  }

  return 1U;
}

static uint8_t snapshot_has_well_formed_tag(const valhallaTag *tags)
{
  uint32_t i;

  if (tags == NULL)
  {
    return 0U;
  }

  for (i = 0U; i < SHRINE_LED_VALHALLA_TAG_MAX; i++)
  {
    if (valhalla_tag_well_formed(&tags[i]) != 0U)
    {
      return 1U;
    }
  }
  return 0U;
}

static void rx_flash_trigger(void)
{
  uint32_t n;

  n = HAL_GetTick();
  /* Reader restarts pulse on overlapping triggers; mimic that (extend activity window). */
  s_rx_flash_deadline_ms    = n + RX_FLASH_DURATION_MS;
  s_rx_flash_next_toggle_ms = n;
}

void status_pb3_led_init(void)
{
  s_rx_flash_deadline_ms    = 0U;
  s_rx_flash_next_toggle_ms = 0U;
  s_hb_deadline_ms          = 0U;
  s_hb_phase                = 0U;
}

uint8_t status_pb3_led_flash_is_active(void)
{
  return (s_rx_flash_deadline_ms != 0U) ? 1U : 0U;
}

void status_pb3_led_systick_hook(void)
{
  uint32_t deadline;
  uint32_t now;

  deadline = s_rx_flash_deadline_ms;
  if (deadline == 0U)
  {
    return;
  }

  now = HAL_GetTick();

  if ((int32_t)(now - deadline) >= 0)
  {
    s_rx_flash_deadline_ms    = 0U;
    s_rx_flash_next_toggle_ms = 0U;
    HAL_GPIO_WritePin(STATUS_PB3_PORT, STATUS_PB3_PIN, GPIO_PIN_RESET);
    return;
  }

  if ((int32_t)(now - s_rx_flash_next_toggle_ms) >= 0)
  {
    HAL_GPIO_TogglePin(STATUS_PB3_PORT, STATUS_PB3_PIN);
    s_rx_flash_next_toggle_ms = now + RX_FLASH_TOGGLE_MS;
  }
}

void status_pb3_led_poll(uint32_t now_ms)
{
  if (status_pb3_led_flash_is_active() != 0U)
  {
    return;
  }

  if ((int32_t)(now_ms - s_hb_deadline_ms) < 0)
  {
    return;
  }

  if (s_hb_phase == 0U)
  {
    HAL_GPIO_WritePin(STATUS_PB3_PORT, STATUS_PB3_PIN, GPIO_PIN_SET);
    s_hb_deadline_ms = now_ms + HB_PULSE_MS;
    s_hb_phase       = 1U;
  }
  else
  {
    HAL_GPIO_WritePin(STATUS_PB3_PORT, STATUS_PB3_PIN, GPIO_PIN_RESET);
    s_hb_deadline_ms = now_ms + HB_INTERVAL_MS;
    s_hb_phase       = 0U;
  }
}

void status_pb3_led_on_i2c_snapshot_if_valid(const valhallaTag *tags)
{
  if (snapshot_has_well_formed_tag(tags) != 0U)
  {
    rx_flash_trigger();
  }
}
