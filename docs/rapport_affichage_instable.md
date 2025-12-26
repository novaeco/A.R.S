# Rapport affichage instable – ESP32-S3 Touch LCD 7B (1024×600 RGB)

## 1. Reproduction
- Matériel : Waveshare ESP32-S3 Touch LCD 7B (ST7262, interface RGB565, PSRAM octal 80 MHz, écran 1024×600).
- Logiciel : ESP-IDF 6.1, LVGL 9.4, PCLK initial = 26 MHz.
- Symptôme terrain : l’image “glisse/décale” en continu (drift/frame shift).

## 2. Paramètres relevés dans le dépôt (avant correctif)
- Timings `esp_lcd_rgb_panel_config_t` (components/rgb_lcd_port/rgb_lcd_port.c) :
  - hsync_pulse_width=20, hsync_back_porch=140, hsync_front_porch=160
  - vsync_pulse_width=3, vsync_back_porch=12, vsync_front_porch=12
  - polarités : pclk_active_neg=1, hsync_idle_low=0, vsync_idle_low=0, de_idle_high=0
- Pixel clock effective (`sdkconfig`) : **26 MHz** (CONFIG_ARS_LCD_PCLK_HZ).
- Buffers :
  - Double framebuffer PSRAM (RGB565), bounce buffer 20 lignes SRAM (CONFIG_ARS_LCD_BOUNCE_BUFFER_LINES=20).
  - LVGL en mode DIRECT (double buffer natif si disponible).

## 3. Références officielles
- FAQ Espressif “RGB screen drift” : max PCLK ≈ **22 MHz** avec PSRAM octal @80 MHz et bounce buffer obligatoire + cache 64 B. Source : https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html#why-do-i-get-drift-overall-drift-of-the-display-when-esp32-s3-is-driving-an-rgb-lcd-screen (lignes indiquant PCLK 22 MHz pour octal 80 MHz et nécessité `CONFIG_ESP32S3_DATA_CACHE_LINE_64B`).
- Wiki Waveshare ESP32-S3 Touch LCD 7B : carte 1024×600, interface RGB565, I2C partagé, IO expander CH32V003 (adresse 0x24). Source : https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7B

## 4. Diagnostic (preuves)
- Divergence PCLK : 26 MHz > 22 MHz (limite FAQ pour PSRAM octal 80 MHz) ⇒ risque de drift global malgré bounce buffer.
- Timings/polarités conformes aux valeurs Waveshare ST7262 déjà utilisées ; pas d’anomalie relevée sur porches.
- LVGL flush : mode DIRECT avec double framebuffer, `esp_lcd_panel_draw_bitmap` appelé ; VSYNC wait optionnelle (active par défaut). Rien d’anormal côté pipeline.

Cause racine la plus probable (P0) : PCLK au-dessus de la limite recommandée pour PSRAM octal 80 MHz, conduisant à des pertes de données (drift) malgré le bounce buffer.

## 5. Correctifs appliqués
1) **Abaissement PCLK** à 21 MHz (`sdkconfig`: CONFIG_ARS_LCD_PCLK_HZ=21000000) pour repasser sous le plafond 22 MHz recommandé par Espressif (FAQ drift).
2) **Diagnostics timings** : log détaillé des porches/polarités + alerte runtime si PCLK > 22 MHz avec PSRAM octal 80 MHz (rgb_lcd_port.c).
3) **Auto-test stabilité optionnel** : nouveau Kconfig `CONFIG_ARS_LCD_STABILITY_SELFTEST` (désactivé par défaut). Si activé, tâche dédiée qui écrit un motif fixe (bandes R/G/B/W/K + carré jaune centré) dans le framebuffer natif et le maintient 60 s pour vérifier l’absence de drift (sans bloquer l’initialisation principale).

Valeurs après correctif :
- PCLK = **21 MHz**
- Timings inchangés (pw/back/front : 20/140/160, 3/12/12 ; polarités idem).
- Buffers : double FB PSRAM, bounce buffer 20 lignes.

## 6. Risques / régressions
- Framerate théorique réduit (21 MHz au lieu de 26 MHz) mais stabilité accrue ; UI restera fluide (≈25–30 fps).
- Auto-test activable rallonge le démarrage de 60 s : option désactivée par défaut pour ne pas impacter les builds standard.
- Aucun changement sur touch/SD/IO expander.

## 7. Plan de validation
Commandes :
```bash
idf.py fullclean
idf.py build
# Optionnel matériel
idf.py -p COMx flash monitor
```
Critères “Done” :
- Boot sans panic, logs affichant :
  - `RGB panel config ... pclk=21000000Hz ...`
  - `RGB timings: HSYNC pw=20 bp=140 fp=160 | VSYNC pw=3 bp=12 fp=12`
  - (si auto-test activé) `Starting LCD stability self-test (60s)...` puis `complete`
- Visuel : motif fixe sans déplacement pendant 60 s ; en usage normal, absence de drift.
- UI, tactile et SD toujours opérationnels (ou échec toléré pour SD sans panic).
