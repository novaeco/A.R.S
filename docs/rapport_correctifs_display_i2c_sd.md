# Rapport de Correctifs: Display RGB, I²C et SD Card

**Date**: 2025-12-28  
**Cible**: Waveshare ESP32-S3-Touch-LCD-7B (1024×600)  
**ESP-IDF**: v6.1-dev  

---

## Résumé des Symptômes Initiaux

### 1. LCD RGB (P0)
- **Symptôme**: Aplat vert uniforme + bande verticale à gauche
- **Test pattern RGBWB**: Non visible correctement sur l'écran réel
- **Logs**: "RGB panel ready", "VSYNC callback OK", mais rendu incorrect

### 2. SD Card / IO Expander (P0)  
- **Symptôme**: `ESP_ERR_INVALID_RESPONSE` lors de `IO_Mode` et `CS HIGH`
- **État final**: `SD state -> MOUNT_FAIL`

---

## Diagnostic et Root Causes

### A. LCD RGB - Mapping GPIO Incorrect

**Fichier**: `components/board/include/board.h`  
**Lignes**: 54-69

**Preuve**:  
Le document `docs/hardware_waveshare_7b_pinmap.md` définit le mapping GPIO validé:
```
LCD Data0 (G3)  -> GPIO0
LCD Data1 (R3)  -> GPIO1
...
```

Mais le code `board.h` utilisait:
```c
#define BOARD_LCD_IO_DATA0 GPIO_NUM_14  // Incorrect!
#define BOARD_LCD_IO_DATA1 GPIO_NUM_38  // Incorrect!
...
```

**Impact**: Les signaux de données RGB étaient envoyés sur les mauvais pins, causant un affichage monochrome (vert) car seuls certains bits étaient correctement routés.

**Correctif appliqué**: Mapping GPIO corrigé selon `hardware_waveshare_7b_pinmap.md`.

### B. LCD RGB - Timings Incorrects

**Fichier**: `components/rgb_lcd_port/rgb_lcd_port.c`  
**Lignes**: 156-163

**Preuve**:  
Le document `docs/hw_waveshare_7b_pinout.md` spécifie:
```
Timings: HSYNC pulse 20, back porch 140, front porch 160; 
         VSYNC pulse 3, back porch 12, front porch 12.
```

Le code utilisait les timings ST7262 génériques:
```c
.hsync_pulse_width = 4,  // Devrait être 20
.hsync_back_porch = 8,   // Devrait être 140
.hsync_front_porch = 8,  // Devrait être 160
```

**Impact**: La zone active de l'écran était décalée, causant la bande verticale à gauche et un affichage tronqué.

**Correctif appliqué**: Timings mis à jour selon la documentation hardware.

### C. I²C - Recovery Forcé sur Bus Sain

**Fichier**: `main/main.c`  
**Lignes**: 173-181

**Preuve** (log boot):
```
I (1007) io_ext: IOEXT PROBE OK addr=0x24    <- Bus OK après init
...
I (2887) main: Forcing I2C bus recovery before SD init...
...
W (3207) io_ext: IO_Mode attempt 1 failed: ESP_ERR_INVALID_RESPONSE
```

Le bus I²C était sain (probe OK au boot), mais le recovery forcé envoyait des impulsions SCL + condition STOP qui perturbaient le CH32V003 IO expander.

**Impact**: Le CH32V003 ne répondait plus aux commandes `IO_Mode` après le recovery non nécessaire.

**Correctif appliqué**: Le recovery n'est maintenant déclenché que si `i2c_bus_shared_get_error_streak() > 0`. Sur un bus sain, seul `reset_backoff()` est appelé.

---

## Fichiers Modifiés

| Fichier | Modification |
|---------|--------------|
| `components/board/include/board.h` | Mapping GPIO LCD DATA0-DATA15 corrigé |
| `components/rgb_lcd_port/rgb_lcd_port.c` | Timings HSYNC/VSYNC corrigés |
| `main/main.c` | Recovery I²C conditionnel (streak > 0) |

---

## Commandes de Build/Flash/Monitor

```bash
# Clean build recommandé après ces changements
cd c:\Users\woode\Desktop\ai\A.R.S
idf.py fullclean
idf.py build
idf.py -p COM3 flash monitor
```

---

## Logs Attendus Après Correctifs

### LCD RGB
```
I (xxxx) rgb_lcd: RGB timing totals: h_total=1344 v_total=627 -> est_fps=26
I (xxxx) board: Test Pattern queued. You should see RGBWB bands.
```
L'écran devrait afficher 5 bandes verticales: Rouge, Vert, Bleu, Blanc, Noir.

### I²C / SD
```
I (xxxx) main: I2C bus healthy (no error streak), skipping forced recovery
I (xxxx) i2c_bus_shared: I2C backoff reset (including recover timestamp)
I (xxxx) sd: Initializing SD (ExtCS Mode)...
I (xxxx) io_ext: IO_Mode OK (attempt 1, mask=0xFF)
I (xxxx) sd: SD state -> INIT_OK
```

### Touch (GT911)
Le touch devrait continuer à fonctionner normalement car:
- Le mutex I²C partagé reste en place
- La pause touch pendant SD init est conservée

---

## Points de Vérification Manuels

1. **Test pattern visible**: Après boot, les 5 bandes RGBWB doivent être nettes
2. **Touch réactif**: Appuyer sur l'écran doit naviguer dans l'UI
3. **SD montée** (si carte présente): `SD state -> INIT_OK` dans les logs
4. **Pas d'erreurs I²C répétées**: Pas de `ESP_ERR_INVALID_RESPONSE` en boucle

---

## Contraintes Respectées

- ✅ Aucun framework "inventé" - uniquement ESP-IDF + composants existants
- ✅ Pas de modification du bus I²C partagé (SDA=GPIO8, SCL=GPIO9)
- ✅ CH32V003 @0x24 conservé (pas de CH422G introduit)
- ✅ GT911 @0x5D non impacté par les correctifs
- ✅ Patch minimal, ciblé sur les root causes identifiées

---

## Références

- **ESP-IDF LCD RGB**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- **Waveshare Wiki**: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7B
- **CH32V003 Component**: https://components.espressif.com/components/waveshare/custom_io_expander_ch32v003
- **Documentation locale**: `docs/hardware_waveshare_7b_pinmap.md`, `docs/hw_waveshare_7b_pinout.md`
