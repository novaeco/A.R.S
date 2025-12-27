# Rapport — SD ExtCS (SDSPI via IO extender)

## 1) Symptôme observé
- Log terrain: CMD0 retourne uniquement `0xFF` malgré plusieurs tentatives, classant la carte en **ABSENT** et laissant le reste du système démarrer.

## 2) Fichiers concernés
- `components/sd/sd_host_extcs.c` : séquence SDSPI ExtCS (CMD0/8/ACMD41/58) et verrous CS/I²C/SPI.
- `components/sd/sd.c` : orchestration d’init SD, retries, backoff, état public.
- `components/i2c/i2c_bus_shared.c` : gestion du bus I²C partagé (GT911 + IO extender) et stratégie de désinitialisation.

## 3) Avant / Après
### Avant
- CMD0 jusqu’à 12 tentatives avec logs très verbeux, pouvant rallonger le boot en cas d’absence de carte.
- Retry SD sans backoff ni exclusion stricte, possible re-entrée concurrente.
- `i2c_bus_shared_deinit()` supprimait potentiellement le bus maître (`i2c_del_master_bus`) même si des devices étaient encore attachés.

### Après
- CMD0 borné à 3 tentatives avec logs réduits (résumé final seulement en cas d’échec), exit rapide en ABSENT quand tout reste à `0xFF`.
- `sd_card_init()` protégé par mutex + deadline (≈1,2 s) pour éviter le blocage boot; retries limités.
- Nouvelle politique de retry montable: backoff minimal (2 s), ignore si déjà en cours.
- Désinitialisation I²C sécurisée : le bus n’est plus supprimé pour éviter d’invalider les handles actifs (GT911, IOEXT); log d’avertissement uniquement.

## 4) Reproduction / Validation
1. **Build**  
   - `idf.py fullclean build`
2. **Monitor (carte absente)**  
   - `idf.py -p <PORT> flash monitor`  
   - Attendus: boot complet (LCD/LVGL/GT911 OK), logs SD limités (~quelques lignes), état SD=ABSENT sans panic.
3. **Monitor (carte insérée FAT32)**  
   - `idf.py -p <PORT> flash monitor`  
   - Attendus: mount OK, lecture secteur 0 OK, état SD=INIT_OK, clock relevée après OCR.
4. **Retry manuel**  
   - Depuis UI ou commande existante, appeler `sd_card_retry_mount()`/`sd_retry_mount()` une fois carte insérée; si appel répété <2 s, il est ignoré (backoff).  
   - Attendus: aucun blocage, état cohérent (INIT_OK ou ABSENT/INIT_FAIL selon présence).

## 5) Acceptation
- Boot non bloquant, SD absente → continuation sans crash.
- Concurrence: verrous pour SPI/I²C/CS + mutex SD pour init/retry.
- Pas de suppression du bus I²C tant que des périphériques sont potentiellement actifs.
