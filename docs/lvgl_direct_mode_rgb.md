# LVGL DIRECT mode sur panneau RGB (ESP32-S3)

## Architecture d'affichage
- Panneau RGB 1024x600 piloté via `esp_lcd_new_rgb_panel()`.
- Framebuffers alloués par le driver RGB (PSRAM) et exposés via
  `rgb_lcd_port_get_framebuffers()`. LVGL reçoit les pointeurs bruts, ne les
  libère pas.
- `lv_display_set_buffers(..., LV_DISPLAY_RENDER_MODE_DIRECT)` utilise
  `fb0/fb1` (selon `BOARD_LCD_RGB_BUFFER_NUMS`). Aucun memcpy intermédiaire en
  mode direct.

## Mode DIRECT vs ancien mode
- **DIRECT** : LVGL dessine directement dans les framebuffers du driver RGB.
  `flush_cb` se contente de synchroniser VSYNC puis appelle
  `lv_display_flush_ready()`.
- **Fallback PARTIAL** : si les framebuffers ne sont pas exposés, LVGL alloue un
  ou deux buffers de lignes et `flush_cb` appelle
  `esp_lcd_panel_draw_bitmap()` (copie partielle) avec timeout VSYNC borné.

## Ownership & mémoire
- Allocation des framebuffers : driver RGB (PSRAM si disponible).
- LVGL : usage en lecture/écriture uniquement, aucun `free`.
- Stride fourni par `rgb_lcd_port_get_framebuffers(..., stride_bytes)`
  (`h_res * bpp/8`).

## Anti-tearing / VSYNC
- Le callback VSYNC du driver notifie `lvgl_port_notify_rgb_vsync()`
  (sémaphore). `flush_cb` attend un VSYNC avec timeout
  `CONFIG_ARS_LCD_WAIT_VSYNC_TIMEOUT_MS` (désactivation automatique après
  timeout pour éviter les blocages).
- Si VSYNC indisponible : `flush_ready()` immédiat et warning unique.

## Points de debug (logs clés)
- `rgb_lcd`: configuration du panneau (timings, fb, bounce) et adresses des
  framebuffers.
- `board`: VCOM/VDD, reset LCD, backlight, test pattern.
- `lv_port`: entrée/sortie `lvgl_port_init`, mode direct/fallback, premier flush
  (zone, buffer, durée). Warnings si buffer inconnu ou VSYNC absent.

## Usage rapide
1. Initialiser le panneau via `app_board_init()` (alim + reset + backlight).
2. `lvgl_port_init(panel, touch)` configure les buffers LVGL en DIRECT et lance
   la tâche LVGL (core configuré).
3. Activer `CONFIG_ARS_LVGL_DEBUG_SCREEN` pour vérifier l'affichage minimal
   "LVGL DIRECT OK".
