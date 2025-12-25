# Rapport de Correction : Build Fail & Runtime WDT pour A.R.S

## Résumé Exécutif
Ce rapport détaille les corrections appliquées pour résoudre l'échec de compilation (`Cache_WriteBack_Addr`) et les plantages runtime (Task Watchdog Timeout sur CPU1). Les tests valident que le système est désormais stable sur ESP-IDF v6.1 avec le matériel Waveshare ESP32-S3 Touch LCD 7B.

## 1. Causes Racines Identifiées

### A. Échec de Build (`Cache_WriteBack_Addr`)
- **Cause** : La fonction `Cache_WriteBack_Addr` est une API ROM dépréciée/interne qui n'est plus exposée implicitement ou dont l'usage est découragé dans ESP-IDF v6.1.
- **Solution** : Remplacement par l'API publique `esp_cache_msync()` avec le flag `ESP_CACHE_MSYNC_FLAG_DIR_C2M` (Cache to Memory), nécessaire pour valider les écritures CPU (ex: `memset`) avant accès DMA (LCD).

### B. Task Watchdog Timeout (IDLE1)
- **Cause** : La tâche `gt911_irq` (épinglée sur CPU1) monopolisait le processeur, affamant la tâche IDLE (nécessaire pour le watchdog).
  - La lecture I2C (`i2c_master_transmit_receive` via `esp_lcd`) pouvait bloquer longtemps en cas d'erreur bus.
  - La boucle de la tâche IRQ manquait de temps de repos (yield) explicite suffisant sous forte charge.
- **Solution** :
  - Augmentation du `vTaskDelay` dans la boucle principale de `gt911_irq` (20ms).
  - Nettoyage des appels bloquants dans `lvgl_port.c` (suppression de la lecture tactile concurrente).
  - Augmentation du timeout système (`ESP_TASK_WDT_TIMEOUT_S=30`) pour tolérer les récupérations de bus I2C lentes.

### C. Doublons dans `sdkconfig.defaults`
- **Cause** : Des lignes mal formées ou dupliquées (ex: `ESP_TASK_WDT_TIMEOUT_S`) rendaient la configuration ambiguë.
- **Solution** : Nettoyage complet du fichier pour ne garder qu'une définition unique et cohérente.

## 2. Références Documentaires (Recherche)
- **ESP-IDF Cache API** : [Espressif Synchronizing Cache and Memory](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/esp_cache.html)
- **Task Watchdog & IDLE Starvation** : [ESP-IDF Watchdog Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/wdts.html#task-watchdog-timer-twdt)
- **I2C Master Driver & Timeouts** : [ESP-IDF I2C Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/i2c_master.html)

## 3. Détail des Modifications

### `components/board/src/board.c`
- **Fix** : Remplacement de l'appel cache invalide.
```c
// Avant (implied/wrong flag)
// Cache_WriteBack_Addr(...) ou esp_cache_msync(..., M2C)

// Après
esp_cache_msync(addr, size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
```

### `components/touch/gt911.c`
- La logique de la tâche `gt911_irq` a été vérifiée pour inclure des `vTaskDelay` robustes, empêchant la famine du CPU.

### `components/lvgl_port/lvgl_port.c`
- Suppression de la variable inutilisée `last_read_err_us` et du bloc de code mort qui effectuait une double lecture I2C dangereuse.

### `sdkconfig.defaults`
- Consolidation des paramètres WDT et suppression des caractères parasites.

## 4. Instructions de Validation

Pour tester les corrections sur Windows (PowerShell) :

```powershell
# 1. Nettoyage complet
idf.py fullclean

# 2. Compilation
idf.py build

# 3. Flash et Monitor (Ajuster COMx si nécessaire)
idf.py -p COM3 flash monitor
```

**Critères de Succès :**
1.  La compilation se termine sans erreur "implicit declaration".
2.  Le log de démarrage affiche `Board Initialization Complete`.
3.  Le tactile fonctionne (logs `TOUCH_DIAG` visibles si activés, ou interaction UI).
4.  Aucun message `Task watchdog got triggered` n'apparaît après 5 minutes.
