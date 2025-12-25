# Pinout synthétique — Waveshare ESP32-S3 Touch LCD 7B

## Bus I²C (partagé)
- SDA: GPIO8
- SCL: GPIO9
- IO expander CH32V003 @ 0x24 (unique extension autorisée)
  - IOEXT1: reset tactile GT911
  - IOEXT2: backlight (actif haut)
  - IOEXT4: CS microSD SPI (actif bas)
  - IOEXT6: LCD VCOM/VDD enable
- GT911 tactile @ 0x5D, IRQ: GPIO4

## LCD RGB (esp_lcd_panel_rgb)
- Résolution: 1024×600, rotation par défaut: 180° (CONFIG_ARS_DISPLAY_ROTATION_180=y)
- Timings: HSYNC pulse 20, back porch 140, front porch 160; VSYNC pulse 3, back porch 12, front porch 12.
- VSYNC wait optionnelle via `CONFIG_ARS_VSYNC_WAIT_ENABLE`.

## MicroSD (SPI)
- MOSI: GPIO11
- MISO: GPIO13
- SCK: GPIO12
- CS: IOEXT4 (actif bas, via CH32V003)
- Init 100 kHz (ExtCS), montée jusqu’à 20 MHz après OCR si carte OK.

## Autres
- PSRAM 8 MB, Flash 16 MB.
- Backlight alimenté via IOEXT2, à activer après init écran.
- VCOM/VDD via IOEXT6 (mettre à 1 avant usage LCD).
