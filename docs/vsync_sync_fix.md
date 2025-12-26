# Correction synchronisation VSYNC (RGB 1024×600 / LVGL 9.x)

## Contexte
- Matériel : ESP32-S3 + panneau RGB 1024×600 (Waveshare Touch LCD 7B), LVGL 9.x en mode direct.
- Problème observé (logs monitor) : `VSYNC wait timeout — disabling wait`, `VSYNC wait result: timeout after 20ms ...`, compteur `wakeups` bloqué à 0 malgré un ISR à ~30 Hz. La tâche LVGL ne recevait pas de notification VSYNC, le timeout (20 ms) était trop court et la logique désactivait définitivement l’attente.

## Correctifs appliqués
- Chaîne ISR → tâche LVGL rétablie via `vTaskNotifyGiveFromISR()` et consommation par `ulTaskNotifyTake()`, avec incrément du compteur `wakeups` dans l’ISR.
- Timeout rendu configurable via `CONFIG_ARS_LCD_WAIT_VSYNC_TIMEOUT_MS` (défaut 80 ms) pour couvrir ~30 Hz.
- Après un timeout, on reste en mode attente (pas de désactivation définitive) avec simple log « retrying ».
- Diag VSYNC inchangé : log périodique `VSYNC diag: total=... wakeups=... rate=...` doit montrer `wakeups > 0`.

## Commandes de validation (reproductible)
```bash
idf.py fullclean build
idf.py -p <PORT> flash monitor
```

## Attendus dans les logs (flash + monitor)
- Plus de `VSYNC wait timeout — disabling wait`.
- `VSYNC sync: ACTIVE (timeout=80ms)` lors de l’init LVGL.
- Périodiquement : `VSYNC diag: total=N wakeups=M rate≈30/s ...` avec `M` qui augmente (wakeups > 0).
- Sur flush : au besoin un seul warning `VSYNC wait timeout — retrying`, suivi de notifications réussies.

## Rollback
- Revenir au commit précédent ou ramener `CONFIG_ARS_LCD_WAIT_VSYNC_TIMEOUT_MS` à l’ancienne valeur dans `sdkconfig.defaults` puis `idf.py fullclean build`.
