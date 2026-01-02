# Correctif SD SDSPI - ESP_ERR_INVALID_RESPONSE sur IO Expander CH32V003

**Date**: 28/12/2025  
**Cible**: Waveshare ESP32-S3 Touch LCD 7B (1024×600)  
**ESP-IDF**: v6.1-dev, LVGL 9.x  

---

## 1. RÉSUMÉ ROOT-CAUSE

### Symptômes observés dans les logs:
```
io_ext: IO_Mode attempt X failed: ESP_ERR_INVALID_RESPONSE
io_ext: CS HIGH attempt ... failed: ESP_ERR_INVALID_RESPONSE
SD: CS line check failed. Card will not respond until fixed.
SD state -> MOUNT_FAIL, pipeline cmd0/cmd8/acmd41/cmd58 = 0
```

### Cause racine identifiée:

**Fichier**: `components/io_extension/io_extension.c`, lignes 180-184  
**Fichier**: `components/i2c/i2c_bus_shared.c`, lignes 207-214  

Le problème est une **fenêtre de backoff de 200ms** qui bloque les tentatives de recovery I2C.

1. Quand `IO_Mode(0xFF)` échoue avec `ESP_ERR_INVALID_RESPONSE`, le code tentait une recovery via `i2c_bus_shared_recover()`.

2. **MAIS** cette fonction vérifie si une recovery a eu lieu dans les 200ms précédentes:
   ```c
   if (s_last_recover_us != 0 && (now - s_last_recover_us) < 200000) {
     ESP_LOGW(TAG, "I2C recover skipped (backoff active)...");
     return ESP_ERR_INVALID_STATE;  // Recovery non exécutée!
   }
   ```

3. La fonction `i2c_bus_shared_reset_backoff()` appelée avant l'init SD ne réinitialisait **pas** `s_last_recover_us`, donc le backoff pouvait encore bloquer les retries.

4. Résultat: Les retries après échec ne font pas de vraie recovery → échecs enchaînés → MOUNT_FAIL.

---

## 2. CORRECTIFS APPLIQUÉS

### A. `components/i2c/i2c_bus_shared.c` - Reset complet du backoff

**Avant:**
```c
void i2c_bus_shared_reset_backoff(void) {
  portENTER_CRITICAL(&s_i2c_stats_spinlock);
  s_i2c_backoff_ticks = 0;
  s_i2c_error_streak = 0;
  s_i2c_success_after_error = 0;
  portEXIT_CRITICAL(&s_i2c_stats_spinlock);
  ESP_LOGI(TAG, "I2C backoff reset");
}
```

**Après:**
```c
void i2c_bus_shared_reset_backoff(void) {
  portENTER_CRITICAL(&s_i2c_stats_spinlock);
  s_i2c_backoff_ticks = 0;
  s_i2c_error_streak = 0;
  s_i2c_success_after_error = 0;
  portEXIT_CRITICAL(&s_i2c_stats_spinlock);
  // P0 FIX: Also reset recovery timestamp to allow immediate recovery
  // after reset_backoff(). This is critical for SD init which needs
  // to force recovery without hitting the 200ms backoff window.
  s_last_recover_us = 0;
  ESP_LOGI(TAG, "I2C backoff reset (including recover timestamp)");
}
```

### B. `components/io_extension/io_extension.c` - Recovery forcée dans IO_Mode

**Avant (ligne 182):**
```c
i2c_bus_shared_recover("IO_Mode_retry");
```

**Après:**
```c
// P0 FIX: Use forced recovery to bypass backoff window.
i2c_bus_shared_recover_force("IO_Mode_retry");
```

### C. `components/io_extension/io_extension.c` - Recovery forcée dans IO_EXTENSION_Init

**Avant (ligne 263):**
```c
i2c_bus_shared_recover("io_ext_probe");
```

**Après:**
```c
// P0 FIX: Use forced recovery to bypass backoff
i2c_bus_shared_recover_force("io_ext_probe");
vTaskDelay(pdMS_TO_TICKS(30)); // Extra settle after recovery
```

---

## 3. CHECKLIST DE VALIDATION

### Commandes à exécuter:
```powershell
cd c:\Users\woode\Desktop\ai\A.R.S
idf.py fullclean
idf.py build
idf.py -p COM3 flash monitor
```

### Comportement attendu (SUCCÈS):

1. **IO_Mode doit réussir**:
   ```
   io_ext: IO_Mode OK (attempt 1, mask=0xFF)
   ```

2. **CS probe doit réussir**:
   ```
   sd_extcs: CS probe: IOEXT4 toggle OK (I2C writes successful shadow=1/0/1 diag_match=1)
   ```

3. **Pipeline SD doit progresser**:
   ```
   sd_extcs: CMD0 -> IDLE (freq=100 kHz)
   sd_extcs: CMD8 -> OK (pattern=0x000001AA)
   sd_extcs: ACMD41 ready after X attempt(s)
   sd_extcs: CMD58 OK (attempt=1)
   ```

4. **Montage SD réussi**:
   ```
   sd: SD state -> INIT_OK
   sd: SD pipeline: pre_clks=20B cmd0=1 cmd8=1 acmd41=1 cmd58=1 ...
   ```

### Comportement de régression (ÉCHEC persistant):

Si vous voyez encore:
```
io_ext: IO_Mode attempt X failed: ESP_ERR_INVALID_RESPONSE
I2C recover [IO_Mode_retry] -> ESP_OK (forced=1)  // recovery exécutée mais échec
```

Cela indique un problème matériel possible:
- Vérifier les pull-ups I2C (SDA=GPIO8, SCL=GPIO9)
- Vérifier l'alimentation du CH32V003
- Vérifier l'adresse I2C (doit être 0x24 en 7-bit)

---

## 4. FICHIERS MODIFIÉS

| Fichier | Modification |
|---------|--------------|
| `components/i2c/i2c_bus_shared.c` | Reset de `s_last_recover_us` dans `reset_backoff()` |
| `components/io_extension/io_extension.c` | Utilisation de `recover_force()` dans IO_Mode et Init |

---

## 5. SOURCES ET RÉFÉRENCES

- **Waveshare ESP32-S3 7B Wiki**: Confirme SD CS via IO extension (EXIO4)
- **ESP-IDF I2C new driver**: `i2c_new_master_bus()` + reset via récupération manuelle SCL
- **Composant registry**: `waveshare/custom_io_expander_ch32v003` (non utilisé, notre driver custom est conservé)
- **Registres CH32V003 (implémentation Waveshare)**: 
  - 0x02 = Mode (dir), 0x03 = Output, 0x04 = Input
  - Adresse I2C: 0x24 (7-bit)

---

## 6. NOTE SUR LES ERREURS IDE

Les erreurs "Unknown argument: '-mdisable-hardware-atomics'" et similaires dans l'IDE sont des **faux positifs de clangd** qui n'a pas accès au toolchain Xtensa ESP-IDF. Ces erreurs n'affecteront pas la compilation réelle avec `idf.py build`.
