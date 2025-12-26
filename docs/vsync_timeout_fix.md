# Correction timeout VSYNC (LVGL DIRECT, double framebuffer)

## Symptôme observé
- Log de boot : `W lv_port: VSYNC wait timeout — disabling wait` dès le premier flush.
- Contexte matériel : ESP32-S3 + écran Waveshare 7B 1024x600 (LVGL 9.x en DIRECT, double framebuffer).

## Mécanisme VSYNC
- **Avant** : attente LVGL sur sémaphore binaire, timeout fixe 20 ms ; aucune télémétrie sur le débit VSYNC, risque de timeout au premier flush.
- **Après** : notification directe de la tâche LVGL via `vTaskNotifyGiveFromISR()` (ISR VSYNC) → `ulTaskNotifyTake()` côté flush, timeout paramétrable (par défaut 40 ms). Instrumentation ajoutée :
  - Compteur global `isr_count` + timestamp `last_vsync_us` dans le callback.
  - Compteur `wait_wakeups` incrémenté à chaque réveil du flush.
  - Log de résultat d’attente sur les premiers flushs (et en cas de timeout).
  - Timer 1 Hz (`vsync_diag`) qui logge `total`, `wakeups`, `rate` (VSYNC/s), `last` (µs), `wait` (flag actif).

## Logs attendus
- Au boot, plus de `VSYNC wait timeout — disabling wait`.
- Sur les premiers flushs (DIRECT) : `VSYNC wait result: notified=1 wakeups=1 last_us=<timestamp>`.
- Télémétrie périodique : `VSYNC diag: total=<n> wakeups=<n> rate=XX/s last=<ts>us wait=1` avec `rate` stable (>0).

## Procédure de validation
1. Nettoyage + build :  
   ```bash
   idf.py fullclean
   idf.py build
   ```
2. Flash + monitor (adapter le port série) :  
   ```bash
   idf.py -p /dev/ttyACMx flash monitor
   ```
3. Vérifier au boot :
   - Absence de `VSYNC wait timeout`.
   - Présence des logs `VSYNC wait result` puis `VSYNC diag` 1 Hz avec `rate` non nul.
   - LVGL en mode DIRECT + double framebuffer toujours annoncé.

## Rollback simple
- Revenir au commit précédent (git checkout <hash>) ou rétablir `CONFIG_ARS_LCD_WAIT_VSYNC_TIMEOUT_MS` à la valeur antérieure (20 ms) dans `sdkconfig.defaults`.
