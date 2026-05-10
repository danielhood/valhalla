# Shrine LED Driver

The shrine-led-driver project runs no a STM32L432KC nucelo board. 

It is an I2C slave for the shrine-rfid-reader, receiving an array of 4 valhallaTag structs when any change is detected by the shrine-rfid-reader. 

The shrine-led-driver generates patterns accross two RGB LED strips connected to two sets of RGB PWM GPIO pins, allowing two independent patterns to be displayed.

The following is a summary of the GPIO pin mappings:

I2C1_SCL -> PB6
I2C1_SDA -> PB7

TIM1_CH1 -> PA8 (Strip 1 - Red)
TIM1_CH2 -> PA9 (Strip 1 - Red)
TIM1_CH3 -> PA10 (Strip 1 - Red)

TIM2_CH1 -> PA0 (Strip 1 - Red)
TIM2_CH2 -> PA1 (Strip 1 - Red)
TIM2_CH4 -> PA3 (Strip 1 - Red)

Note the explict use of TIM2_CH4 as TIM2_CH3 (PA2) is used by the USART2 for serial debug.

