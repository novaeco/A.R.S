# Rapport SDSPI Ext-CS — Diagnostic CMD0 (Waveshare ESP32-S3 Touch LCD 7B)

## Contexte et problème observé
- Cible : ESP32-S3 (ESP-IDF v6.1-dev), LCD RGB 1024×600, LVGL 9.x, touch GT911.
- SD en SDSPI avec CS via IO extension CH32V003 (IOEXT4 actif à l’état bas).
- Symptôme terrain : CMD0 retourne uniquement `0xFF` sur toutes les tentatives (`"CMD0 failed after 12 tries ... raw= FF ... MISO stuck high or CS inactive"`).

## Analyse et corrections apportées (logiciel)
- Vérification systématique du chemin CS :
  - CS (IOEXT4) est asserti **avant** chaque transaction CMD0/commandes ultérieures et désasserti juste après.
  - Instrumentation `cs_shadow` pour tracer le niveau demandé et son timing.
  - Ajout d’un échantillonnage MISO sous CS bas (quelques clocks 0xFF) pour vérifier que la ligne répond avant d’émettre CMD0.
- Instrumentation CMD0 détaillée :
  - Log par tentative : fréquence effective, délai pré-CMD0, dump des 16 premiers octets reçus, échantillon MISO sous CS bas, statut de release CS.
  - Classification explicite : `OK` (R1=0x01), `WIRED_BUT_NO_RESP` (octets non-FF mais R1 invalide), `ABSENT` (uniquement 0xFF).
- Log d’ordre d’initialisation :
  - Vérification I2C/io_extension avant SPI.
  - Log du host SPI utilisé (SPI2), mode 0, DMA, fréquence init (chemin basse vitesse).

## Comment reproduire / vérifier
1. Build complet : `idf.py fullclean build`
2. Flash + monitor : `idf.py flash monitor`
3. Logs attendus (extraits) :
   - `SDSPI host=2 mode=0 dma=... init_khz=...`
   - `CMD0 pre-sequence: CS high -> ...`
   - Tentatives CMD0 : `CMD0 try 1/12 @100 kHz ... idx=... cs_shadow=0 miso_probe= FF FF ... -> ...`
   - Classification finale :
     - **OK** : `CMD0: Card entered idle state (R1=0x01) class=OK`
     - **ABSENT** : `CMD0 failed ... class=ABSENT (all 0xFF)` + message `MISO stuck high or CS inactive...`
     - **WIRED_BUT_NO_RESP** : `CMD0 failed ... class=WIRED_BUT_NO_RESP` (présence d’octets ≠ 0xFF sans R1 valide)

## Interprétation matérielle vs logicielle
- Si après ces traces **ABSENT (all 0xFF)** persiste avec `cs_shadow=0` et miso_probe 100% 0xFF :
  - Probable défaut matériel : CS non câblé ou toujours haut, MISO flottant, carte non alimentée/absente.
- Si `WIRED_BUT_NO_RESP` apparaît avec octets non-FF :
  - Liaison câblée mais protocole perturbé (bruit, fréquence trop haute, mauvais contact). Baisser la fréquence init ou vérifier câblage.
- Si `OK` puis échec ultérieur :
  - Problème post-CMD0 (ex : CMD8/ACMD41), vérifier alimentation et continuité SPI.

## Rollback simple
`git checkout -- components/sd/sd_host_extcs.c docs/rapport_sd_extcs.md`
