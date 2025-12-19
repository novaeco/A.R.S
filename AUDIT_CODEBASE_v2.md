# AUDIT_CODEBASE_v2

## Statut des actions d'audit
- T1 — Récursion I2C `i2c_bus_shared_init` ↔ `DEV_I2C_Init_Bus` : **FIXED** (voir commits fix(i2c) / board init).
- T2 — Incohérence macros batterie/Kconfig : **FIXED** (alignement `CONFIG_ARS_BAT_ADC_CHANNEL`, diviseur num/dén.).
- T3 — Init board/IO expander sans gestion d'échec : **FIXED** (retours propagés, modes dégradés touch/backlight/SD).
- T4 — LVGL buffers PSRAM non DMA + attente VSYNC longue : **FIXED** (alloc DMA interne avec fallback, VSYNC timeout court & désactivation).
- T5 — GT911 concurrence/logs : **FIXED** (logs déplacés hors chemin IRQ, compteurs d’erreurs, backoff I2C).
- SD ext-CS retries/état clair : **FIXED** (retries bornés, état remis, logs explicites).
- Net logs/backoff : **FIXED** (cap retries Wi-Fi, pas de logs de mdp, backoff plafonné).

## Éléments additionnels
- Ajout option Kconfig `CONFIG_ARS_SKIP_TEST_PATTERN` pour éviter l’allocation PSRAM au boot.
- Renforcement LVGL init (gestion d’erreur allocations/timer) et résumé boot fiabilisé.
