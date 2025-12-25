# Rapport de correction TWDT / I2C / GT911

**Date** : 2025-12-24
**Sujet** : Correction des Task Watchdog Timeouts (TWDT) sur Waveshare ESP32-S3 Touch LCD 7B avec I2C partagé.

## 1. Analyse des Causes Racines

Les déclenchements TWDT (CPU1 bloqué) observés ont pour origine principale un **interblocage (deadlock) et une famine** sur le bus I2C partagé et le processeur :

1.  **Utilisation incorrecte des Mutex Récursifs (I2C Shared)** :
    *   L'utilisation de `xSemaphoreCreateRecursiveMutex` permettait à une tâche (ex: `gt911_irq_task`) de reprendre le verrou indéfiniment sans le libérer correctement si une erreur survenait au niveau logique, ou simplement masquer des appels imbriqués non maîtrisés (ex: `update_config` appelant `read_data`).
    *   Sur le driver ESP-IDF v6, le blocage dans `esp_lcd_panel_io_rx_param` sans timeout strict géré par l'application causait des attentes infinies.

2.  **Boucle infinie potentielle dans `gt911_irq_task`** :
    *   La tâche pouvait entrer dans une boucle serrée de lecture I2C en cas de flood d'interruptions (IRQ storm) sans rendre la main explicitement à l'OS (`vTaskDelay(1)` insuffisant ou absent au mauvais endroit), affamant la tâche IDLE et déclenchant le TWDT.

3.  **Initialisation lourde de l'écran** :
    *   Le nettoyage des framebuffers (1024x600x2 octets x 2 buffers ≈ 2.4 Mo) se faisait via un `memset` monolithique. Sur ESP32S3, cela monopolise le bus mémoire et le CPU pendant plusieurs dizaines de millisecondes, déclenchant le TWDT si d'autres tâches (comme le Wifi ou le Touch) sont affamées.

## 2. Correctifs Appliqués

### A. Bus I2C Partagé (`i2c_bus_shared.c`)
*   **Migration vers Mutex Standard** : Remplacement de `RecursiveMutex` par `Mutex` standard. Cela impose une discipline stricte : toute ré-entrance est désormais interdite et détectable immédiatement (retour erreur).
*   **Timeouts Finis** : `i2c_bus_shared_lock` utilise désormais un timeout fini (ex: 200ms) et jamais `portMAX_DELAY` pour éviter les blocages éternels.

### B. Driver GT911 (`gt911.c`)
*   **Séparation Lock / Logique** : Création de fonctions internes `_internal` (sans lock) et publiques (avec lock).
*   **Correction `gt911_update_config`** : Utilise désormais les fonctions internes pour éviter de locker deux fois (ce qui causerait un deadlock avec le nouveau Mutex non-récursif).
*   **Yielding Strict** : Ajout de `vTaskDelay(1)` et `taskYIELD()` explicites dans la boucle `gt911_irq_task` pour garantir l'exécution de l'IDLE task.
*   **Protection Flood** : La tâche attend désormais une notification bloquante (`ulTaskNotifyTake`) mais gère le cas des notifications multiples.

### C. Board Init (`board.c`)
*   **Chunked Memset** : Le nettoyage des framebuffers est découpé en blocs de 32 Ko. Entre chaque bloc, un `vTaskDelay(1)` est inséré pour laisser respirer le système et réinitialiser le chien de garde logiciel.

## 3. Risques et Plan de Rollback

*   **Risque** : `ESP_ERR_TIMEOUT` plus fréquents si le bus est très chargé (car on ne bloque plus à l'infini).
    *   *Atténuation* : Les retries sont gérés par le driver tactile (backoff).
*   **Risque** : Deadlock si une fonction oubliée appelle `read/write` alors qu'elle détient déjà le verrou.
    *   *Atténuation* : L'analyse statique a couvert `gt911.c`. Si cela arrive, la fonction retournera `ESP_ERR_TIMEOUT` après 200ms au lieu de bloquer à vie, ce qui est préférable.

**Rollback** :
Si le système devient instable (I2C timeouts en boucle), remettre `RecursiveMutex` dans `i2c_bus_shared.c` et restaurer les appels imbriqués dans `gt911.c`.

## 4. Validation

1.  **Build** : `idf.py build`
2.  **Flash** : `idf.py -p COMx flash monitor`
3.  **Test Runtime** :
    *   Vérifier logs boot : "Shared I2C bus initialized successfully".
    *   Vérifier logs touch : "GT911 Configuration Updated Successfully".
    *   Attendre 3 minutes. Vérifier absence de message "Task watchdog got triggered".
    *   Toucher l'écran : vérifier les logs de coordonnées.

## 5. Fichiers Modifiés
- `components/i2c/i2c_bus_shared.c`
- `components/touch/gt911.c`
- `components/board/src/board.c`
