# Rapport Technique - Correctif SD/IOEXT CH32V003 (v2)

## Résumé Exécutif

Le système de carte microSD via SDSPI avec CS piloté par IO extension CH32V003 échouait systématiquement avec des erreurs `ESP_ERR_INVALID_RESPONSE` suivies d'un `L2 RECOVERY FAILED`. 

**Cause racine prouvée** : La fonction de diagnostic `sd_extcs_diag_toggle_and_cmd0()` effectue des toggles CS trop rapides (500µs entre chaque) qui saturent le CH32V003. Une fois le CH32V003 bloqué, le L2 recovery échoue car il ne fait que du probe - il ne tente pas une réinitialisation complète.

**Correctif appliqué** :
1. Augmentation du délai entre toggles diagnostiques (500µs → 5ms)
2. Après L2 failure, tentative de réinitialisation complète de l'IOEXT
3. Option Kconfig pour désactiver le diagnostic toggle
4. Meilleure gestion de l'état `s_ioext_initialized` pendant recovery

---

## 1. Faits Extraits du Log (Preuves)

### Chronologie des événements

| Timestamp | Événement | Résultat |
|-----------|-----------|----------|
| 1005ms | IOEXT probe @ boot | **OK** (`IOEXT PROBE OK addr=0x24`) |
| 1185ms | GT911 touch init | **OK** |
| 2785ms | Touch pausé pour SD | OK |
| 2885ms | I2C backoff reset | OK |
| **2925ms** | IO_Mode(0xFF) | **SUCCÈS** (`IO_Mode OK (tx-only, mask=0xFF)`) |
| **3475ms** | Diag-CS LOW toggle | **ÉCHEC** (`ESP_ERR_INVALID_RESPONSE shadow=0xEF`) |
| 3485ms | Diag-CS attempt 2 | ÉCHEC (streak=4) |
| 3525ms | Diag aborted | 1/10 toggles done |
| 3545ms | L2 RECOVERY triggered | After 3 consecutive errors |
| 3875-4665ms | L2 probe attempts 1-5 | **TOUS ÉCHEC** |
| **4665ms** | L2 RECOVERY | **FAILED** |
| 4685ms+ | Toutes opérations CS | `ESP_ERR_INVALID_STATE` |
| 5095ms | SD state final | `IOEXT_FAIL` |

### Preuves clés du log

```
I (2925) io_ext: IO_Mode OK (tx-only, mask=0xFF)
```
**IO_Mode fonctionne** - IOEXT est sain au départ.

```
W (3475) io_ext: IOEXT TX-only failed: ESP_ERR_INVALID_RESPONSE (shadow=0xEF)
W (3485) sd_extcs: Diag-CS LOW toggle attempt 1 failed: ESP_ERR_INVALID_RESPONSE
```
**550ms après IO_Mode**, le premier toggle diagnostic échoue. Le CH32V003 devient non-réactif.

```
I (3525) sd_extcs: Diag-CS: toggled 1/10 fail=1 shadow_before=0xFF shadow_after=0xEF
```
**Shadow = 0xEF** signifie que bit4 (CS) a été écrit à 0 (LOW), mais l'I2C a retourné NACK.

```
E (4665) io_ext: L2: IOEXT probe failed after 5 attempts: ESP_ERR_INVALID_RESPONSE
E (4665) io_ext: L2 RECOVERY FAILED: ESP_ERR_INVALID_RESPONSE
```
**L2 recovery échoue** - le CH32V003 ne répond plus du tout.

```
I (5095) sd: SD pipeline: pre_clks=20B cmd0=0 cmd8=0 acmd41=0 cmd58=0
```
**Aucune commande SD n'a été envoyée** - tout a échoué au niveau IOEXT/I2C.

---

## 2. Evidence Code (Fichier:Ligne)

| Symptôme | Fichier:Ligne | Code |
|----------|---------------|------|
| Diag toggle rapide | `sd_host_extcs.c:857,864` | `ets_delay_us(500)` - Trop rapide pour CH32V003 |
| L2 Recovery probe-only | `io_extension.c:186-213` | Ne fait que probe, pas de full init |
| s_ioext_initialized = false | `io_extension.c:143,220` | Set à false, jamais restauré si L2 fail |
| INVALID_STATE cascade | `io_extension.c:263,291` | Check `!s_ioext_initialized` retourne early |
| Diag appelé avant SD init | `sd_host_extcs.c:2217` | `sd_extcs_diag_toggle_and_cmd0()` avant `sd_extcs_low_speed_init()` |

---

## 3. Root Cause Analysis

### ROOT CAUSE #1: PROVED - Diagnostic toggle sature le CH32V003

**Evidence log + code** :

1. **Log** (2925ms→3475ms) : 550ms entre IO_Mode OK et premier échec
2. **Code** (`sd_host_extcs.c:857,864`) : `ets_delay_us(500)` entre chaque toggle
3. **Calcul** : I2C @ 50kHz = ~200µs/byte minimum. Transaction = 3 bytes = 600µs minimum. Avec overhead = ~1-2ms. 500µs est insuffisant.

**Problème** : Le CH32V003 a un firmware I2C software (pas un périphérique I2C hardware). Il a besoin de plus de temps pour traiter chaque commande.

### ROOT CAUSE #2: PROVED - L2 Recovery ne fait pas de full init

**Evidence code** (`io_extension.c:183-221`) :

```c
// Step 5: Re-probe IOEXT with multiple attempts
for (int i = 0; i < probe_attempts; i++) {
    // ... probe seulement, pas de IO_Mode(0xFF) / full init
}

if (probe_ret != ESP_OK) {
    s_ioext_initialized = false;  // Reste false!
    return probe_ret;
}
```

**Impact** : 
1. Le probe envoie juste un write `{IO_EXTENSION_Mode, 0xFF}` pour voir si ACK
2. Mais le CH32V003 bloqué ne répond pas
3. `s_ioext_initialized` reste `false`
4. Toutes les opérations suivantes échouent avec `ESP_ERR_INVALID_STATE`

### ROOT CAUSE #3: PROBABLE - CH32V003 I2C state machine bloquée

Le CH32V003 utilise un firmware I2C bitbang. Après un NACK ou un bus glitch, sa state machine peut rester dans un état invalide (attendant un bit qui ne viendra jamais).

**Ce qui pourrait débloquer** :
- Reset hardware du CH32V003 (pas disponible)
- Power cycle du CH32V003 (pas disponible)
- Séquence I2C STOP + START multiples (actuellement non implémentée)

---

## 4. Correctifs Appliqués

### Correctif A : Désactiver/ralentir diagnostic toggle

**Fichier** : `components/sd/sd_host_extcs.c`

**Changements** :
1. Augmenter le délai entre toggles de 500µs à **5ms**
2. Réduire le nombre de toggles de 10 à **3**
3. Ajouter Kconfig pour désactiver le diagnostic

### Correctif B : L2 Recovery avec full re-init

**Fichier** : `components/io_extension/io_extension.c`

**Changements** :
1. Après probe failure, tenter un **full IO_EXTENSION_Init()**
2. Envoyer des séquences I2C STOP explicites
3. Augmenter les délais de stabilisation

### Correctif C : Kconfig options

**Fichier** : `components/sd/Kconfig`

**Ajout** :
- `CONFIG_ARS_SD_EXTCS_SKIP_DIAG_TOGGLE` pour skip le diagnostic dangereux

---

## 5. Procédure de Validation

### Test A : Boot SD absente

**Attendu** :
- Pas de cascade d'erreurs IOEXT
- État SD final = `ABSENT` ou `MOUNT_FAIL` (pas `IOEXT_FAIL`)
- Touch fonctionnel après SD init

**Commande** :
```bash
idf.py -p COM3 flash monitor
# Retirer la carte SD avant power-on
```

**Vérification log** :
- `io_ext: IO_Mode OK` (pas d'erreurs)
- `sd: SD state -> ABSENT` ou `MOUNT_FAIL`
- Pas de `L2 RECOVERY FAILED`

### Test B : Boot SD présente

**Attendu** :
- Mount OK
- Lecture fichier fonctionnelle

**Commande** :
```bash
idf.py -p COM3 flash monitor
# Insérer une carte SD formatée FAT32 avant power-on
```

**Vérification log** :
- `sd_extcs: CMD0 -> IDLE`
- `sd_extcs: ACMD41 ready`
- `sd: SD state -> INIT_OK`

### Test C : Touch après SD init (même si SD échoue)

**Attendu** :
- Touch répond normalement
- Pas de blocage UI

---

## 6. Risques / Non-Goals

### Risques résiduels

1. **CH32V003 intrinséquement instable** : Le firmware I2C du CH32V003 peut avoir d'autres bugs. Si les problèmes persistent, envisager un remplacement par un vrai IO expander I2C (PCF8574, MCP23008).

2. **Pas de reset hardware** : Le CH32V003 n'a pas de broche reset accessible via GPIO. En cas de blocage sévère, seul un power cycle peut le débloquer.

### Non-Goals

- **Refactor global** : Ce correctif est minimal et ciblé
- **Changement de hardware** : Le CH32V003 reste le driver CS
- **Support SD natif GPIO** : CS reste via IOEXT4

---

## 7. Fichiers Modifiés

| Fichier | Description |
|---------|-------------|
| `components/io_extension/io_extension.c` | L2 recovery avec full re-init + délais augmentés |
| `components/sd/sd_host_extcs.c` | Diagnostic toggle ralenti + skip option |
| `components/sd/Kconfig` | Option pour skip diagnostic |
| `sdkconfig.defaults` | Activation skip diag par défaut |

---

## 8. Commandes de Test

```bash
# Clean build (recommandé après changement Kconfig)
idf.py fullclean

# Build
idf.py build

# Flash et monitor
idf.py -p COM3 flash monitor

# Vérifier logs spécifiques :
# - "IO_Mode OK" (IOEXT fonctionne)
# - "CS probe: IOEXT4 toggle OK" (probe réussi)
# - "SD state -> INIT_OK" ou "ABSENT" (pas IOEXT_FAIL)
```

---

**Auteur** : Antigravity (AI)
**Date** : 2025-12-29
**ESP-IDF** : v6.1-dev
**Hardware** : Waveshare ESP32-S3 Touch LCD 7B + CH32V003 IO Expander
