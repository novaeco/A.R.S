# Rapport code source — A.R.S (v2)

## Inventaire succinct du dépôt
- `main/`: orchestration boot, init NVS/netif, séquence board/LVGL/UI/SD.
- `components/board`: BSP écran/touch, orientation (rotation 180° par défaut), backlight via IOEXT.
- `components/rgb_lcd_port` & `components/lvgl_port`: passerelles RGB + LVGL (flush, VSYNC optionnel).
- `components/touch`, `touch_orient`, `touch_transform`: driver GT911, orientation, calibration + persistance NVS.
- `components/data_manager`: stockage LittleFS JSON (reptiles/événements/poids).
- `components/sd`: SDSPI avec CS via IO expander (IOEXT4).
- `components/net`, `web_server`: provisionnement Wi‑Fi, HTTP(S) embarqué.
- `components/io_extension`, `i2c`: accès partagé I²C (CH32V003), lignes backlight/VCOM/SD CS.
- `docs/`: guides build, analyses précédentes.

## Audit par domaines
- **BSP/board**: orientation écran appliquée (rotation 180°), backlight sur IOEXT2, VCOM/VDD via IOEXT6. Touch transform chargé avant UI; fallback orientation appliqué si NVS absent.
- **I²C/IOEXT**: bus partagé SDA=GPIO8 / SCL=GPIO9, IOEXT @0x24, CS SD sur IOEXT4, backlight IOEXT2, VCOM/VDD IOEXT6. Verrous i2c_bus_shared présents.
- **GT911 (touch)**: IRQ GPIO4, reset via IOEXT1. Chargement calibration avec validation CRC; fallback identité + orientation par défaut si absent.
- **LCD RGB/LVGL**: timings ST7262 1024×600, flush LVGL → esp_lcd_panel_draw_bitmap. Attente VSYNC désormais optionnelle (config `CONFIG_ARS_VSYNC_WAIT_ENABLE` par défaut désactivée) pour éviter timeouts.
- **SD (SPI ext‑CS)**: init SDSPI bas débit, CS via IOEXT4, montée fréquence après OCR. Logs réduits (sdmmc en niveau ERROR) et échecs bornés → état INIT_FAIL sans panic.
- **Stockage LittleFS**: montage sur `/data` partition `storage`. Désormais non bloquant: état `storage_unavailable` propagé, appels renvoient `ESP_ERR_INVALID_STATE` sans crash.
- **Stockage LittleFS/Joltwallet**: utilisé via VFS; pas de modification du port upstream.
- **Réseau/Wi‑Fi provisioning**: NVS namespace `net`, retry avec backoff. Copie SSID/mdp sécurisée via `strlcpy`; logs ne contiennent pas le mot de passe.
- **UI**: calibration tactile (LVGL) s’appuie sur transform active; suppression fonction non utilisée (`create_toggle_row`).
- **Sécurité**: génération token web via esp_random, stockage NVS `web`. Copies chaînes bornées dans web_server/net/data_manager.

## Constatations et statut (P0/P1/P2)
- **P0 LittleFS bloquant** (`main/main.c`, `components/data_manager`): `ESP_ERROR_CHECK` sur `data_manager_init` causait panic si partition absente ou corrompue. ✔️ Corrigé: init dégradé, état `storage_ok` loggé, fonctions renvoient `ESP_ERR_INVALID_STATE` sans crash.
- **P0 Chaînes non terminées** (`components/data_manager/src/data_manager.c`, `components/net/src/net_manager.c`, `components/web_server/src/web_server.c`): usages `strncpy` sans terminaison. ✔️ Corrigé via `strlcpy`/helpers + assertions de taille.
- **P1 Warning compilation** (`components/ui/ui_calibration.c`): fonction `create_toggle_row` non utilisée. ✔️ Supprimée.
- **P1 SD ext‑CS fréquence/logs** (`components/sd/sd_host_extcs.c`, `components/sd/Kconfig`): init par défaut 200 kHz et logs sdmmc verbeux. ✔️ Init ramenée à 100 kHz, cible 20 MHz conservée, logs sdmmc abaissés à ERROR, séquence inchangée.
- **P1 VSYNC wait timeout** (`components/lvgl_port`, `components/rgb_lcd_port`, `sdkconfig.defaults`): attente VSYNC forcée provoquait timeouts. ✔️ Ajout master switch `CONFIG_ARS_VSYNC_WAIT_ENABLE` (défaut off) pour désactiver proprement l’attente, tout en conservant l’option.
- **P1 Touch/orientation** (`components/touch_transform/touch_transform_storage.c`): sauvegarde calibration pouvait écraser orientation par défaut (swap/mirror vides). ✔️ Les flags d’orientation existants sont repliés depuis NVS lors de la sauvegarde si absents.
- **P2 Documentation** (`docs/*.md`): nouveaux livrables v2 ajoutés. ✔️ Rédigé.

## Points résiduels / surveillances (P2)
- Exécution `idf.py build/flash` non réalisée dans cet environnement; valider sur cible réelle (logs attendus dans matrice de validation).
- SD: si carte très lente, ajuster `CONFIG_ARS_SD_EXTCS_CMD0_PRE_CLKS_BYTES`/timeouts via Kconfig.
- VSYNC: si tearing visible et callback supporté, activer `CONFIG_ARS_VSYNC_WAIT_ENABLE` et vérifier absence de timeout.
