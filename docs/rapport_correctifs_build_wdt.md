# Rapport Correctifs : Build & WDT (ESP32-S3 / GT911 / IOEXT)

## 1. Cause Racine

### A. Build (io_extension.c)
- **Symptôme** : Erreurs de syntaxe "unexpected token", "initializer element not constant".
- **Analyse** : Le code source contenait probablement des corruptions structurelles (accolades fermantes manquantes ou en trop) amenant du code exécutable (appels de fonctions, logique conditionnelle) à se retrouver hors de tout scope de fonction (niveau global), ce qui est interdit en C. De plus, la gestion des verrous I2C (`lock`/`unlock`) manquait de robustesse (risques de retour anticipé sans unlock).

### B. Instabilité Runtime / Watchdog (IDLE1 Starvation)
- **Symptôme** : `Task watchdog got triggered. The following tasks did not reset the watchdog in time: IDLE1 (CPU1)`.
- **Analyse** :
  - La tâche `gt911_irq_task` (bien que pinnée sur CPU0 ? ou flottante) consommait trop de temps CPU ou monopolisait une ressource partagée (Bus I2C) sans "yield" suffisant.
  - L'utilisation de boucles de polling (ou quasi-polling) sans attente bloquante (`ulTaskNotifyTake` mal configuré ou absent) empêchait l'IDLE task de s'exécuter pour reset le WDT.
  - Les transactions I2C synchrones en boucle rapide (IRQ storm ou retries infinis) aggravent le problème.

## 2. Correctifs Appliqués

### components/io_extension/io_extension.c
- **Refonte Structurelle** : Réécriture complète des fonctions `IO_EXTENSION_Output_With_Readback`, `IO_EXTENSION_Read_Output_Latch` et `IO_EXTENSION_Input`.
- **Scope Fix** : Tout le code logique a été remis strictement à l'intérieur des corps de fonctions.
- **Single Point of Return** : Adoption du pattern `goto cleanup;` pour garantir que `i2c_bus_shared_unlock()` est **toujours** appelé, quel que soit le point de sortie (succès ou erreur).

### components/touch/gt911.c
- **Refonte `gt911_irq_task`** :
  - **Attente Bloquante** : Remplacement de la boucle aggressive par `ulTaskNotifyTake(pdTRUE, wait_ticks)`. La tâche dort désormais totalement tant qu'aucune IRQ n'arrive.
  - **Yield Explicite** : Ajout de `vTaskDelay(1)` immédiatement après le réveil et `vTaskDelay(pdMS_TO_TICKS(10))` en fin de boucle pour forcer le context switch et laisser respirer l'IDLE task.
  - **Throttling** : Ajout d'une limitation temporelle (minimum 5ms entre deux traitements) pour contrer les IRQ storms.
  - **Backoff** : En cas d'erreur I2C, la tâche attend exponentiellement (10ms -> 20ms...) au lieu de retenter immédiatement.

## 3. Risques et Points à Surveiller
- **Latence Tactile** : L'introduction de délais (yield 10ms, throttle 5ms) peut théoriquement réduire la fluidité extrême (100Hz -> ~80Hz effectifs), mais c'est un compromis nécessaire pour la stabilité système.
- **Polling Fallback** : Si l'IRQ physique est déconnectée, le mode polling s'active (60ms). Vérifier que cela reste réactif.

## 4. Procédure de Test
1. **Clean Build** : `idf.py fullclean && idf.py build` (Doit passer sans erreur syntaxique).
2. **Flash & Monitor** : `idf.py -p COM3 flash monitor`.
3. **Validation WDT** :
   - Laisser tourner 5 minutes sans toucher l'écran -> Pas de log "Task watchdog".
   - Utiliser l'écran intensivement (scroll, tap) -> Pas de crash/reset.
   - Vérifier les logs : `GT911` doit apparaître périodiquement ou sur événement, sans message d'erreur "I2C busy" en boucle infinie.
