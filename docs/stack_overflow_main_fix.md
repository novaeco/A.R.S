# Correction du stack overflow de la tâche main

## Symptômes observés
- Monitor série: `***ERROR*** A stack overflow in task main has been detected.` juste après le chemin d’init `sd_extcs: init 100 kHz / cmd0 slow path` et `ui: Initializing UI orchestration...`.
- Effet: redémarrage/panic dès le boot, avant stabilisation SD/UI.

## Cause racine (mesures HWM)
- Instrumentation ajoutée dans `main/main.c` via `uxTaskGetStackHighWaterMark()`:
  - logs `[stack] app_main entry`, `before/after LVGL port init`, `before/after SD init`, etc.
  - permet de mesurer la marge en mots et en octets au fur et à mesure de l’orchestration.
- Hypothèse confirmable: la pile par défaut de `app_main` était insuffisante pour l’ensemble NVS + réseau + BSP + LVGL + SD. La marge basse mesurée (HWM faible) explique le débordement.

## Changements appliqués
- `main/main.c`: instrumentation HWM aux points clés (entrée, avant/après LVGL, avant/après SD, avant dispatch UI) pour prouver la consommation de pile.
- `sdkconfig.defaults`: augmentation de `CONFIG_ESP_MAIN_TASK_STACK_SIZE` à `12288` mots (≈48 KiB) pour fournir une marge suffisante pendant l’init combinée SD/UI.

## Comment reproduire et valider
1) Nettoyer et reconstruire:  
   - `idf.py fullclean`  
   - `idf.py build`
2) Flasher et monitorer:  
   - `idf.py flash monitor`
3) Indicateurs de succès dans les logs:  
   - Absence de `***ERROR*** A stack overflow in task main has been detected.`  
   - Présence des logs `[stack] ... HWM=...` avec une marge positive après SD et LVGL.  
   - `BOOT-SUMMARY ...` imprimé, SD état cohérent (`mounted` ou `not present`), UI lancée.

## Rollback simple
- Revenir à l’état précédent: `git checkout -- main/main.c sdkconfig.defaults docs/stack_overflow_main_fix.md` (puis re-build).
