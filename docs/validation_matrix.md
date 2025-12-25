# Validation Matrix

| Domaine | Commande / Action | Attendu | Statut |
| --- | --- | --- | --- |
| Build | `idf.py fullclean` | Nettoyage complet sans erreur | ⚠️ Non exécuté (environnement CI absent) |
| Build | `idf.py build` | Binaire `ars.elf` généré, zéro warning compilation | ⚠️ Non exécuté (environnement CI absent) |
| Flash | `idf.py -p <PORT> flash monitor` | Boot jusqu’aux logs BOOT-SUMMARY, pas de panic, UI visible | ⚠️ À exécuter sur carte cible |
| Storage | Boot sans partition LittleFS | Log `storage_unavailable`, application continue | ⚠️ À tester terrain |
| SD | Boot sans carte SD | Log `SD state -> ABSENT` ou INIT_FAIL, aucune panic | ⚠️ À tester terrain |
| SD | Boot avec carte FAT32 | Montage OK, logs sdmmc limités (niveau ERROR), lecture/écriture ok | ⚠️ À tester terrain |
| Touch | Calibration + reboot | Orientation 180° préservée, points alignés | ⚠️ À tester terrain |
| VSYNC | Config défaut (`CONFIG_ARS_VSYNC_WAIT_ENABLE=n`) | Pas de `VSYNC wait timeout`, affichage stable | ⚠️ À tester terrain |
| VSYNC (optionnel) | Activer `CONFIG_ARS_VSYNC_WAIT_ENABLE=y` | Pas de timeout si callback supporté | ⚠️ À tester terrain |
| Web | POST ajout reptile via /api | Réponse `{"status":"ok"}`, chaînes tronquées proprement | ⚠️ À tester terrain |

## Commandes recommandées
- Linux/macOS :
  - `idf.py fullclean build`
  - `idf.py -p /dev/ttyACM0 flash monitor`
- Windows PowerShell :
  - `idf.py fullclean build`
  - `idf.py -p COM3 flash monitor`

Indicateurs de succès :
- Logs BOOT-SUMMARY indiquent `storage=ok` ou `unavailable`, `sd=ABSENT/INIT_OK`, UI opérationnelle.
- Aucun `abort()` ou panic même sans SD/LittleFS.
- SD montée monte à ~20 MHz après OCR (voir logs `SD pipeline`).
