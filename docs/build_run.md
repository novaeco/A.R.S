# Build & Run

## Prérequis
- ESP-IDF 6.1 (ou 5.2+ avec LVGL 9 via composant géré).
- Toolchain ESP32-S3 + Python requis par `idf.py`.

## Commandes
```bash
idf.py fullclean
idf.py build
idf.py -p /dev/ttyUSBx flash monitor
```
- Succès attendu :
  - Logs indiquant `LVGL initialized` puis `UI started`.
  - Si SD absente : warning "SD card not detected" mais boot continue.

## Variables importantes
- `sdkconfig.defaults` force PSRAM et LVGL tick custom 1kHz.
- `partitions.csv` inclut SPIFFS (storage) et FAT (userdata) pour documents.

## Rollback
- `git clean -fdx` pour repartir d'un arbre propre.
- Rejouer `idf.py fullclean build` après modification de `sdkconfig.defaults`.
