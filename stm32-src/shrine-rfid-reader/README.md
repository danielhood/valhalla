# shrine-rfid-reader

STM32 firmware for the Valhalla shrine RFID reader, built around MFRC522 over SPI with support for multiple readers on a shared bus.

## Highlights

- **Valhalla tag parsing**: reads NTAG NDEF Text payload and parses `type,camp,color,rune`.
- **Multiple reader support**: up to 4 MFRC522 readers on one SPI bus with independent CS lines.
- **STM32 target**: generated from STM32CubeIDE project for `STM32L432KCUx` (`NUCLEO-L432KC` board setup).
- **Established pin/peripheral config**: SPI1, USART2, GPIO, RCC, SYS, NVIC are configured and in use.


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
