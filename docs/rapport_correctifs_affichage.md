# Rapport correctifs affichage (RGB + LVGL)

## Causes identifiées
- Intégration LVGL/RGB non conforme au mode DIRECT : `flush_cb` recopiait les
  surfaces via `esp_lcd_panel_draw_bitmap()` alors que les framebuffers du driver
  étaient déjà exposés. Risque d'écrasement hors bande et d'écran noir sans
  visibilité sur le premier flush.
- Instrumentation manquante : difficile de savoir si le pipeline passait par
  VCOM/reset/backlight et si VSYNC était fonctionnel, rendant le diagnostic WDT
  et I2C compliqué.

## Correctifs appliqués
- Passage en DIRECT mode strict : LVGL écrit directement dans les framebuffers
  du driver RGB, `flush_cb` se limite à l'attente VSYNC bornée puis
  `lv_display_flush_ready()` ; fallback partiel conservé si framebuffers
  absents.
- Journalisation renforcée : logs ponctuels sur init panneau (timings/fb/bounce),
  VCOM/reset/backlight/test-pattern, entrée/sortie `lvgl_port_init`, mode
  direct/fallback, premier flush (zone/buffer/durée) et VSYNC manquant.
- Sécurité WDT : `flush_cb` non bloquant (timeout VSYNC), warnings uniques sur
  dérives de durée, tâche LVGL inchangée avec yield systématique.

## Risques résiduels / vérifications
- Vérifier en cible que le callback VSYNC est bien supporté par le driver RGB ;
  sinon un warning unique apparaît et le rendu reste immédiat (tearing possible).
- Si le panneau refuse encore d'afficher : contrôler que
  `rgb_lcd_port_get_framebuffers()` retourne au moins un buffer et que
  `CONFIG_ARS_LCD_WAIT_VSYNC` correspond au matériel.
