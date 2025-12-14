# SD Boot Log Analysis — CMD0 timeouts (ExtCS SDSPI)

## Context
- Device: ESP32-S3, RGB 1024×600 panel, GT911 touch, IO extender-controlled CS for SD (ExtCS mode).
- Firmware: `reptiles_assistant` built with ESP-IDF v6.1-dev; bootlog shows PSRAM + LVGL stack initialized normally.
- Symptom: SD init always fails during pre-init with repeated `CMD0 timeout waiting R1` followed by `CMD0 failed (resp=0xFF). Insert SD card or check wiring.`; behavior identical with and without microSD inserted.

### Extracted log evidence
```
W (2445) sd_extcs: CMD0 timeout waiting R1
...
W (3586) sd_extcs: CMD0 timeout waiting R1
W (3596) sd_extcs: CMD0 failed (resp=0xFF). Insert SD card or check wiring.
W (3596) sd_extcs: Pre-init failed. Card may be absent or wiring is wrong.
W (3598) main: SD Card mounting failed or card not present
```

## Likely root causes (prioritized)
- **P0 — CS line not toggling (IO extender path):** `sd_extcs` drives CS via CH32V003 IO extender (`IO_EXTENSION_IO_4`), asserting low/high through `IO_EXTENSION_Output`. If that pin is miswired, not powered, or the extender is not initialized correctly, CMD0 will always read `0xFF`, mimicking “no card” even when inserted.
- **P0 — No 3.3V / level-shift to card socket:** A floating or unpowered socket (3.3V or pull-up network missing) leaves MISO high, producing the same `0xFF` responses across CMD0 retries.
- **P1 — SPI clock/polarity reach the socket?** ExtCS uses SPI2 with GPIOs `CONFIG_ARS_SD_MISO=13`, `MOSI=11`, `SCK=12` (per runtime log). If the actual hardware routes differ (e.g., swapped lines or missing series resistors), the card will never enter SPI mode.
- **P1 — Lack of card-detect gating:** The board apparently omits a CD pin; driver always tries init. With identical logs with/without card, an undetected mechanical issue (bad socket, card not fully seated) remains possible.
- **P2 — Firmware timing edge-cases:** The driver already sends 80 dummy clocks with CS high then retries CMD0 up to 8× with 10 ms gaps. If the IO extender response is slow, adding a longer CS-assert setup or dummy clocks after each retry could help, but hardware verification should come first.

## Proposed corrective actions
1. **Verify IO extender CS path (priority P0):**
   - Confirm CH32V003 is powered and responsive (I2C probe occurs earlier at `0x24`; ensure that success really toggles GPIO4).
   - Using a scope or logic analyzer on the SD socket CS pin, verify it toggles low/high during CMD0. If not, check the IO extension firmware mapping for IO4 or update wiring.
2. **Validate SD power and pull-ups (priority P0):**
   - Measure 3.3V at the socket during boot; ensure proper pull-ups on MISO/MOSI/SCK/CS. An unpowered card will mirror `0xFF` exactly as observed.
3. **Cross-check SPI pin routing (priority P1):**
   - Compare board netlist to `CONFIG_ARS_SD_MISO=13`, `MOSI=11`, `SCK=12`; rewire or update `sdkconfig.defaults`/BSP only if the schematic proves a mismatch (do not guess pins).
4. **Optional firmware hardening (priority P2):**
   - Add diagnostics to log CS toggle success (`sd_extcs_set_cs` already warns on failures); consider extending CMD0 retries with longer CS-assert delays if hardware is confirmed good but marginal.

## Acceptance criteria
- With card absent: boot completes, UI runs, SD init logs a clear warning but no crash (already observed).
- With card inserted and wiring/power fixed: CMD0 returns `0x01`, CMD8/ACMD41 complete, mount succeeds, and SPI clock raises to target frequency without errors.
