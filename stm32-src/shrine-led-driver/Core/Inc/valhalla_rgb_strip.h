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

#endif /* VALHALLA_RGB_STRIP_H */
