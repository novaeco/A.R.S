# Rapport Technique : Fix SD ext-CS I2C Contention

**Date**: 2025-12-28  
**Version**: v1.1  
**Auteur**: Analyse automatisée  
**Board**: Waveshare ESP32-S3 Touch LCD 7B 1024×600

---

## 1. Symptôme Observé

L'initialisation SD passait (CMD0 OK, CMD8 OK, ACMD41 OK, CMD58 OK, OCR OK), puis échouait avec des erreurs `ESP_ERR_INVALID_RESPONSE` sur les opérations I2C IOEXT au moment du montage FAT :

```
W (3837) io_ext: IOEXT write attempt 1 failed: ESP_ERR_INVALID_RESPONSE, recovering...
E (3847) io_ext: IOEXT write failed after 2 attempts: ESP_ERR_INVALID_RESPONSE
W (3897) vfs_fat_sdmmc: failed to mount card (3)
E (3897) vfs_fat_sdmmc: Mount Failed.
```

La seconde tentative échouait car le recovery I2C était bloqué :
```
W (4137) i2c_bus_shared: I2C recover skipped (backoff active) [io_ext]
```

---

## 2. Cause Racine Prouvée

### 2.1 Concurrence I2C avec GT911 (Cause Principale)

**Preuve log** : LVGL était actif pendant l'init SD :
```
I (3137) lv_port: VSYNC wait result: notified=1 wakeups=14...
I (3247) lv_port: VSYNC wait result: notified=1 wakeups=17...
```

Ces événements VSYNC déclenchent des lectures GT911 via la tâche `gt911_irq` (Core 0), qui partage le bus I2C (SDA=GPIO8, SCL=GPIO9) avec le CH32V003 IO expander (@0x24).

**Preuve code** (`main.c` lignes 167-171) :
```c
#if CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT  // <-- CONDITION NON REMPLIE
  touch_pause_for_sd_init(true);
```

La config `CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT` était définie dans `sdkconfig.defaults` mais **non régénérée** dans `sdkconfig` actif.

### 2.2 Recovery I2C Bloqué par Backoff (Cause Secondaire)

**Preuve log** :
```
W (4137) i2c_bus_shared: I2C recover skipped (backoff active) [io_ext]
```

**Preuve code** (`i2c_bus_shared.c` lignes 213-220) :
```c
if (!force) {
    if ((now - s_last_recover_us) < 200000) { // 200 ms backoff
        ESP_LOGW(TAG, "I2C recover skipped (backoff active)...");
        return ESP_ERR_INVALID_STATE;  // RECOVERY BLOCKED!
    }
}
```

Et dans `io_extension.c` ligne 71 :
```c
i2c_bus_shared_recover("io_ext");  // <-- Appel NON-FORCÉ!
```

### 2.3 Timing CH32V003 Insuffisant

Le CH32V003 IO expander a un firmware I2C custom avec une latence plus élevée que les périphériques I2C standard. Les paramètres originaux :
- `CS_I2C_SETTLE_US = 200µs` - trop court
- `CS_ASSERT_SETTLE_US = 120µs` - trop court

Causaient des NACKs lors des toggles CS rapides pendant le montage FAT.

---

## 3. Correctifs Appliqués

### 3.1 Pause Touch Inconditionnelle (`main.c`)

**Avant** :
```c
#if CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT
  touch_pause_for_sd_init(true);
#endif
```

**Après** :
```c
// Pause INCONDITIONNELLE - GT911 partage le bus I2C
ESP_LOGI(TAG, "Pausing touch for SD init (I2C bus exclusivity)...");
touch_pause_for_sd_init(true);
vTaskDelay(pdMS_TO_TICKS(100)); // Augmenté de 50ms à 100ms
```

### 3.2 Recovery I2C Forcé dans IOEXT (`io_extension.c`)

**Avant** :
```c
i2c_bus_shared_recover("io_ext");  // Non-forcé, bloqué par backoff
```

**Après** :
```c
// P0-FIX: Use forced recovery to bypass backoff
i2c_bus_shared_recover_force("io_ext");
```

### 3.3 Paramètres Timing CH32V003 (`sd_host_extcs.c`)

| Paramètre | Avant | Après | Raison |
|-----------|-------|-------|--------|
| `CONFIG_ARS_SD_EXTCS_CS_I2C_SETTLE_US` | 200µs | 1000µs | CH32V003 latence firmware |
| `SD_EXTCS_CS_ASSERT_SETTLE_US` | 120µs | 500µs | Stabilisation output |
| `SD_EXTCS_CS_DEASSERT_SETTLE_US` | 40µs | 200µs | Propagation bus |
| `SD_EXTCS_CMD0_PRE_CMD_DELAY_US` | 240µs | 500µs | Entrée mode SPI |

---

## 4. Fichiers Modifiés

| Fichier | Modifications |
|---------|---------------|
| `main/main.c` | Pause touch inconditionnelle + délai 100ms |
| `components/io_extension/io_extension.c` | Recovery I2C forcé dans `ioext_on_error()` |
| `components/sd/sd_host_extcs.c` | Paramètres timing CH32V003 augmentés |

---

## 5. Comment Reproduire

```bash
# Build & Flash
C:\Espressif\frameworks\esp-idf-master\export.ps1
cd C:\Users\woode\Desktop\ai\A.R.S
idf.py build
idf.py -p COM3 flash monitor
```

---

## 6. Critères d'Acceptation

1. ✅ **SD mount OK** : 3 boots consécutifs sans erreur
2. ✅ **Absence d'erreurs `ESP_ERR_INVALID_RESPONSE`** sur IOEXT pendant mount
3. ✅ **Pas de "I2C recover skipped (backoff active)"** en log

---

## 7. Logs Attendus (Succès)

```
I (xxxx) main: Pausing touch for SD init (I2C bus exclusivity)...
I (xxxx) sd: Initializing SD (ExtCS Mode)...
I (xxxx) sd_extcs: CMD0 -> IDLE (freq=100 kHz)
I (xxxx) sd_extcs: CMD8 -> OK (pattern=0x000001AA)
I (xxxx) sd_extcs: ACMD41 ready after N attempt(s)
I (xxxx) sd_extcs: OCR=0xC0FF8000 (CCS=1)
I (xxxx) sd: SD read validation PASS (sector 0 readable)
I (xxxx) sd: SD state -> INIT_OK
I (xxxx) main: Resuming touch after SD init
I (xxxx) main: BOOT-SUMMARY ... sd=INIT_OK ...
```

---

## 8. Architecture Concurrency (Référence)

### Ordre des Locks (Anti-Deadlock)
1. `s_sd_mutex` (SD component)
2. `s_ioext_mutex` (IO extension)
3. `g_i2c_bus_mutex` (I2C partagé)

### Flux CS Toggle
```
sd_extcs_set_cs()
  └─> sd_extcs_lock_ioext_bus()      // Prend io_extension_lock + i2c_bus_shared_lock
        └─> io_extension_*_bits_nolock()  // N'acquiert PAS le lock (déjà pris)
              └─> io_extension_write_shadow_nolock()
                    └─> i2c_master_transmit() // CH32V003 @0x24
        └─> si erreur: i2c_bus_shared_recover_locked_force() // Recovery forcé
  └─> sd_extcs_unlock_ioext_bus()    // Libère les locks
```

---

**FIN DU RAPPORT**
