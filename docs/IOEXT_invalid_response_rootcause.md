# IOEXT ESP_ERR_INVALID_RESPONSE Root Cause Analysis

## 1. Sources exactes de ESP_ERR_INVALID_RESPONSE dans le code

### 1.1 `components/sd/sd_host_extcs.c`

| Ligne | Fonction | Condition de retour |
|-------|----------|---------------------|
| 1021 | `sd_extcs_wait_for_r1()` | `invalid_seen == true` après timeout R1 |
| 1441 | `sd_extcs_reset_and_cmd0()` | R1=0x05 (illegal command) avec valid_r1 |
| 1446 | `sd_extcs_reset_and_cmd0()` | `valid_r1` true mais R1 != 0x01/0x00 |  
| 1448 | `sd_extcs_reset_and_cmd0()` | `saw_data && !valid_r1` |
| 1745 | `sd_extcs_low_speed_init()` | `saw_non_ff` après CMD0 fail |

### 1.2 `components/sd/sd.c`

| Ligne | Fonction | Condition de retour |
|-------|----------|---------------------|
| 130 | `sd_card_init()` | Read validation échoue après mount OK |

### 1.3 `components/io_extension/io_extension.c`

Ces fonctions ne retournent pas directement `ESP_ERR_INVALID_RESPONSE` mais propagent les erreurs I2C de `i2c_master_transmit()`:

| Ligne | Fonction | Description |
|-------|----------|-------------|
| 124 | `io_extension_write_shadow_unsafe()` | `i2c_master_transmit()` peut retourner INVALID_RESPONSE sur NACK |
| 153 | `io_extension_write_shadow_nolock()` | `i2c_master_transmit()` peut retourner INVALID_RESPONSE sur NACK |
| 248 | `IO_EXTENSION_IO_Mode()` | `i2c_master_transmit()` peut retourner INVALID_RESPONSE sur NACK |
| 334 | `IO_EXTENSION_Init()` | PROBE via `i2c_master_transmit()` |

## 2. Chaîne de causalité

```
sd_extcs_mount_card()
  ├── IO_EXTENSION_IO_Mode(0xFF)  ← NACK I2C possible ici
  │     └── i2c_master_transmit() → ESP_ERR_INVALID_RESPONSE
  │
  ├── sd_extcs_probe_cs_line()
  │     └── sd_extcs_set_cs() × 3
  │           └── io_extension_set/clear_bits_nolock()
  │                 └── io_extension_write_shadow_nolock()
  │                       └── i2c_master_transmit() → ESP_ERR_INVALID_RESPONSE
  │
  ├── sd_extcs_low_speed_init()
  │     ├── sd_extcs_reset_and_cmd0()
  │     │     └── sd_extcs_assert_cs() / sd_extcs_deassert_cs() × N
  │     │           └── sd_extcs_set_cs()
  │     │                 └── io_extension_*_bits_nolock()
  │     │                       └── i2c_master_transmit() → ESP_ERR_INVALID_RESPONSE
  │     │
  │     └── sd_extcs_send_command() × N (CMD8, CMD55, ACMD41, CMD58)
  │           └── sd_extcs_assert_cs() / sd_extcs_deassert_cs()
  │                 └── même chaîne...
```

## 3. Cause racine

**Le CH32V003 (IO Expander @ 0x24) ne peut pas traiter les commandes I2C à haute fréquence.**

Pendant l'initialisation SD:
1. Chaque commande SPI (CMD0, CMD8, etc.) nécessite un toggle CS via I2C
2. Les toggles CS rapides (< 1ms entre commandes) saturent le firmware I2C du CH32V003
3. Le CH32V003 répond par NACK → `ESP_ERR_INVALID_RESPONSE` de l'ESP-IDF I2C master
4. Les mécanismes de recovery I2C ajoutent de la latence et parfois échouent aussi

## 4. Facteurs aggravants

1. **Readback inutile**: Le `io_extension_write_shadow_nolock()` tente un readback qui génère des transactions I2C supplémentaires
2. **Recovery cascade**: Chaque échec déclenche `i2c_bus_shared_recover()` qui peut elle-même échouer
3. **Phase "CS probe RESTORE"**: Dépend d'un readback conditionnel qui peut échouer
4. **Aucun pacing**: Pas de délai entre les opérations I2C consécutives

## 5. Solution implémentée

### CONFIG_ARS_IOEXT_WRITE_ONLY_INIT (nouveau)

Quand activé:
- `io_extension_write_shadow_nolock()` considère OK si `i2c_master_transmit()` retourne `ESP_OK`
- Aucun readback
- Aucune restauration du shadow basée sur lecture
- Les phases critiques (IO_Mode, CS toggles) utilisent ce mode pendant l'init SD

### Pacing anti-saturation

- 1ms delay après IO_Mode OK
- 1ms delay après chaque write CS pendant sd_extcs init
- 5ms delay dans sd_extcs_probe_cs_line() pour la phase RESTORE

### Test IOEXT burnin

- Toggle IOEXT4 2000× avec 1ms entre toggles
- Log l'itération de première erreur
- Exécuté avant sd_init si CONFIG_ARS_IOEXT_BURNIN=y
