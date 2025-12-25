# Fix Watchdog & Build Issues (GT911 / RGB LCD)

## Symptômes (État Initial)
1. **Compilation Bloquée (CMake)** : Impossible de résoudre le composant `esp_ipc`.
2. **Compilation Bloquée (C)** : Erreur `undeclared identifier 's_spurious_block_until_us'` dans `gt911.c` et warning `unused variable 'notified'`.
3. **Runtime WDT / Panic** : "Task watchdog triggered" ou "Interrupt wdt timeout" au boot.
   - Cause : Initialisation du panneau RGB sur la tâche `main` (CPU1). Les interruptions GDMA/LCD sont allouées sur CPU1, affamant la tâche IDLE1 lors des transferts intenses, ce qui déclenche le Watchdog.

## Solution Implémentée (Solution A: Task CPU0)
Nous avons appliqué le correctif "Solution A" recommandé :

1. **Suppression de `esp_ipc`** :
   - Retiré des dépendances dans `components/rgb_lcd_port/CMakeLists.txt`.
   - Code `esp_ipc_call_blocking` retiré de `rgb_lcd_port.c` au profit d'une initialisation directe.

2. **Fix GT911** :
   - Vérification de la déclaration de `s_spurious_block_until_us`.
   - Suppression du warning `notified` via `(void)notified`.

3. **Init LCD sur CPU0** :
   - Modification de `board.c` (`app_board_init`) pour lancer l'initialisation du LCD dans une tâche dédiée `lcd_init_task` épinglée sur le **CPU0**.
   - Cette tâche initialise le driver (ce qui alloue les ISR sur CPU0), signale la fin via sémaphore, et se termine.
   - `app_board_init` (sur CPU1) attend la fin de l'initialisation avant de continuer.

## Fichiers Modifiés
- `components/rgb_lcd_port/CMakeLists.txt`
- `components/touch/gt911.c`
- `components/rgb_lcd_port/rgb_lcd_port.c`
- `components/board/src/board.c`

## Validation
Commandes à exécuter :
```bash
idf.py fullclean
idf.py build
idf.py -p COM3 flash monitor
```

**Résultats Attendus** :
- Compilation 100% OK (plus d'erreur esp_ipc ou variables gt911).
- Boot propre.
- Plus de "Task watchdog triggered" ou "Interrupt wdt timeout".
- Le tactile et l'écran fonctionnent normalement.
