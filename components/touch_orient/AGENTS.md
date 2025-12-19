# components/touch_orient/AGENTS — Touch orientation rules (Strict)

## Scope
Applies to coordinate orientation helpers in `components/touch_orient` used by the touch pipeline.

## Expectations
- Keep orientation constants in sync with the display mounting defined by `components/board` and LVGL rotation settings.
- Transformations must be deterministic and stateless; avoid global mutable flags shared with other touch modules.
- Validate input pointers and bounds (max width/height) before applying transforms.
- Log orientation changes at init only; do not spam coordinates during normal operation.
- Add simple tests or assertions when adding new orientation modes to prevent swapped axes/flip regressions.
