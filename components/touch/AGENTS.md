# components/touch/AGENTS — Touch driver rules (Strict)

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

## Definition of Done
- Build: `idf.py fullclean build` passe.
- Boot: aucun panic/assert; absence de GT911 ou d’IRQ ne bloque pas le démarrage.
- Logs: TAG touch signale init et erreurs I2C avec `esp_err_to_name` sans spam.
- Threading: accès I2C sérialisé selon règles bus, callbacks LVGL non bloquants.
