# Valhalla tags over I2C (shrine-rfid-reader → shrine-led-driver)

End-to-end **implemented and bench-verified**: the reader pushes a snapshot of `s_last_valhalla_tag_by_board[]` to the LED MCU over I2C whenever the snapshot changes. The LED slave receives the blob and exposes it via `valhalla_i2c_get_last_tags()`.

This file remains the **protocol contract** between projects; keep it aligned when changing address, payload shape, or slot count.

---

## 1. Target contract (must not drift)

| Parameter | Value |
|-----------|--------|
| Slave 7-bit address | `0x10` |
| HAL `Master_Transmit` `DevAddress` | `(0x10 << 1)` = `0x20` |
| Payload | `sizeof(valhallaTag) * MFRC522_INTERFACE_MAX_DEVICES` consecutive bytes |
| Order | Index `0` first, then `1`, … `MFRC522_INTERFACE_MAX_DEVICES - 1` |
| Framing | Single write transaction: START, address+W, payload, STOP |

Source of truth on payload layout: **`Core/Inc/valhalla_tag.h`** in **both** repos (shared `valhallaTag`; `SHRINE_LED_VALHALLA_TAG_MAX` on the reader side must match **`MFRC522_INTERFACE_MAX_DEVICES`** — enforced with `_Static_assert` in reader `valhalla_led_i2c.c`). Live snapshot on reader: `Core/Src/main.c` → `s_last_valhalla_tag_by_board[]`.

---

## 2. Reader (shrine-rfid-reader)

**Peripheral (Cube):**

- `HAL_I2C_MODULE_ENABLED` in `Core/Inc/stm32l4xx_hal_conf.h`
- `hi2c1`, `MX_I2C1_Init()`, timing `0x00B07CB4` for this project’s **32 MHz** clock tree
- I2C1 MSP: **PA9** SCL, **PA10** SDA (`Core/Src/stm32l4xx_hal_msp.c`)
- I2C1 EV/ER NVIC + `stm32l4xx_it.c` stubs (for HAL consistency; blocking master TX does not rely on them)

**Application:**

- `Core/Inc/valhalla_led_i2c.h`, `Core/Src/valhalla_led_i2c.c` — `valhalla_led_i2c_init(&hi2c1, blob, len)` and `valhalla_led_i2c_push_if_changed()` (memcmp shadow, `HAL_I2C_Master_Transmit`, 100 ms timeout; logs HAL error flags on failure)
- `main.c` → init after `MX_I2C1_Init()`; push after tag updates in **`readNTAG()`** and at end of each main-loop scan pass

---

## 3. Slave (shrine-led-driver)

- `valhalla_i2c_slave_init()`, listen + address callback + `HAL_I2C_Slave_Seq_Receive_IT` → full payload into staging, then `memcpy` to stored snapshot (`Core/Src/valhalla_i2c_slave.c`)

**I2C kernel clock (important):** System clock on this firmware is **4 MHz MSI** (`PLL_NONE`). Cube’s `Timing = 0x00100D14` assumes a **~16 MHz** I2C peripheral clock. **`HAL_I2C_MspInit` therefore selects `RCC_I2C1CLKSOURCE_HSI`** (16 MHz HSI) for I2C1 and turns **HSI on** if it was off so `TIMINGR` matches `f_I2CCLK`. Without this, the master can see `HAL_I2C_ERROR_TIMEOUT` (`0x20`) and a stalled bus.

Pins: **PB6** SCL, **PB7** SDA (`Core/Src/stm32l4xx_hal_msp.c`).

---

## 4. Hardware: pin map between boards

| Board | Role | Instance | SCL | SDA |
|-------|------|----------|-----|-----|
| shrine-rfid-reader | Master | I2C1 | PA9 | PA10 |
| shrine-led-driver | Slave | I2C1 | PB6 | PB7 |

Wire **SCL ↔ SCL**, **SDA ↔ SDA**, **common GND**. Provide **pull-ups** on the bus (often one board is enough; use ~4.7 kΩ if in doubt).

---

## 5. Software design notes (as implemented)

- **Push policy:** Diff-based (`memcmp` vs shadow) so traffic only runs when the snapshot changes.
- **Concurrency:** Transmit from main context (main loop / `readNTAG`), not from RC522 ISRs.
- **Errors:** Non-fatal; failed transmit does not update the shadow (retries on next change). Reader logs decode timeout vs NACK vs bus error where possible.

---

## 6. Verification (completed on bench)

- [x] Payload length **40** bytes with 4 devices and `sizeof(valhallaTag) == 10`.
- [x] `SHRINE_LED_VALHALLA_TAG_MAX` / `MFRC522_INTERFACE_MAX_DEVICES` alignment.
- [x] Successful transfer increments `valhalla_i2c_rx_complete_count()`; `valhalla_i2c_get_last_tags()[i]` matches reader snapshot per index.
- [x] Reader RC522 operation unchanged (no pin clash with I2C1 on PA9/PA10).

---

## 7. Optional follow-ups

- Master **IT** or **DMA** to shorten blocking time in the loop.
- **Magic / version byte** or **CRC** if mixed firmware versions are a risk (requires slave + master change).
- **Backoff** on repeated bus errors.

---

## 8. References in repo

| Location | Notes |
|----------|--------|
| `shrine-rfid-reader/Core/Src/main.c` | Snapshot array, scan updates, `valhalla_led_i2c_*` calls |
| `shrine-rfid-reader/Core/Src/valhalla_led_i2c.c` | Master transmit, shadow, address `0x20` |
| `shrine-rfid-reader/Core/Inc/valhalla_tag.h` | Shared struct + `SHRINE_LED_VALHALLA_TAG_MAX` |
| `shrine-rfid-reader/Core/Src/stm32l4xx_hal_msp.c` | Reader I2C1 GPIO |
| `shrine-rfid-reader/README.md` | Pin summary |
| `shrine-led-driver/Core/Inc/valhalla_tag.h` | Same layout as reader |
| `shrine-led-driver/Core/Src/valhalla_i2c_slave.c` | Slave path |
| `shrine-led-driver/Core/Src/stm32l4xx_hal_msp.c` | PB6/PB7, **HSI + I2C1 clock mux** |
| `shrine-led-driver/README.md` | Wire protocol + pins |
