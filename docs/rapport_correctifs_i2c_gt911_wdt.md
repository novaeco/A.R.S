# Rapport Correctifs I2C, GT911 et WDT

**Date**: 2025-12-22  
**Composants concernés**: `board.c`, `i2c_bus_shared.c`, `gt911.c`

---

## Résumé des Problèmes

### Symptômes Observés
```
GT911 recovery (status): ESP_ERR_TIMEOUT
i2c_bus_shared: Bus recovery skipped: mutex busy
GT911 IOEXT reset skipped: I2C bus busy
GT911 reset failed after recovery: ESP_ERR_TIMEOUT
task_wdt: ... IDLE0 (CPU 0) ... CPU0: main
```

### Causes Racines Identifiées

| Problème | Fichier | Ligne | Cause |
|----------|---------|-------|-------|
| WDT Timeout | `board.c` | 500-520 | Test pattern LCD exécuté inline, bloquant CPU0 ~500ms |
| Mutex Busy | `i2c_bus_shared.c` | 116 | Recovery tente d'acquérir mutex déjà détenu |
| Cascade GT911 | `gt911.c` | 221 | Recovery déclenche IOEXT reset pendant contention bus |

---

## Corrections Appliquées

### A) WDT - Test Pattern LCD
**Fichier**: `components/board/src/board.c`

- Test pattern exécuté **toujours** dans une tâche dédiée
- Tâche épinglée sur CPU1 avec priorité basse (2)
- Plus jamais d'exécution inline qui bloquerait le WDT

```c
// Avant: xTaskCreate(...) avec fallback inline
// Après: xTaskCreatePinnedToCore(..., CPU1) sans fallback
if (xTaskCreatePinnedToCore(board_lcd_test_pattern_task, "lcd_test", 
                             4096, NULL, 2, NULL, 1) != pdPASS) {
  ESP_LOGW(TAG, "Test pattern task creation failed, skipping to avoid WDT");
}
```

### B) I2C Recovery Deadlock
**Fichiers**: `components/i2c/i2c_bus_shared.h`, `components/i2c/i2c_bus_shared.c`

Nouvelles APIs ajoutées:
- `i2c_bus_shared_is_locked_by_me()` - Détection de propriété du mutex
- `i2c_bus_shared_recover_locked()` - Recovery pour appelants détenant le mutex
- `i2c_bus_shared_recover_internal()` - Logique interne factorisée

```c
// La fonction recover() détecte maintenant automatiquement
if (i2c_bus_shared_is_locked_by_me()) {
  return i2c_bus_shared_recover_internal();  // Version locked
}
// Sinon: acquisition mutex normal
```

### C) GT911 Recovery
**Fichier**: `components/touch/gt911.c`

- Mode dégradé ajouté (`s_gt911_degraded_mode`)
- Logging rate-limited pour éviter le spam
- Recovery échoué planifie retry via backoff au lieu de bloquer
- Sortie automatique du mode dégradé après récupération réussie

---

## Paramètres Kconfig

| Option | Défaut | Description |
|--------|--------|-------------|
| `CONFIG_ARS_LCD_BOOT_TEST_PATTERN` | n | Affiche pattern test au boot |
| `CONFIG_ARS_LCD_BOOT_TEST_PATTERN_MS` | 2000 | Durée du pattern test (si activé) |
| `CONFIG_ARS_SKIP_TEST_PATTERN` | n | Ignore complètement le test pattern |

---

## Procédures de Test

### Build et Flash
```bash
idf.py fullclean build
idf.py flash monitor
```

### Vérification WDT (10 minutes)
1. Boot système et observer logs
2. **Critère**: Aucun message `task_wdt` pendant 10 minutes

### Vérification I2C Recovery
1. Observer logs pendant fonctionnement
2. **Critère**: Pas de boucle `Bus recovery skipped: mutex busy`
3. Si recovery se déclenche: doit afficher `I2C bus recovery complete`

### Vérification Tactile (5 minutes)
1. Interactions tactiles (tap, drag)
2. **Critère**: 
   - Touch répond correctement
   - Pas de boucle `GT911 recovery: ESP_ERR_TIMEOUT`
   - Erreurs transitoires acceptables si auto-récupérées

---

## Fichiers Modifiés

| Fichier | Type | Changement |
|---------|------|------------|
| `components/board/src/board.c` | MODIFY | Task-based test pattern uniquement |
| `components/i2c/i2c_bus_shared.h` | MODIFY | +2 nouvelles APIs |
| `components/i2c/i2c_bus_shared.c` | MODIFY | Mutex ownership + locked recovery |
| `components/touch/gt911.c` | MODIFY | Degraded mode + meilleure gestion erreurs |

---

## Résultat Attendu

```
I (xxx) main: BOOT-SUMMARY storage=ok display=ok touch=ok lvgl=ok sd=xxx wifi=xxx
```

- ✅ Aucun `task_wdt` en fonctionnement normal
- ✅ Recovery I2C fonctionne (pas de "mutex busy" en boucle)
- ✅ GT911 stable sans boucle de reset/timeout
