# components/AGENTS — Component engineering rules (Strict)

## 1) CMake rules
- Every component must have:
  - `CMakeLists.txt` with correct `idf_component_register`
  - if configurable: `Kconfig` with sane defaults
- Do not add cyclic dependencies between components.

## 2) API design
- Public API: headers in component include dir, prefixed, with docs.
- Return type: `esp_err_t` (or explicit result structs) for init/mount operations.
- No hidden background tasks unless required; if a task exists:
  - name it
  - document stack/core/priority
  - provide stop/deinit path if used outside boot.

## 3) Logging policy
- Use stable TAG per component.
- Error logs must include `esp_err_to_name(err)` and context (pin, addr, state).

## 4) “Do not resurrect” policy
- Never reintroduce CH422G codepaths or `sd_waveshare` references anywhere in components.
