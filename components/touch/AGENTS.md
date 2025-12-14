# components/gt911/AGENTS — Touch driver rules (Strict)

## 1) Init robustness
- Must handle:
  - missing reset/int pins
  - I2C timeouts
  - wrong address (0x5D/0x14) only if repo supports switching
- Never block boot indefinitely.

## 2) Runtime behavior
- Prefer IRQ-driven reading (if supported) or bounded polling.
- Apply basic jitter filtering only if it does not break latency.
- Do not allocate large buffers on stack.

## 3) Safety
- No touch data -> return “no points” cleanly.
- Any I2C error should not crash LVGL input device path.
