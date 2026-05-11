# shrine-rfid-reader

STM32 firmware for the Valhalla shrine RFID reader, built around MFRC522 over SPI with support for multiple readers on a shared bus.

## Highlights

- **Valhalla tag parsing**: reads NTAG NDEF Text payload and parses `type,camp,color,rune`.
- **Multiple reader support**: up to 4 MFRC522 readers on one SPI bus with independent CS lines.
- **STM32 target**: generated from STM32CubeIDE project for `STM32L432KCUx` (`NUCLEO-L432KC` board setup).
- **Established pin/peripheral config**: SPI1, I2C1, USART2, GPIO, RCC, SYS, NVIC are configured and in use.
- **I2C to LED board**: when the per-reader Valhalla tag snapshot changes, firmware pushes it to `shrine-led-driver` over I2C1 (see `docs/I2C_VALHALLA_TAGS_I2C_TRANSMISSION_PLAN.md`).


## Valhalla RFID Tag Format

The RFID chip for the totem rune cubes will contain one Text NTAG with expected NDEF Text content (CSV):

`type,camp,color,rune`

Validation rules currently implemented:

- `type`: exactly 1 character
- `camp`: exactly 2 characters
- `color`: exactly 2 characters
- `rune`: exactly 2 characters

If payload does not meet this format, the scan is ignored.

## Hardware/Target

- MCU: `STM32L432KCUx` (UFQFPN32)
- Board profile in `.ioc`: `NUCLEO-L432KC`
- Toolchain target: STM32CubeIDE (GCC)

## Enabled Peripherals

- `SPI1` in master, full-duplex, 8-bit, soft NSS
  - Prescaler: `SPI_BAUDRATEPRESCALER_128` (configured ~250 Kbit/s)
- `USART2` for debug/telemetry at `115200 8N1`
- `I2C1` master → Valhalla tag snapshot to shrine-led-driver (slave address `0x10`)
- `GPIO` for chip select lines and status LED
- `RCC/SYS/NVIC` from Cube configuration

## Pin Configuration (current project)

### SPI bus (MFRC522 shared bus)

- `PA5` -> `SPI1_SCK`
- `PA6` -> `SPI1_MISO`
- `PA7` -> `SPI1_MOSI`

### Reader chip select (CS)

- Reader 0 (default): `PA4`
- Additional optional reader CS outputs available by configuration:
  - Reader 1: compile-time define `RC522_READER1_CS_PORT` / `RC522_READER1_CS_PIN`
  - Reader 2: compile-time define `RC522_READER2_CS_PORT` / `RC522_READER2_CS_PIN`
  - Reader 3: compile-time define `RC522_READER3_CS_PORT` / `RC522_READER3_CS_PIN`
- `PA3` and `PA8` are configured as GPIO outputs in the current CubeMX pinout and can be used as CS lines if assigned.

### I2C transmission of Valhalla tags (to shrine-led-driver)

When `s_last_valhalla_tag_by_board[]` changes, the firmware sends the full snapshot (raw `valhallaTag` array, reader index order) in one I2C write to the LED MCU slave at **7-bit address `0x10`**. Implementation: `Core/Src/valhalla_led_i2c.c`, shared struct in `Core/Inc/valhalla_tag.h`. Protocol and wiring are documented in **`docs/I2C_VALHALLA_TAGS_I2C_TRANSMISSION_PLAN.md`** (pins on the LED side are **PB6/PB7**, not PA9/PA10).

Pinout on this MCU:

- `PA9` → `I2C1_SCL`
- `PA10` → `I2C1_SDA`

### Debug / status

- `PA2` -> `USART2_TX` (VCP TX)
- `PA15` -> `USART2_RX` (VCP RX)
- `PB3` -> `LD3` status LED

## Multi-Reader Behavior

- Firmware registers configured readers (max 4) and keeps all CS lines high by default.
- One active reader is selected at a time for SPI transactions.
- Main loop scans configured readers round-robin.
- Debug output is prefixed with reader index (for example, `[R0]`, `[R1]`) when a reader is active.

## Build and Flash

1. Open the project in STM32CubeIDE.
2. Build the project (`Debug` configuration is already present).
3. Flash to the STM32L432 target (ST-LINK).
4. Open serial output at `115200` baud to view startup and scan logs.

## Notes

- SPI is shared; only one MFRC522 must be selected at a time.
- Reader 0 defaults to `PA4` unless overridden by compile definitions.
- Multi-reader implementation details are tracked in `RC522_MULTI_READER_PLAN.md`.
- I2C Valhalla snapshot format, clocking notes for the LED board, and verification checklist: `docs/I2C_VALHALLA_TAGS_I2C_TRANSMISSION_PLAN.md`.

### USART2 debug and USB power-only (NUCLEO-L432KC)

USART2 TX/RX (`PA2` / `PA15`) is routed through the onboard **ST-Link** virtual COM port. Debug output uses **`HAL_UART_Transmit` with a bounded timeout** (`MAIN_DEBUG_UART_TX_TIMEOUT_MS` in `Core/Inc/driver_mfrc522_interface.h`, default **150 ms** per call) plus abort on failure, so firmware does **not** wait forever on COM. That avoids the common failure mode where heavy serial logging interacts badly with USB **charger-only** (no enumerated host) and starves **`valhalla_led_i2c_push_if_changed()`**.

If I2C or other peripherals still behave differently **with a PC vs USB wall wart**, also verify:

- **Common ground** between the Nucleo, MFRC522 board(s), and the LED MCU (especially when the laptop provides the only return path for GND).
- **3.3 V / 5 V** headroom on the same supply; brownouts can show up on one bus first.
