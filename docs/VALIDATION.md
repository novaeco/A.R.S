# VALIDATION

## Commandes à exécuter
1. Nettoyage + build complet
   ```
   idf.py fullclean build
   ```
2. Taille binaire
   ```
   idf.py size size-components
   ```
3. (Sur cible) Flash + monitor
   ```
   idf.py -p <PORT> flash monitor
   ```

## Attendus
- Build sans warnings projet ; les warnings éventuels issus d’ESP-IDF (esp_wifi/wpa_supplicant) sont connus et non patchés.
- Logs boot :
  - `BOOT-SUMMARY display=ok|fail touch=ok|fail lvgl=ok|fail sd=STATE wifi=...`
  - En cas d’absence SD : log `SD init: NO_CARD` ou `mounting failed` mais pas de panic.
  - Pas de logs dans l’ISR GT911 ; les erreurs I2C sont regroupées (`TOUCH_EVT i2c_errors=...`).
- LVGL : premier flush logué, aucune attente VSYNC après le premier timeout.

## Tests manuels recommandés
- Interaction tactile rapide pendant 30s : aucune panic, pas de spam d’erreurs I2C (>1/s).
- Navigation UI sur plusieurs écrans : affichage stable, pas de tearing visible.
- SD (si carte présente) : montage OK puis lecture simple via l’UI ou en shell (`ls /sdcard`).

## Limitations connues
- Validation matérielle non exécutée dans cet environnement CI ; lancer les commandes ci-dessus sur carte Waveshare ESP32-S3 Touch LCD 7B pour confirmation.
