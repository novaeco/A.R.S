## Symptômes
- `idf.py build` échoue sur `main/main.c: fatal error: esp_rom_printf.h: No such file or directory`.
- Warnings Kconfig : valeurs `0`/`1` invalides pour les booléens `ARS_TOUCH_SWAP_XY`, `ARS_TOUCH_MIRROR_X`, `ARS_TOUCH_MIRROR_Y`.

## Cause racine
- L’en-tête `esp_rom_printf.h` n’est pas fourni par l’ESP-IDF 6.1-dev utilisé ; `esp_rom_printf` est désormais déclaré dans `esp_rom_sys.h`.
- Les symboles de configuration `ARS_TOUCH_*` sont des booléens ; les assignations `0`/`1` dans les fichiers de configuration provoquent des avertissements `kconfgen`.

## Changements effectués (fichiers + diff résumé)
- `main/main.c` : remplacement de l’inclusion `<esp_rom_printf.h>` par `<esp_rom_sys.h>` pour utiliser l’en-tête disponible qui déclare `esp_rom_printf`.
- `sdkconfig` : normalisation des booléens `CONFIG_ARS_TOUCH_SWAP_XY` / `MIRROR_X` / `MIRROR_Y` en `n`/`y` explicites afin d’éliminer les warnings Kconfig.
- `docs/monitor_windows_troubleshooting.md` : ajout d’une fiche de dépannage pour les erreurs `GetOverlappedResult failed (Accès refusé)` sur Windows.
- `docs/fix_report_build_kconfig.md` : ce rapport de correction.

## Comment reproduire (commandes)
1. Nettoyer puis reconstruire le firmware :  
   `idf.py fullclean`  
   `idf.py build`
2. Vérifier qu’aucun avertissement `kconfgen` n’apparaît pour `ARS_TOUCH_SWAP_XY`, `MIRROR_X`, `MIRROR_Y`.
3. (Optionnel) Lancer le moniteur série :  
   `idf.py -p COMx flash monitor`  
   Confirmer que la compilation aboutit et que le boot log inclut les checkpoints `app_main reached`.
