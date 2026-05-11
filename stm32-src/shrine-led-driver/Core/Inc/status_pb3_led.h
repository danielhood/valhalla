/**
 * User LED on PB3: slow heartbeat (~1 s) plus I2C received-tag burst (~500 ms, ~40 ms toggle).
 */

#ifndef STATUS_PB3_LED_H
#define STATUS_PB3_LED_H

#include <stdint.h>
#include "valhalla_tag.h"

#ifdef __cplusplus
extern "C" {
#endif

void status_pb3_led_init(void);

/** Call from SysTick_Handler after HAL_IncTick(); services rapid flash toggling when active. */
void status_pb3_led_systick_hook(void);

/** Call from thread context periodically (e.g. main loop): heartbeat when flash is idle. */
void status_pb3_led_poll(uint32_t now_ms);

/** If any tag passes Valhalla field checks like the RFID reader parsing, arms the burst flash (~500 ms). */
void status_pb3_led_on_i2c_snapshot_if_valid(const valhallaTag *tags);

/** Non-zero while burst flash owns the GPIO (heartbeat defers); same role as shrine-rfid reader helper. */
uint8_t status_pb3_led_flash_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* STATUS_PB3_LED_H */
