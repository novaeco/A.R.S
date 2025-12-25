# 🔧 Rapport Correctifs Task Watchdog - GT911 IRQ Task & LVGL Port

## Date: 24 décembre 2024

## Problème Initial
**Symptôme**: `task_wdt: Tasks currently running` - Le Task Watchdog se déclenche, indiquant que des tâches bloquantes (GT911 IRQ task, LVGL port) affament les tâches IDLE sur CPU0 ou CPU1.

## Causes Racines Identifiées

### 1. Priorité LVGL excessive (CPU1)
- **Problème**: La tâche LVGL était configurée avec une priorité de 10 sur CPU1
- **Impact**: Affamait la tâche IDLE1, déclenchant le WDT
- **Solution**: Réduire la priorité à 5

### 2. Rate limiting insuffisant dans GT911 IRQ Task
- **Problème**: Le rate limiting était de 5ms, insuffisant pour garantir que IDLE s'exécute
- **Impact**: Boucle serrée possible sous charge tactile intense
- **Solution**: Augmenter à 15ms avec yield garanti

### 3. Timeouts I2C trop courts
- **Problème**: Timeout I2C de 50ms, trop court si l'IO Expander occupe le bus
- **Impact**: Timeouts et erreurs I2C fréquents
- **Solution**: Augmenter à 100ms

### 4. Yields insuffisants dans la boucle LVGL
- **Problème**: `vTaskDelay(5ms)` insuffisant avec priorité élevée
- **Impact**: IDLE1 n'avait pas assez de temps CPU
- **Solution**: Augmenter à 10ms avec commentaire explicatif

---

## Fichiers Modifiés

### 1. `components/touch/gt911.c`

#### Modification 1: Timeout I2C Read (ligne ~1632)
```c
// AVANT
if (!i2c_bus_shared_lock(pdMS_TO_TICKS(50))) {

// APRÈS
// Finite timeout: 100ms to allow for IO Expander contention
if (!i2c_bus_shared_lock(pdMS_TO_TICKS(100))) {
```

#### Modification 2: Timeout I2C Write (ligne ~1659)
```c
// AVANT
if (!i2c_bus_shared_lock(pdMS_TO_TICKS(50))) {

// APRÈS
if (!i2c_bus_shared_lock(pdMS_TO_TICKS(100))) {
```

#### Modification 3: Rate Limiting & Yield (lignes ~1740-1750)
```c
// AVANT
if ((now - s_last_process_us) < 5000) {
  // Too fast, ignore
  gt911_enable_irq_guarded();
  continue;
}

// APRÈS
// Rate limiting: prevent processing more than one frame every ~15ms
// This prevents WDT triggers by ensuring IDLE task gets CPU time
if ((now - s_last_process_us) < 15000) {
  // Too fast - yield to IDLE before continuing
  gt911_enable_irq_guarded();
  vTaskDelay(pdMS_TO_TICKS(5)); // Guaranteed yield to IDLE
  continue;
}
```

### 2. `components/lvgl_port/lvgl_port.c`

#### Modification: Yield dans boucle principale (lignes ~158-167)
```c
// AVANT
// Yield to prevent WDT on this core (though usually it's Core 0 complaining
// about IDLE0, but being safe)
vTaskDelay(pdMS_TO_TICKS(5));

// APRÈS
// Yield to prevent WDT on CPU1 - CRITICAL for preventing IDLE1 starvation
// With priority reduced to 5 (from 10), this delay ensures IDLE gets time
vTaskDelay(pdMS_TO_TICKS(10));
```

### 3. `sdkconfig`

#### Modification: Priorité tâche LVGL (ligne ~3868)
```kconfig
# AVANT
CONFIG_ARS_LVGL_TASK_PRIO=10

# APRÈS
# Reduced from 10 to 5 to prevent IDLE task starvation and WDT triggers
CONFIG_ARS_LVGL_TASK_PRIO=5
```

### 4. `components/i2c/i2c_bus_shared.c`

#### Ajout: Implémentation de `i2c_bus_shared_recover_locked()`
La fonction était déclarée dans le header mais manquante. Permet la récupération du bus quand l'appelant détient déjà le mutex.

```c
esp_err_t i2c_bus_shared_recover_locked(void) {
  if (xPortInIsrContext()) {
    return ESP_ERR_INVALID_STATE;
  }

  // Caller MUST already hold the mutex
  if (!i2c_bus_shared_is_locked_by_me()) {
    ESP_LOGE(TAG, "i2c_bus_shared_recover_locked called without holding mutex!");
    return ESP_ERR_INVALID_STATE;
  }

  return i2c_bus_shared_recover_internal();
}
```

#### Ajout: Implémentation de `i2c_bus_shared_deinit()`
Permet de libérer les ressources I2C (principalement pour les tests).

```c
void i2c_bus_shared_deinit(void) {
  if (s_shared_bus) {
    i2c_del_master_bus(s_shared_bus);
    s_shared_bus = NULL;
  }
  if (g_i2c_bus_mutex) {
    vSemaphoreDelete(g_i2c_bus_mutex);
    g_i2c_bus_mutex = NULL;
  }
  s_initialized = false;
}
```

---

## Configuration des Tâches Après Correctifs

| Tâche           | CPU    | Priorité | Description                    |
|-----------------|--------|----------|--------------------------------|
| GT911 IRQ       | CPU0   | 2        | Gestion tactile (inchangé)     |
| LVGL            | CPU1   | 5        | Interface graphique (réduit)   |
| IDLE0           | CPU0   | 0        | WDT check                      |
| IDLE1           | CPU1   | 0        | WDT check                      |

---

## Comment Tester

### 1. Rebuild complet
```powershell
# Dans ESP-IDF Command Prompt ou PowerShell avec ESP-IDF activé
idf.py fullclean
idf.py build
```

### 2. Flash et Monitor
```powershell
idf.py -p COMx flash monitor
```

### 3. Vérifications
- ✅ **Aucun message** `task_wdt: Tasks currently running`
- ✅ **Touch fonctionnel** avec réponse rapide
- ✅ **Pas de reboot** pendant interactions tactiles intensives
- ✅ **Stabilité > 10 minutes** en utilisation normale

---

## Critères de Succès
1. **Build réussi** sans erreurs de compilation
2. **Aucun WDT trigger** pendant 10+ minutes d'utilisation
3. **Touch réactif** avec latence < 50ms
4. **Pas de messages I2C timeout** récurrents dans les logs

---

## Notes Techniques

### Architecture des Tâches
```
CPU0:
  ├── main task (prio 1)
  ├── gt911_irq (prio 2) - épinglée
  ├── lcd_init (prio 5) - temporaire
  └── IDLE0 (prio 0) - WDT monitored

CPU1:
  ├── lvgl (prio 5) - épinglée (réduit de 10)
  ├── lcd_test (prio 2) - temporaire
  └── IDLE1 (prio 0) - WDT monitored
```

### Pourquoi ces valeurs?
- **Priorité LVGL = 5**: Suffisant pour UI fluide, mais permet à IDLE de s'exécuter
- **Rate limit 15ms**: ~66 Hz max pour le touch, largement suffisant
- **Timeout I2C 100ms**: Tolère contention avec IO Expander (CH32V003)
- **Yield 10ms dans LVGL**: Garantit ~100ms/s pour IDLE1 (WDT = 5s)

---

## Références
- [ESP-IDF Task Watchdog Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/wdts.html)
- [FreeRTOS Task Priorities](https://www.freertos.org/RTOS-task-priority.html)
