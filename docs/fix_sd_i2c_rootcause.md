# Correction init SD / I2C partagé (ESP32-S3 Waveshare 7B)

## Cause racine observée
- Les échecs SD (CMD8/ACMD41) déclenchaient `i2c_bus_shared_recover()` qui **supprimait** le bus I2C (`i2c_del_master_bus`) alors que des handles actifs (GT911, IO expander) existaient encore.
- La suppression/recréation du bus provoquait une cascade d'erreurs fatales : `I2C bus already acquired`, `Cannot delete I2C bus: devices are still attached`, perte temporaire d'IRQ touch et accumulation d'`i2c_errors` pendant les retries SD.
- La machine d’état SD considérait un timeout/0xFF sur CMD8 comme « SDSC », poursuivant ACMD41 sans diagnostic explicite alors que la communication SPI était instable.

## Correctifs appliqués
- **Récupération I2C non destructive** : le bus partagé n’est plus supprimé ; une séquence de dégagement (pulses SCL + STOP) est appliquée sous mutex global avec backoff pour éviter les boucles serrées.
- **Sérialisation stricte** : le même mutex global protège la récupération ; les appels existants IOEXT/GT911 restent sérialisés sur ce verrou unique.
- **Init SD robuste** :
  - CMD8 est retenté une fois ; un timeout/0xFF est traité comme instabilité (HCS forcé à 0, délai supplémentaire) au lieu d’annoncer SDSC.
  - Le chemin « ILLEGAL_CMD » est le seul à qualifier explicitement un flux legacy (SDSC/SDv1), avec log dédié.
  - Les retries ACMD41 se font après ces gardes, sans réinitialiser le bus I2C.

## Validation attendue
Commandes à exécuter depuis la racine du dépôt :
1. `idf.py fullclean`
2. `idf.py build`
3. `idf.py -p COM3 flash monitor`

Critères de succès en log (flash/monitor) :
- **I2C** : absence de `Cannot delete I2C bus`, `bus already acquired`, `i2c_new_master_bus(...): I2C bus acquire failed`.
- **Touch/IOEXT** : plus de montée d’`i2c_errors` pendant les retries SD ; touch continue de reporter ou se met en pause proprement.
- **SD** : soit montage OK (CMD8/ACMD41/CMD58 visibles), soit échec propre (`INIT_FAIL`/`ABSENT`) sans impact sur les modules I2C.

En cas d’échec, collecter les logs autour de `CMD8`, `ACMD41`, et `I2C shared bus error` pour diagnostic.
