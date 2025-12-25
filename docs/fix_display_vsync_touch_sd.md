# Fix: VSYNC, Touch Transform, SD Validation

**Date:** 2025-12-25  
**Version:** 1.0  
**Auteur:** Antigravity AI Assistant

---

## Résumé des Corrections

Ce patch corrige trois problèmes critiques identifiés dans le projet A.R.S :

| Axe | Problème | Solution |
|-----|----------|----------|
| **(A) VSYNC** | Tearing/flicker de l'affichage | Activation de `CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE` |
| **(B) Touch** | Transformations contradictoires | Unification via `touch_transform` comme source unique |
| **(C) SD** | Faux INIT_OK malgré erreurs 0x107 | Validation de lecture avant INIT_OK |

---

## (A) Affichage - VSYNC Synchronization

### Symptômes observés
- Image "bouge", se déchire, instabilité visuelle
- Logs: `RGB VSYNC callback skipped (CONFIG disabled)`
- Logs: `VSYNC sync: DISABLED (CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE=n)`

### Cause identifiée
La configuration `CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE` était désactivée dans `sdkconfig` (ligne 4567).

### Correction appliquée
**Fichier:** `sdkconfig.defaults` (créé)

```
CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE=y
CONFIG_ARS_LCD_WAIT_VSYNC=y
CONFIG_ARS_LCD_WAIT_VSYNC_TIMEOUT_MS=20
```

### Mécanisme VSYNC (déjà implémenté)
1. **ISR callback** : `rgb_lcd_on_vsync_event()` → appelle `lvgl_port_notify_rgb_vsync()`
2. **Notification** : `xSemaphoreGiveFromISR()` depuis l'ISR
3. **Attente** : `flush_callback()` attend avec `xSemaphoreTake()` (timeout 20ms)

### Logs attendus après correction
```
lv_port: VSYNC sync: ACTIVE (timeout=20ms)
rgb_lcd: RGB VSYNC callback registered successfully
```

---

## (B) Tactile - Unification Transformation

### Symptômes observés
- Driver/orient applique : `Swap=0, MirX=1, MirY=1`
- UI calibration applique ensuite : `mirX=0 mirY=0`
- Coordonnées incohérentes selon le moment

### Cause identifiée
Double source de vérité :
1. `touch_orient` sauvegarde dans NVS `touch/orient`
2. `touch_transform` sauvegarde dans NVS `touchcal/slotA|B`

La fonction `apply_config_to_driver()` dans `ui_calibration.c` appelait `touch_orient_save()` à chaque application, créant des conflits.

### Correction appliquée
**Fichier:** `components/ui/ui_calibration.c`

```c
// AVANT: Double sauvegarde
touch_orient_save(&orient_cfg);
touch_orient_apply(tp, &orient_cfg);

// APRÈS: Application runtime uniquement, pas de sauvegarde touch_orient
esp_lcd_touch_set_swap_xy(tp, tf.swap_xy);
esp_lcd_touch_set_mirror_x(tp, tf.mirror_x);
esp_lcd_touch_set_mirror_y(tp, tf.mirror_y);
```

### Pipeline de transformation
```
raw → esp_lcd_touch (swap/mirror runtime) → touch_transform (affine) → LVGL
```

### Log unique attendu
```
TOUCH CONFIG FINAL: swap=0 mirX=1 mirY=1 a=[[1.0000 0.0000 0.00];[0.0000 1.0000 0.00]]
```

---

## (C) SD - Validation avant INIT_OK

### Symptômes observés
- `CMD0 try 1 ... timeout/all-FF`
- `sdmmc_read_sectors_dma ... returned 0x107`
- État passe en `INIT_OK` malgré échecs de lecture

### Cause identifiée
`sd_card_init()` déclarait `SD_STATE_INIT_OK` dès que `sd_extcs_mount_card()` retournait `ESP_OK`, sans valider la lecture effective.

### Correction appliquée
**Fichier:** `components/sd/sd.c`

```c
// Validation par lecture du secteur 0 (MBR)
uint8_t test_buf[512] __attribute__((aligned(4)));
esp_err_t read_ret = sdmmc_read_sectors(card, test_buf, 0, 1);
if (read_ret == ESP_OK) {
  // Vérification minimale de données lisibles
  sd_set_state(SD_STATE_INIT_OK);
} else {
  ESP_LOGE(TAG, "SD read validation FAILED: %s (0x%x)", ...);
  sd_set_state(SD_STATE_MOUNT_FAIL);
}
```

### Logs attendus
**Succès:**
```
SD read validation PASS (sector 0 readable)
SD state -> INIT_OK
```

**Échec:**
```
SD read validation FAILED: ESP_ERR_TIMEOUT (0x107)
SD mounted but read validation failed; state=MOUNT_FAIL
```

---

## Fichiers Modifiés

| Fichier | Action | Lignes |
|---------|--------|--------|
| `sdkconfig.defaults` | **Créé** | ~60 |
| `components/ui/ui_calibration.c` | Modifié | 72-113 |
| `components/sd/sd.c` | Modifié | 1-9, 51-90 |

---

## Instructions de Test

### 1. Compilation
```bash
cd c:\Users\woode\Desktop\ai\A.R.S
idf.py fullclean
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

### 2. Tests sur carte

#### Test VSYNC (A)
1. Afficher une scène statique (écran d'accueil)
2. Vérifier : **pas de tremblement/flicker**
3. Lancer une animation (menu scroll)
4. Vérifier : **pas de tearing horizontal**

#### Test Touch (B)
1. Boot → vérifier log unique `TOUCH CONFIG FINAL:`
2. Toucher les 4 coins de l'écran
3. Vérifier cohérence coordonnées vs position physique
4. Lancer calibration UI, valider
5. Reboot → vérifier que la calibration persiste

#### Test SD (C)
1. Boot avec carte SD insérée
2. Vérifier log `SD read validation PASS`
3. Vérifier `SD state -> INIT_OK`
4. Tester lecture/écriture fichier
5. Boot sans carte → vérifier `SD_STATE_ABSENT`

---

## Limites Connues

1. **VSYNC timeout** : Si le callback n'est pas reçu dans 20ms, la synchronisation est désactivée automatiquement pour éviter les blocages. Log d'avertissement émis une fois.

2. **Touch legacy** : Le namespace NVS `touch/orient` reste en lecture seule pour la migration. Les nouvelles calibrations sont stockées uniquement dans `touchcal`.

3. **SD all-FF intermittent** : Les retries CMD0 sont toujours possibles sur certains boots (matériel). La correction garantit seulement que l'état final est cohérent avec la capacité de lecture réelle.

---

## Rollback

Pour revenir à l'état précédent :
1. Supprimer `sdkconfig.defaults`
2. `git checkout -- components/ui/ui_calibration.c components/sd/sd.c`
3. `idf.py fullclean build`
