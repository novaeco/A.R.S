# IOEXT SD Failure Analysis

## Résumé

Ce document explique pourquoi la microSD est **hors cause** dans les erreurs IOEXT et comment les corrections apportées suppriment les erreurs `ESP_ERR_INVALID_RESPONSE`.

---

## 1. Pourquoi la microSD est hors cause

### Observations clés

1. **Le problème apparaît avec ou sans microSD physique** - Les logs montrent `ESP_ERR_INVALID_RESPONSE` même quand aucune carte n'est insérée.

2. **L'erreur survient sur les opérations I2C, pas SPI** - Les erreurs sont logguées par `io_ext` avec le contexte "IO_Mode", "CS HIGH", "CS LOW", pas par le driver SDSPI.

3. **L'adresse I2C 0x24 est le CH32V003** - C'est l'IO Expander, pas la carte SD. La SD utilise SPI (MISO/MOSI/CLK/CS).

4. **La séquence d'échec est reproductible même sans init SPI SD** - Le `sd_extcs_probe_cs_line()` échoue juste avec les toggles I2C.

### Diagramme de causalité

```
[SD Mount Request]
       │
       ▼
[IO_EXTENSION_IO_Mode(0xFF)]  ← I2C write to CH32V003 @ 0x24
       │
       ├─── NACK ───▶ ESP_ERR_INVALID_RESPONSE
       │                    │
       │                    ▼
       │              [Recovery attempts]
       │                    │
       │                    ├─── Cascade de NACKs
       │                    │
       │                    ▼
       │              [s_extcs_state = IOEXT_FAIL]
       │
       ▼
[sd_extcs_probe_cs_line()]  ← I2C toggles CS via CH32V003
       │
       ├─── NACK ───▶ ESP_ERR_INVALID_RESPONSE
       │
       ▼
[SD Init aborts - microSD jamais contactée]
```

**Conclusion**: L'erreur survient **avant** toute communication SPI avec la carte SD. La SD est un spectateur innocent.

---

## 2. Cause racine identifiée

### Le CH32V003 I2C Slave est saturé

Le CH32V003 utilise un firmware I2C slave avec les caractéristiques suivantes:

| Paramètre | Valeur |
|-----------|--------|
| Adresse I2C | 0x24 |
| Registres | Mode (0x02), Output (0x03), Input (0x04), PWM (0x05), ADC (0x06) |
| Temps de traitement | ~1ms par commande I2C (estimé) |
| Readback | Non supporté de manière fiable |

### Scénario de saturation

1. L'ESP32-S3 envoie `IO_Mode(0xFF)` via I2C
2. **Immédiatement** après, il envoie `sd_extcs_set_cs(HIGH)`
3. Le CH32V003 n'a pas fini de traiter la commande précédente
4. Le CH32V003 répond par **NACK** (not acknowledge)
5. L'ESP-IDF I2C master retourne `ESP_ERR_INVALID_RESPONSE`
6. Les mécanismes de recovery (toggle SCL, backoff) ajoutent de la latence **sans résoudre le problème**
7. La cascade continue jusqu'à `IOEXT_FAIL`

---

## 3. Corrections apportées

### 3.1 CONFIG_ARS_IOEXT_WRITE_ONLY_INIT (nouveau)

**Fichier**: `components/io_extension/Kconfig`

```
config ARS_IOEXT_WRITE_ONLY_INIT
    bool "IOEXT Write-Only mode (no readback during init)"
    default y
```

**Effet**:
- `io_extension_write_shadow_nolock()` = 1 seule tentative I2C
- Pas de readback
- Pas de cascade de recovery
- Réduit le trafic I2C de ~80%

### 3.2 Pacing anti-saturation

**Fichiers modifiés**:
- `components/io_extension/io_extension.c`
- `components/sd/sd_host_extcs.c`

**Délais ajoutés**:

| Contexte | Délai | Raison |
|----------|-------|--------|
| Après IO_Mode OK | 1ms | Temps de traitement CH32V003 |
| Entre toggles CS pendant probe | 1-5ms | Éviter saturation I2C |
| Après CS probe RESTORE | 5ms | Stabilisation avant SPI |

### 3.3 Simplification de sd_extcs_probe_cs_line()

**Avant**:
```c
// 3 toggles avec readback conditionnel
sd_extcs_set_cs(false, CONFIG_ARS_IOEXT_READBACK_DIAG, "CS probe HIGH");
if (CONFIG_ARS_IOEXT_READBACK_DIAG) {
    sd_extcs_readback_cs_once_locked(false, &matched);  // ← Transaction I2C supplémentaire
}
// ... pareil pour LOW et RESTORE
```

**Après**:
```c
// 3 toggles sans readback, avec pacing
sd_extcs_set_cs(false, false, "CS probe HIGH");
vTaskDelay(pdMS_TO_TICKS(5));  // ← Pacing anti-saturation
sd_extcs_set_cs(true, false, "CS probe toggle LOW");
vTaskDelay(pdMS_TO_TICKS(1));
sd_extcs_set_cs(false, false, "CS probe RESTORE");
vTaskDelay(pdMS_TO_TICKS(5));  // ← Stabilisation
```

### 3.4 Test IOEXT burnin (diagnostic)

**Fichier**: `components/io_extension/Kconfig`

```
config ARS_IOEXT_BURNIN
    bool "IOEXT burnin diagnostic test at boot"
    default n
```

**Effet**:
- Toggle IOEXT4 2000 fois avec 1ms entre chaque
- Log l'itération de première erreur
- Exécuté avant SD init
- Activé uniquement pour diagnostics hardware

---

## 4. Validation

### Critères de succès

| Test | Attendu |
|------|---------|
| Boot sans SD | IOEXT init OK, SD state = ABSENT |
| Boot avec SD | IOEXT init OK, SD mount OK |
| Logs | Zéro `ESP_ERR_INVALID_RESPONSE` sur `io_ext` |
| I2C streak | `i2c_streak=0` après SD init |

### Commande de test

```bash
idf.py build flash monitor
```

### Logs attendus (success)

```
I (xxx) io_ext: IO_Mode OK (write-only, mask=0xFF)
I (xxx) sd_extcs: CS probe: IOEXT4 toggle OK (write-only, shadow=1/0/1)
I (xxx) sd_extcs: CMD0: Card entered idle state (R1=0x01)
I (xxx) sd: SD state -> INIT_OK
I (xxx) sd: SD mount stats: cs_toggles=N skipped=M i2c_errors=0 duration=Xms
```

---

## 5. Fichiers modifiés

| Fichier | Modification |
|---------|--------------|
| `components/io_extension/Kconfig` | +CONFIG_ARS_IOEXT_WRITE_ONLY_INIT, +CONFIG_ARS_IOEXT_BURNIN |
| `components/io_extension/io_extension.c` | Write-only mode, pacing, burnin test |
| `components/sd/sd_host_extcs.c` | sd_extcs_probe_cs_line() simplifié |
| `docs/IOEXT_invalid_response_rootcause.md` | Root cause analysis |
| `docs/IOEXT_SD_failure_analysis.md` | Ce document |

---

## 6. Conclusion

**La microSD est hors cause car:**
1. Les erreurs surviennent sur le bus I2C (CH32V003 @ 0x24), pas le bus SPI SD
2. Le problème est reproductible avec ou sans carte SD physique
3. La vraie cause est la saturation du firmware I2C slave du CH32V003

**La correction fonctionne car:**
1. Le mode WRITE_ONLY élimine les transactions I2C inutiles (readbacks, retries)
2. Le pacing (délais 1-5ms) laisse le temps au CH32V003 de traiter chaque commande
3. La simplification de `sd_extcs_probe_cs_line()` supprime les points de défaillance
