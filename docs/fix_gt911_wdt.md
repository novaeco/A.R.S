# Correctif GT911 Watchdog et Stabilité (ESP-IDF v6.1)

Ce document décrit les correctifs appliqués pour résoudre les erreurs de compilation et les déclenchements du Task Watchdog (WDT) liés au pilote tactile GT911 et à la gestion du bus I2C partagé sur projet A.R.S.

## Cause Racine
1.  **Erreur de Build**: La variable `s_spurious_block_until_us` était utilisée mais non déclarée dans `components/touch/gt911.c`, bloquant la compilation.
2.  **Task Watchdog (IDLE1 Starvation)**:
    - La tâche `gt911_irq_task` pouvait entrer dans une boucle serrée (busy loop) si l'IRQ restait actif ou en cas de rafale (storm), affamant le CPU1 (IDLE1).
    - La tâche TCP/IP (`tcpip_thread`) était fixée sur le CPU1 via `sdkconfig.defaults`, créant une contention avec l'interface utilisateur et le driver tactile.
3.  **Politique I2C**: L'accès concurrent au bus I2C entre l'IO Expander (CH32V003 @ 0x24) et le GT911 (@ 0x5D/0x14) nécessitait une validation stricte du verrouillage (mutex).

## Fichiers Modifiés

### 1. `components/touch/gt911.c`
- **Ajout Déclaration Correcte**: `static int64_t s_spurious_block_until_us = 0;` ajouté (Ligne ~81).
- **Fix Logic `gt911_irq_task`**:
    - Utilisation de `ulTaskNotifyTake(pdTRUE, wait_ticks)` pour bloquer efficacement la tâche en attente d'interruptions.
    - Ajout d'une garde: `if (notified == 0 && wait_ticks == portMAX_DELAY) continue;`.
    - Protection anti-rafale : si `s_spurious_block_until_us > now`, la tâche yield (`vTaskDelay`) et ignore l'IRQ temporairement.
    - Suppression warning compilation sur variable `notified`.

### 2. `sdkconfig.defaults`
- **Rééquilibrage Affinité CPU**:
    - `CONFIG_LWIP_TCPIP_TASK_AFFINITY` passé de `CPU1` à `CPU0`.
    - Ceci libère le CPU1 pour les tâches critiques UI (LVGL) et Touch, réduisant le risque de WDT sur IDLE1.

### 3. `components/io_extension/io_extension.c`
- Vérification que tous les accès I2C utilisent `i2c_bus_shared_lock` avec timeout borné (`pdMS_TO_TICKS(200)`). (Pas de chgt nécessaire, code existant validé).

## Comment Tester

1.  **Nettoyage et Build**:
    ```powershell
    idf.py fullclean
    idf.py build
    ```
    *Le build doit réussir sans erreur `s_spurious_block_until_us undeclared`.*

2.  **Runtime**:
    ```powershell
    idf.py -p COMx flash monitor
    ```
    - Observer les logs au démarrage.
    - Vérifier l'absence de message: `Task watchdog got triggered. The following tasks did not reset the watchdog in time:`.
    - Vérifier que IDLE1 et IDLE0 apparaissent dans `tasks` (si command `tasks` dispo) ou simplement que le système ne reboote pas.
    - Toucher l'écran : le tactile doit répondre sans provoquer de crash.

## Critère de Succès
- **Compilation OK**.
- **Stabilité > 5 minutes** avec interactions tactiles fréquentes sans trigger WDT.
