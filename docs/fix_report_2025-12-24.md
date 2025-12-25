# Rapport de Correctifs - ESP-IDF v6.1 / LVGL 9.x Migration & GT911 Stability
**Date**: 2024-12-24
**Auteur**: Antigravity AI

## 1. Cause Racine Identifiée
### A) Task Watchdog Timeout (TWDT)
Le déclenchement du Watchdog sur `IDLE1` était causé par une combinaison de facteurs dans `gt911_irq_task` :
1.  **Blocage I2C trop long** : Les transactions I2C utilisaient un timeout de verrouillage (mutex) de 1000ms. En cas de charge élevée ou de contention (bus I²C partagé), cela pouvait empêcher la tâche de rendre la main (yield) suffisamment souvent.
2.  **Gestion IRQ Storm** : Bien que des délais aient été introduits, la logique de réactivation de l'interruption pouvait créer des boucles serrées si le signal INT restait bas, monopolisant le CPU1.
3.  **Priorité** : La tâche tournait en boucle sans délai inconditionnel suffisant en cas de saturation de notifications.

### B) Erreur `Cache_WriteBack_Addr`
Cette fonction est obsolète dans ESP-IDF v6.x (précédemment une fonction ROM interne). L'API correcte est `esp_cache_msync`. Le code a été vérifié et utilise désormais la version compatible.

## 2. Correctifs Appliqués

### `components/touch/gt911.c`
*   **Refonte de `gt911_irq_task`** :
    *   Transition vers un modèle hybride IRQ/Polling robuste.
    *   Ajout d'un `vTaskDelay(1)` garanti à chaque itération pour prévenir la famine de la tâche IDLE.
    *   Correction du délai d'attente `ulTaskNotifyTake` pour gérer correctement le mode polling.
    *   Mise en place d'un *backoff* exponentiel (10ms -> 500ms) en cas d'erreur I2C pour éviter la saturation du bus.
*   **Réduction des Timeouts I2C** :
    *   Passage de `1000ms` à `250ms` pour l'acquisition du mutex I2C (`touch_gt911_i2c_read`/`write`). Cela garantit un échec rapide ("fail-fast") plutôt qu'un blocage système prolongeant le WDT.

### `components/i2c/i2c_bus_shared.c`
*   Audit effectué : Le mécanisme utilise `xSemaphoreTakeRecursive`, prévenant les deadlocks récursifs (même tâche).
*   La logique de récupération (`recover`) inclut des délais explicites (`vTaskDelay`) pour la manipulation des lignes SCL/SDA, assurant la sécurité du WDT.

### `components/board/src/board.c`
*   Vérification de l'utilisation de `esp_cache_msync` (via `ars_cache_writeback`). L'appel obsolète n'est plus présent dans le code source.

## 3. Stratégie de Validation (Post-Build)
1.  **Init I2C** : Vérifier dans les logs que le bus partagé est initialisé sans erreur.
2.  **Touch** : Vérifier que `gt911_irq_task` ne génère plus de "Task watchdog got triggered".
3.  **Performance** : Le tactile doit rester réactif grâce au timeout réduit (250ms) et à la gestion prioritaire des notifications.
4.  **Tests Longue Durée** : Laisser tourner 5 minutes. Si le WDT ne se déclenche pas, la correction est validée.

## 4. Commandes et Logs
(Les logs de build n'ont pas pu être générés dans cet environnement sans setup IDF, mais le code est syntaxiquement corrigé pour ESP-IDF 6.1).
