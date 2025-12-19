# Rapport d'audit du code source — Projet A.R.S / reptiles_assistant

## Page de garde
- **Projet** : A.R.S / reptiles_assistant (ESP32-S3, LVGL 9.x)
- **Commit audité** : c16f5c06af26c52711bbf0585f2c4804537baf17
- **Date** : 2025-12-19T22:34:37+00:00
- **Environnement d’audit** : Linux container (uname -a ci-dessous), environnement ESP-IDF absent (idf.py introuvable).
  - `uname -a` : Linux 8f00de3bf01d 6.12.13 #1 SMP Thu Mar 13 11:34:50 UTC 2025 x86_64 x86_64 x86_64 GNU/Linux

## Méthodologie
1. **Inventaire** : `find` pour recenser tous les fichiers (types et répartition), lecture des AGENTS.md globaux et docs.
2. **Analyse build** : tentative `idf.py fullclean` → échec (idf.py manquant). Partition & sdkconfig vérifiés manuellement.
3. **Analyse statique manuelle** : revue multi-pass de chaque composant (main, board, LVGL, SD, I2C, net, storage, UI, web), inspection des Kconfig/CMake.
4. **Recherche textuelle ciblée** : `rg` sur TODO/FIXME/HACK, sur artefacts interdits (CH422G), et sur patterns sensibles (malloc/free, format printf).
5. **Conformité RTOS/ISR/LVGL** : vérification des contextes d’appel, mutex LVGL/I2C, usage ISR.
6. **Sécurité/robustesse** : stockage NVS, exposition réseau, gestion d’erreurs boot (SD/FS), watchdogs.

## Inventaire
- **Comptage par extension (hors .git)** : c (62), h (55), md (35), py (22), toml (23), txt (29), csv (3), projbuild (3), Kconfig (6), etc.【F:find_output†L1-L32】
- **Architecture** :
  - **main/** : orchestration boot, injection callbacks UI, séquence BSP/SD/Wi-Fi.
  - **components/** :
    - **board** (BSP LCD/RGB, GT911, IO extension, ADC batterie)
    - **lvgl_port** (tâche LVGL, flush RGB, touch driver glue)
    - **sd** (SDSPI Ext-CS via IO extension, état global)
    - **i2c / io_extension / touch / touch_orient / touch_transform** (bus partagé, GT911, calibrations)
    - **ui** (écrans multiples), **rgb_lcd_port**, **net / web_server / iot / core_service**, **data_manager & reptile_storage**, **littlefs wrapper**.
  - **partitions.csv** : factory 4M + littlefs 11M, pas de slot OTA.
  - **docs/tools** : scripts d’environnement, consignes build.

## Résumé exécutif (10 points majeurs)
1. **Blocage boot sur échec LittleFS** : `data_manager_init()` est `ESP_ERROR_CHECK`, donc corruption stockage → panic immédiat (P0).【F:main/main.c†L62-L64】
2. **Environnement build absent** : `idf.py` introuvable → aucun build ni génération compile_commands possible ; CI locale non reproductible (P1).【a8f34a†L1-L2】
3. **SD dépend du IO extender non vérifié** : `sd_card_init()` enregistre l’IO extender via handle potentiellement NULL si BSP échoue, puis retourne erreur sans fallback ; état global partagé sans mutex (P1).【F:components/sd/sd.c†L24-L74】
4. **Aucune partition OTA** : table fixe 4M factory + 11M LittleFS ; empêche mise à jour sécurisée et réduit marge log/heap (P1).【F:partitions.csv†L1-L7】
5. **Wi-Fi credentials stockés en clair NVS** : clés `wifi_ssid`/`wifi_pass` enregistrées sans chiffrement ni effacement sur échec (P1).【F:components/net/src/net_manager.c†L38-L42】
6. **FS monté avant BSP sans tolérance d’erreur** : séquence boot exige stockage avant affichage/touch alors que UI pourrait démarrer sans FS, augmentant risques de panic en terrain (P1).【F:main/main.c†L62-L79】
7. **SD self-test destructif** : `sd_card_self_test` crée/efface fichier `hello.txt` sans vérifier espace ni permissions, en contexte potentiellement multitâche (P2).【F:components/sd/sd.c†L136-L185】
8. **Mutex LVGL non vérifié dans callbacks externes** : APIs publiques `lvgl_port_lock/unlock` exposées mais aucune protection autour d’appels LVGL faits hors tâche (risque d’usage futur incorrect, pas de assert) (P2).【F:components/lvgl_port/lvgl_port.c†L231-L273】
9. **Backlight/IO extender dépendances fragiles** : `board.c` retourne `ESP_ERR_INVALID_STATE` si IO extender KO après avoir activé VCOM ; main continue et peut laisser LCD alimenté partiellement (P2).【F:components/board/src/board.c†L33-L125】
10. **Absence de surveillance WDT sur tâches additionnelles (wifi_retry, provisioning)** : boucles sans garde WDT ni délais bornés peuvent masquer blocages réseau (P2).【F:components/net/src/net_manager.c†L120-L150】

## Sections détaillées
### 1. Build System
- **CMake/Kconfig** : composants correctement enregistrés ; dépendances ESP-IDF implicites (esp_lcd, wifi, fatfs). Pas de `idf_component.yml` pour plusieurs modules (hors main/board/littlefs) → non verrouillage versions.
- **sdkconfig.defaults** : PSRAM octal 80MHz actif, WDT main 20s, buffer LVGL 80 lignes. Pas de traces d’activation `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` : risque fragmentation si double buffer.
- **Partitions** : 16MB flash allouée, factory 4M, LittleFS 11M, pas d’OTA ni partition backup → P1 sécurité/maintenance.

### 2. Drivers Board
- Initialisation : I2C partagé puis IO extender (CH32V003). Si IO extender échec, le code alimente VCOM conditionnel mais retourne erreur, laissant app_main continuer à appeler LVGL et SD sans handles valides.
- LCD : test pattern forcé après init peut consommer temps/dma avant LVGL.
- Touch : orientation/calibration appliquées après init ; pas de timeouts sur GT911 read (dépend du driver). Diagnostics tâche optionnelle logue en boucle 10s (risque WDT si LCD init échoue).

### 3. Concurrence/Threading
- **LVGL** : tâche dédiée (core configurable), mutex récursif ; tick via esp_timer (non ISR). Risque si autres tâches appellent LVGL sans lock (aucun assert).
- **I2C** : bus partagé avec mutex global ; certains drivers (touch/IO extender) doivent respecter `i2c_bus_shared_lock`, non vérifié systématiquement.
- **SD** : mutex interne dans `sd_extcs` mais état global `card` exposé dans board sans protection.
- **Tasks Wi-Fi** : retry task + timer ; watchdog non explicit.

### 4. Mémoire/PSRAM/DMA
- Buffers LVGL alloués en interne/DMA, fallback en divisant lignes jusqu’à 10 ; pas de vérification PSRAM fragmentation. Double buffer dynamique mais peut rester NULL sans dégradation déclarée.
- ADC calibration : fallback approximation 1100mV*4 sans compensation réelle (précision limitée).

### 5. Stockage/Data
- **LittleFS** : wrapper `esp_littlefs` présent ; data_manager non inspecté en profondeur mais init obligatoire (ESP_ERROR_CHECK) → panic si FS absent/corrompu.
- **SD** : montage FATFS via VFS ; self-test modifie FS.

### 6. Réseau/Sécurité
- Wi-Fi : SSID/mot de passe stockés en clair NVS (`wifi_pass`), pas de purge sur déprovisionnement.
- Web server : options TLS via proxy, pas de TLS embarqué ; nécessite protection externe.
- HTTP client compilé conditionnel, pas d’option de pinning certs mentionnée (s’appuie sur bundle).

### 7. Robustesse Boot
- Séquence fixe : NVS → netif → FS → BSP → LVGL → SD → Wi-Fi. Aucun backoff ou timeout sur FS ; SD init après UI mais dépend IO extender.
- `sd_card_init` boucle 2 tentatives seulement, délai 100 ms ; pas de différenciation CRC/bus busy.
- Absence d’OTA et partition unique → en cas de crash pendant mise à jour manuelle, récupération compliquée.

### 8. Qualité code
- Style homogène (ESP_LOG/esp_err_to_name), mais globales non encapsulées (`card`, flags net). Peu de tests unitaires (uniquement littlefs). Plusieurs TODO non traités.

## Findings (tableau)
| ID | Sévérité (P0..P3) | Domaine | Fichier:ligne | Symptôme | Cause probable | Preuve (extrait court) | Recommandation |
|----|-------------------|---------|---------------|----------|----------------|------------------------|----------------|
| F1 | P0 | Boot/FS | main/main.c:62-64 | Panic si LittleFS corrompu | `ESP_ERROR_CHECK` sur `data_manager_init` | `ESP_ERROR_CHECK(data_manager_init());`【F:main/main.c†L62-L64】 | Rendre init tolérant (retours d’erreur, montage optionnel, formatage conditionnel) |
| F2 | P1 | Build | (commande) | `idf.py` absent → build impossible | Environnement ESP-IDF non configuré | `bash: command not found: idf.py`【a8f34a†L1-L2】 | Fournir script d’amorçage Linux ou vendoriser toolchain/IDF ; documenter version exacte |
| F3 | P1 | SD/IO | components/sd/sd.c:24-74 | SD init dépend handle IO ext possiblement NULL, état global non protégé | Pas de vérification `IO_EXTENSION_Get_Handle()` ni mutex autour de `card` | Code d’init et état global【F:components/sd/sd.c†L24-L74】 | Vérifier handle et refuser init avant BSP OK ; encapsuler `card` derrière mutex/API thread-safe |
| F4 | P1 | Partitions | partitions.csv:1-7 | Pas d’OTA, factory 4M seulement | Partition table custom sans slots OTA | Table partitions【F:partitions.csv†L1-L7】 | Ajouter OTA_0/OTA_1 ou slot backup ; ajuster LittleFS si besoin |
| F5 | P1 | Sécurité | components/net/src/net_manager.c:38-42 | SSID/mot de passe stockés en clair NVS | Pas de chiffrement/effacement | Clés NVS définies【F:components/net/src/net_manager.c†L38-L42】 | Activer chiffrement NVS, effacer à la demande, ou stocker via secure element |
| F6 | P1 | Boot order | main/main.c:62-79 | Boot bloque sur FS avant BSP/UI | Séquence imposée + ESP_ERROR_CHECK | Séquence boot【F:main/main.c†L62-L79】 | Déporter FS après UI ou rendre init non bloquante |
| F7 | P2 | SD test | components/sd/sd.c:136-185 | Self-test crée/supprime fichier sans vérifs | Test destructif sans vérification contexte | Fonction self-test【F:components/sd/sd.c†L136-L185】 | Ajouter optionnel/conditionnel, vérifier espace, protéger par mutex |
| F8 | P2 | LVGL/API | components/lvgl_port/lvgl_port.c:231-273 | Pas d’assert si appels LVGL hors tâche, lock facultatif | API publique sans garde | Mutex init & lock【F:components/lvgl_port/lvgl_port.c†L231-L273】 | Documenter et ajouter assertions/context-check pour appels externes |
| F9 | P2 | BSP/Power | components/board/src/board.c:33-125 | Retour erreur si IO extender KO après avoir manipulé VCOM/retours partiels | Dépendance forte IO extender, pas de rollback | Séquence init BSP【F:components/board/src/board.c†L33-L125】 | Ajouter rollback/safe-state, différer SD/LCD quand IO extender absent |
| F10 | P2 | WDT/Tasks | components/net/src/net_manager.c:120-150 | Tâches réseau sans watchdog ni délai borné | Boucles de retry/notify sans garde | Boucles Wi-Fi retry/provisioning【F:components/net/src/net_manager.c†L120-L150】 | Ajouter vTaskDelay/esp_task_wdt_add, bornes sur retries |

## Références
- ESP-IDF SD/SDSPI : https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/sdspi.html
- ESP-IDF NVS sécurité : https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/storage/nvs_flash.html#nvs-encryption
- LVGL threading (v9) : https://docs.lvgl.io/9.0/porting/os.html
- Partitions OTA : https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/ota.html
