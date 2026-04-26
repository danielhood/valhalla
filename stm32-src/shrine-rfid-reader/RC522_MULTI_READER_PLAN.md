# RC522 Multi-Reader SPI Plan

## Goal

Support up to 4 MFRC522 boards on one SPI bus, each with an independent CS GPIO, without rewriting the upstream MFRC522 driver internals.

## Current Constraints

- The basic layer uses a single global `mfrc522_handle_t` instance.
- SPI interface callbacks are context-free (`spi_read/spi_write` have no per-device argument).
- CS was previously hardcoded to one pin.

## Chosen Approach

### Phase 1 (implemented)

Add an interface-layer active-device abstraction:

- Register up to 4 logical RC522 slots, each with a CS pin.
- Select an active slot before driver calls.
- SPI read/write use active slot CS pin instead of a fixed pin.

This keeps vendor driver linkage unchanged and minimizes risk.

### Phase 2 (implemented)

Create a project-owned wrapper layer with explicit per-device contexts:

- `rc522_device_t` with bus config + runtime state.
- Wrapper APIs like `rc522_select(dev)`, `rc522_scan(dev, ...)`.
- Prefix logs with reader index.

### Phase 3 (next)

Optional deeper refactor:

- Remove remaining global assumptions from application flow.
- Add independent state machines per reader for round-robin polling.
- Add optional IRQ support per reader if wired.

## Phase 1 Progress

- [x] Add interface constants/prototypes for multi-slot CS management.
- [x] Add SPI slot registry and active-slot selector in interface source.
- [x] Update SPI read/write to use active slot CS and fail if none selected.
- [x] Keep backward compatibility by defaulting slot 0 to `GPIOA PIN4` in SPI init.
- [x] Update app init to explicitly register and select reader 0.
- [x] Add optional readers 1..3 via `RC522_READERn_CS_PORT` / `RC522_READERn_CS_PIN` and round-robin scan in `main`.
- [x] Add per-reader log prefixes in `main_debug_print` when a reader is selected (`[R#] `).

## Phase 2 Progress

- [x] Add explicit per-device context type in app layer (`rc522_device_t`).
- [x] Move registration presence/state into per-device objects (`s_rc522_devices[]`).
- [x] Add wrapper APIs `rc522_select(dev)` and `rc522_scan(dev, out_tag)`.
- [x] Convert init path to use first present device object.
- [x] Convert round-robin polling loop to operate on device objects.
- [x] Add per-device counters (`scan_count`, `scan_ok_count`) for runtime diagnostics.

## How To Use Phase 1 APIs

1. Register each reader CS:
   - `mfrc522_interface_spi_register_device(index, port, pin)`
2. Select reader before talking to it:
   - `mfrc522_interface_spi_select_device(index)`
3. Run existing MFRC522 operations.

### App integration (this repo)

- `main.c` calls `rc522_boards_register_all()` which registers reader 0 on `GPIOA` / `PIN4` and optional readers if `RC522_READER1_CS_PORT` … `RC522_READER3_CS_PORT` are non-`NULL` (set via compile defines or `#define` in `main.c`).
- The main loop selects each present reader, runs `readNTAG()` + `picc_halt()`, then clears the log prefix.

## Notes

- Only one reader should be selected/active at a time on shared SPI.
- Registered CS pins are driven high on registration to keep readers deselected.
- If no active reader is selected, SPI read/write return failure intentionally.
