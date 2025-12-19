# components/board/AGENTS — Board configuration rules (Strict)

## Scope
Applies to BSP/pinout definitions, Kconfig, and board-level helpers in `components/board`.

## Expectations
- Preserve authoritative pin maps for ESP32-S3 and attached peripherals; do not invent alternates without repo evidence.
- Keep Kconfig defaults in sync with hardware reality and other components (display, touch, IO extension, SD).
- Document voltage domains, enables, and reset lines; ensure init code drives safe defaults at boot.
- Use ESP-IDF drivers and `idf_component.yml` metadata consistently; avoid hidden dependencies on removed components.
- Provide clear ownership/lifetime for board-level handles and avoid blocking init paths that could stall boot.
