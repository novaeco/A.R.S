# components/sd/AGENTS — SDSPI + External CS (Strict)

## 1) Primary objective
Make SD initialization robust while guaranteeing **no boot crash** when:
- card absent
- card inserted but incompatible
- wiring/pins wrong
- CRC errors / timeouts

## 2) Required behaviors
### 2.1 No-panic guarantee
- Any SD error must be converted to:
  - `esp_err_t` return
  - warning/error logs
  - no null deref, no mem corruption, no `abort()`

### 2.2 Conservative initialization
- SPI init at low speed (e.g., 400 kHz to 1 MHz) for card bring-up.
- After card enters SPI mode and OCR is stable, optionally increase frequency.
- Always provide at least 74+ clocks with CS high before CMD0.

### 2.3 Command correctness
- SD SPI flow should match standard sequence:
  - CMD0 -> idle
  - CMD8 (if SDv2) check pattern
  - ACMD41 loop (CMD55 + ACMD41) until ready
  - CMD58 read OCR
  - CMD59 CRC enable/disable **only if required** by the chosen host/driver behavior
- Do not issue commands that do not apply to SPI mode unless explicitly required.

## 3) External CS (“ExtCS”) rules
If CS is not a native GPIO but controlled via IO extender:
- Provide an explicit `sd_extcs_set_cs(level)` contract.
- CS timing must honor:
  - assert before command
  - deassert after response read
  - minimum deassert time between commands
- Avoid races: CS toggling must be serialized with the SPI transactions.

## 4) Mount behavior
- `sd_mount()` must:
  - return quickly on failure (bounded retries)
  - log a clear reason and next-action (insert card, check wiring, format)
- Do not assume exFAT. Prefer FAT32 unless configured.

## 5) Diagnostics (allowed)
Diagnostics are allowed but must be non-invasive:
- “MISO idle level” checks are fine if they do not touch SPI driver internals unsafely.
- Avoid direct `spi_device_transmit()` on half-initialized devices unless you have fully valid descriptors.

## 6) Acceptance criteria
- With no SD inserted:
  - boot completes
  - UI runs
  - logs show SD not present / timeout, no crash
- With SD inserted (FAT32):
  - mount succeeds
  - simple file read/write sanity passes (if repo includes a test)

## Definition of Done
- Build: `idf.py fullclean build` passe.
- Boot: aucun panic/assert; absence ou échec SD se traduit par logs et poursuite du boot.
- Logs: TAG sd indique init/mount et erreurs `esp_err_to_name` avec causes claires.
- Threading: séquence SDSPI sérialisée (CS externe inclus), timeouts bornés, pas de blocage LVGL/boot.
