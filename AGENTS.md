# AGENTS — Reptiles Assistant (A.R.S) — Strict Operating Rules

## 0) Mission
You are an automated engineering agent operating inside the **A.R.S** ESP-IDF repository.
Your job is to implement requested fixes **safely**, **deterministically**, and **verifiably**:
- no regressions in boot
- no crashes/panics
- build must pass
- SD, UI (LVGL), and touch must remain functional (or fail gracefully if HW absent)

## 1) Project context (non-negotiable)
- Target: **ESP32-S3** (ESP-IDF v6.1-dev used by this repo)
- Display: **RGB 1024×600**
- UI: **LVGL 9.x** (port task on Core 1, display flush)
- Touch: **GT911** (I2C)
- IO extension: **CH32V003-based IO extender** via component `io_extension`
  - **CH422G is permanently removed** from this repo: never re-introduce it.
- SD: component `components/sd` only
  - **`components/sd_waveshare` is permanently removed**: never re-introduce it.

## 2) Repository navigation rules
- Read `AGENTS.md` at:
  1) repo root (this file)
  2) the closest subdirectory for the code you change (e.g. `components/sd/AGENTS.md`)
- Subdirectory rules override root rules when stricter.

## 3) Hard constraints (must not violate)
### 3.1 Forbidden re-introductions
- Do **NOT** add back:
  - any `ch422g` component, sources, includes, or probing logic
  - `components/sd_waveshare` or any references to it
  - legacy EXIO/CH422G-specific pin constants or Kconfig options

### 3.2 Pinout / hardware assumptions
- Do not “guess” alternate pin maps.
- Use **only** the pin configuration already present in the repo (board/bsp/config headers).
- If a pin map is inconsistent, fix it only when you have evidence in the repo (schematics in docs, config headers, or existing working demo notes).

### 3.3 Fail-safe boot policy
- SD init failure must **never** crash the device.
- SD mount is optional at boot: on any failure, log and continue.

### 3.4 Dependency policy
- Prefer ESP-IDF built-in APIs.
- Do not add new third-party libs without explicit request.
- Avoid adding new components unless strictly necessary.

## 4) Required execution / verification steps
Before delivering changes, you must:
1) `idf.py fullclean`
2) `idf.py build`
3) (If requested or feasible) `idf.py -p COMx flash monitor` and provide boot logs until:
   - LVGL task started
   - UI initialized
   - SD failure handled without panic (if card absent)

If you cannot run commands, provide:
- exact commands to run
- expected “success indicators” in logs
- what logs to paste back if it fails

## 5) Output requirements (strict)
When responding with a proposed patch:
- Provide a **file list** (paths) to be changed/added/removed.
- For each file: **why** it changed and **what** changed (high signal).
- Provide **diff-style** snippets (unified diff) for the most critical sections.
- State acceptance criteria (what must be true after patch).

When responding with analysis:
- Tie each finding to an observed log line or code path.
- Provide a prioritized fix list (P0/P1/P2).

## 6) Coding standards
- C (ESP-IDF): use `ESP_LOGx(TAG, ...)`, return `esp_err_t`, avoid global side effects.
- All public APIs must have header docs and explicit ownership rules.
- Concurrency: LVGL calls must occur in LVGL task context (or protected by the repo’s existing mechanism).
- Defensive programming:
  - null checks
  - bounds checks
  - timeouts with fallback
  - no unchecked pointer arithmetic

## 7) SD reliability requirements (summary)
(Full rules in `components/sd/AGENTS.md`)
- Ensure **SDSPI** init is standards-compliant (CMD0/CMD8/ACMD41/CMD58 etc).
- Use conservative SPI frequency for init, then optionally raise.
- If using “external CS” toggling, CS must be asserted/deasserted correctly with correct timing.
- Never issue non-SPI SD commands to the card (avoid stray CMD52/CMD5 behavior unless required by the chosen flow).

## 8) Antigravity / Codex compatibility (how to behave)
If you are used inside Google Antigravity or Codex:
- act as a patching agent: minimal, targeted diffs
- never refactor unrelated code “for style”
- do not touch unrelated components
- preserve existing public APIs unless requested

## 9) Safety: secrets and credentials
- Do not print or store Wi-Fi passwords in logs.
- Do not add API keys to source control.
- If credentials are needed, require them via runtime provisioning (NVS) already in repo.

## 10) Directory-specific rules
- `main/AGENTS.md`: app orchestration, lifecycle, tasks
- `components/AGENTS.md`: component standards, CMake/Kconfig conventions
- `components/sd/AGENTS.md`: SD init/mount rules (must be followed for SD fixes)
- `components/io_extension/AGENTS.md`: IO extender contract (CH32V003), pin/semantic mapping
- `components/touch/AGENTS.md`: touch robustness rules
- `components/lv_port/AGENTS.md`: LVGL port constraints
- `components/net/AGENTS.md`: provisioning/wifi state rules
- `components/data_manager/AGENTS.md`: storage integrity rules
- `components/i2c/AGENTS.md`: shared I2C rules
