# docs/AGENTS — Documentation rules (Strict)

## Scope
All content under `docs/` must stay aligned with the ESP32-S3 target, LVGL 9.x UI stack, and the current board configuration.

## Expectations
- Keep diagrams/configs consistent with `components/board` pin maps and root hardware assumptions.
- Document optional hardware gracefully (SD, touch, IO extension) without promising availability.
- Prefer reproducible commands (idf.py, python scripts) and indicate expected logs or outputs.
- Do not store secrets, Wi-Fi credentials, or endpoints in docs; use placeholders only.
- When referencing external material, cite the source or repository location.
