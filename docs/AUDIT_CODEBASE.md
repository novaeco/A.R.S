# Audit du code A.R.S (ESP-IDF v6.1-dev, ESP32-S3)

## Executive Summary
- L’initialisation I2C est gravement défectueuse : `i2c_bus_shared_init()` rappelle `DEV_I2C_Init_Bus()`, qui rappelle à son tour `i2c_bus_shared_init()`, causant une récursion infinie et un crash au boot (P0).
- La configuration batterie/Kconfig est incohérente (macro `BOARD_BAT_ADC_CHAN` vs `CONFIG_ARS_BAT_ADC_CHANNEL`), ce qui force des valeurs par défaut erronées et invalide la mesure batterie (P1).
- Le chemin tactile GT911 et l’init BSP effectuent des appels I2C/IO sans vérifier les retours et sans garde contre l’absence de bus ou d’IO-expander, risquant blocage ou spam d’erreurs (P1).
- Le port LVGL alloue les buffers d’affichage en PSRAM avec le flag DMA sans vérifier la capacité réelle pour RGB, et attend le VSYNC dans le callback de flush, pouvant bloquer la tâche LVGL et provoquer des timeouts visuels (P1/P2).
- L’initialisation SD/ext-CS et plusieurs composants manquent de gestion d’erreurs structurée (retours silencieux, timeouts larges), augmentant les risques de blocage ou de dégradation silencieuse (P2).

## Repo Map
- **main/** : orchestration boot, NVS/net init, déclenchement BSP, LVGL, SD, réseau.
- **components/board** : init matériel (I2C partagé, IO expander, GT911, RGB LCD, backlight, ADC batterie, pattern test).
- **components/i2c** : bus I2C partagé, helpers Waveshare, mutex global, recovery.
- **components/io_extension** : pilote IO expander CH32V003 (CS SD, backlight, reset, etc.).
- **components/touch** : pilote GT911, stats, calibration legacy.
- **components/lvgl_port** : port LVGL 9.x, buffers, tâche LVGL, flush RGB, input transform.
- **components/sd** : pipeline SD SDSPI + ext-CS via IO expander.
- **components/net** : gestion état Wi-Fi/provisioning, retries.
- **Autres composants** (iot, data_manager, web_server, etc.) : présents mais non modifiés; risques principaux liés aux dépendances I2C/LVGL/SD ci-dessus.

## File-by-File Audit

### main/main.c
**Rôle** : séquence boot (NVS, netif/events), init BSP, LVGL, SD, réseau, log résumé.

**Problèmes**
- **Robustesse** (P2) : ignore les retours de `app_board_init()`/`lvgl_port_init()`/`sd_card_init()` dans le flux de contrôle (continue sans stratégie de fallback ni message d’état structuré en cas d’échec partiel).【F:main/main.c†L18-L93】

**Correctifs recommandés**
- Propager les `esp_err_t`, structurer un résumé d’état unique (display/touch/lvgl/sd/wifi) et court-circuiter l’init dépendante en cas d’échec critique.

### components/board/src/board.c
**Rôle** : init I2C partagé, IO expander, GT911, orientation/transform tactile, LCD RGB, backlight, ADC batterie, pattern test, SD délégué.

**Problèmes**
- **Correctness** (P0) : appelle `i2c_bus_shared_init()` qui est récursif (cf. composant i2c), provoquant un crash de boot certain.【F:components/board/src/board.c†L36-L76】【F:components/i2c/i2c_bus_shared.c†L14-L42】【F:components/i2c/i2c.c†L88-L118】
- **Robustesse** (P1) : `IO_EXTENSION_Init()` et `touch_gt911_init()` sont appelés sans vérifier/traiter les retours ; un bus absent ou un IO-expander non présent entraîne des logs mais aucune désactivation des fonctionnalités dépendantes (backlight/SD).【F:components/board/src/board.c†L36-L155】
- **Config** (P1) : macros batterie utilisent `CONFIG_ARS_BAT_ADC_CHAN`/`CONFIG_ARS_BAT_DIVIDER` tandis que Kconfig expose `CONFIG_ARS_BAT_ADC_CHANNEL` et un ratio num/den séparé, induisant des valeurs par défaut 0/2.0 et donc des mesures erronées.【F:components/board/include/board.h†L9-L35】【F:components/board/Kconfig.projbuild†L8-L32】
- **Ressources** (P2) : `board_lcd_test_pattern()` alloue ~1.2 MiB en PSRAM à chaque boot et attend 500 ms, sans check de disponibilité mémoire ni option runtime, pouvant fragmenter et retarder l’UI.【F:components/board/src/board.c†L99-L154】【F:components/board/src/board.c†L260-L311】
- **Robustesse** (P2) : `board_set_backlight_percent()` ne retourne pas d’erreur, log en debug uniquement ; absence de clamp côté driver IO expander et pas de teardown LEDC.

**Correctifs recommandés**
- Corriger la dépendance I2C (voir composant i2c) puis encapsuler les appels BSP avec vérification/retour `esp_err_t`. Désactiver proprement touch/backlight/SD si l’IO-expander échoue.
- Harmoniser macros batterie avec Kconfig (`CONFIG_ARS_BAT_ADC_CHANNEL`, num/den) et recalculer `s_bat_divider` au boot.
- Rendre le pattern test optionnel (flag Kconfig) ou limité à un mode debug, avec allocation vérifiée et délai réduit.

### components/i2c/i2c_bus_shared.c & i2c.c
**Rôle** : singleton I2C bus, mutex global, helpers Waveshare, accès sérialisé.

**Problèmes**
- **Correctness** (P0) : récursion infinie `i2c_bus_shared_init()` → `DEV_I2C_Init_Bus()` → `i2c_bus_shared_init()` ; le flag `initialized` est posé avant l’appel récursif, mais la fonction n’a pas fini d’allouer le bus, menant à stack overflow ou bus non initialisé.【F:components/i2c/i2c_bus_shared.c†L14-L42】【F:components/i2c/i2c.c†L88-L118】
- **Concurrence** (P1) : `DEV_I2C_Set_Slave_Addr()` supprime le device handle sans mutex global, possible collision avec d’autres utilisateurs du bus partagé.【F:components/i2c/i2c.c†L121-L160】
- **Robustesse** (P2) : timeouts de 1 s sur toutes les transactions (risque WDT LVGL) et erreurs loguées à chaque probe/read sans backoff ; `i2c_bus_shared_lock()` rappelle init dans le lock path (peut ré-entrer en init depuis ISR/tâche).【F:components/i2c/i2c.c†L96-L118】【F:components/i2c/i2c.c†L200-L280】【F:components/i2c/i2c_bus_shared.c†L44-L54】

**Correctifs recommandés**
- Casser la récursion : init mutex local puis créer le bus directement ici, ou faire de `DEV_I2C_Init_Bus` un helper privé sans appel inverse.
- Encadrer l’ajout/suppression de devices par le mutex global et fournir une API idempotente par adresse.
- Réduire les timeouts (100–200 ms) et ajouter backoff/compteurs d’erreurs côté bus.

### components/io_extension/io_extension.c
**Rôle** : pilotage IO expander (CS SD, backlight, resets) via I2C partagé.

**Problèmes**
- **Concurrence** (P1) : mutex interne + mutex bus pris séquentiellement, mais `IO_EXTENSION_Output` peut retourner sans rendre le lock bus si `DEV_I2C_Write_Nbyte` échoue avant `bus_locked` testé (chemin d’erreur silencieux).【F:components/io_extension/io_extension.c†L84-L160】
- **Robustesse** (P2) : init ne vérifie pas la validité de l’adresse/ACK autrement qu’en écrivant 0xFF sur tous les GPIO ; aucun retry/backoff ni désactivation des fonctions dépendantes en cas d’échec.【F:components/io_extension/io_extension.c†L52-L123】

**Correctifs recommandés**
- Garantir la restitution des mutex par blocs `goto unlock`/RAII et ajouter un path de désactivation (retour `ESP_ERR_NOT_FOUND`) pour que board/SD/backlight puissent se mettre en mode dégradé.

### components/touch/gt911.c
**Rôle** : pilote GT911 (IRQ + tâche), filtrage, calibration legacy, stats.

**Problèmes**
- **Concurrence** (P1) : la tâche IRQ prend le lock bus pour chaque lecture (OK) mais l’ISR désactive/réactive l’IRQ avec `ets_delay_us`, dépendant de la stack ROM ; pas de garde contre l’appel I2C depuis l’ISR (actuellement absent mais non protégé par assertion).【F:components/touch/gt911.c†L1360-L1460】
- **Robustesse** (P2) : plusieurs compteurs/états (`s_poll_mode`, `s_empty_window_count`) non protégés par mutex, pouvant dériver en cas de reconfiguration dynamique ; pas de watchdog sur la tâche GT911 (boucle infinie).【F:components/touch/gt911.c†L1400-L1460】

**Correctifs recommandés**
- Ajouter un flag d’état protégé (spinlock) pour les modes poll/irq, et un délai borné + backoff sur les erreurs I2C pour éviter le spam log.

### components/lvgl_port/lvgl_port.c
**Rôle** : port LVGL (tick, buffers, flush RGB, tâche LVGL, input touch transform, VSYNC sync).

**Problèmes**
- **Mémoire/DMA** (P1) : buffers LVGL alloués en PSRAM avec `MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM` sans vérifier la compatibilité RGB DMA ; si PSRAM non DMA-capable, `esp_lcd_panel_draw_bitmap` peut échouer ou corrompre (aucun fallback vers RAM interne).【F:components/lvgl_port/lvgl_port.c†L234-L290】
- **Performance/Blocage** (P1) : flush attend un sémaphore VSYNC jusqu’à 100 ms dans le callback LVGL, potentiellement bloquant la tâche LVGL entière en cas d’absence de VSYNC (init panel raté). Désactivation après 3 timeouts, mais la première boucle peut déclencher WDT si le driver ne poste jamais l’event.【F:components/lvgl_port/lvgl_port.c†L180-L230】
- **Robustesse** (P2) : tick timer n’est jamais arrêté/désenregistré ; en cas de réinit partielle, risque de double timer. Pas de contrôle de retour sur `lv_display_create`/`indev_create` en cas d’allocation partielle (tâche LVGL quand même lancée).【F:components/lvgl_port/lvgl_port.c†L204-L362】

**Correctifs recommandés**
- Forcer un test de capacité DMA (ou `MALLOC_CAP_INTERNAL` fallback) et log explicite de la localisation des buffers ; autoriser un mode single-buffer réduit si PSRAM non DMA.
- Ajouter un timeout court + bypass VSYNC dès la première erreur, et exposer un hook pour désactiver l’attente quand le driver RGB n’est pas prêt.
- Valider toutes les allocations avant lancement de la tâche, sinon retourner `ESP_FAIL`.

### components/sd/sd.c
**Rôle** : montage SD via ext-CS, état, self-test.

**Problèmes**
- **Robustesse** (P2) : `sd_card_init()` propage l’erreur IO-expander mais ne tente aucune remédiation (reinit bus, backoff) ; `sd_extcs_mount_card` est appelé sans timeout configurable, et l’état interne n’est pas remis à `UNINITIALIZED` en cas d’échec partiel avant mount (risque de fausse détection).【F:components/sd/sd.c†L17-L63】
- **Sécurité/Blocage** (P3) : self-test écrit/efface un fichier sans vérifier l’espace libre ni verrouillage concurrent (peut perturber utilisation utilisateur).【F:components/sd/sd.c†L65-L132】

**Correctifs recommandés**
- Introduire une stratégie de retry bornée (ex: 2 tentatives avec délai) et un reset d’état clair ; exposer une API non bloquante pour tester la présence carte.

### components/net/src/net_manager.c
**Rôle** : provisioning Wi-Fi, gestion état, retries/backoff, événements, stockage NVS.

**Problèmes**
- **Sécurité/Logs** (P2) : les SSID/MdP ne sont pas explicitement masqués dans les logs de configuration (méthode `esp_wifi_set_config` logguée sans scrub).【F:components/net/src/net_manager.c†L60-L132】
- **Robustesse** (P2) : `schedule_wifi_retry` et la tâche retry utilisent des timers sans vérification d’erreur systématique ; le backoff monte à 30 s mais pas de plafond de tentatives ni de remise à zéro sur succès (risque de boucle infinie).【F:components/net/src/net_manager.c†L104-L200】

**Correctifs recommandés**
- Masquer le mot de passe dans tous les logs (ne jamais tracer `wifi_config_t.sta.password`). Ajouter un nombre maximal de tentatives ou un état “attente intervention UI”.

### Build/Config
- **Kconfig/CMake** (P1) : `components/board/CMakeLists.txt` dépend de `touch`/`touch_transform` mais `touch_transform` dépend du board pour dimensions ; risque de cycle latent. Harmoniser REQUIRES/PRIV_REQUIRES (esp_adc listé mais pas `driver/adc`).【F:components/board/CMakeLists.txt†L1-L7】
- **sdkconfig.defaults vs Kconfig** : divergence macros batterie (cf. plus haut) ; vérifier également `CONFIG_ARS_LVGL_BUF_LINES`/résolution 1024×600 pour buffer sizing.

## Top Issues (P0/P1)
| ID | Priorité | Composant | Symptôme | Cause | Fix | Effort |
| --- | --- | --- | --- | --- | --- | --- | --- |
| T1 | P0 | i2c/board | Crash boot dès l’init I2C | Récursion `i2c_bus_shared_init()` ↔ `DEV_I2C_Init_Bus()` | Supprimer l’appel croisé, init bus une seule fois sous mutex | M |
| T2 | P1 | board | Mesure batterie fausse / compilation confuse | Macros `CONFIG_ARS_BAT_ADC_CHAN` vs `CONFIG_ARS_BAT_ADC_CHANNEL` | Harmoniser Kconfig/macros, recalcul divider | S |
| T3 | P1 | board/io_extension | Périphériques dépendants non désactivés | Retours init ignorés, pas de mode dégradé | Propager `esp_err_t`, désactiver touch/backlight/SD en cas d’échec IOEXT | M |
| T4 | P1 | lvgl_port | Risque d’échec flush/latence | Buffers PSRAM potentiellement non DMA + attente VSYNC dans flush | Vérifier caps DMA, fallback RAM interne; rendre VSYNC optionnel après 1 timeout | M |
| T5 | P1 | touch | Concurrence/réentrance | État poll/irq non protégé, logs spurious sans backoff | Ajouter spinlock/critique + backoff sur erreurs I2C | M |

## Plan de remédiation
- **Sprint 1 (stabilisation)** : corriger récursion I2C (T1) ; harmoniser Kconfig batterie (T2) ; sécuriser init board/IOEXT avec gestion d’échec (T3). Validation : `idf.py fullclean build`, boot sans panic, log BOOT-SUMMARY cohérent.
- **Sprint 2 (qualité/perf)** : durcir lvgl_port (DMA check, VSYNC timeout, validation allocations) (T4) ; ajouter backoff + protections état dans GT911 (T5) ; réduire timeouts I2C et ajouter retries bornés.
- **Sprint 3 (refactor + dette tech)** : structurer SD ext-CS avec retry/état clair, sécuriser self-test ; durcir net_manager (masquage credentials, plafond retries) ; rendre le pattern LCD optionnel et documenter consommation mémoire.

## Commandes de validation recommandées
- `idf.py fullclean build` : doit réussir sans warnings critiques.
- `idf.py -p COM3 flash monitor` : vérifier logs init (I2C ok, LCD/touch ok ou dégradé, SD absent toléré), absence de WDT.
- `idf.py size` et `idf.py size-components` : suivre l’impact mémoire des buffers LVGL et du test pattern.

