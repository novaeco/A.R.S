# Analyse complète de la pile UI (ESP32-S3 / LVGL 9)

## Architecture et points d'entrée
- `main/main.c` orchestre la séquence : NVS/netif -> data_manager -> `app_board_init()` (LCD + GT911) -> `lvgl_port_init()` -> UI (`ui_init()` dans la tâche LVGL) -> SD -> réseau/web. Ce flux garantit que l'affichage et le touch sont prêts avant de charger l'UI.
- `components/board` expose les handles LCD/touch et applique l'orientation écran (rotation 180° dans `board_orientation`).
- `components/lvgl_port` démarre la tâche LVGL (Core1) avec mutex, tick timer, et appelle le callback `ui_init` enregistré par `ui_set_battery_cb`/`lvgl_port_set_ui_init_cb`.
- `components/rgb_lcd_port` fournit le driver RGB 1024×600 et les buffers LVGL (flush DMA + premier flush loggué).
- `components/touch` + `touch_orient` + `touch_transform` gèrent GT911, l'orientation (swap/mirror) et l'affine calibrée.

## Orchestration UI (components/ui)
- `ui.c` : vérifie le flag NVS `setup_done`, initialise thème/gestionnaire/ Navigation. Si première mise en route : lance l'assistant (`ui_wizard_start`), sinon vérifie la calibration et charge le Dashboard.
- `ui_screen_manager` : impose le chargement des écrans via `lv_scr_load_anim` (auto-destruction de l'écran précédent), fournit overlays (spinner) et toasts temporisés.
- `ui_navigation` : routeur central vers les écrans (Dashboard, Animaux, Wi-Fi, Documents, Web, Logs, Alertes, Détails/Formulaire/Reproduction). Gère une pile (10 entrées) pour le retour arrière.
- `ui_theme` : styles globaux (écran, cards, titres, boutons) + thème LVGL par défaut. Les écrans doivent appeler `ui_theme_apply` pour rester cohérents.
- `ui_wizard` : assistant d’onboarding en plusieurs étapes (calibration -> Wi-Fi -> succès/erreur) ; marque `setup_done` en NVS et bascule vers le Dashboard.
- `ui_calibration` : interface avancée de calibration/diagnostic touch (affine, swap/mirror, auto-détection). Applique les paramètres au driver (touch_orient + touch_transform) et empêche l'utilisation tant qu’aucune solution valide n’est enregistrée.
- Écrans concrets sous `components/ui/screens/` (Dashboard, listes d’animaux, Wi-Fi, etc.) consomment le routeur et les helpers (`ui_helpers`).

## Chaîne affichage (LCD)
- Init LCD via `app_board_init()` puis `rgb_lcd_port` : timings et buffers LVGL alloués avant `lvgl_port_init`. Le flush attend le VSYNC si configuré ; bascule sur "VSYNC wait timeout — disabling wait" si trop lent (observé dans les logs fournis).
- Le buffer LVGL est dimensionné pour ~60 lignes (~120 KB) ; la tâche LVGL tient le verrou avant tout accès `lv_display_flush_ready`.

## Chaîne input (touch)
- GT911 initialisé sur I2C partagé, avec reset externe via IO expander. Paramètres (resolution 1024×600, max 5 points) loggués au boot.
- `touch_orient` applique swap/mirror et sauvegarde en NVS ; `touch_transform` gère l’affine (pour calibrations multi-points) mais remet swap/mirror à false après injection dans le driver (pour éviter double inversion).
- L’UI ne déverrouille pas l’assistant tant que la calibration n’a pas produit de mapping valide (`s_has_valid_solution`).

## Constat à partir du log fourni
- Boot complet : écran, touch, LVGL, SD initialisés ; LVGL désactive l’attente VSYNC (timeout) mais continue à flusher.
- Le flag de setup absent -> assistant lancé : `ui_wizard` déclenche `ui_calibration` dès le démarrage. Tant que la calibration n’est pas confirmée, la navigation reste dans l’assistant.
- Touch config par défaut appliquée (swap/mirror = 0) et sauvegardée dès l’assistant.

## Causes probables d’une UI "figée" (interaction impossible)
1. Calibration incomplète : l’assistant attend 5 points (ou auto-détection) et bloque les boutons tant que `s_has_valid_solution` reste false. Vérifier que les marqueurs deviennent verts et que le bouton "Enregistrer" est actif.
2. Contexte LVGL : toute création d’écran hors tâche LVGL déclenche un log d’erreur (`ui_screen_manager`), mais les écrans actuels sont créés dans la tâche LVGL via `lvgl_port`. Faible probabilité ici.
3. Touch non converti : si l’orientation réelle diffère (écran inversé mécaniquement), swap/mirror doivent être ajustés dans l’assistant. Les indicateurs d’orientation affichés (`Orientation actuelle`) doivent suivre vos gestes.
4. Perf LCD : le timeout VSYNC peut réduire la fréquence de rafraîchissement mais ne bloque pas les entrées. Si l’écran semble gelé, vérifier que le tick LVGL tourne (logs périodiques inexistants par design) et que `lvgl_port` ne retourne pas d’erreur au flush.

## Checklist de validation (reproductible)
1. Build local :
   ```bash
   idf.py fullclean build
   ```
   Succès = build sans erreur; vérifier la présence de `reptiles_assistant.elf`.
2. Flash + monitor (avec carte insérée) :
   ```bash
   idf.py -p COM3 flash monitor
   ```
   Attendus :
   - Logs GT911 (Product ID, Resolution 1024x600).
   - `lv_port: LVGL First Flush` puis (si timeout) `VSYNC wait timeout — disabling wait`.
   - `ui_wizard: Starting Setup Wizard` puis `ui_calibration: Starting Calibration UI`.
3. Calibration manuelle : suivre les 5 marqueurs; valider que `Applied transform ...` apparaît et que le bouton d’enregistrement n’est plus grisé.
4. Navigation : après sauvegarde, l’assistant doit afficher le succès ou le Wi-Fi; la touche retour doit revenir au Dashboard (`ui_nav_go_back`).
5. Rollback simple : supprimer le namespace NVS `ui_setup` pour relancer l’assistant (via `nvs_erase` ou `esptool.py erase_region` ciblé), ou re-flasher avec `idf.py -p COM3 erase_flash flash`.
