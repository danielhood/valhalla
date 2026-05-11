/**
 * Drive TIM1/TIM2 RGB strip PWM from Valhalla tag snapshots (colour field).
 */

#ifndef VALHALLA_RGB_STRIP_H
#define VALHALLA_RGB_STRIP_H

#include "main.h"
#include "valhalla_tag.h"

void valhalla_rgb_strip_apply(const valhallaTag *tags,
                              TIM_HandleTypeDef *htim1,
                              TIM_HandleTypeDef *htim2,
                              uint32_t now_ms);

/** Call once after `MX_TIM1_Init` / `MX_TIM2_Init` so `valhalla_rgb_strip_systick_hook` can refresh PWM at 1 ms. */
void valhalla_rgb_strip_bind_timers(TIM_HandleTypeDef *htim1, TIM_HandleTypeDef *htim2);

/** Optional: call from `SysTick_Handler` after `HAL_IncTick()` for smooth dual crossfades (requires `valhalla_rgb_strip_bind_timers`). */
void valhalla_rgb_strip_systick_hook(void);

#endif /* VALHALLA_RGB_STRIP_H */
