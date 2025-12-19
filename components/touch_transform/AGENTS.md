# components/touch_transform/AGENTS — Touch transform rules (Strict)

## Scope
Covers calibration and coordinate transforms under `components/touch_transform`.

## Expectations
- Keep calibration math consistent with LVGL coordinate space (origin, width/height from display config).
- Store calibration data using the existing storage backend only; validate size/version before applying.
- All transforms must guard against overflow and invalid calibration coefficients; fall back to identity on errors.
- Expose clear init/load/save APIs returning `esp_err_t`; avoid hidden globals and ensure thread safety with LVGL input handling.
- Include minimal tests or sample data to exercise calibration load/save paths when modifying algorithms.
