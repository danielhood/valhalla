/**
 * I2C master: push Valhalla tag snapshot to shrine-led-driver slave (0x10).
 */

#ifndef VALHALLA_LED_I2C_H
#define VALHALLA_LED_I2C_H

#include "main.h"
#include <stddef.h>
#include <stdint.h>

void valhalla_led_i2c_init(I2C_HandleTypeDef *hi2c, const uint8_t *tag_blob, size_t tag_blob_len);
void valhalla_led_i2c_push_if_changed(void);

#endif /* VALHALLA_LED_I2C_H */
