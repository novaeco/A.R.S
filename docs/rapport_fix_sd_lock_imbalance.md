# Correctif SD SDSPI - Déséquilibre de verrous I2C (Lock Imbalance Fix)

**Date**: 28/12/2025  
**Cible**: Waveshare ESP32-S3 Touch LCD 7B (1024×600)  
**ESP-IDF**: v6.1-dev, LVGL 9.x  

---

## 1. RÉSUMÉ ROOT-CAUSE

### Symptômes observés dans les logs:
```
io_ext: IO_Mode attempt X failed: ESP_ERR_INVALID_RESPONSE
sd_extcs: CS HIGH attempt ... failed: ESP_ERR_INVALID_RESPONSE
SD: CS line check failed. Card will not respond until fixed.
SD state -> MOUNT_FAIL, pipeline cmd0/cmd8/acmd41/cmd58 = 0
```

### Cause racine identifiée:

**Déséquilibre de verrous I2C (Lock Imbalance)**

**Fichier**: `components/sd/sd_host_extcs.c`, lignes 359-361  
**Fichier**: `components/io_extension/io_extension.c`, lignes 103-119  

Le problème était un **déséquilibre de verrous** dans la chaîne d'appel:

1. `sd_extcs_set_cs()` appelle `sd_extcs_lock_ioext_bus()` qui prend le verrou I2C:
   ```c
   i2c_bus_shared_lock(...)  // Lock #1 acquis
   s_cs_i2c_locked = true;
   ```

2. Ensuite, `io_extension_set_bits_locked()` → `io_extension_write_shadow_locked()` → `io_extension_write_shadow_unsafe()`:
   ```c
   ioext_take_bus(...)  // Lock #2 (recursif, OK)
   i2c_master_transmit(...);
   ars_i2c_unlock();  // ← PROBLÈME: libère le verrou!
   ```

3. **Conséquence**: À la fin de `io_extension_write_shadow_unsafe()`, le verrou est libéré par `ars_i2c_unlock()`, mais `sd_extcs_lock_ioext_bus()` pense toujours le détenir (`s_cs_i2c_locked = true`).

4. Les transactions I2C suivantes échouent car le bus n'est plus protégé, causant des conflits avec GT911 et des `ESP_ERR_INVALID_RESPONSE`.

---

## 2. CORRECTIFS APPLIQUÉS

### A. `components/io_extension/io_extension.c` - Ajout de fonctions _nolock

Nouvelles fonctions qui ne touchent PAS aux verrous I2C (pour les appelants qui détiennent déjà le bus):

```c
/**
 * @brief Write shadow register to IO expander (caller MUST hold I2C lock).
 * This function does NOT acquire/release the I2C bus lock.
 */
static esp_err_t io_extension_write_shadow_nolock(void);

/**
 * @brief Set bits in shadow register (caller MUST hold I2C lock).
 */
esp_err_t io_extension_set_bits_nolock(uint8_t mask);

/**
 * @brief Clear bits in shadow register (caller MUST hold I2C lock).
 */
esp_err_t io_extension_clear_bits_nolock(uint8_t mask);
```

### B. `components/io_extension/io_extension.h` - Déclarations ajoutées

```c
esp_err_t io_extension_set_bits_nolock(uint8_t mask);
esp_err_t io_extension_clear_bits_nolock(uint8_t mask);
```

### C. `components/sd/sd_host_extcs.c` - Utilisation des fonctions _nolock

**Avant** (lignes 360-361):
```c
ret = assert_low ? io_extension_clear_bits_locked(SD_EXTCS_IOEXT_CS_MASK)
                 : io_extension_set_bits_locked(SD_EXTCS_IOEXT_CS_MASK);
```

**Après**:
```c
// FIXED: Use _nolock variants because sd_extcs_lock_ioext_bus() already
// holds the I2C bus lock. Using _locked variants would cause lock imbalance.
ret = assert_low ? io_extension_clear_bits_nolock(SD_EXTCS_IOEXT_CS_MASK)
                 : io_extension_set_bits_nolock(SD_EXTCS_IOEXT_CS_MASK);
```

### D. `components/io_extension/Kconfig` - Self-test optionnel

Ajout d'une option `CONFIG_ARS_IOEXT_SELFTEST_SDCS` pour diagnostiquer le pin CS au boot.

---

## 3. FICHIERS MODIFIÉS

| Fichier | Modification |
|---------|--------------|
| `components/io_extension/io_extension.c` | Ajout de `io_extension_write_shadow_nolock()`, `io_extension_set_bits_nolock()`, `io_extension_clear_bits_nolock()`, self-test optionnel |
| `components/io_extension/io_extension.h` | Déclarations des nouvelles fonctions _nolock |
| `components/sd/sd_host_extcs.c` | Utilisation de `_nolock` au lieu de `_locked` dans `sd_extcs_set_cs()` |
| `components/io_extension/Kconfig` | Ajout option `CONFIG_ARS_IOEXT_SELFTEST_SDCS` |

---

## 4. COMMANDES DE TEST

```powershell
# Ouvrir ESP-IDF PowerShell ou exécuter d'abord:
# . $env:IDF_TOOLS_PATH\export.ps1

cd c:\Users\woode\Desktop\ai\A.R.S
idf.py fullclean
idf.py build
idf.py -p COM3 flash monitor
```

**Pour activer le self-test SD CS (optionnel):**
```powershell
idf.py menuconfig
# Naviguer vers: Component config -> IO Extension (CH32V003) -> Self-test SD CS pin
```

---

## 5. LOGS ATTENDUS APRÈS CORRECTION

### Cas 1: Boot avec SD présente (SUCCÈS)

```
io_ext: IOEXT PROBE OK addr=0x24
io_ext: IO_Mode OK (attempt 1, mask=0xFF)
sd_extcs: CS probe: IOEXT4 toggle OK (I2C writes successful shadow=1/0/1 diag_match=1)
sd_extcs: CMD0 pre-sequence: CS high -> 20 dummy bytes -> CS low -> CMD0
sd_extcs: CMD0 -> IDLE (freq=100 kHz)
sd_extcs: CMD8 -> OK (pattern=0x000001AA)
sd_extcs: ACMD41 ready after X attempt(s)
sd_extcs: CMD58 OK (attempt=1)
sd: SD state -> INIT_OK
sd: SD pipeline: pre_clks=20B cmd0=1 cmd8=1 acmd41=1 cmd58=1 init=100 kHz target=20000 kHz final=IDLE_READY
```

### Cas 2: Boot sans SD (FAIL propre)

```
io_ext: IOEXT PROBE OK addr=0x24
io_ext: IO_Mode OK (attempt 1, mask=0xFF)
sd_extcs: CS probe: IOEXT4 toggle OK (I2C writes successful shadow=1/0/1 diag_match=1)
sd_extcs: CMD0 failed: state=ABSENT class=ABSENT err=ESP_ERR_TIMEOUT (saw_non_ff=0 ...)
sd: SD state -> ABSENT
sd: SD init: NO_CARD detected (ext-CS path healthy)
```

**Note**: Pas de rafale `ESP_ERR_INVALID_RESPONSE` - les échecs sont propres et le GT911/LVGL continuent de fonctionner.

---

## 6. VÉRIFICATION NON-RÉGRESSION

- **Display RGB**: Inchangé (ne dépend pas de io_extension pour le rendu)
- **Touch GT911**: Inchangé (utilise son propre handle I2C sur le bus partagé)
- **Backlight/VCOM/LCD Reset**: Inchangé (utilise `IO_EXTENSION_Output()` qui n'est pas affecté)
- **LVGL**: Inchangé

---

## 7. SOURCES ET RÉFÉRENCES

- **ESP-IDF I2C Master Driver**: Les mutex récursifs permettent une ré-entrée mais chaque `take` doit avoir un `give` correspondant
- **Waveshare CH32V003**: Adresse I2C 0x24, registres Mode=0x02, Output=0x03
- **Code source**: Analyse de `sd_host_extcs.c` lignes 270-420, `io_extension.c` lignes 103-133

---

## 8. NOTE SUR LES ERREURS IDE

Les erreurs "Unknown argument: '-mdisable-hardware-atomics'" et similaires dans l'IDE sont des **faux positifs de clangd** qui n'a pas accès au toolchain Xtensa ESP-IDF. Ces erreurs n'affectent pas la compilation avec `idf.py build`.
