# Rapport stabilité affichage (ESP32-S3 + RGB 1024×600)

## Symptôme
Affichage « drift/shift » (image qui se décale) sur panneau RGB 1024×600 (Waveshare 7B, ST7262) avec LVGL 9.x. PCLK mesuré à 40 MHz, PSRAM octal 80 MHz, double framebuffer en PSRAM, bounce buffer court (20 lignes) et tâches RGB/LVGL sur des cœurs différents.

## Cause retenue (sources officielles)
- FAQ Espressif « drift » : bande passante PSRAM limite le PCLK. Octal PSRAM 80 MHz → PCLK typique ≈ 22 MHz, au-delà risque de dérive. Recommandation d’aligner RGB et `lv_timer_handler()` sur le même cœur. [FAQ LCD](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html)
- Doc IDF RGB LCD : en mode bounce buffer, si deux cœurs lisent la PSRAM en parallèle (ISR GDMA + CPU opposé), le basculement des buffers peut rater → shift. [RGB LCD](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html)
- Doc IDF `esp_lcd_rgb_panel_set_pclk`/`_restart` : possible de baisser le PCLK ou relancer en VSYNC si nécessaire. [esp_lcd](https://docs.espressif.com/projects/esp-idf/en/v5.1.1/esp32s3/api-reference/peripherals/lcd.html)

## Changements appliqués
- PCLK nominal abaissé à 22 MHz (CONFIG_ARS_LCD_PCLK_HZ par défaut) pour rester sous la limite PSRAM octal 80 MHz.
- Alignement des cœurs : tâche LVGL par défaut sur le même cœur que l’initialisation RGB (CPU0) pour réduire la contention PSRAM.
- Bounce buffer dimensionné dynamiquement : tentative préférée 30 lignes, puis chutes (24/20/16/12/8/4/0) en fonction du plus grand bloc DMA disponible ; logs détaillés des essais.
- Journalisation au boot : PCLK configuré, mode/frequence PSRAM, cœurs LVGL/RGB, configuration bounce.
- Ajout de dépendance esp_psram pour tracer la configuration.

## Comment ajuster le PCLK
- Via `menuconfig` ou `sdkconfig.defaults` : `CONFIG_ARS_LCD_PCLK_HZ`.
- Monter prudemment (24/26 MHz max) et valider l’absence de drift. Si PSRAM passe à 120 MHz octal, la FAQ indique un plafond typique ~30 MHz.

## Procédure de test/reproduction
1. Nettoyage + build : `idf.py fullclean build`
2. Flash + monitor : `idf.py -p <COM/tty> flash monitor --disable-address-decoding`
3. Attendus dans les logs :
   - Ligne de pré-init RGB : PCLK=22000000 Hz, PSRAM=octal 80 MHz, cores LVGL/RGB = 0, bounce_cfg=30.
   - Création panel : bounce_lines final annoncé (si fallback).
   - Tâche LVGL : “pinned core=0”.
4. Observation écran : absence de dérive/shifting pendant UI et pendant opérations mémoire (NVS/flash). En cas de dérive résiduelle, tenter PCLK plus bas (ex. 18 MHz) ou augmenter `CONFIG_ARS_LCD_BOUNCE_BUFFER_LINES` si la SRAM le permet (les logs DMA indiquent la marge).

## Commandes utiles rollback
- Revenir à l’ancienne valeur PCLK : éditer `sdkconfig`/`sdkconfig.defaults` (`CONFIG_ARS_LCD_PCLK_HZ`) ou `idf.py menuconfig`.
- Forcer bounce fixe : définir `CONFIG_ARS_LCD_BOUNCE_BUFFER_LINES` plus petit ; la logique tentera cette valeur puis dégradera si insuffisant.
