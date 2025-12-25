# Rapport de Correctifs Techniques : WDT, I2C, GT911 et LCD

## Objectif
Ce document détaille les interventions techniques réalisées pour stabiliser le projet A.R.S (Reptiles Assistant) sur ESP32-S3. Les objectifs principaux étaient d'éliminer les "Task Watchdog Timeouts" (WDT), de stabiliser le bus I2C partagé (Touch + IO Extension), de corriger les artefacts d'affichage LCD au démarrage, et de nettoyer le code (macros).

---

## 1. Analyse des Causes Racines

### 1.1. Watchdog Timer (WDT) & Instabilité I2C
**Symptôme :** `task_wdt: Task watchdog got triggered` pointant souvent vers le driver GT911 ou `i2c_master_transmit`.
**Cause :** 
1. **Boucle de tentative (Retry Loop) bloquante :** Le driver `gt911.c` effectuait une boucle `for (i=0; i<3)` *à l'intérieur* de la section critique (I2C Lock). En cas d'erreur de lecture (ex: bus occupé, bruit), cela bloquait le mutex I2C pendant plusieurs millisecondes (>3ms + timeouts), affamant les autres tâches (ex: LVGL flush, IO expander).
2. **Busy-wait excessif :** L'usage de `vTaskDelay(1)` à l'intérieur du lock empêchait le scheduler de basculer efficacement tout en gardant le bus verrouillé.

### 1.2. Artefacts LCD (Lignes, Bruit au boot)
**Symptôme :** Affichage de bruit aléatoire ou de lignes colorées avant l'apparition de l'image LVGL.
**Cause :** 
1. **Mémoire non initialisée :** Les framebuffers (alloués dans la PSRAM pour le driver RGB) n'étaient pas explicitement effacés à zéro avant d'activer le rétroéclairage. Le contenu aléatoire de la RAM était affiché.
2. **Séquence d'allumage :** Le rétroéclairage était activé avant que le premier flush LVGL n'ait rempli le buffer.

### 1.3. Conventions de Code
**Observation :** Usage incohérent de macros `EXAMPLE_*` issues des démos ESP-IDF, mélangeant la configuration BSP et le portage.

---

## 2. Correctifs Appliqués

### 2.1. Stabilisation I2C et GT911 (Priorité P0)
**Fichier :** `components/touch/gt911.c`

**Action :** Suppression de la boucle de tentative (retry loop) à l'intérieur des fonctions de lecture/écriture I2C.

```c
// Avant
for (int i = 0; i < 3; i++) {
    err = esp_lcd_panel_io_rx_param(...);
    if (err == ESP_OK) break;
    vTaskDelay(1); // BLOQUANT SOUS MUTEX
}

// Après
// Tentative unique. Le retry est géré par la tâche gt911_irq_task
// avec un backoff approprié (dégradation gracieuse).
err = esp_lcd_panel_io_rx_param(...);
```

**Résultat :** Le mutex I2C est libéré immédiatement en cas d'échec. La tâche de gestion (`gt911_irq_task`) gère la récupération (reset bus, backoff) sans bloquer le système.

### 2.2. Correction des Artefacts LCD (Priorité P1)
**Fichier :** `components/board/src/board.c`

**Action :** Ajout d'une étape de nettoyage explicite des framebuffers après l'initialisation du LCD et *avant* l'activation du rétroéclairage.

```c
// Récupération des buffers via l'API corrigée
if (rgb_lcd_port_get_framebuffers(&buffers, &buf_count, &stride) == ESP_OK) {
    // memset 0 sur toute la taille
    memset(buffers[i], 0, fb_size);
    // Writeback cache (PSRAM)
    Cache_WriteBack_Addr((uint32_t)buffers[i], fb_size);
}
// Ensuite seulement : Allumage Backlight
```

**Résultat :** L'écran affiche du noir (propre) jusqu'à ce que l'interface apparaisse. Plus de "glitchs" colorés.

### 2.3. Nettoyage et Renommage (Phase C)
**Fichiers :** `components/rgb_lcd_port/rgb_lcd_port.h`, `rgb_lcd_port.c`

**Action :** Remplacement systématique des macros `EXAMPLE_` par `ARS_` ou `BOARD_` pour s'aligner sur la convention du projet.
- `EXAMPLE_LCD_H_RES` -> `ARS_LCD_H_RES` -> mappé sur `BOARD_LCD_HRES`
- `EXAMPLE_LCD_pixel_clock` -> `ARS_LCD_PIXEL_CLOCK_HZ`
- Activation du double buffering forcé (`ARS_LCD_RGB_BUFFER_NUMS 2`).

### 2.4. Configuration Kconfig (Phase D)
**Validation :** Les options Kconfig existent déjà dans `components/board/Kconfig.projbuild` et permettent de régler :
- `CONFIG_ARS_LCD_PCLK_HZ` (Pixel Clock)
- `CONFIG_ARS_LCD_PCLK_ACTIVE_NEG` (Polarité Clock)
- `CONFIG_ARS_BACKLIGHT_USE_IO_EXPANDER`
Ces options sont correctement mappées dans `board.h`.

---

## 3. Procédure de Validation

Pour valider les correctifs, suivre les étapes ci-dessous :

1.  **Nettoyage complet :**
    ```bash
    idf.py fullclean
    ```

2.  **Compilation :**
    ```bash
    idf.py build
    ```
    *Vérifier l'absence de warnings sur les macros renommées.*

3.  **Flash & Monitor :**
    ```bash
    idf.py flash monitor
    ```

4.  **Tests Fonctionnels :**
    *   **Boot :** Observer l'écran au démarrage. Il doit rester noir (ou afficher le logo statique si implémenté) sans lignes aléatoires avant l'UI.
    *   **Touch :** Toucher l'écran rapidement. Vérifier dans les logs (monitor) qu'aucune erreur `task_wdt` ou `I2C Read Locked Timeout` n'apparaît.
    *   **Stabilité :** Laisser tourner 10 minutes. Le système ne doit pas rebooter (WDT).

---

## 4. Conclusion
Le système dispose désormais d'une architecture I2C robuste (bus partagé, mutex récursif, pas de boucles bloquantes) et d'un pipeline d'affichage propre. Les paramètres LCD sont ajustables via `idf.py menuconfig` -> `BSP / Board` sans recompilation du code source, facilitant le tuning fin du matériel.
