# components/io_extension/AGENTS — CH32V003-based IO extension (Strict)

## 1) Non-negotiable
- CH422G is removed. This component is the only acceptable IO extension layer.

## 2) Interface contract
- Expose semantic IO lines (examples):
  - BACKLIGHT_EN
  - LCD_VCOM_EN / LCD_VDD_EN
  - TP_RST (if routed)
  - SD_CS (if routed)
  - USB/CAN select (if applicable)
- Do not leak raw expander register details into other components.
  Other components must call semantic APIs only.

## 3) Concurrency and timing
- I2C access must be serialized through the shared bus layer.
- Provide bounded retries with backoff for I2C transactions.
- Do not call IO extension APIs from ISR context.

## 4) Error handling
- If IO extension is unavailable:
  - return `ESP_ERR_NOT_FOUND` or `ESP_FAIL` as appropriate
  - higher layers must degrade gracefully (e.g., SD disabled)

## 5) Logging
- On init: log detected IO extender and version if available.
- On IO set failure: log pin semantic + error + bus address.

## Definition of Done
- Build: `idf.py fullclean build` passe.
- Boot: aucun panic/assert; IO extension absente entraîne dégradation contrôlée (backlight/SD optionnels désactivés).
- Logs: TAG io_extension mentionne init/détection et erreurs avec `esp_err_to_name` sur 1–3 lignes clés.
- Threading: appels sérialisés via bus I2C partagé, retries bornés sans ISR.
