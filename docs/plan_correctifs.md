# Plan correctifs (v2)

1. **Build & flash de validation**
   - Commandes : `idf.py fullclean build`, puis `idf.py -p <PORT> flash monitor`.
   - Vérifier boot jusqu’à affichage UI, logs BOOT-SUMMARY, absence de panic même sans SD/LittleFS.

2. **Stockage LittleFS**
   - Sur cible, tester partition absente/corrompue : vérifier que l’app continue, `storage_unavailable` loggé, appels data_manager renvoient `ESP_ERR_INVALID_STATE` sans crash.
   - Injecter partition valide et re-tester save/load JSON.

3. **SD ext‑CS**
   - Avec et sans carte : `sd_card_init()` doit retourner OK/AB S ENT/INIT_FAIL sans panic; fréquence montée à ~20 MHz si OK.
   - Observer logs (sdmmc niveau ERROR uniquement), vérifier auto-montage et lecture.

4. **VSYNC wait**
   - Par défaut (`CONFIG_ARS_VSYNC_WAIT_ENABLE=n`), vérifier qu’aucun timeout VSYNC n’apparaît et que l’affichage est stable.
   - Optionnel : activer `CONFIG_ARS_VSYNC_WAIT_ENABLE=y` et s’assurer que le callback `on_vsync` est supporté (pas de logs "VSYNC wait timeout").

5. **Touch / orientation**
   - Effacer NVS touch (`touch_transform_storage_clear`) et vérifier orientation 180° préservée (swap/mirror issus des defaults rotation).
   - Faire une calibration, redémarrer et confirmer que les flags d’orientation restent cohérents (pas d’inversion).

6. **Sécurité chaînes / web**
   - Tester endpoints web ajout reptile : noms longs sont tronqués proprement, pas de crash.
   - Vérifier que SSID/MDP ne sont pas loggés en clair (observations monitor).

7. **Documentation**
   - Tenir à jour matrice de validation avec résultats réels, ajuster plan si régressions.
