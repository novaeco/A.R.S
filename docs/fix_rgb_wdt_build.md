# Corrections techniques : RGB LCD WDT & Build

## Problème A : Crash Watchdog (Interrupt WDT)

### Cause racine
Le contrôleur LCD RGB de l'ESP32-S3 utilise un "bounce buffer" pour transférer les pixels via DMA. La configuration par défaut utilisait un buffer extrêmement petit (4 lignes ou 10 lignes).
*   **Impact :** Pour un écran 1024x600 @ 60Hz, avec 4 lignes de buffer, l'interruption "End of Frame" / "Empty" du GDMA se déclenche toutes les (600/4) * 60 = 9000 fois par seconde (approximatif, en réalité bursté).
*   Une telle fréquence d'interruption sature le CPU et déclenche l'Interrupt Watchdog Timeout.

### Correctifs appliqués
1.  **Augmentation du Bounce Buffer :**
    *   Configuration forcée à **40 lignes** dans `sdkconfig.defaults` (`CONFIG_ARS_LCD_BOUNCE_BUFFER_LINES=40`).
    *   Taille mémoire : 1024 pixels * 2 octets * 40 lignes = 80 KB (SRAM interne). C'est un compromis acceptable pour la stabilité.
    *   Réduit la fréquence d'interruption d'un facteur 10 vs la config par défaut (4 lignes).

2.  **Verrouillage Pixel Clock :**
    *   Maintien d'une fréquence raisonnable (~26 MHz ou configurable via `CONFIG_ARS_LCD_PCLK_HZ`) pour ne pas surcharger le bus.

## Problème B : Erreurs de Build (ESP-IDF 6.1)

### Causes
1.  **`esp_ipc` introuvable :** Ce composant a été déplacé/modifié dans IDF v6.1. Le composant `rgb_lcd_port` le demandait inutilement.
2.  **`esp_lcd_panel_rgb.h` missing :** Le header n'était pas exposé car la cible (Target) n'était pas fixée à `esp32s3`. En mode "guess" (esp32), la feature RGB est désactivée dans `esp_lcd`, masquant les headers.

### Correctifs appliqués
1.  **Target Lock :** Ajout de `CONFIG_IDF_TARGET="esp32s3"` dans `sdkconfig.defaults`.
2.  **Nettoyage CMake :** Suppression de `esp_ipc` dans `components/rgb_lcd_port/CMakeLists.txt`.

## Tuning et Paramètres

Si de nouveaux problèmes de stabilité surviennent, ajuster ces variables dans menuconfig ou `sdkconfig.defaults` :

*   **`CONFIG_ARS_LCD_BOUNCE_BUFFER_LINES`** :
    *   Augmenter (ex: 60) réduit encore la charge CPU mais consomme plus de SRAM.
    *   Diminuer (ex: 20) libère de la RAM mais augmente le risque de WDT.
*   **`CONFIG_ARS_LCD_PCLK_HZ`** :
    *   Réduire si artefacts visuels (flickering).

## Commandes de Build

```powershell
idf.py set-target esp32s3
idf.py clean
idf.py build
idf.py -p COMx flash monitor
```
