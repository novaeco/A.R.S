# Rapport diagnostic boot / SD / LCD / Touch

## Contexte et limites
- Aucun log runtime fourni dans la demande (`[COLLER ICI LE LOG]` absent). Analyse basée uniquement sur le code actuel et les contraintes matérielles (ESP32-S3, SD SPI via IOEXT4, LCD RGB 1024×600, GT911 sur I2C partagé).
- Hypothèses à vérifier par log: présence/absence carte SD, niveau MISO au repos, fréquence de VSYNC réelle.

## Constats code (avant correctifs)
- **SD ExtCS** : CS piloté par IO_EXTENSION_IO_4, mode sortie configuré via `IO_EXTENSION_IO_Mode(0xFF)` juste avant l’init SDSPI. Séquence CMD0 déjà conforme (CS haut + ≥20 octets dummy puis CMD0 avec CS bas).
- **Diag MISO existant** : `sd_extcs_check_miso_health()` échantillonne MISO CS haut/bas mais ne forçait pas un test dédié avec CS maintenu bas.
- **I2C partagé** : mutex partagé déjà utilisé, mais pas de compteur/temporisation commune entre GT911 et IOEXT → risque de collisions lors des bursts d’erreurs.
- **GT911** : backoff local (doublage jusqu’à 500 ms) mais pas synchronisé avec l’état global du bus.
- **LCD / LVGL** : log “Flush duration high” ne distinguait pas temps de copie vs attente VSYNC → faux positifs à ~26 Hz.
- **Drift LCD** : aucun mécanisme pour baisser temporairement PCLK pendant Wi‑Fi/flash/NVS (recommandé par l’ESP LCD FAQ).

## Correctifs apportés
- **SD ExtCS** :
  - Journalisation explicite de la configuration IOEXT (push-pull, CS=IO4).
  - Nouveau mode debug conditionnel `CONFIG_ARS_SD_EXTCS_MISO_STUCK_DEBUG` : force CS bas via IOEXT4, cadence SCK (`CONFIG_ARS_SD_EXTCS_MISO_STUCK_SAMPLE_BYTES` octets), lit MISO/GPIO13, log si niveau constant (diagnostic “MISO stuck high / CS inactive”).
- **I2C partagé & GT911/IOEXT** :
  - Compteurs globaux d’erreurs et backoff dynamique (`i2c_bus_shared_backoff_ticks()`), alimentés par `i2c_bus_shared_note_error/success`.
  - IOEXT consomme ce backoff avant de prendre le bus.
  - GT911 note succès/erreurs sur chaque transaction et applique le backoff partagé en plus du backoff local dans la tâche IRQ/polling.
- **LCD / stabilité PCLK** :
  - Garde optionnel `CONFIG_ARS_LCD_PCLK_GUARD_ENABLE` : baisse PCLK à `CONFIG_ARS_LCD_PCLK_GUARD_SAFE_HZ` (par défaut 12 MHz) avec stabilisation `CONFIG_ARS_LCD_PCLK_GUARD_SETTLE_MS`, puis restauration.
  - Guard appelé autour de `esp_wifi_start/connect` (net_manager) et des opérations NVS au boot (main). Si l’écran n’est pas encore initialisé ou option désactivée, l’appel est neutre.
- **Logs LVGL flush** :
  - Séparation des temps de copie et d’attente VSYNC dans l’alerte. Alerte seulement si copie >8 ms, attente VSYNC > timeout ou (mode sans VSYNC) total >5 ms → évite les faux positifs à ~26 Hz.

## Étapes de vérification recommandées
1. `idf.py fullclean`
2. `idf.py build`
3. `idf.py -p COM3 flash monitor`
   - SD absente : log `SD state -> ABSENT` et diag MISO (si debug activé) sans panic.
   - SD présente : CMD0/ACMD41/CMD58 OK, pas d’erreur “MISO stuck high”.
   - I2C : pas de rafale de `i2c_errors` dans `TOUCH_EVT`; si erreurs, vérifier que backoff augmente (logs i2c_bus_shared).
   - LVGL : si fréquence ~26 Hz, pas de “Flush slow” sauf copie >8 ms ou VSYNC hors budget.

## Acceptation / impacts
- Boot ne bloque pas si SD absente ou I2C dégradé; diagnostics plus déterministes (MISO forcé, comptage erreurs I2C).
- Option PCLK guard neutre par défaut, activable via Kconfig pour réduire le drift pendant Wi‑Fi/flash/NVS.
- Logging LVGL plus lisible pour distinguer VSYNC vs copie.
