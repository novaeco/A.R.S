# Rapport correctifs build et BSP (Waveshare ESP32-S3 Touch LCD 7B)

## Cause racine
- Échec de build sur `driver/spi_common.h` car le composant `board` ne dépendait pas de `esp_driver_spi` (ESP-IDF 6.x sépare les headers par composant).
- `sdkconfig.defaults` contenait des symboles obsolètes/doublons (`CONFIG_ESP32S3_*_PSRAM`, double choix FATFS LFN, options LVGL non déclarées).
- Pinmap générique ne correspondait pas au câblage Waveshare 7B (I²C sur GPIO8/9, IRQ tactile sur GPIO4, CS SD via expander, alimentation LCD via expander).

## Correctifs apportés
- Déclarations CMake mises à jour : `board`, `sd`, `touch`, `i2c_bus_shared`, `io_extension` explicitent les dépendances `esp_driver_spi`, `esp_driver_gpio`, `esp_driver_i2c`, `esp_driver_sdspi` selon les headers utilisés.
- `board` refactoré : `board_pins.h` documente le pinout officiel, initialisation I²C partagée (GPIO8/9) + IO expander CH32V003 @0x24, helpers pour puissance LCD/backlight, reset tactile, CS SD (actif bas) via expander.
- `touch` : IRQ sur GPIO4 (ISR minimale sans I²C), reset GT911 via EXIO1, bus I²C partagé avec mutex unique.
- `sd` : SDSPI sur GPIO11/12/13 avec CS réel sur EXIO4 maintenu bas (Option A, bus SD dédié) et CS "dummy" GPIO6 pour satisfaire l’hôte SDSPI. Échec de montage = log + libération bus.
- `sdkconfig.defaults` nettoyé pour IDF 6.1+ (PSRAM auto octal, LFN sur heap, cibles esp32s3, flash 16MB, FreeRTOS 1 kHz, log INFO).

## Points d’attention
- Aucun accès LVGL depuis les drivers (touch/display séparés).
- Mutex I²C unique réutilisé par le CH32V003 et le GT911.
- CH422G proscrit (aucune référence ajoutée).
