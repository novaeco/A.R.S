# Fix VSYNC, Touch Transform & SD CS — Documentation

## Résumé des modifications

Ce patch corrige 3 problèmes identifiés dans le dépôt A.R.S :

1. **VSYNC désactivé** → Tearing/flicker en mode DIRECT
2. **Double configuration tactile** → Incohérences de transformation
3. **Warning SD CS mismatch** → Readback CH32V003 non fiable

---

## Axe 1 : Synchronisation VSYNC

### Fichiers modifiés

| Fichier | Modification |
|---------|--------------|
| `components/lvgl_port/Kconfig` L63 | `default n` → `default y` pour `ARS_LCD_VSYNC_WAIT_ENABLE` |
| `components/lvgl_port/lvgl_port.c` L306-320 | Log info unique au boot : "VSYNC sync: ACTIVE" ou "DISABLED (raison)" |
| `components/rgb_lcd_port/rgb_lcd_port.c` L246-258 | Log amélioré pour le callback VSYNC |

### Comportement après patch

- Au boot : `lv_port: VSYNC sync: ACTIVE (timeout=20ms)`
- Ou si config désactivée : `lv_port: VSYNC sync: DISABLED (CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE=n)`
- Le callback `rgb_lcd_on_vsync_event` notifie le sémaphore après chaque VSYNC matériel
- `flush_callback()` attend le sémaphore avec timeout borné
- Si timeout répété → désactivation dynamique + log warning unique

### Pour désactiver

```bash
idf.py menuconfig
# → LVGL Port Configuration → Enable RGB VSYNC wait integration → n
```

---

## Axe 2 : Unification transformation tactile

### Fichiers modifiés

| Fichier | Modification |
|---------|--------------|
| `components/touch_transform/touch_transform_storage.c` L317-335 | `sync_touch_orient_flags()` ne fait plus de write NVS |
| `components/touch_orient/touch_orient.c` L197-220 | `touch_orient_apply()` ne fait plus de calibration (scale/offset) |

### Architecture après patch

```
NVS "touchcal" (slotA/B) = SOURCE UNIQUE de calibration
  ↓
touch_transform_set_active() → s_active_transform
  ↓
lvgl_port.c:touchpad_read() → touch_transform_apply_ex()
  ↓
LVGL reçoit les coordonnées finales

NVS "touch/orient" = LECTURE SEULE (migration legacy uniquement)
```

### Logs au boot

```
touch_orient: Applied orientation: Swap=0, MirX=0, MirY=0 (calibration via touch_transform)
touch_tf_store: Loaded transform from slotA gen=1 swap=0 mirX=0 mirY=0 a=[[1.000,0.000,0.0];[0.000,1.000,0.0]]
```

---

## Axe 3 : Durcissement SD CS

### Fichiers modifiés

| Fichier | Modification |
|---------|--------------|
| `components/sd/sd_host_extcs.c` L329-420 | Fonction `sd_extcs_set_cs_level()` simplifiée |

### Cause du warning original

Le CH32V003 (IO expander I2C @0x24) **ne supporte pas la lecture fiable** des registres de sortie.
L'ancien code tentait de vérifier la valeur écrite en la relisant, ce qui échouait systématiquement.

### Solution

- Suppression de la boucle de vérification `IO_EXTENSION_Output_With_Readback()`
- Utilisation de `IO_EXTENSION_Output()` simple
- Confiance au shadow state après write I2C réussi
- Délais de settling maintenus pour la stabilité électrique

### Logs au boot (après patch)

```
sd_extcs: CS->HIGH via IOEXT4 (shadow=1)   # Logs DEBUG uniquement
sd_extcs: CMD0 pre-sequence: CS high -> 20 dummy bytes -> CS low -> CMD0
sd_extcs: CMD0 try 1/12 @100 kHz ...
```

Le warning `CS->LOW verify mismatch` **n'apparaît plus**.

---

## Comment tester

### 1. Compilation

```bash
cd c:\Users\woode\Desktop\ai\A.R.S
idf.py build
```

### 2. Flash et Monitor

```bash
idf.py -p COM<X> flash monitor
```

### 3. Vérifications à effectuer

| Test | Résultat attendu |
|------|------------------|
| Log VSYNC au boot | `VSYNC sync: ACTIVE (timeout=20ms)` |
| Pas de tearing | Animation fluide sans artefact horizontal |
| Log touch au boot | `Applied orientation: ... (calibration via touch_transform)` |
| Un seul log touch config | Pas de logs contradictoires swap/mirror |
| Pas de warning CS mismatch | Aucun `CS->LOW verify mismatch` dans les logs |
| SD init OK | `SD card info:` affiché correctement |

---

## Limitations connues

1. **VSYNC fallback** : Si le timeout VSYNC se déclenche 3 fois consécutives, le système désactive la synchro pour éviter les blocages. Cela peut arriver si le refresh rate est trop bas ou si le driver RGB ne supporte pas les callbacks.

2. **Legacy touch_orient** : L'ancien namespace NVS `touch/orient` n'est pas effacé mais n'est plus utilisé pour la calibration. Seul `touchcal` fait foi.

3. **CH32V003 readback** : Le readback I2C reste disponible dans `IO_EXTENSION_Output_With_Readback()` pour d'autres usages, mais SD CS ne l'utilise plus.

---

## Fichiers non modifiés (contraintes respectées)

- ❌ Aucun changement GPIO/pinmap
- ❌ Pas de CH422G introduit
- ❌ Pas de dépendance externe ajoutée
- ❌ Pas de refactor hors des 3 sujets
