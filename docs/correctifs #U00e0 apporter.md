# Correctifs à apporter — Projet A.R.S / reptiles_assistant

## Priorisation P0 → P3

### P0
1. **Tolérance au défaut LittleFS**
   - **Contexte/objectif** : éviter panic quand `data_manager_init` échoue (corruption ou absence partition).
   - **Fichiers impactés** : `main/main.c`, `components/data_manager/*`.
   - **Changement attendu** : remplacer `ESP_ERROR_CHECK` par gestion d’erreur non bloquante (formatage conditionnel, mode dégradé UI sans données). Ajouter logs structurés.
   - **Risque de régression** : faible si testé avec partition saine et corrompue ; risque de masquer erreurs si absence d’alertes.
   - **Stratégie de test** :
     - `idf.py fullclean build`
     - Boot avec partition intacte (logs sans panic).
     - Boot avec partition LittleFS effacée ou corrompue : vérifier que l’app démarre et remonte un message d’erreur UI/log.
   - **Commandes** : `idf.py fullclean build`; éventuellement `idf.py -p <PORT> flash monitor` et observer absence de panic.

### P1
2. **Restaurer environnement build reproductible**
   - **Fichiers impactés** : `tools/setup_env.sh`, README/docs.
   - **Changement attendu** : fournir script d’export ESP-IDF pour Linux (chemin docker), vérifier présence d’`idf.py --version` et définir `IDF_TARGET=esp32s3`.
   - **Risque** : moyen (erreurs de chemin). Tests simples.
   - **Commandes** : `./tools/setup_env.sh` ou équivalent puis `idf.py --version`.

3. **Durcir init SD vs IO extender**
   - **Fichiers impactés** : `components/sd/sd.c`, `components/io_extension/*`, `components/board/src/board.c`.
   - **Changement attendu** : refuser init SD si handle IO extender NULL, protéger `card` par mutex ou accessor, retourner état explicite pour UI.
   - **Risque** : moyen (régression SD). Tester avec/ sans carte et IO extender simulé.
   - **Commandes** : `idf.py build`; `idf.py -p <PORT> flash monitor` vérifier logs `SD state`.

4. **Partition OTA**
   - **Fichiers impactés** : `partitions.csv`, `sdkconfig.defaults` (offset OTA), docs.
   - **Changement attendu** : introduire OTA_0/OTA_1 ou OTA_0 + factory réduite ; ajuster LittleFS en conséquence ; mettre à jour `sdkconfig.defaults`.
   - **Risque** : moyen (données LittleFS réduites, offsets). Test flash complet.
   - **Commandes** : `idf.py fullclean build`; `idf.py -p <PORT> flash monitor` ; vérifier `esp_ota_get_running_partition` en log.

5. **Chiffrement / gestion des credentials Wi-Fi**
   - **Fichiers impactés** : `components/net/src/net_manager.c`, docs.
   - **Changement attendu** : activer NVS chiffré (si support), ou chiffrer SSID/MDP via API IDF ; ajouter fonction d’effacement et de rotation token web.
   - **Risque** : moyen (compatibilité provisioning existant). Prévoir migration avec détection version NVS.
   - **Commandes** : `idf.py build`; tests provisioning (connect/disconnect), vérifier absence de stockage en clair (nvs_dump or log).

6. **Boot order résilient**
   - **Fichiers impactés** : `main/main.c`, éventuellement UI pour afficher mode dégradé.
   - **Changement attendu** : déplacer init FS après BSP/LVGL, ou encapsuler init dans tâche séparée avec timeouts ; toujours afficher boot UI même si FS absent.
   - **Risque** : faible-moyen (ordre dépendances). Valider UI init.
   - **Commandes** : `idf.py build`; boot sans partition disponible.

### P2
7. **Self-test SD sécurisé**
   - **Fichiers impactés** : `components/sd/sd.c`.
   - **Changement attendu** : rendre self-test optionnel (flag), vérifier espace libre, protéger par mutex, chemin dédié (ex: `/data/sd/.health/hello.txt`).
   - **Risque** : faible.
   - **Commandes** : `idf.py build`; exécuter self-test et vérifier nettoyage.

8. **Assertions LVGL / Contexte**
   - **Fichiers impactés** : `components/lvgl_port/lvgl_port.c`, docs API UI.
   - **Changement attendu** : ajouter vérifications `lvgl_port_in_task_context` ou verrouillage obligatoire pour fonctions publiques, logs WARN sur mauvais contexte.
   - **Risque** : faible (peut révéler mauvais usages existants).
   - **Commandes** : `idf.py build`; UI smoke test.

9. **Rollback safe-state BSP**
   - **Fichiers impactés** : `components/board/src/board.c`.
   - **Changement attendu** : en cas d’échec IO extender, couper VCOM/backlight, renseigner état pour UI ; éviter d’exposer handles NULL.
   - **Risque** : faible.
   - **Commandes** : `idf.py build`; boot avec IO extender débranché/simulé.

10. **WDT sur tâches réseau**
    - **Fichiers impactés** : `components/net/src/net_manager.c`.
    - **Changement attendu** : enregistrer tâches dans task watchdog, ajouter délais bornés et compteur d’essais avec retour à l’UI.
    - **Risque** : faible.
    - **Commandes** : `idf.py build`; simulations de déconnexion prolongée.

### P3
11. **Documentation et scripts**
    - **Fichiers impactés** : `docs/BUILD.md`, `README.md`, scripts tools.
    - **Changement attendu** : clarifier procédures Linux, ajout exemples monitor avec `--disable-address-decoding`, préciser versions LVGL/IDF.
    - **Risque** : très faible.
    - **Commandes** : N/A (relecture).

## Roadmap de remédiation
- **Vague 1 (P0 immédiat)** : F1 (LittleFS tolérant), F2 (environnement build), préparation tests boot sans panic.
- **Vague 2 (P1 court terme)** : F3 (SD/IO), F4 (OTA), F5 (credentials), F6 (boot order résilient).
- **Vague 3 (P2/P3 maintenance)** : F7 (self-test), F8 (assertions LVGL), F9 (rollback BSP), F10 (WDT réseau), documentation mise à jour.
