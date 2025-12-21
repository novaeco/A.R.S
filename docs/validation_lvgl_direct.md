# Validation LVGL DIRECT (ESP32-S3 RGB 1024x600)

## Paramètres Kconfig recommandés
- `CONFIG_ARS_LCD_PCLK_HZ=51200000` (Htotal 1344, Vtotal 635 → ~51.2 MHz @60 Hz)
- `CONFIG_ARS_LCD_PCLK_ACTIVE_NEG=y`
- `CONFIG_ARS_LCD_HSYNC_IDLE_LOW=n`
- `CONFIG_ARS_LCD_VSYNC_IDLE_LOW=n`
- `CONFIG_ARS_LCD_DE_IDLE_HIGH=n`
- `CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE=n` (activer pour A/B test VSYNC)
- `CONFIG_ARS_LCD_BOOT_TEST_PATTERN=y` (pattern 2 s optionnel au boot)
- `CONFIG_ARS_UI_HEARTBEAT=y` (compteur d’angle pour vérifier DIRECT)

## Procédure de test
1. **Compilation propre**
   - `idf.py fullclean build`
   - Attendu : build sans erreur, log CMake sans dépendance `esp_lvgl_port` gérée.
2. **Flash + monitor**
   - `idf.py -p <PORT> flash monitor`
   - Attendu au boot :
     - Log RGB : résolution, pclk≈51200000, polarités.
     - Log LVGL : `LVGL DIRECT mode` + `fb_count=2`.
     - Si `CONFIG_ARS_LCD_BOOT_TEST_PATTERN=y` : motif RGBWB pendant ~2 s, message `Test Pattern window elapsed`.
     - Si `CONFIG_ARS_UI_HEARTBEAT=y` : label `HB <n>` incrémente chaque seconde (changement visible → rendu DIRECT OK).
     - Pas de reboot même si SD absente (warning uniquement).
3. **VSYNC (optionnel)**
   - Activer `CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE=y`, vérifier :
     - Log callback enregistré, pas de blocage ; si timeout → warning unique et fallback sans attente.

## Indicateurs de succès
- L’écran sort du motif de test puis affiche l’UI ; le heartbeat bouge.
- Les logs ne mentionnent pas `VSYNC wait timeout` en boucle.
- Aucun crash SD/tactile ; tâches LVGL/GT911 toujours actives.
