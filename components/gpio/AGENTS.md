# components/gpio/AGENTS — GPIO helper rules (Strict)

## Scope
Applies to the GPIO utility layer in `components/gpio` used by display, touch, and peripheral bring-up.

## Expectations
- Use only ESP-IDF GPIO/LEDC APIs; do not poke registers directly or assume undocumented pin states.
- Respect board pin definitions from `components/board`; do not remap pins without evidence.
- Keep ISR installation idempotent and avoid leaking handlers; disable/teardown cleanly when provided.
- PWM/LEDC setup must bound frequencies and duty cycles to safe ranges for attached hardware (backlight, buzzers).
- Logging must include pin numbers and `esp_err_to_name` on failure; never block boot on GPIO errors.
