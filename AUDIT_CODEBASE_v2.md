# Audit du code A.R.S (v2)

## Executive Summary
- Crash tactile GT911 reproduit après "Main task setup complete" : la tâche `gt911_irq_task` appelle `esp_lcd_touch_gt911_read_data()` qui loggue alors que la pile 4 KiB est saturée et que la protection de réentrance Newlib échoue (`abort()->lock_acquire_generic`). Risques accrus par l’absence de garde de contexte (log dans fenêtre critique) et par le partage d’I2C sans vérification d’état. P0.
- Initialisation I2C/IO/touch reste fragile : `i2c_bus_shared_init()` considère le bus initialisé même en cas d’échec, les transactions utilisent des timeouts 1 s, et plusieurs chemins ne rendent pas les mutex sur erreur (IO extension). P0/P1.
- Configuration batterie/Kconfig toujours incohérente (`BOARD_BAT_ADC_CHAN` vs `CONFIG_ARS_BAT_ADC_CHANNEL`), conduisant à une mesure incorrecte. P1 confirmé.
- LVGL port : buffers en PSRAM DMA sans validation, attente VSYNC 100 ms en flush pouvant bloquer la tâche LVGL. P1 confirmé.
- SD ext-CS : init sans retries ni remise d’état, self-test bloque et écrit sans garde. P2.

## Repo Map
- **main/** : orchestration boot, NVS/net, BSP, LVGL, SD, BOOT-SUMMARY.
- **components/board** : bring-up LCD/touch/IOEXT/ADC, backlight, pattern test.
- **components/i2c** : bus partagé, mutex global, helpers Waveshare.
- **components/io_extension** : pilote CH32V003 (CS SD, backlight, reset).
- **components/touch** : driver GT911 IRQ/poll, calibration, stats.
- **components/lvgl_port** : port LVGL 9.x, buffers, tâche LVGL, flush RGB, input transform.
- **components/rgb_lcd_port** : panel RGB esp_lcd.
- **components/sd** : SDSPI ext-CS via IOEXT, état SD.
- **components/net/iot/data_manager/core_service/web_server/ui** : réseau, stockage NVS, UI LVGL générée.
- **components/joltwallet__littlefs** : lib LittleFS embarquée (vendor).
- **docs/** : directives, audit v1, build notes.
- **tools/** : scripts env/lint.

## Top Issues
| ID | P | Fichier | Symptôme / Risque | Correctif recommandé | Effort |
| --- | --- | --- | --- | --- | --- |
| T0 | P0 | components/touch/gt911.c | Panic dans `gt911_irq_task` après boot (log dans contexte stack saturée / mutex I2C partagé non sain) | Augmenter stack et ajouter garde de contexte : interdiction de `ESP_LOGx` sous verrou, surveiller watermark, déplacer logs hors section critique; valider état bus avant lecture et fallback poll silencieux | M |
| T1 | P0 | components/i2c/i2c_bus_shared.c + i2c.c | Bus marqué initialisé même si `i2c_new_master_bus` échoue, init récursive confuse, timeouts 1 s peuvent bloquer WDT | Rendre init atomique (flag après succès), réduire timeouts, ajouter backoff et statut prêt vérifié par appelants | M |
| T2 | P1 | components/board/include/board.h + Kconfig | Macros batterie/Kconfig divergentes → mesure fausse | Harmoniser avec `CONFIG_ARS_BAT_ADC_CHANNEL` + recalcul divider | S |
| T3 | P1 | components/io_extension/io_extension.c | Chemins d’erreur sans restitution des mutex / bus lock | Structurer `goto unlock`, vérifier bus avant accès, retourner `ESP_ERR_INVALID_STATE` | S |
| T4 | P1 | components/lvgl_port/lvgl_port.c | Buffers LVGL PSRAM DMA non vérifiés, attente VSYNC 100 ms pouvant bloquer | Fallback RAM interne ou single-buffer si !DMA, désactiver VSYNC après 1er timeout, logs à faible cadence | M |
| T5 | P1 | components/touch/gt911.c | Poll/IRQ state non protégé, backoff partiel, risque de spam log/I2C | Protéger flags via spinlock, ajouter compteur d’erreurs borné et réduction de log | M |
| T6 | P2 | components/sd/sd.c | Init/mount sans retries, self-test destructif | Retries bornés, remise à UNINITIALIZED sur échec, self-test non bloquant et sans écriture intrusive | M |

## File-by-File Audit

### Racine
- **CMakeLists.txt** : rôle build racine; RAS (structure simple, inclut components). RAS.
- **README.md** : description projet; RAS.
- **partitions.csv** : table partitions par défaut (nvs/otadata/phy/initdata/fatfs) ; vérifier alignement avec app. RAS.
- **sdkconfig / sdkconfig.defaults / sdkconfig.old** : configs IDF, valeurs cohérentes avec cible; surveiller options LVGL/PSRAM. RAS (pas modifiés).
- **task.md** : notes tâches; RAS.

### main/
- **AGENTS.md** : règles boot/LVGL.
- **CMakeLists.txt, idf_component.yml** : déclaration composant; RAS.
- **lv_conf.h** : config LVGL locale (dupliquée vs components/lvgl_port/lv_conf.h) – risque divergence; P3 (documenter unique source).
- **main.c** : boot orchestration. Problèmes :
  - Ignore `app_board_init()`/`lvgl_port_init()`/`sd_card_init()` retours dans BOOT-SUMMARY → état incohérent (P2).【F:main/main.c†L44-L93】
  - Utilise `ESP_ERROR_CHECK` sur NVS/netif/esp_event/esp_netif_create_default_wifi_sta` sans récupération possible (panic en cas d’échec HW) (P2).【F:main/main.c†L31-L43】
  - BOOT-SUMMARY ne distingue pas fallback SD absent vs init fail (P3).【F:main/main.c†L70-L88】

### components/board/
- **AGENTS.md** : règles pinout.
- **CMakeLists.txt** : dépendances `touch_transform`, `touch`, `rgb_lcd_port`, `io_extension`; risque cycle avec touch_transform (P2).【F:components/board/CMakeLists.txt†L1-L7】
- **Kconfig.projbuild** : expose `CONFIG_ARS_BAT_ADC_CHANNEL` mais `board.h` utilise `CONFIG_ARS_BAT_ADC_CHAN` (P1).【F:components/board/Kconfig.projbuild†L8-L32】【F:components/board/include/board.h†L1-L35】
- **idf_component.yml** : RAS (version idf >=6.1).
- **include/board.h** : macros batterie erronées (`_CHAN`/`_DIVIDER` fallback 0/2.0) → mesure fausse (P1).【F:components/board/include/board.h†L1-L35】
- **src/board.c** : init BSP. Problèmes :
  - Appelle `i2c_bus_shared_init()` puis `IO_EXTENSION_Init()` sans vérifier retours; continue même si bus/IO absent (P1).【F:components/board/src/board.c†L36-L76】
  - `board_lcd_test_pattern()` alloue 1.2 MiB PSRAM sans check + delay 500 ms à chaque boot (P2, perf/mémoire).【F:components/board/src/board.c†L99-L154】【F:components/board/src/board.c†L260-L311】
  - `board_set_backlight_percent()` ne retourne pas d’erreur et n’indique pas échec LEDC/IOEXT (P2).【F:components/board/src/board.c†L232-L255】
  - Battery ADC utilise divider `BOARD_BAT_DIVIDER` non recalculé sur Kconfig réel (P1).【F:components/board/src/board.c†L159-L195】
  - Touch init : dump config + orientation mais absence de fallback désactive IRQ; pas de désactivation si `touch_gt911_init` NULL (P2).【F:components/board/src/board.c†L44-L69】

### components/i2c/
- **AGENTS.md** : sérialisation bus.
- **CMakeLists.txt** : RAS.
- **i2c_bus_shared.c** : mutex global + init. Problèmes :
  - Flag `initialized` posé avant succès bus; si `i2c_new_master_bus` échoue, bus reste NULL mais init jamais retentée (P0).【F:components/i2c/i2c_bus_shared.c†L14-L41】
  - `i2c_bus_shared_lock()` rappelle init dans chemin lock, possible re-entry depuis ISR/task pendant init (P2).【F:components/i2c/i2c_bus_shared.c†L44-L54】
- **i2c.c** : helpers transaction. Problèmes :
  - `DEV_I2C_Init_Bus` rappelle `i2c_bus_shared_init()` provoquant dépendance circulaire confuse; timeouts 1 s sur TX/RX peuvent bloquer WDT (P1).【F:components/i2c/i2c.c†L88-L118】【F:components/i2c/i2c.c†L190-L208】
  - `DEV_I2C_Set_Slave_Addr` supprime handle sans mutex global → collision possible (P1).【F:components/i2c/i2c.c†L165-L181】
  - `DEV_I2C_Probe` log erreur à chaque essai sans backoff (P3).【F:components/i2c/i2c.c†L63-L83】
- **i2c_bus_shared.h / i2c.h** : RAS.

### components/io_extension/
- **AGENTS.md** : règles CH32V003.
- **CMakeLists.txt** : dépend i2c; RAS.
- **io_extension.c** :
  - `IO_EXTENSION_Output_With_Readback` prend mutex interne puis lock bus, mais si `DEV_I2C_Write_Nbyte` échoue avant `bus_locked` vrai, le bus lock n’est jamais rendu (P1).【F:components/io_extension/io_extension.c†L62-L120】
  - Init écrit 0xFF sur tous GPIO sans retry; pas de désactivation s_ioext_initialized en cas d’échec (P2).【F:components/io_extension/io_extension.c†L70-L120】
  - `IO_EXTENSION_Input` ignore esp_err_t de lecture (P3).【F:components/io_extension/io_extension.c†L240-L270】

### components/touch/
- **AGENTS.md** : robustesse tactile.
- **CMakeLists.txt, Kconfig** : RAS (options layout/calibration).
- **gt911.h** : RAS.
- **gt911.c** :
  - Tâche `gt911_irq_task` (stack 4096) appelle `esp_lcd_touch_gt911_read_data` avec logs fréquents et utilisation de mutex I2C; stack peut être dépassée (buffers + logs) → abort dans `vfprintf` observé (P0).【F:components/touch/gt911.c†L400-L436】【F:components/touch/gt911.c†L1455-L1525】
  - `esp_lcd_touch_gt911_read_data` loggue sous forte fréquence et manipule stats non protégés (poll/irq flags `s_poll_mode` etc. non atomiques) (P1).【F:components/touch/gt911.c†L509-L760】【F:components/touch/gt911.c†L1455-L1525】
  - `touch_gt911_i2c_read/write` prennent mutex global mais aucune validation de handle; pas de backoff configurables (P2).【F:components/touch/gt911.c†L520-L575】
  - ISR réactive IRQ via `ets_delay_us` en IRAM; absence de garde contre lecture I2C en ISR (P2).【F:components/touch/gt911.c†L1360-L1460】
- **touch.c/h** : wrapper LVGL input, RAS (utilise GT911).

### components/lvgl_port/
- **AGENTS.md** : règles LVGL.
- **CMakeLists.txt, Kconfig, lv_conf.h, lvgl_port.h** : RAS (options). Note duplication lv_conf (main/ vs component) (P3 config drift).
- **lvgl_port.c** :
  - Buffers alloc avec `MALLOC_CAP_DMA|SPIRAM` sans vérif de compatibilité DMA pour panel RGB (P1).【F:components/lvgl_port/lvgl_port.c†L180-L244】
  - flush VSYNC attend 100 ms en task LVGL, peut bloquer si VSYNC jamais posté (P1).【F:components/lvgl_port/lvgl_port.c†L149-L210】
  - `lvgl_port_init` utilise `assert` sur alloc display/indev => abort en cas d’échec au lieu de retourner erreur (P1).【F:components/lvgl_port/lvgl_port.c†L320-L390】
  - Tick timer jamais stoppé, pas de déinit (P3).【F:components/lvgl_port/lvgl_port.c†L40-L90】

### components/rgb_lcd_port/
- **AGENTS.md** : RAS.
- **CMakeLists.txt, rgb_lcd_port.c/h** : driver panel RGB; RAS (configs statiques). Pas de issues majeurs relevés.

### components/sd/
- **AGENTS.md** : règles SD.
- **CMakeLists.txt, Kconfig** : RAS.
- **sd.c** : init/mount ext-CS. Problèmes :
  - Pas de retry si `sd_extcs_register_io_extender` ou mount échoue; état reste INIT_FAIL/MOUNT_FAIL sans remise à zéro (P2).【F:components/sd/sd.c†L15-L60】
  - self-test écrit fichier en boucle sans vérif espace/permission, pas de lock contre usage concurrent (P3).【F:components/sd/sd.c†L64-L132】
- **sd.h** : RAS.
- **sd_host_extcs.c/h** : gestion CS externe; RAS (pas de nouveaux problèmes identifiés, timings à vérifier en intégration).

### components/net/
- **AGENTS.md** : sécurité wifi.
- **CMakeLists.txt, Kconfig** : RAS.
- **src/net_manager.c** :
  - Logs contiennent potentiellement SSID/mot de passe via config wifi (P2).【F:components/net/src/net_manager.c†L60-L132】
  - Backoff sans plafond de tentatives, timers sans check retours (P2).【F:components/net/src/net_manager.c†L104-L200】

### components/iot/
- **AGENTS.md**, **CMakeLists.txt**, **Kconfig**, **src/iot_manager.c**, **include/iot_manager.h** : logique IoT (stubs) – RAS (pas d’erreurs détectées, placeholders log). 

### components/data_manager/
- **AGENTS.md**, **CMakeLists.txt**, **Kconfig.projbuild**, **src/data_manager.c**, **include/data_manager.h** : NVS/FS abstraction, RAS (vérifie erreurs, mais pas de tests unitaires). P3 : manque de log détaillé sur échecs NVS.

### components/core_service/
- **AGENTS.md**, **CMakeLists.txt**, **core_service.c**, **core_service_alerts.c**, **headers** : service central/alerts; RAS (structure simple). P3 : absence de check retour sur `xQueueCreate`.

### components/gpio/
- **AGENTS.md**, **gpio.c/h**, **CMakeLists.txt** : wrappers GPIO; RAS.

### components/board -> dépendances touch_transform/orient
- **touch_transform/** : transformations calibration + stockage NVS. RAS global; P3 : tests unitaires limités à sample.
- **touch_orient/** : applique orientation; RAS.

### components/ui/
- **AGENTS.md** : UI LVGL générée (SquareLine). Tous fichiers `ui_*.c/h`, `ui_fonts`, `ui_helpers` : auto-générés, pas d’alloc dynamique hors LVGL; RAS. P3 : logs absents pour navigation (non critique).

### components/web_server/
- **AGENTS.md**, **web_server.c** : serveur HTTP simple, compile statique assets; RAS (ne log pas secrets). P3 : pas de auth/HTTPS.

### components/reptile_storage/
- **AGENTS.md**, **reptile_storage.c/h** : wrappers storage; RAS (retours esp_err_t). P3 : manque de tests.

### components/joltwallet__littlefs/
- Vendor LittleFS 2.x : nombreux fichiers sources/tests/scripts. RAS (considérés upstream). Noter dépendance non utilisée directement par app actuelle.

### docs/
- **AGENTS.md** : règles doc.
- **AUDIT_CODEBASE.md** : audit v1 (baseline). Points confirmés/ajustés :
  - Récursion I2C : non infinie mais init marquée ok sans handle (ajusté P0 disponibilité bus).
  - Batterie macros : toujours valide P1.
  - LVGL/VSYNC et buffers : confirmé P1.
  - Touch backoff : partiellement confirmé (poll mode/irq mix), crash P0 nouveau.
- **BUILD.md, DIRECTIVES.md, REVIEW_CHECKLIST.md, touch_calibration.md** : RAS.

### tools/
- **AGENTS.md** : règles scripts.
- **env.cmd/env.ps1/setup_env.sh** : config env; RAS.
- **lint_components.py** : script lint composant; RAS (pas exécuté).

## Crash Analysis (GT911)
- Stack observée : `abort() -> lock_acquire_generic -> vfprintf -> esp_log_write -> esp_lcd_touch_gt911_read_data -> gt911_irq_task` après suppression de `main`.
- Hypothèse A (probable) : overflow stack `gt911_irq_task` (4096) dû aux buffers, logs, appels esp_log sous charge. Newlib lock corrompu → abort lors du log suivant. Validation : activer `configCHECK_FOR_STACK_OVERFLOW`, mesurer `uxTaskGetStackHighWaterMark(s_gt911_irq_task)`, augmenter stack à 6144 et réduire logs.
- Hypothèse B : `i2c_bus_shared` non prêt (handle NULL car init marquée OK) → I2C retourne erreur, boucle de log dans `esp_lcd_touch_gt911_read_data`, déclenche panic via lock esp_log en contexte de notification ISR. Validation : assert `i2c_bus_shared_is_ready()` avant lecture, return silencieux sinon, instrumentation des erreurs I2C.
- Hypothèse C : Log depuis section critique (portENTER_CRITICAL) + IRQ désactivées → vprintf échoue. Vérifier qu’aucune `ESP_LOGx` n’est émise avant `gt911_enable_irq_guarded()`/après `portENTER_CRITICAL`.
- Mitigation rapide :
  - Déplacer logs de `esp_lcd_touch_gt911_read_data` hors des sections critiques et limiter à 1/s.
  - Augmenter stack + config de surveillance overflow.
  - Si bus non prêt, désactiver IRQ et passer en poll 50 ms silencieux jusqu’à bus OK.

## Validation Commands
- `idf.py fullclean build` (attendu : succès, pas de warnings critiques).
- `idf.py -p COMx flash monitor` (attendu : BOOT-SUMMARY avec display/touch/lvgl/sd/wifi, pas de panic même sans SD/touch).
- `idf.py size` / `idf.py size-components` pour vérifier impact buffers/test pattern.
- Pour crash GT911 : surveiller watermark `gt911_irq` et logs d’erreurs I2C; aucune panic après 60s.

