# Build/Flash/Monitor sous Windows (ESP-IDF 6.1+)

1. Ouvrir un terminal PowerShell dans le dossier du projet, sourcer l’environnement ESP-IDF :
   ```powershell
   $Env:IDF_PATH="C:\\esp\\idf"   # adapter si besoin
   . "$Env:IDF_PATH\\export.ps1"
   ```
2. Sélectionner la cible et nettoyer :
   ```powershell
   idf.py set-target esp32s3
   idf.py fullclean
   ```
3. Construire :
   ```powershell
   idf.py build
   ```
   Attendu : build sans warning “unknown symbol”, génération du binaire principal.
4. Flasher (adapter COMx) puis monitor :
   ```powershell
   idf.py -p COMx flash monitor
   ```
   Indicateurs de succès : boot sans panic, logs indiquant l’initialisation I²C (GPIO8/9), IO expander 0x24, affichage LVGL lancé. Si la carte SD est absente, message d’avertissement mais pas de reboot.

Notes matérielles :
- Bus I²C partagé (GPIO8/9) protégé par mutex unique pour GT911 + CH32V003.
- SDSPI : MOSI=11, MISO=13, SCLK=12, CS réel via EXIO4 maintenu bas (bus SD dédié), CS factice GPIO6 pour l’hôte SDSPI.
- Alimentation LCD : EXIO6 (VDD/VCOM) et EXIO2 (backlight) sont activés au boot.
