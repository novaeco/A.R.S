# Rapport de correction : Affichage RGB ESP32-S3 + LVGL Direct Mode

## 1. Contexte et Diagnostic

Le système souffrait d'artefacts visuels (tearing, parasites, bandes) sur un écran 1024x600 via interface RGB (ST7262) piloté par un ESP32-S3.

### Symptômes identifiés
* **Logs** : `LVGL DIRECT fb_count=2`, `VSYNC disabled`.
* **Code** :
    * `lvgl_port.c` : En mode DIRECT, la fonction `flush_callback` attendait la VSYNC puis appelait `lv_display_flush_ready`, **sans jamais ordonner au driver LCD de changer de buffer**. Le code contenant `esp_lcd_panel_draw_bitmap` était exclu par `if (!s_direct_mode)`.
    * `rgb_lcd_port.c` : Le paramètre `bounce_buffer_size_px` était commenté. Avec une PCLK de ~51.2 MHz et un framebuffer en PSRAM, l'absence de bounce buffer (SRAM) augmente considérablement le risque d'underrun (famine) du contrôleur LCD, causant des parasites horizontaux (bandes de bruit).

### Causes Racines
1.  **Absence de Swap Buffer** : En mode double-buffer direct, LVGL écrit dans le buffer inactif. Sans appel explicite au driver pour inverser les pointeurs (`esp_lcd_panel_draw_bitmap` ou `switch_buffer`), le LCD continue d'afficher l'ancien buffer (ou le buffer en cours d'écriture si mal synchronisé), créant du tearing.
2.  **Instabilité PSRAM/Bus** : L'accès direct DMA -> PSRAM à 51MHz est limite pour l'ESP32-S3 sans bounce buffer, surtout si le bus est partagé (Wi-Fi, etc.).

## 2. Correctifs Appliqués

### A) `lvgl_port.c` (Patch Critique)
*   **Correction du Flush** : Suppression de l'exclusion `if (!s_direct_mode)` pour l'appel au driver.
*   **Buffer Hand-off** : En mode DIRECT (`s_direct_mode`), appel de `esp_lcd_panel_draw_bitmap(0,0, W, H, px_map)` pour forcer le driver RGB à basculer sur le nouveau buffer (`px_map` est l'adresse du buffer complet).
*   **Synchronisation** : Maintien de l'attente VSYNC (si activée) pour éviter le tearing avant de notifier LVGL.

### B) `rgb_lcd_port.c` & `board`
*   **Activation Bounce Buffer** : Décommenté `.bounce_buffer_size_px` et assigné à `EXAMPLE_RGB_BOUNCE_BUFFER_SIZE`.
*   **Configuration Dynamique** : Ajout de `CONFIG_ARS_LCD_BOUNCE_BUFFER_LINES` (Kconfig) pour ajuster la taille du bounce buffer (défaut: 10 lignes). Cela alloue un tampon en SRAM interne pour lisser le flux de données vers le LCD.

### C) Configuration (Kconfig / sdkconfig)
*   `CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE=y` : Active l'écoute des interruptions VSYNC.
*   `CONFIG_ARS_LCD_WAIT_VSYNC=y` : LVGL attend le VSYNC matériel avant de libérer le buffer.

## 3. Risques et Limites
*   **Consommation SRAM** : Le bounce buffer de 10 lignes consomme `1024 * 10 * 2 octets = ~20 KB` de SRAM interne (DIRAM/SRAM1). C'est acceptable pour l'ESP32-S3 (qui a ~300KB+ de SRAM libre typiquement).
*   **PCLK** : 51.2 MHz est élevé. Si des parasites persistent malgré le bounce buffer, il faudra réduire le PCLK (via `CONFIG_ARS_LCD_PCLK_HZ`), par exemple à 40 MHz ou 28 MHz (mais le framerate 60Hz chutera).

## 4. Procédure de Validation

1.  **Nettoyage et Build** :
    ```bash
    idf.py fullclean
    idf.py build
    ```
2.  **Flash & Monitor** :
    ```bash
    idf.py -p COMx flash monitor
    ```
3.  **Critères de Succès** :
    *   [ ] **Boot** : Pas de crash "Guru Meditation" (sur allocation SRAM).
    *   [ ] **Log** : Vérifier la présence de :
        *   `RGB panel ready ... bounce_lines=10`
        *   `LVGL DIRECT mode ready: fb_count=2`
        *   Aucun log `Flush buffer not tracked` ou erreur I2C massive.
    *   [ ] **Visuel** :
        *   Image stable (pas de décalage horizontal/vertical).
        *   Pas de "neige" ou lignes aléatoires (signe de PSRAM bandwidth issue -> augmenter bounce lines ?).
        *   Pas de tearing (coupure horizontale) lors des animations rapides (si testé).

Si l'écran reste noir, vérifier log `Direct swap failed`.
Si instable, monter `ARS_LCD_BOUNCE_BUFFER_LINES` à 20 ou 40.
