# FIX_BACKLOG

- Amélioration future : validation matérielle complète (monitor) quand le matériel est disponible pour confirmer l’absence de tearing et la stabilité du bus I2C partagé.
# Backlog de corrections (A.R.S)

## Priorités P0
1. **Stabiliser tâche GT911**
   - Actions : augmenter stack (>=6 KiB), limiter logs dans `gt911_irq_task`, ajouter garde sur `i2c_bus_shared_is_ready()` et bascule poll si bus NOK, monitoring watermark + erreurs I2C.
   - Dépendances : i2c_bus_shared (état prêt), LVGL/touch init.
   - DoD : aucun abort après 60s sans SD/touch, watermark >2 KiB, logs ≤1/s sur erreurs.
2. **Sécuriser init bus I2C**
   - Actions : déplacer flag `initialized` après succès, retourner erreur si bus NULL, réduire timeouts à 100–200 ms, backoff sur probe; protéger `DEV_I2C_Set_Slave_Addr` par mutex.
   - DoD : `i2c_bus_shared_is_ready()` vrai uniquement si bus valide, build OK, pas de blocage >200 ms.

## Priorités P1
3. **Aligner macros batterie**
   - Actions : utiliser `CONFIG_ARS_BAT_ADC_CHANNEL` + ratio num/den Kconfig, recalcul divider au boot, logs valeur effective.
   - DoD : lecture batterie correcte ±5%, build sans warnings.
4. **IO Extension chemin d’erreur**
   - Actions : structurer `IO_EXTENSION_Output_With_Readback` avec `goto unlock`, retourner `ESP_ERR_INVALID_STATE` si bus absent, retries limités.
   - Dépendance : correctif I2C.
   - DoD : aucun mutex retenu en cas d’erreur (watermark), logs esp_err_to_name.
5. **LVGL buffers/VSYNC**
   - Actions : vérifier capacité DMA (fallback RAM interne/single buffer), réduire attente VSYNC (désactiver après 1 timeout), retourner esp_err_t au lieu d’assert.
   - DoD : flush ne dépasse pas 30 ms si VSYNC absent, boot sans panic même si alloc échoue.
6. **Touch backoff/états**
   - Actions : protéger `s_poll_mode`/compteurs par spinlock, borne backoff et logs (≤1/s), désactiver IRQ sur erreur répétée.
   - DoD : pas de spam log, bascule poll documentée.

## Priorités P2
7. **SD ext-CS robustesse**
   - Actions : retries bornés (x2) mount, remise état UNINITIALIZED sur échec, self-test non destructif ou optionnel.
   - DoD : boot sans card → logs warning, pas de panic; self-test n’écrit pas si carte occupée.
8. **Pattern LCD optionnel**
   - Actions : Kconfig pour désactiver `board_lcd_test_pattern`, check alloc PSRAM avant usage.
   - DoD : boot rapide (<1 s) sans pattern en prod, logs clairs.
9. **Net credentials**
   - Actions : masquer password dans logs, plafond retries Wi-Fi, vérifier retours timer.
   - DoD : aucun mot de passe en clair, état réseau stable.

## Priorités P3
10. **Nettoyage configs LVGL**
    - Actions : choisir une seule `lv_conf.h` (component vs main) et documenter.
    - DoD : build unique, pas de double définition.
11. **Tests/observabilité**
    - Actions : ajouter hooks watermark/heap pour tâches critiques (LVGL, GT911), logs synthétiques BOOT-SUMMARY enrichis.
    - DoD : affichage watermark dans monitor, pas de régression build.

## Dépendances
- Correctifs P0 (I2C + GT911) doivent précéder IOEXT/SD/LVGL.
- LVGL buffer dépend de panel RGB (rgb_lcd_port) mais pas l’inverse.

