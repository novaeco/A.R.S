# Rapport Technique: Correctifs SD/I2C Robustesse

**Date:** 2024-12-28  
**Auteur:** Antigravity (Ingénieur Firmware)  
**Projet:** A.R.S (ESP32-S3 + Waveshare Touch LCD 7B)

---

## Résumé Exécutif

Ce document décrit les correctifs apportés pour résoudre les erreurs `ESP_ERR_INVALID_RESPONSE` (NACK I2C) survenant lors de l'initialisation de la carte SD en mode SDSPI avec CS externe via l'IO expander (CH32V003 @ I2C 0x24).

### Symptômes Observés (avant correctifs)
- Boot OK (LCD/touch/LVGL fonctionnels)
- Init SD progresse (CMD0/CMD8/ACMD41 OK)
- Échec sur CMD58 : l'assertion CS via io_ext échoue
- Erreurs I2C : `ESP_ERR_INVALID_RESPONSE` sur `[sd_extcs_cs]` et `io_ext IO_Mode`
- Résultat final : `SD_STATE_MOUNT_FAIL` ou `SD_STATE_INIT_FAIL`

---

## Cause Racine Identifiée

### Analyse Factuelle

1. **Bus I2C partagé** : Un seul bus I2C (SDA=GPIO8, SCL=GPIO9) est partagé entre:
   - IO Expander CH32V003 (addr 0x24) - contrôle le CS SD (EXIO4)
   - Touch GT911 (addr 0x5D) - polling régulier pour les événements touch

2. **Latence CH32V003** : Le firmware CH32V003 a une latence I2C plus élevée qu'un périphérique I2C standard. À 100kHz, les toggles CS rapides pendant l'init SD causaient des NACKs.

3. **Absence de retry sur chemin critique** : La fonction `io_extension_write_shadow_nolock()` (appelée par `io_extension_set_bits_nolock()` / `io_extension_clear_bits_nolock()`) n'avait **aucun mécanisme de retry**, causant des échecs immédiats.

4. **Pas d'abort précoce** : Lors d'erreurs I2C persistantes, le code continuait à spammer des tentatives CMD55/ACMD41/CMD58 au lieu d'abandonner rapidement.

---

## Correctifs Appliqués

### P0-A: Mutex I2C (Validation)
**Fichier:** `components/i2c/i2c_bus_shared.c`  
**État:** ✅ Déjà correctement implémenté

Le mutex global `g_i2c_bus_mutex` (récursif) est utilisé via:
- `i2c_bus_shared_lock()` / `i2c_bus_shared_unlock()`
- `ars_i2c_lock()` / `ars_i2c_unlock()`

**Ordre de verrouillage cohérent:**
1. `sd_extcs_lock()` (mutex SD)
2. `io_extension_lock()` (mutex IOEXT)
3. `i2c_bus_shared_lock()` (mutex I2C)

### P0-B: Vitesse I2C Stable (Validation)
**Fichier:** `sdkconfig.defaults`  
**État:** ✅ Déjà configuré

```makefile
CONFIG_ARS_IOEXT_SCL_SPEED_HZ=50000  # 50kHz pour CH32V003
```

Aucun changement dynamique de fréquence I2C n'est effectué. La vitesse est fixée à l'init.

**Source:** ESP-IDF I2C Master Driver - la configuration par-device permet des vitesses différentes sans reconfigurer le bus master.

### P0-C: Retry Borné sur IOEXT
**Fichier:** `components/io_extension/io_extension.c`  
**Fonction:** `io_extension_write_shadow_nolock()`

**Changement:**
```c
// P0-C FIX: Bounded retry (2 attempts max) with recovery on NACK
const int max_attempts = 2;
esp_err_t ret = ESP_OK;
for (int attempt = 0; attempt < max_attempts; ++attempt) {
  ret = i2c_master_transmit(s_ioext_handle, data, 2, pdMS_TO_TICKS(100));
  if (ret == ESP_OK) {
    if (attempt > 0) {
      ESP_LOGI(TAG, "IOEXT write recovered after %d attempt(s)", attempt + 1);
    }
    return ESP_OK;
  }
  // On first failure, try I2C recovery
  if (attempt < max_attempts - 1) {
    ESP_LOGW(TAG, "IOEXT write attempt %d failed: %s, recovering...",
             attempt + 1, esp_err_to_name(ret));
    i2c_bus_shared_recover_locked_force("ioext_shadow");
    ets_delay_us(500);
  }
}
```

**Comportement:**
- Maximum 2 tentatives
- Recovery I2C forcé entre les tentatives
- Log clair en cas de récupération ou échec final

### P0-D: State Machine SD avec IOEXT_UNHEALTHY
**Fichier:** `components/sd/sd_host_extcs.c`

**Changements:**

1. **Tracking des erreurs consécutives:**
```c
static uint32_t s_ioext_consecutive_errors = 0;
#define SD_EXTCS_IOEXT_UNHEALTHY_THRESHOLD 3
```

2. **Dans `sd_extcs_set_cs()`:**
   - Reset du compteur sur succès
   - Incrément sur échec
   - Si >= seuil: état → `SD_EXTCS_STATE_IOEXT_FAIL`

3. **Early abort dans boucle ACMD41:**
```c
while (esp_timer_get_time() < init_deadline) {
  // P0-D FIX: Early abort if IOEXT became unhealthy
  if (s_extcs_state == SD_EXTCS_STATE_IOEXT_FAIL) {
    ESP_LOGE(TAG, "ACMD41 aborted: IOEXT unhealthy (CS via I2C failed)");
    return ESP_ERR_INVALID_STATE;
  }
  // ... reste de la boucle
}
```

4. **Reset dans `sd_extcs_seq_reset()`:**
```c
s_ioext_consecutive_errors = 0;  // Permet une nouvelle tentative propre
```

---

## Fichiers Modifiés

| Fichier | Lignes modifiées | Description |
|---------|------------------|-------------|
| `components/io_extension/io_extension.c` | L126-163 | Retry + recovery dans `io_extension_write_shadow_nolock()` |
| `components/sd/sd_host_extcs.c` | L153-158 | Variables IOEXT health tracking |
| `components/sd/sd_host_extcs.c` | L373-375 | Reset compteur sur succès CS |
| `components/sd/sd_host_extcs.c` | L440-448 | Increment + IOEXT_FAIL sur échec CS |
| `components/sd/sd_host_extcs.c` | L467-469 | Reset dans `seq_reset()` |
| `components/sd/sd_host_extcs.c` | L1822-1826 | Early abort ACMD41 |

---

## Comment Reproduire / Valider

### Build
```bash
cd c:\Users\woode\Desktop\ai\A.R.S
idf.py fullclean build
```

### Flash & Monitor
```bash
idf.py -p COMx flash monitor
```

### Critères de Validation
1. ✅ Boot complet sans panic/reboot
2. ✅ LCD affiche correctement
3. ✅ Touch GT911 répond aux événements
4. ✅ Log SD montre:
   - Soit `SD_STATE_INIT_OK` (carte montée)
   - Soit `SD_STATE_ABSENT` (pas de carte)
   - Soit `SD_STATE_IOEXT_FAIL` puis abort rapide (problème I2C)
5. ✅ Pas de spam de logs CMD55/ACMD41 en cas d'erreur IOEXT
6. ✅ Compteur d'erreurs I2C visible dans logs si activé

### Log attendu (succès)
```
I (xxx) sd_extcs: Mounting SD (SDSPI ext-CS) init=100 kHz target=20000 kHz
I (xxx) sd_extcs: CMD0 OK
I (xxx) sd_extcs: CMD8 OK
I (xxx) sd_extcs: ACMD41 ready after N attempt(s)
I (xxx) sd_extcs: CMD58 OK
I (xxx) sd: SD state -> INIT_OK
I (xxx) sd: SD read validation PASS (sector 0 readable)
```

### Log attendu (IOEXT défaillant)
```
W (xxx) io_ext: IOEXT write attempt 1 failed: ESP_ERR_INVALID_RESPONSE, recovering...
E (xxx) io_ext: IOEXT write failed after 2 attempts: ESP_ERR_INVALID_RESPONSE
E (xxx) sd_extcs: IOEXT unhealthy: 3 consecutive CS errors, aborting SD init
E (xxx) sd_extcs: ACMD41 aborted: IOEXT unhealthy (CS via I2C failed)
I (xxx) sd: SD state -> INIT_FAIL
```

---

## Vérifications Hardware Optionnelles

> *Cette section est fournie uniquement si les symptômes persistent malgré les correctifs firmware.*

1. **Pull-ups I2C**: Vérifier présence de résistances 4.7kΩ sur SDA (GPIO8) et SCL (GPIO9)

2. **Alimentation CH32V003**: Mesurer la tension VCC du CH32V003 pendant les toggles CS rapides (doit rester stable à 3.3V)

3. **Capacité de découplage**: Ajouter 100nF proche du CH32V003 si non présent

4. **Longueur des pistes I2C**: Si > 10cm, envisager des résistances série 22-47Ω sur SDA/SCL

---

## Commits Suggérés

```
fix(io_ext): add bounded retry with recovery in io_extension_write_shadow_nolock

fix(sd_extcs): add IOEXT health tracking for early abort on I2C failures

refactor(sd_extcs): reset IOEXT error counter in sd_extcs_seq_reset

docs: add rapport_fix_sd_i2c.md with technical analysis and fixes
```

---

## Notes Techniques

### Référence ESP-IDF
- **I2C Master New Driver**: La configuration `scl_speed_hz` par device permet des vitesses différentes sans affecter le bus master global.
- **Recovery**: `i2c_master_bus_reset()` n'existe plus dans la new API; on utilise le bit-bang SCL manuel suivi de STOP.

### Compatibilité LVGL/Touch
- Les correctifs ne modifient pas le chemin LCD/LVGL
- GT911 continue de fonctionner normalement (utilise le même mutex I2C)
- Le backoff I2C dynamique (`i2c_bus_shared_backoff_ticks()`) s'applique aussi au GT911
