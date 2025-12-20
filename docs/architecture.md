# Architecture

## Vue composants
- **board** : constantes de pins et init léger.
- **i2c_bus_shared** : bus maître avec mutex unique (GPIO8/9).
- **io_extension** : accès CH32V003 (reset tactile, CS SD).
- **touch** : GT911 lecture coordonnée brute.
- **touch_transform** : projection coordonnée vers 1024×600.
- **display** : stub driver LCD, callbacks LVGL.
- **lvgl_port** : init LVGL, buffers, tâches `lv_tick` et `lv_loop`.
- **sd** : montage SDSPI non bloquant.
- **storage_core** : contexte en mémoire + mutex, persistance à compléter.
- **domain_models** : structures et dataset de démonstration.
- **compliance_rules** : règles data-driven simplifiées.
- **documents** : index documentaire (stub SD).
- **export_share** : export CSV / dossier.
- **ui** : écrans LVGL (topbar, navigation, listes, conformité).

## Flux d'init (boot)
1. NVS
2. board
3. I²C partagé
4. IO extension
5. Display
6. LVGL
7. Touch
8. SD (non bloquant)
9. Storage + données + règles
10. Documents + export
11. UI Dashboard

## Séparation LVGL
- Une seule boucle LVGL (task dédiée), tous les appels UI depuis ce contexte.
- Drivers et services ne référencent pas LVGL.

## Synchronisation
- Mutex I²C global dans `i2c_bus_shared`.
- Mutex storage pour accès futurs aux données persistantes.

## Évolution prévue
- Remplacer les stubs (LCD, IO extender) par implémentations hardware.
- Ajouter scheduler d'échéances et notifications.
