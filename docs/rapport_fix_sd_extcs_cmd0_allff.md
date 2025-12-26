# Rapport correctif — SD ext-CS CMD0 tout à 0xFF

## Contexte
- Bus I2C partagé : SCL=9, SDA=8.
- IO extender CH32V003 @0x24, utilisé pour SD CS (IOEXT4).
- SDSPI en mode ext-CS : MOSI=11, SCK=12, MISO=13, CS via IOEXT4 (actif bas).
- Symptôme P0 : CMD0 ne reçoit que 0xFF, suspicion CS non maintenu bas ou réécrit pendant la transaction.

## Cause racine
- Le registre de sortie du CH32V003 était réécrit à l’octet sans mutex dédié ni shadow unique. Plusieurs appels IOEXT (SD, backlight, reset) pouvaient écraser le bit CS entre l’assertion et la transaction SPI.

## Correctifs
- Ajout d’un mutex IOEXT dédié + shadow `s_ioext_out_shadow` unique. Toute écriture passe par `io_extension_lock()` + `io_extension_{set,clear}_bits_locked()` + `io_extension_write_shadow_locked()`.
- Sérialisation CS dans `sd_host_extcs`: lock IOEXT, CS bas, délai, transaction SPI, CS haut, unlock. Aucune autre écriture IOEXT possible pendant le SPI.
- Logs de preuve avant/après CMD0 : shadow, bit CS, sondes MISO CS=HIGH et CS=LOW.

## Validation proposée
1. `idf.py fullclean && idf.py build`
2. `idf.py -p <PORT> flash monitor`
3. Observer les logs `sd_extcs` :
   - `CMD0 diag pre` puis `CMD0 try ... cs_shadow=0 miso_probe=...`
   - `CMD0 diag post` avec `cs_shadow=1` après relâchement.
4. Carte absente : boot sans panic, logs class=ABSENT.
5. Carte présente : séquence CMD0->CMD8->ACMD41->CMD58 passe, montage OK.

## Rollback simple
`git checkout -- components/io_extension components/sd docs/rapport_fix_sd_extcs_cmd0_allff.md`
