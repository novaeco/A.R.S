# Calibration tactile GT911 (Waveshare ESP32-S3 Touch LCD 7B)

## Pipeline coordonnées
1. **GT911 brut** : lecture I²C (aucun swap/mirror appliqué dans le driver), `raw_x/raw_y` conservés via `gt911_get_stats`.
2. **Transform unique** (`touch_transform`)
   - Orientation logique : `swap_xy`, `mirror_x`, `mirror_y`.
   - Affine 2D : matrice `[a11 a12 a13; a21 a22 a23]` (float, contrôles NaN/déterminant).
   - Clamp final aux bornes LVGL (1024×600).
3. **LVGL indev** : `touchpad_read` ne consomme que le point brut + transform actif, puis applique un léger lissage anti-jitter.

## Procédure de calibration
1. Ouvrir **Paramètres → Écran tactile → Calibrer**.
2. Étape Orientation :
   - Option **Auto-détection** : glisser le doigt vers la droite puis vers le bas, le wizard règle swap/mirror.
   - Sinon basculer manuellement Swap/Miroirs, vérifier le curseur suiveur.
3. Étape Capture :
   - Appuyer successivement sur les 5 croix (4 coins + centre). Les points sont capturés en **raw + orientation** (sans affine ni jitter LVGL).
4. Calcul :
   - Solveur affine par moindres carrés (6 paramètres) avec métriques RMS/max; fallback scale/offset si conditionnement mauvais.
   - Erreur > ~12 px ou matrice singulière ⇒ rejet et message.
5. Sauvegarde :
   - Double-slot NVS `touchcal/slotA|slotB` avec magic/version/génération + CRC32.
   - Migration automatique depuis l’ancien format `touch/orient` (scale/offset ↦ affine diagonale).

## Forcer un reset calibration
```
idf.py -T touch_transform/test_touch_transform    # vérifier la math affine
idf.py erase_flash                                # pour effacer le NVS (ou)
esp32 nvs_flash init + supprimer namespace "touchcal" via nvs_cli
```

## Diagnostic rapide
- Activer logs `TOUCH_EVT` et `touch_tf_store` pour vérifier le chargement du slot actif.
- Dans l’écran de calibration : label debug affiche `raw:x,y xy:x,y irq/err`.
- Bouton **Exporter debug** (à ajouter côté settings) : lire coefficients et RMS (présents en UI/calcul).
