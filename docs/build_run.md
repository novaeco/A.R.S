# Build & Run

## Prérequis
- ESP-IDF 6.1 (toolchain ESP32-S3) avec LVGL 9.x (port mono-contexte fourni par `lvgl_port`).
- Flash 16 MB (DIO) + PSRAM octal 80 MHz (activés dans `sdkconfig.defaults`).
- Python et outils `idf.py` disponibles dans le PATH.

## Procédure standard
```bash
idf.py fullclean
idf.py build
idf.py -p /dev/ttyUSBx flash monitor
```

### Critères de succès à observer dans les logs série
- `Board initialized`, `display_driver_init`, `lvgl_port_init` puis `UI started`.
- La barre haute affiche `Wi-Fi: --`, `SD: montée/absente`, une horloge qui tourne.
- En absence de SD : warning `SD card not mounted` ou `SD card not detected`, pas de panic et boucle principale continue.

### Rollback rapide
- `git clean -fdx && git checkout -- sdkconfig.defaults partitions.csv` pour revenir à l'état de référence.
- Rejouer `idf.py fullclean build` ensuite.

## Stratégie LVGL mono-contexte
- Toutes les créations/updates d'objets passent par le verrou `lvgl_port_lock` dans `ui_app_start`, plus un `lv_timer` (1 Hz) pour rafraîchir les statuts top bar.
- Les callbacks de navigation LVGL restent dans la tâche LVGL (pas d'accès concurrent aux buffers).
- Buffer vidéo partiel (`CONFIG_LVGL_LCD_BUF_SIZE=786432`) en PSRAM, compatible écran 1024×600 RGB.

## Contraintes temps-réel I²C / SD
- Bus I²C partagé (GT911 + IO extender) : éviter les traitements longs côté callback, privilégier timers/tâches dédiées.
- SD en SDSPI : init non bloquante, CS via IO extender, montée optionnelle (log puis continue en cas d'absence/erreur).

## Partitions & configuration
- `factory` porté à 4M pour absorber l'UI LVGL (flash 16M requise).
- `storage` (SPIFFS) 1M pour snapshots internes, `userdata` (FAT) 8M pour documents/exports.
- `sdkconfig.defaults` force tick LVGL 1 kHz, PSRAM, flash 16M.
