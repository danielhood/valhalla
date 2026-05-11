/**
 * I2C slave: receives RF reader `s_last_valhalla_tag_by_board` blob (raw struct array).
 */

#ifndef VALHALLA_I2C_SLAVE_H
#define VALHALLA_I2C_SLAVE_H

#include <stdint.h>
#include "stm32l4xx_hal.h"
#include "valhalla_tag.h"

#ifdef __cplusplus
extern "C" {
#endif

void valhalla_i2c_slave_init(I2C_HandleTypeDef *hi2c);

/** Last fully received snapshot; valid after at least one successful write (same layout as RFID reader). */
const valhallaTag *valhalla_i2c_get_last_tags(void);

/** Increments once per HAL_I2C_SlaveRxCpltCallback (full payload received). */
uint32_t valhalla_i2c_rx_complete_count(void);

/**
 * Emit USART2 debug lines when a new I2C payload has been received.
 * Call periodically from the main loop (not from ISR): uses blocking UART transmit.
 */
void valhalla_i2c_slave_poll_uart_log(void);

#ifdef __cplusplus
}
#endif

#endif /* VALHALLA_I2C_SLAVE_H */
