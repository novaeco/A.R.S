# Rapport refactor LVGL DIRECT — état initial

## Cartographie rapide
- Port LVGL custom : `components/lvgl_port/` (`lvgl_port.c`, `lv_conf.h`, Kconfig dédié) avec flush `esp_lcd_panel_draw_bitmap` en mode PARTIAL et buffers lignes dynamiques.
- Driver RGB : `components/rgb_lcd_port/` (wrapper Waveshare) qui installe `esp_lcd_panel_rgb` avec 2 framebuffers PSRAM et VSYNC optionnel.
- Gestion écran/test : `components/board/src/board.c` appelle `waveshare_esp32_s3_rgb_lcd_init()`, force VDD, lance motif de test `board_lcd_test_pattern()` (RGBWB) sauf si `CONFIG_ARS_SKIP_TEST_PATTERN`.
- LVGL init/app : `main/main.c` branche UI via `lvgl_port_init()` après `app_board_init()`.
- Dépendance gérée : `main/idf_component.yml` référence `espressif/esp_lvgl_port@^2.7` en plus du port custom (risque de double configuration/LV_CONF concurrente). Aucun `managed_components/` présent mais la dépendance serait tirée au fetch.

## Config actuelle LCD
- Résolution : 1024×600 (BOARD_LCD_HRES/VRES).
- Timings : hsync pulse 20, back 140, front 160 ; vsync pulse 3, back 12, front 12 (`rgb_lcd_port.c`).
- PCLK : `BOARD_LCD_PCLK_HZ` fixé à 18 MHz (`components/board/include/board.h`).
- Polarité : `pclk_active_neg = 1` (défini en dur), polarités hsync/vsync/de non exposées.
- Buffers : `num_fbs = 2`, bounce buffer 10 lignes.

## Points concurrents / configurations
- LVGL config locale `components/lvgl_port/lv_conf.h` (LV_MEM_CUSTOM avec heap_caps_malloc) ; aucune trace d’un `lv_conf.h` du composant géré mais le `idf_component.yml` peut importer un second port LVGL avec son propre `lv_conf_internal.h` → conflit potentiel d’options (LV_MEM_CUSTOM, flush, tâches) si activé.
- Kconfig LVGL : options de VSYNC, double buffer (désactivé par défaut), lignes buffer=80, render mode PARTIAL implicitement dans le code.

## Emplacements clés
- flush_cb LVGL : `components/lvgl_port/lvgl_port.c` `flush_callback()` (copie vers panel via `esp_lcd_panel_draw_bitmap`, wait VSYNC optionnel, flush_ready immédiat).
- Test pattern LCD : `components/board/src/board.c` `board_lcd_test_pattern()` (PSRAM alloc 1024x600x2, barres RGBWB, delay 200 ms, non bloquant si skip flag).

## Diagnostic PCLK
- Budget requis 60 Hz @ 1024×600 avec Htotal 1344 et Vtotal 635 → `1344 × 635 × 60 ≈ 51.2 MHz`.
- Valeur actuelle : 18 MHz → sous-cadencé d’un facteur ~2.8, risque de mode latéral (refresh <60 Hz, blanking prolongé) et incompatibilités timing.

## Risques identifiés
- Conflit de ports LVGL si `espressif/esp_lvgl_port` est téléchargé (Kconfig et lv_conf concurrents).
- Pipeline actuel fait une copie ligne→panel (PARTIAL) alors que le driver RGB possède des framebuffers → risque d’écran figé si callbacks mal séquencés.
- PCLK trop bas + polarités figées : tearing/blank permanent ou image figée suivant la dalle.
- Test pattern conditionné par flag “skip” uniquement, pas de mode temporisé ni polarités testables.

## Plan de migration (P0/P1/P2)
- **P0 (analyse/doc)** : documenter l’état et les chemins (ce mémo), confirmer dépendances et calc PCLK.
- **P1 (timings/diagnostic)** : exposer `pclk_hz` et polarités clés via Kconfig, ajouter test pattern temporisé activable au boot (indépendant de LVGL), maintenir SD non bloquante.
- **P2 (LVGL DIRECT)** : basculer LVGL en `LV_DISPLAY_RENDER_MODE_DIRECT` en consommant les framebuffers `esp_lcd_panel_rgb`, refactor flush en commit/swap avec VSYNC optionnel, ajouter heartbeat UI Kconfig, nettoyer dépendance `esp_lvgl_port` pour éviter double port, sécuriser `lv_conf.h`.

## Fichiers ciblés (prévision)
- `components/board/include/board.h`, `components/board/src/board.c`, `components/board/Kconfig.projbuild`
- `components/rgb_lcd_port/*`
- `components/lvgl_port/*`
- `main/idf_component.yml`
- `docs/validation_lvgl_direct.md` (nouveau)
