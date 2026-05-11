# Shrine LED Driver

The shrine-led-driver project runs on an STM32L432KC Nucleo board.

It is an **I2C slave** for **shrine-rfid-reader**, receiving a snapshot of the reader’s last Valhalla tag state per RC522 slot. **End-to-end master↔slave transfer is implemented and verified** on hardware. The firmware also drives patterns across two RGB LED strips (two independent TIM PWM groups).

Cross-project protocol reference: `shrine-rfid-reader/docs/I2C_VALHALLA_TAGS_I2C_TRANSMISSION_PLAN.md`.

## Valhalla tags over I2C

### Wire protocol

| Item | Detail |
|------|--------|
| **7-bit slave address** | `0x10` |
| **HAL `OwnAddress1`** | `32` (`0x20` in `OAR1`; STM32 HAL uses left-aligned 7-bit value) |
| **Direction** | Master **write** (slave receive): one I2C frame, **no register/sub-address** |
| **Payload** | Raw `valhallaTag` structs in **reader index order** (`0 .. N-1`), packed with **no padding** |
| **Payload size** | `sizeof(valhallaTag) * SHRINE_LED_VALHALLA_TAG_MAX` bytes (default **40**: 10 bytes × **4** tags) |

`valhallaTag` is defined in **`Core/Inc/valhalla_tag.h`** — same layout as shrine-rfid-reader’s copy (GCC builds assert `sizeof(valhallaTag) == 10`).

### I2C peripheral clock (required for correct operation)

Application **CPU** runs at **4 MHz MSI** (`PLL_NONE` in `SystemClock_Config`). Cube’s I2C **`Timing` register (`0x00100D14`)** is calculated for a **~16 MHz I2C kernel clock**, not 4 MHz PCLK1.

**`HAL_I2C_MspInit`** therefore:

1. Turns **HSI16** on if it was not already running (MSI-only boot leaves HSI off).
2. Sets **`RCC_I2C1CLKSOURCE_HSI`** so I2C1 runs at **16 MHz**, matching `TIMINGR`.

If I2C1 were clocked from PCLK1 at 4 MHz with that timing word, the bus can **stall** and the reader reports **`HAL_I2C_ERROR_TIMEOUT` (`0x20`)** on `Master_Transmit`.

### Firmware configuration

- **`SHRINE_LED_VALHALLA_TAG_MAX`** (in `Core/Inc/valhalla_tag.h`, default **4**): number of tags in the payload. Must stay aligned with **`MFRC522_INTERFACE_MAX_DEVICES`** on the reader. Override at compile time with `-DSHRINE_LED_VALHALLA_TAG_MAX=…` if that changes.

### Implementation files

| File | Role |
|------|------|
| `Core/Inc/valhalla_tag.h` | `valhallaTag`, `SHRINE_LED_VALHALLA_TAG_MAX`, `VALHALLA_TAGS_I2C_PAYLOAD_BYTES` |
| `Core/Inc/valhalla_i2c_slave.h` | Public init / accessor API |
| `Core/Src/valhalla_i2c_slave.c` | HAL listen mode, `HAL_I2C_AddrCallback` → `HAL_I2C_Slave_Seq_Receive_IT`, staging → stored snapshot |
| `Core/Inc/valhalla_rgb_strip.h` | Strip PWM API (`valhalla_rgb_strip_apply`, `bind_timers`, `systick_hook`) |
| `Core/Src/valhalla_rgb_strip.c` | PWM mapping, fades, dual crossfade (stable RGB endpoint order), atomic tag snapshot |
| `Core/Inc/status_pb3_led.h` | Nucleo user LED (**PB3**) heartbeat + I2C-received burst |
| `Core/Src/status_pb3_led.c` | ~1.1 s heartbeat; ~500 ms rapid flash if snapshot has a well-formed tag |
| `Core/Src/stm32l4xx_hal_msp.c` | I2C1 GPIO (**PB6/PB7**) + **HSI + I2C1 clock mux** |

### Reader counterpart

- Shrine-rfid-reader: `Core/Src/valhalla_led_i2c.c` (shadow + `HAL_I2C_Master_Transmit` to address `0x20`).

### Application usage

1. **Init** (already called from `main.c` after `MX_I2C1_Init()`): `valhalla_i2c_slave_init(&hi2c1);`
2. **Read snapshot**: `const valhallaTag *tags = valhalla_i2c_get_last_tags();` — array length `SHRINE_LED_VALHALLA_TAG_MAX`. Content is undefined until the first successful master write; after that it holds the last **complete** transfer.
3. **Detect new data** (optional): `valhalla_i2c_rx_complete_count()` increments once per completed full payload (useful for logging or waking pattern logic).
4. **RGB strips** (see **`Core/Inc/valhalla_rgb_strip.h`**): after init, `valhalla_rgb_strip_bind_timers(&htim1, &htim2)` is called from `main.c`; **`valhalla_rgb_strip_systick_hook()`** runs from **`SysTick_Handler`** each **1 ms** so dual crossfades and fades advance smoothly (avoiding coarse ~20 Hz updates from the main loop alone). You can still call `valhalla_rgb_strip_apply(tags, &htim1, &htim2, HAL_GetTick())` from the main loop if you prefer not to use the hook. Each apply copies the tag blob **with interrupts briefly masked** so an I2C completion `memcpy` cannot tear a half-updated snapshot. For two colours on one strip, blend endpoints are **sorted by RGB** so transient slot/order differences do not flip which colour is mixed in on successive frames. Tag index **0** and **2** drive **TIM1**; **1** and **3** drive **TIM2**. Steady-target changes **crossfade** over **`VALHALLA_RGB_STRIP_FADE_MS`** (default **10 s**); dual-tag A↔B uses **`VALHALLA_RGB_STRIP_DUAL_TRANSITION_MS`** **10 s** per leg (**20 s** full A→B→A cycle). Overrides: `-DVALHALLA_RGB_STRIP_DUAL_TRANSITION_MS=…`, `-DVALHALLA_RGB_STRIP_FADE_MS=…`.

#### Supported colour codes (tag `color` field, two-letter)

| Colour   | Code |
|----------|------|
| Red      | RD   |
| Deep red | DR   |
| Orange   | OR   |
| Yellow   | YL   |
| Green    | GN   |
| Purple   | PR   |
| Blue     | BL   |

### Electrical / pins (I2C1)

Wiring to **shrine-rfid-reader** (master uses **PA9/PA10** for I2C1):

- **I2C1_SCL** → **PB6**
- **I2C1_SDA** → **PB7**

Common ground required. Pull-ups on SCL/SDA (on one board or the bus).

## Status LED (PB3)

Mirrors **shrine-rfid-reader** behaviour in spirit: **~100 ms on** then **~1000 ms off** for a slow blink when idle, and — on each **completed** I2C blob when **at least one** `valhallaTag` looks like a successful reader parse (non-zero `type`, exactly two letters + NUL in `camp`, `color`, and `rune`) — a **~500 ms** window of **~40 ms** toggles serviced from `SysTick_Handler` (same timing constants as the reader’s scan-success LD3 burst). Heartbeat is suppressed while the burst owns the pin.

## GPIO (PWM strips)

TIM1_CH1 → PA8 (Strip 1 - Red)  
TIM1_CH2 → PA9 (Strip 1 - Green)  
TIM1_CH3 → PA10 (Strip 1 - Blue)  

TIM2_CH1 → PA0 (Strip 2 - Red)  
TIM2_CH2 → PA1 (Strip 2 - Green)  
TIM2_CH4 → PA3 (Strip 2 - Blue)  

TIM2_CH3 (PA2) is reserved for USART2 serial debug, hence TIM2_CH4 for the third strip channel.

### PWM vs LED brightness

Firmware sets each channel’s timer **compare** proportional to the desired RGB byte (0 → off, full scale at `Period` → maximum brightness). With Cube’s PWM1 and normal active-high polarity, **higher duty cycle means a brighter** LED channel on the strip.
