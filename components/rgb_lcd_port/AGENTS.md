# components/rgb_lcd_port/AGENTS — RGB display port rules (Strict)

## Scope
Covers the RGB LCD bridge and its integration with LVGL for the 1024×600 panel.

## Expectations
- Keep timing and resolution consistent with the board configuration; no unverified pin or clock changes.
- Flush callbacks must respect LVGL 9.x threading rules (LVGL task/core) and avoid blocking longer than necessary.
- Coordinate conversions must stay aligned with touch orientation modules; avoid duplicate transforms.
- Manage frame buffers deterministically: document allocation, alignment, and ownership; avoid silent reallocations.
- Log initialization parameters (h/v timings, data width) and fail gracefully with clear errors when panel init fails.
