# Rapport de Correction SD IOEXT — 27 décembre 2025

## Symptôme

Lors de l'initialisation de la carte SD, le système échoue avec des erreurs I2C :

```
I (xxx) sd_extcs: Mounting SD (SDSPI ext-CS)…
E (xxx) io_ext: IO_Mode Failed: ESP_ERR_INVALID_RESPONSE
W (xxx) sd_host_extcs: CS HIGH attempt … failed: ESP_ERR_INVALID_RESPONSE
E (xxx) sd_host_extcs: SD: CS line check failed…
I (xxx) sd: SD state -> MOUNT_FAIL
```

L'IO Expander CH32V003 (addr 0x24) est correctement probé au démarrage, mais échoue lors de l'appel `IO_EXTENSION_IO_Mode(0xFF)` pendant l'init SD.

---

## Cause Racine

### Fichiers impliqués :
- `components/io_extension/io_extension.c` — fonction `IO_EXTENSION_IO_Mode`, `ioext_take_bus`
- `components/i2c/i2c_bus_shared.c` — backoff dynamique I2C
- `components/sd/sd_host_extcs.c` — appel à `IO_EXTENSION_IO_Mode` ligne 2025
- `components/touch/gt911.c` — accès I2C concurrent via `ars_i2c_lock`

### Séquence problématique identifiée :

1. **Au boot** : Le tactile GT911 (addr 0x5D) génère des erreurs I2C transitoires
2. **Accumulation de backoff** : `i2c_bus_shared_note_error()` augmente `s_i2c_backoff_ticks`
3. **Pendant l'init SD** : `ioext_take_bus()` (ligne 44-47 de io_extension.c) applique `vTaskDelay(backoff)` AVANT de prendre le lock I2C
4. **Fenêtre de collision** : Pendant ce délai, le GT911 (même pausé) peut avoir laissé le bus dans un état instable
5. **`ESP_ERR_INVALID_RESPONSE`** : L'IO Expander ne peut pas ACK car le bus I2C est "bloqué" (SDA ou SCL maintenus bas)

### Code problématique (io_extension.c ligne 44-47) :
```c
static bool ioext_take_bus(TickType_t wait_ticks, const char *ctx) {
  TickType_t backoff = i2c_bus_shared_backoff_ticks();  // ← Lit backoff global
  if (backoff > 0) {
    vTaskDelay(backoff);  // ← DÉLAI avant prise du lock = fenêtre de course
  }
  // ...
}
```

---

## Corrections Appliquées

### 1. Nouvelle fonction `i2c_bus_shared_reset_backoff()`

**Fichiers :** `components/i2c/i2c_bus_shared.c` et `.h`

Permet de réinitialiser les compteurs de backoff/erreurs avant une séquence critique.

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

### 2. Reset backoff + Recovery I2C avant init SD

**Fichier :** `components/sd/sd_host_extcs.c` (dans `sd_extcs_mount_card`)

```c
// FIX: Reset I2C backoff accumulated from other drivers (GT911)
i2c_bus_shared_reset_backoff();

// FIX: Recover I2C bus to clear any lingering stuck state
esp_err_t recover_ret = i2c_bus_shared_recover_force("sd_extcs_pre_init");
if (recover_ret != ESP_OK) {
  ESP_LOGW(TAG, "SD pre-init I2C recovery: %s (continuing anyway)",
           esp_err_to_name(recover_ret));
}
vTaskDelay(pdMS_TO_TICKS(20)); // Settle after recovery

IO_EXTENSION_IO_Mode(0xFF);
```

### 3. Retry avec recovery dans `IO_EXTENSION_IO_Mode`

**Fichier :** `components/io_extension/io_extension.c`

Refactorisation pour ajouter jusqu'à 3 tentatives avec récupération I2C entre chaque échec :

```c
void IO_EXTENSION_IO_Mode(uint8_t pin) {
  const int max_attempts = 3;
  // ...
  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    // ... tentative de transmission I2C ...
    if (ret == ESP_OK) {
      ioext_on_success();
      ESP_LOGI(TAG, "IO_Mode OK (attempt %d, mask=0x%02X)", attempt + 1, pin);
      return;
    }
    // Attempt I2C bus recovery before retry
    if (attempt < max_attempts - 1) {
      i2c_bus_shared_recover("IO_Mode_retry");
      vTaskDelay(pdMS_TO_TICKS(30));
    }
  }
}
```

---

## Fichiers Modifiés

| Fichier | Modification |
|---------|--------------|
| `components/i2c/i2c_bus_shared.c` | Ajout `i2c_bus_shared_reset_backoff()` |
| `components/i2c/i2c_bus_shared.h` | Déclaration `i2c_bus_shared_reset_backoff()` |
| `components/sd/sd_host_extcs.c` | Reset backoff + recovery avant `IO_Mode` |
| `components/io_extension/io_extension.c` | Retry mechanism dans `IO_EXTENSION_IO_Mode` |

---

## Procédure de Test

### Build
```bash
cd C:\Users\woode\Desktop\ai\A.R.S
idf.py fullclean
idf.py build
```

### Flash et Monitor
```bash
idf.py -p COM3 flash monitor
```

### Critères de Succès

**AVEC carte SD insérée :**
```
I (xxx) i2c_bus_shared: I2C backoff reset
I (xxx) io_ext: IO_Mode OK (attempt 1, mask=0xFF)
I (xxx) sd_extcs: SD ExtCS: IOEXT outputs configured (mask=0xFF, CS=IO4 push-pull)
I (xxx) sd: SD state -> INIT_OK
```

**SANS carte SD :**
```
I (xxx) i2c_bus_shared: I2C backoff reset
I (xxx) io_ext: IO_Mode OK (attempt 1, mask=0xFF)
I (xxx) sd: SD state -> ABSENT
```

**Dans les DEUX cas :**
- ❌ ZÉRO `IO_Mode Failed: ESP_ERR_INVALID_RESPONSE`
- ✅ Boot LCD/Touch/LVGL fonctionnel
- ✅ Pas d'accumulation d'erreurs I2C (streak reste à 0)

---

## Notes Techniques

### Pourquoi `ESP_ERR_INVALID_RESPONSE` ?

Cette erreur ESP-IDF signifie que le périphérique I2C n'a pas émis d'ACK. Causes possibles :
1. Bus I2C "bloqué" (SDA ou SCL maintenus bas par un device)
2. Transaction précédente non terminée correctement
3. Conflit d'adresse ou périphérique non alimenté

Le correctif résout les causes 1 et 2 en forçant une récupération du bus avant l'opération.

### Architecture I2C du projet

```
        ┌─────────────────┐
        │  i2c_bus_shared │  ← Mutex récursif + backoff dynamique
        │   (GPIO 8/9)    │
        └────────┬────────┘
                 │
     ┌───────────┴───────────┐
     │                       │
┌────┴─────┐          ┌──────┴─────┐
│  GT911   │          │  CH32V003  │
│ (0x5D)   │          │  (0x24)    │
│  Touch   │          │ IO Expander│
└──────────┘          └────────────┘
                            │
                      ┌─────┴─────┐
                      │ SD CS Pin │
                      │ (EXIO 4)  │
                      └───────────┘
```

---

*Rapport généré le 27 décembre 2025*
