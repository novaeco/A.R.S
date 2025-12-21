# Rapport runtime I2C / WDT / LVGL DIRECT

## Causes racine
- **Collisions I2C** : verrou local de l'IO expander et verrou bus distincts, absence de récupération en cas de bus bloqué.
- **GT911** : relances I2C agressives et resets sans temporisation -> boucle d'erreurs/WDT si le bus reste bloqué.
- **Affichage** : motif de test LCD désactivé par défaut, rendant le diagnostic écran/alim incertain.

## Correctifs appliqués
- **Mutex unique I2C + recovery** : toutes les transactions passent par `i2c_bus_shared`; ajout de `i2c_bus_shared_recover()` (reset IDF + toggling SCL/SDA + re-init) exposé à `DEV_I2C_BusReset()` et appelé sur erreurs.
- **IO expander** : suppression du mutex privé, sérialisation via bus partagé, backoff sur erreurs avec appel recovery après 3 échecs (500 ms de repos).
- **GT911** : backoff exponentiel (50 → 1000 ms) sur erreurs I2C, recovery bus + reset touch seulement après seuil et hors fenêtre de backoff.
- **LVGL DIRECT VSYNC** : attente VSYNC uniquement en mode DIRECT via sémaphore ISR, `lv_display_flush_ready()` libéré après VSYNC ou timeout contrôlé.
- **Test pattern** : `CONFIG_ARS_SKIP_TEST_PATTERN=n` par défaut pour valider le pipeline LCD dès le boot.

## Options Kconfig
- `CONFIG_ARS_SKIP_TEST_PATTERN` (board): forcée à `n` par défaut pour activer le motif de test.
- `CONFIG_ARS_LCD_BOOT_TEST_PATTERN[_MS]`: conservées pour régler la durée du motif.
- `CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE`: contrôle l'attente VSYNC côté LVGL DIRECT.

## Reproduction / validation
1. **Build** :
   - `idf.py fullclean`
   - `idf.py build`
2. **Boot attendu** :
   - Pas de `task_wdt triggered`.
   - Logs I2C indiquant init bus partagé et recovery éventuel (`I2C bus recovery complete`).
   - Motif de test LCD (barres RGBWB) visible au boot.
   - Touch GT911 : pas de boucle d'erreurs, backoff loggé au-delà de 3 échecs seulement.
3. **Si bus I2C bloqué** : vérifier que `i2c_bus_shared_recover()` loggue le toggling SCL puis que les transactions reprennent.
