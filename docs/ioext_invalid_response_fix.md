# IOEXT ESP_ERR_INVALID_RESPONSE Fix Report

## 1. Root Cause Analysis

### 1.1 Où est généré ESP_ERR_INVALID_RESPONSE ?

**Source primaire: ESP-IDF I2C Master Driver**

L'erreur `ESP_ERR_INVALID_RESPONSE` n'est **PAS** générée par le code du projet. Elle est retournée par la fonction `i2c_master_transmit()` du driver ESP-IDF lorsque le device I2C (CH32V003 @ 0x24) répond par **NACK** (Not Acknowledge).

**Fichiers qui propagent cette erreur:**

| Fichier | Ligne | Fonction | Appel I2C |
|---------|-------|----------|-----------|
| `io_extension.c` | 152 | `io_extension_write_shadow_nolock()` | `i2c_master_transmit()` |
| `io_extension.c` | 260 | `IO_EXTENSION_IO_Mode()` | `i2c_master_transmit()` |
| `io_extension.c` | 379 | `IO_EXTENSION_Init()` PROBE | `i2c_master_transmit()` |
| `io_extension.c` | 500, 526 | BURNIN test | `i2c_master_transmit()` |

### 1.2 Chaîne de causalité

```
[sd_extcs_mount_card()]
       │
       ▼
[sd_extcs_set_cs()] 
       │
       ▼
[io_extension_set/clear_bits_nolock()]
       │
       ▼
[io_extension_write_shadow_nolock()]
       │
       ▼
[i2c_master_transmit(s_ioext_handle, data, 2, timeout)]
       │
       ├─── CH32V003 ACK ───▶ ESP_OK ✓
       │
       └─── CH32V003 NACK ───▶ ESP_ERR_INVALID_RESPONSE ✗
```

### 1.3 Pourquoi le CH32V003 répond NACK ?

1. **Saturation I2C slave firmware**: Le CH32V003 a un firmware I2C slave personnalisé avec ~1ms de latence par commande
2. **Rafale de commandes**: Pendant SD init, les toggles CS sont trop rapides (< 1ms entre commandes)
3. **GT911 concurrent**: Le driver touch GT911 peut faire des transactions I2C pendant SD init
4. **Recovery cascade**: Les mécanismes de recovery ajoutent du trafic I2C supplémentaire

## 2. Changements réalisés

### 2.1 CONFIG_ARS_IOEXT_TX_ONLY_STRICT (nouveau)

**Fichier:** `components/io_extension/Kconfig`

```
config ARS_IOEXT_TX_ONLY_STRICT
    bool "IOEXT TX-only strict mode (no readback/verify)"
    default y
```

**Effet:**
- `io_extension_write_shadow_nolock()` = 1 seul `i2c_master_transmit()`, pas de retry
- `IO_EXTENSION_IO_Mode()` = 1 seul transmit, pas de recovery cascade
- Aucun `i2c_master_transmit_receive()` sur les chemins CS
- Si l'ACK I2C est OK, considérer la commande OK (pas de "ACK applicatif")

### 2.2 Mode SD Init avec mutex I2C exclusif

**Fichier:** `components/sd/sd_host_extcs.c`

```c
// Pendant sd_extcs_mount_card():
// 1. Pause GT911 (désactive IRQ + polling + I2C)
// 2. Prendre le mutex I2C UNE fois pour toute l'init SD
// 3. Toutes les opérations CS utilisent _nolock variants
// 4. Libérer le mutex après mount
// 5. Reprendre GT911
```

### 2.3 Pacing anti-saturation

| Contexte | Délai | Raison |
|----------|-------|--------|
| Après chaque write CS OK | 2ms | Temps de traitement CH32V003 |
| Après erreur avant retry | 15ms | Recovery CH32V003 |
| Entre toggles CS probe | 5ms | Stabilisation bus |

### 2.4 Recovery L2 (bus destroy/recreate)

**Fichier:** `components/i2c/i2c_bus_shared.c`

```c
// Nouvelle fonction: i2c_bus_shared_hard_reset()
// Si 3 erreurs IOEXT consécutives:
// 1. Détruire le bus I2C master
// 2. Recréer le bus I2C master
// 3. Re-ajouter les devices (IOEXT, GT911)
// 4. Re-probe IOEXT
```

### 2.5 BURNIN test amélioré

**Fichier:** `components/io_extension/Kconfig`

```
config ARS_IOEXT_BURNIN
    bool "IOEXT burnin test (5000 cycles)"
    default n
```

**Comportement:**
- 5000 cycles de toggle IOEXT4 (bit4)
- 2ms entre chaque toggle
- Log itération de première erreur
- Exécuté avant SD init

## 3. Comment valider

### 3.1 Build et flash

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

### 3.2 Logs attendus (success)

```
I (xxx) io_ext: IO_Mode OK (tx-only, mask=0xFF)
I (xxx) sd_extcs: CS probe: IOEXT4 toggle OK (tx-only, shadow=1/0/1)
I (xxx) sd_extcs: SD init: I2C exclusive lock acquired
I (xxx) sd_extcs: CMD0: Card entered idle state (R1=0x01)
I (xxx) sd_extcs: CMD8 -> OK (pattern=0x000001AA)
I (xxx) sd_extcs: ACMD41 ready after N attempt(s)
I (xxx) sd_extcs: CMD58 OK
I (xxx) sd_extcs: SD init: I2C lock released
I (xxx) sd: SD state -> INIT_OK
I (xxx) sd: SD mount stats: cs_toggles=N i2c_errors=0 duration=Xms
```

### 3.3 Burnin test (si activé)

```
CONFIG_ARS_IOEXT_BURNIN=y
```

```
I (xxx) io_ext: IOEXT BURNIN: Starting 5000-toggle test on IO4...
I (xxx) io_ext: IOEXT BURNIN: PASS - 5000/5000 toggles successful (0 errors)
```

### 3.4 Critères de succès

| Test | Attendu |
|------|---------|
| Logs | Zéro `ESP_ERR_INVALID_RESPONSE` sur `io_ext` |
| I2C streak | `i2c_streak=0` après SD init |
| SD mount | state = INIT_OK |
| Touch | GT911 reprend après SD init |

### 3.5 Diagnostic si échec

Si vous voyez encore des erreurs:

1. **Vérifier GT911 pause:**
   ```
   grep "GT911 pause" logs.txt
   ```
   Doit montrer "paused" avant SD init

2. **Vérifier I2C exclusif:**
   ```
   grep "I2C exclusive lock" logs.txt
   ```
   Doit apparaître avant CMD0

3. **Activer burnin:**
   ```
   CONFIG_ARS_IOEXT_BURNIN=y
   ```
   Si burnin échoue avant 5000, problème hardware I2C

4. **Réduire I2C speed:**
   ```
   CONFIG_ARS_IOEXT_SCL_SPEED_HZ=25000
   ```
   Essayer 25kHz au lieu de 50kHz

## 4. Fichiers modifiés

| Fichier | Modification |
|---------|--------------|
| `components/io_extension/Kconfig` | +CONFIG_ARS_IOEXT_TX_ONLY_STRICT, burnin 5000 |
| `components/io_extension/io_extension.c` | TX-only strict, pacing 2ms, L2 recovery |
| `components/sd/sd_host_extcs.c` | Mutex I2C exclusif, pacing, simplified probe |
| `components/i2c/i2c_bus_shared.h` | +i2c_bus_shared_hard_reset() |
| `components/i2c/i2c_bus_shared.c` | Hard reset implementation |
| `sdkconfig.defaults` | +CONFIG_ARS_IOEXT_TX_ONLY_STRICT=y |
