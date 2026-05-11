#include "valhalla_led_i2c.h"

#include "driver_mfrc522_interface.h"
#include "valhalla_tag.h"

#include <string.h>

#if defined(__GNUC__)
_Static_assert(SHRINE_LED_VALHALLA_TAG_MAX == MFRC522_INTERFACE_MAX_DEVICES,
               "I2C payload slots must match MFRC522_INTERFACE_MAX_DEVICES");
_Static_assert(VALHALLA_TAGS_I2C_PAYLOAD_BYTES == sizeof(valhallaTag) * MFRC522_INTERFACE_MAX_DEVICES,
               "I2C payload byte count");
#endif

#define VALHALLA_LED_I2C_ADDR_7BIT 0x10U
#define VALHALLA_LED_I2C_HAL_ADDR ((uint16_t)((VALHALLA_LED_I2C_ADDR_7BIT) << 1))
#define VALHALLA_LED_I2C_TIMEOUT_MS 100U

extern void main_debug_print(const char *const fmt, ...);

static I2C_HandleTypeDef *s_hi2c;
static const uint8_t *s_tag_blob;
static size_t s_tag_blob_len;
static uint8_t s_shadow[sizeof(valhallaTag) * MFRC522_INTERFACE_MAX_DEVICES];
static uint8_t s_have_init;

void valhalla_led_i2c_init(I2C_HandleTypeDef *hi2c, const uint8_t *tag_blob, size_t tag_blob_len)
{
  s_hi2c = hi2c;
  s_tag_blob = tag_blob;
  s_tag_blob_len = tag_blob_len;
  s_have_init = 0U;

  if (hi2c == NULL || tag_blob == NULL)
  {
    return;
  }

  if (tag_blob_len != (size_t)VALHALLA_TAGS_I2C_PAYLOAD_BYTES)
  {
    main_debug_print("valhalla_led_i2c_init: tag_blob_len %u != expected %u\r\n",
                     (unsigned int)tag_blob_len, (unsigned int)VALHALLA_TAGS_I2C_PAYLOAD_BYTES);
    s_hi2c = NULL;
    s_tag_blob = NULL;
    s_tag_blob_len = 0U;
    return;
  }

  memset(s_shadow, 0, sizeof(s_shadow));
  s_have_init = 1U;
}

void valhalla_led_i2c_push_if_changed(void)
{
  HAL_StatusTypeDef st;

  if (s_have_init == 0U || s_hi2c == NULL || s_tag_blob == NULL || s_tag_blob_len == 0U)
  {
    return;
  }

  if (memcmp(s_tag_blob, s_shadow, s_tag_blob_len) == 0)
  {
    return;
  }

  st = HAL_I2C_Master_Transmit(s_hi2c, VALHALLA_LED_I2C_HAL_ADDR, (uint8_t *)s_tag_blob,
                               (uint16_t)s_tag_blob_len, VALHALLA_LED_I2C_TIMEOUT_MS);
  if (st != HAL_OK)
  {
    uint32_t e = HAL_I2C_GetError(s_hi2c);
    main_debug_print(
        "valhalla_led_i2c: Master_Transmit failed (HAL=%d, err=0x%lx; AF=%lu TO=%lu BERR=%lu)\r\n",
        (int)st, (unsigned long)e, (unsigned long)((e & HAL_I2C_ERROR_AF) != 0U),
        (unsigned long)((e & HAL_I2C_ERROR_TIMEOUT) != 0U),
        (unsigned long)((e & HAL_I2C_ERROR_BERR) != 0U));
    return;
  }

  (void)memcpy(s_shadow, s_tag_blob, s_tag_blob_len);
}
