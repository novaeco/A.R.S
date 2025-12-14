# Reptiles Assistant Boot Log Analysis (ESP32-S3, ESP-IDF v6.1-dev)

## Scope and inputs
- Source: user-provided `idf.py monitor` output from ESP32-S3 boot (`reptiles_assistant.elf`).
- Hardware context per repo: RGB 1024×600 LCD, GT911 touch, SD in SDSPI External-CS mode via IO extender.
- Goal: extract faults, map to code paths, and list prioritized fixes.

## Observed boot timeline (key excerpts)
- Bootloader + app start, PSRAM OK, LVGL/UI start, Wi-Fi unprovisioned (expected).
- SD stage shows manual pre-init then driver init:
  - `W ... sd_extcs: CMD0 failed (resp=0x05). Insert SD card or check wiring.`
  - `W ... sd_extcs: Pre-init failed. Card may be absent or wiring is wrong.`
  - `I ... sdspi_transaction: cmd=52/5, R1 response: command not supported` followed by `E ... sdmmc_sd: sdmmc_init_spi_crc ...` and mount failure 0x107 (timeout).
- System continues boot (fail-safe OK), LVGL/UI remain running.

## Findings (tied to log)
1. **SD SPI mode not entered (CMD0 returns 0x05)** — indicates card never acknowledged idle state. This aligns with our manual low-speed init in `sd_extcs_low_speed_init()`; when it returns an error we still proceed to `esp_vfs_fat_sdspi_mount`, causing the driver to send CMD52/CMD5 (SDIO-style) which are rejected ("command not supported").
2. **Pre-init failure not propagated** — even after `sd_extcs_low_speed_init()` warns, mount continues. This elongates boot and produces non-SPI command noise without clear recovery guidance.
3. **Limited diagnostics on CS path** — CS is driven through IO extender but we do not log extender init latency or CS state around failures, making it harder to confirm whether CS toggling or wiring caused the 0x05 response.

## Prioritized fix list
- **P0: Abort mount when SPI-mode entry fails.** If `sd_extcs_low_speed_init()` does not observe R1=0x01, return an error immediately to avoid later CMD52/CMD5 attempts. Provide a concise log that card is absent or wiring is wrong.
- **P0: Add explicit retry/settle around CMD0 with CS high clocks counted.** Increase dummy clocks to a fixed 80 cycles minimum with verification log, and keep bounded retries with backoff to handle slow cards. This reinforces standard SPI bring-up.
- **P1: Strengthen CS diagnostics.** Log IO extender init time and CS toggle status when CMD0 times out to rule out extender/I2C issues causing deasserted CS during init.
- **P1: Early exit guidance.** When mount is skipped due to pre-init failure, surface a single-line status for boot summary (SD not present) per `main/AGENTS.md` requirement.

## Acceptance criteria for fixes
- With no SD card: boot completes, LVGL/UI responsive, log shows single-line SD status, no CMD52/CMD5 noise, and no panic.
- With valid FAT32 SD: manual init succeeds (CMD0→CMD8→ACMD41→CMD58), mount returns `ESP_OK`, SPI clock raised toward target frequency.
