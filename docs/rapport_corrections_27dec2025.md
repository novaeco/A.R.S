# Rapport de Corrections A.R.S — 27 Décembre 2025

## Résumé des Modifications

Ce document détaille les corrections appliquées au projet A.R.S basées sur :
- La documentation officielle Waveshare (https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7B)
- Les spécifications ESP-IDF pour ESP32-S3
- L'analyse des logs de démarrage

---

## 1. Configuration CPU — 240 MHz

**Fichier:** `sdkconfig.defaults`

**Problème:** Le CPU était configuré à 160 MHz alors que la carte Waveshare ESP32-S3-Touch-LCD-7B supporte jusqu'à **240 MHz** selon les spécifications officielles.

**Correction:**
```ini
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240
```

**Source:** https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7B
> "Equipped with high-performance Xtensa 32-bit LX7 dual-core processor, up to 240MHz main frequency"

---

## 2. Stabilisation I2C pour Carte SD

**Fichiers modifiés:**
- `main/main.c`
- `components/sd/sd_host_extcs.c`
- `sdkconfig.defaults`

**Problème:** Erreurs `ESP_ERR_INVALID_RESPONSE` lors de la communication I2C avec l'IO Expander CH32V003 pendant l'initialisation de la carte SD. Le bus I2C est partagé entre le GT911 (tactile) et le CH32V003, causant des conflits.

### 2.1 Reset I2C Forcé Avant Init SD

**Fichier:** `main/main.c`

Ajout d'un reset forcé du bus I2C avant l'initialisation SD :
```c
// Force I2C bus recovery to clear any pending transactions
ESP_LOGI(TAG, "Forcing I2C bus recovery before SD init...");
esp_err_t i2c_recover_ret = i2c_bus_shared_recover_force("pre_sd_init");
if (i2c_recover_ret != ESP_OK) {
  ESP_LOGW(TAG, "I2C recovery before SD init: %s", esp_err_to_name(i2c_recover_ret));
}
// Additional stabilization delay for IO Expander
vTaskDelay(pdMS_TO_TICKS(100));
```

### 2.2 Délai de Stabilisation Augmenté

**Fichier:** `components/sd/sd_host_extcs.c` (ligne 2047)

Augmentation du délai après configuration IO Expander de 10ms à 50ms :
```c
// FIX: Longer delay for I2C bus stabilization after IO Expander config
vTaskDelay(pdMS_TO_TICKS(50));
```

### 2.3 Pause Tactile Renforcée

**Fichier:** `main/main.c`

Ajout d'un délai après la pause du tactile pour permettre la libération complète du bus I2C :
```c
#if CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT
  touch_pause_for_sd_init(true);
  // Allow touch task to fully pause and release I2C bus
  vTaskDelay(pdMS_TO_TICKS(50));
#endif
```

---

## 3. Configuration sdkconfig.defaults Complète

Le fichier a été réécrit avec :

| Paramètre | Ancienne Valeur | Nouvelle Valeur | Justification |
|-----------|-----------------|-----------------|---------------|
| CPU Freq | 160 MHz | **240 MHz** | Spec Waveshare |
| SD Init Delay | Non défini | **100 ms** | Stabilité I2C |
| SD CS Retry | Non défini | **5** | Plus de tentatives |

Structure finale :
```ini
# CPU at 240 MHz (Waveshare spec)
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240

# PSRAM 8MB (N16R8 module)
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y

# SD avec délais renforcés
CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT=y
CONFIG_ARS_SD_EXTCS_INIT_DELAY_MS=100
CONFIG_ARS_SD_EXTCS_CS_RETRY_COUNT=5
```

---

## 4. Prochaines Étapes

### Pour Tester les Corrections

```bash
# Nettoyer et reconstruire
idf.py fullclean
idf.py build

# Flasher et monitorer
idf.py -p COM3 flash monitor
```

### Indicateurs de Succès Attendus

1. **CPU à 240 MHz:**
   ```
   I (xxx) cpu_start: cpu freq: 240000000 Hz
   ```

2. **I2C Recovery avant SD:**
   ```
   I (xxx) main: Forcing I2C bus recovery before SD init...
   ```

3. **SD Init (si carte présente):**
   ```
   I (xxx) sd: SD state -> INIT_OK
   ```
   ou (si carte absente)
   ```
   I (xxx) sd: SD state -> ABSENT
   ```

---

## 5. Notes Techniques

### Configuration Matérielle Waveshare ESP32-S3-Touch-LCD-7B

| Composant | Spécification |
|-----------|---------------|
| Module | ESP32-S3-WROOM-1-N16R8 |
| CPU | Xtensa LX7 Dual-Core @ 240 MHz |
| Flash | 16 MB |
| PSRAM | 8 MB Octal |
| LCD | ST7262 RGB 1024×600 |
| Touch | GT911 I2C @ 0x5D |
| IO Expander | CH32V003 I2C @ 0x24 |

### Bus I2C Partagé

- **SDA:** GPIO8
- **SCL:** GPIO9
- **Périphériques:**
  - GT911 Touch @ 0x5D
  - CH32V003 IO Expander @ 0x24

Le bus I2C partagé nécessite une synchronisation stricte via mutex (`i2c_bus_shared_lock`) et des délais de stabilisation entre les accès aux différents périphériques.

---

*Rapport généré le 27 décembre 2025*

---

## 6. Récapitulatif des Fichiers Modifiés

| Fichier | Action | Description |
|---------|--------|-------------|
| `sdkconfig.defaults` | ✅ **MODIFIÉ** | CPU 240 MHz, PSRAM config complète, délais SD renforcés |
| `main/main.c` | ✅ **MODIFIÉ** | Reset I2C forcé avant SD init, délais de stabilisation |
| `main/CMakeLists.txt` | ✅ **MODIFIÉ** | Ajout dépendances `i2c` et `io_extension` |
| `components/sd/sd_host_extcs.c` | ✅ **MODIFIÉ** | Délai 10ms → 50ms pour IO Expander |
| `docs/rapport_corrections_27dec2025.md` | ✅ **CRÉÉ** | Documentation des corrections |

## 7. Fichiers Vérifiés (Sans Modification Requise)

| Fichier | Statut | Notes |
|---------|--------|-------|
| `components/board/src/board.c` | ✅ OK | Init SD séparée, LCD task sur CPU0 |
| `components/board/include/board.h` | ✅ OK | GPIO et résolution corrects |
| `components/rgb_lcd_port/rgb_lcd_port.c` | ✅ OK | Double buffering PSRAM, VSYNC |
| `components/rgb_lcd_port/rgb_lcd_port.h` | ✅ OK | Defines alignés avec board.h |
| `components/touch/gt911.c` | ✅ OK | Récupération I2C, mode dégradé |
| `components/lvgl_port/lvgl_port.c` | ✅ OK | Direct mode, sync VSYNC |
| `components/io_extension/io_extension.c` | ✅ OK | Mutex, backoff, shadow state |
| `components/i2c/i2c_bus_shared.c` | ✅ OK | Mutex récursif, recovery I2C |
| `components/sd/sd.c` | ✅ OK | Pause tactile, validation lecture |
| `components/net/src/net_manager.c` | ✅ OK | WiFi retry avec backoff |
| `components/sd/Kconfig` | ✅ OK | Options ExtCS correctes |
| `components/touch/Kconfig` | ✅ OK | 1024x600, mirror X/Y |
| `components/io_extension/Kconfig` | ✅ OK | 100 kHz I2C |
| `CMakeLists.txt` | ✅ OK | ESP32-S3 target forcé |

## 8. Conformité avec la Documentation Waveshare

| Spécification | Valeur Officielle | Valeur Projet | Statut |
|---------------|-------------------|---------------|--------|
| CPU Max | 240 MHz | **240 MHz** | ✅ |
| Flash | 16 MB | 16 MB | ✅ |
| PSRAM | 8 MB Octal | 8 MB Octal 80 MHz | ✅ |
| LCD | 1024×600 RGB | 1024×600 RGB565 | ✅ |
| Touch | GT911 I2C 0x5D | GT911 0x5D | ✅ |
| IO Expander | CH32V003 0x24 | CH32V003 0x24 | ✅ |
| SD Interface | SDSPI | SDSPI + ExtCS | ✅ |
| I2C Bus | GPIO8/9 | GPIO8/9 | ✅ |

## 9. Actions Recommandées Post-Correction

1. **Nettoyer et reconstruire**
   ```bash
   idf.py fullclean && idf.py build
   ```

2. **Flasher et vérifier les logs**
   ```bash
   idf.py -p COM3 flash monitor
   ```

3. **Vérifier les indicateurs de succès** :
   - `cpu freq: 240000000 Hz`
   - `Forcing I2C bus recovery before SD init...`
   - `SD state -> INIT_OK` ou `ABSENT`

4. **Configurer le WiFi** via l'interface UI si nécessaire

