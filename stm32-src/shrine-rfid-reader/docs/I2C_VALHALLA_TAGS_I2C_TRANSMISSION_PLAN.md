# Plan: I2C master transmission of Valhalla tags (shrine-rfid-reader → shrine-led-driver)

This document is for a **separate implementation session** in Cursor. Goal: have shrine-rfid-reader act as an I2C **master** and push `s_last_valhalla_tag_by_board` to the LED MCU, which already implements the slave described in `shrine-led-driver/README.md`.

---

## 1. Target contract (must not drift)

| Parameter | Value |
|-----------|--------|
| Slave 7-bit address | `0x10` |
| HAL `Master_Transmit` `DevAddress` | `(0x10 << 1)` = `0x20` |
| Payload | `sizeof(valhallaTag) * MFRC522_INTERFACE_MAX_DEVICES` consecutive bytes |
| Order | Index `0` first, then `1`, … `MFRC522_INTERFACE_MAX_DEVICES - 1` |
| Framing | Single write transaction: START, address+W, payload, STOP |

Source of truth on reader: `valhallaTag` and `static valhallaTag s_last_valhalla_tag_by_board[MFRC522_INTERFACE_MAX_DEVICES]` in `Core/Src/main.c`. On LED: `Core/Inc/valhalla_tag.h` and `SHRINE_LED_VALHALLA_TAG_MAX` (default 4) must match reader device count.

---

## 2. Current reader project state

- **`HAL_I2C_MODULE_ENABLED`** is **not** enabled in `Core/Inc/stm32l4xx_hal_conf.h` (I2C HAL commented out).
- No `hi2c` handle, no `MX_I2Cx_Init`, no MSP I2C GPIO in Cube-generated code (verify `.ioc`).
- Tag updates happen in scan paths that already touch `s_last_valhalla_tag_by_board` (grep in `main.c`).

---

## 3. Hardware / CubeMX checklist

1. Pick an I2C instance and pins that **do not conflict** with existing SPI1 (RC522), USART2, or other used pins on the reader’s board.
2. Configure that I2C as **master**, same bus speed class as the LED slave (slave uses timing from its Cube config; start with **100 kHz** standard mode unless both sides are tuned for Fast Mode).
3. Regenerate code: enable **`HAL_I2C_MODULE_ENABLED`** in `stm32l4xx_hal_conf.h`, add `stm32l4xx_hal_i2c.c` / `hal_i2c_ex.c` to build if the IDE does not add them automatically.
4. Enable I2C NVIC (EV/ER) if using interrupt or DMA master APIs; for **`HAL_I2C_Master_Transmit`** blocking, NVIC is optional but fine to add for future IT/DMA.

Document chosen pins and instance in this repo’s README or a short `docs/` note next to the implementation.

---

## 4. Software design

### 4.1 Thin transmit API

Add something like:

- `valhalla_led_sync_init(void)` — cache `I2C_HandleTypeDef*`, optional probe.
- `valhalla_led_sync_push_if_changed(void)` or `valhalla_led_sync_push(const valhallaTag *tags, size_t count)` — perform `Master_Transmit`.

Keep I2C out of the MFRC522 driver layer unless you prefer a dedicated `valhalla_led_i2c.c` / `.h` next to `main.c`.

### 4.2 When to transmit

Options (pick one or combine):

- **A. Diff-based:** After any code path updates `s_last_valhalla_tag_by_board[i]`, compare a **shadow copy** (same array size) with `memcmp`; if different, transmit and memcpy shadow ← live.
- **B. Periodic:** Main loop or a timer every N ms pushes unconditionally (simple; more bus traffic).
- **C. Event:** Call push from the same places that assign `s_last_valhalla_tag_by_board[...]` (grep for assignments).

Recommendation: **A** or **C** to limit traffic and avoid blocking scan loops unnecessarily.

### 4.3 Error handling

- If `HAL_I2C_Master_Transmit` returns not `HAL_OK`, log via existing UART debug (if enabled) with `HAL_I2C_GetError`.
- Do **not** block forever: use a finite timeout (e.g. `100` ms) first; tune after hardware validation.
- If the LED board is unplugged, expect **NACK** / timeout; treat as non-fatal unless product requirements say otherwise.

### 4.4 Concurrency

If scans run from interrupt context, **do not** call `Master_Transmit` from ISR unless using a proper IT/DMA path and queue. Prefer pushing from **main loop** or a **low-priority task** with data prepared under a short critical section (copy `s_last_valhalla_tag_by_board` to a local buffer, then transmit the copy).

---

## 5. Implementation steps (ordered)

1. CubeMX: add I2C master, set pins, clock, timing; regenerate.
2. Confirm `HAL_I2C_MODULE_ENABLED` and HAL I2C sources in build.
3. Add `valhalla_led_i2c.c` / `.h` (or equivalent) with `DevAddress` constant `0x10 << 1` and payload size `sizeof(valhallaTag) * MFRC522_INTERFACE_MAX_DEVICES`.
4. Wire `init` from `main` after peripheral `MX_*_Init`.
5. Integrate **push** using chosen policy (section 4.2).
6. On bench: logic analyzer or scope optional; verify LED side `valhalla_i2c_rx_complete_count()` increases and tag fields match reader logs.

---

## 6. Verification checklist

- [ ] Payload length on wire equals **40** bytes when `MFRC522_INTERFACE_MAX_DEVICES == 4` and `sizeof(valhallaTag) == 10`.
- [ ] LED firmware `SHRINE_LED_VALHALLA_TAG_MAX` matches reader device count.
- [ ] Successful transfer increments slave `valhalla_i2c_rx_complete_count()` and `valhalla_i2c_get_last_tags()[i]` matches reader `s_last_valhalla_tag_by_board[i]` for each index.
- [ ] Reader still scans RC522 correctly (no pin or clock conflict).

---

## 7. Optional follow-ups (out of scope unless requested)

- Switch master to **IT** or **DMA** to shorten time in scan path.
- Add a **magic/version byte** or CRC prefix if firmware versions can mismatch (would require coordinated slave change).
- **Rate limiting** or exponential backoff on repeated bus errors.

---

## 8. References in repo

| Location | Notes |
|----------|--------|
| `shrine-rfid-reader/Core/Src/main.c` | `valhallaTag`, `s_last_valhalla_tag_by_board`, scan update sites |
| `shrine-rfid-reader/Core/Inc/driver_mfrc522_interface.h` | `MFRC522_INTERFACE_MAX_DEVICES` |
| `shrine-led-driver/README.md` | Slave protocol summary |
| `shrine-led-driver/Core/Src/valhalla_i2c_slave.c` | Slave behavior (listen + seq receive) |
