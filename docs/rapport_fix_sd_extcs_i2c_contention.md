# Rapport — Fix SD ExtCS & contention I²C (Waveshare ESP32-S3 7B)

## Contexte
- Board Waveshare ESP32-S3 Touch LCD 7B (LCD 1024×600, GT911 I²C, IO extender CH32V003 @0x24, SD SPI avec CS sur EXIO4).
- Symptômes : erreurs `I2C shared bus error [sd_extcs_cs]: ESP_ERR_INVALID_RESPONSE` pendant CMD0/CMD8/ACMD41, rafales de re-probe IOEXT, `send_op_cond returned 0x107`, montage FAT échoué.

## Causes identifiées
- Bascule CS via IO extender non atomique (ré-essais sans backoff, re-probe agressif) → surcharge bus I²C partagé avec GT911.
- Recovery I²C peu contextualisé (pas de tag, logs répétitifs) et sans nettoyage contrôlé des lignes.
- Tâche GT911 active pendant la fenêtre critique CMD0/ACMD41 → contention I²C sur SDA=8/SCL=9.

## Correctifs appliqués (P0)
- **CS ExtCS atomique** : une seule séquence CS LOW/HIGH sous mutex I²C + mutex IOEXT, shadow conservé, 1 recovery + 1 retry max avec backoff borné (10 ms), suppression du re-probe en rafale. Logs courts par tentative et streak. 【F:components/sd/sd_host_extcs.c†L332-L385】【F:components/sd/sd_host_extcs.c†L1561-L1605】
- **Recovery I²C taggé** : `i2c_bus_shared_recover(tag)` et variantes locked/force, bit-bang SCL/SDA sous mutex unique, backoff/ratelimit, réinit si bus inactif, logs compactes (forced/ctx). 【F:components/i2c/i2c_bus_shared.c†L27-L123】【F:components/i2c/i2c_bus_shared.c†L198-L349】【F:components/i2c/i2c_bus_shared.h†L56-L96】
- **Contre-mesures contention touch** : `CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT` par défaut `y`; `touch_pause_for_sd_init` gardé sous Kconfig dans `sd.c`; `GT911` renvoie immédiatement “no touch” quand pausé (aucune transaction I²C). 【F:components/sd/Kconfig†L68-L98】【F:components/sd/sd.c†L34-L82】【F:components/touch/gt911.c†L661-L681】
- **Logs de stages init SD** : option `CONFIG_ARS_SD_EXTCS_STAGE_LOG` (def=y) pour une ligne par étape CMD0/CMD8/ACMD41/CMD58 ou recover, sans spam. 【F:components/sd/Kconfig†L101-L116】【F:components/sd/sd_host_extcs.c†L65-L114】【F:components/sd/sd_host_extcs.c†L149-L200】【F:components/sd/sd_host_extcs.c†L1607-L1684】【F:components/sd/sd_host_extcs.c†L1807-L1872】

## Points de validation attendus
- Commandes :
  - `idf.py fullclean`
  - `idf.py build`
  - `idf.py -p <PORT> flash monitor`
- Logs attendus en monitor :
  - Plus d’erreurs `I2C shared bus error [sd_extcs_cs]` pendant CMD0/8/41/58.
  - Traces de stage si `CONFIG_ARS_SD_EXTCS_STAGE_LOG=y` : `CMD0 -> IDLE`, `CMD8 -> OK/illegal/unstable`, `ACMD41 ready`, `CMD58 OK` (ou `CMD58 skipped (MMC)`).
  - Si carte absente : état final `SD_EXTCS_STATE_ABSENT` ou `INIT_FAIL` sans rafale de recover/re-probe, boot continue.
  - Si carte présente FAT32 : montage OK, message `SD state: INIT_OK`, aucun spam I²C.

## Limites / suites possibles (P1)
- Optionnel : abaisser temporairement la fréquence I²C pendant CMD0/ACMD41 si de nouvelles interférences sont observées (non implémenté ici).
- Surveiller les logs `I2C recover [...]` : si récurrents, vérifier câblage SDA/SCL et pulls 4.7kΩ.

## Rollback simple
- Revenir au commit précédent sur `components/i2c`, `components/sd`, `components/io_extension`, `components/touch`.
- Désactiver la pause touch : `CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT=n` dans `sdkconfig` si besoin de comparer. 
