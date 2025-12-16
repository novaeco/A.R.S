# A.R.S

Assistant Reptiles Système (A.R.S) pour ESP32-S3 avec écran LVGL, stockage LittleFS et services réseau sécurisés.

## Fonctionnalités clés
- UI LVGL optimisée (buffers configurables, double buffer optionnel).
- Stockage LittleFS 8 Mio avec sérialisation JSON (mutex global pour éviter les accès concurrents).
- Gestion réseau STA avec reconnexion progressive et provisionnement NVS.
- Serveur web optionnel protégé par jeton Bearer stocké en NVS, CORS désactivé par défaut.
- Mesure batterie calibrée (ADC + calibration line-fitting quand disponible).

## Environnement
- **ESP-IDF Recommandé**: `v5.3` ou `esp-idf-6.1-dev` (Testé sur `esp-idf-6.1-dev`).
- **Vérification**: Lancez `idf.py --version`. Assurez-vous que `IDF_PATH` pointe vers la version choisie.
- **Python**: Un environnement virtuel propre est recommandé.

### Initialisation express (Windows)
- PowerShell: ` .\\tools\\env.ps1`
- CMD: `tools\\env.cmd`
Ces commandes forcent `IDF_PATH` vers `C:\\Espressif\\frameworks\\esp-idf-6.1-dev`, appellent `export.ps1/.bat`, puis affichent `idf.py --version` pour vérifier le chemin actif.

## Build rapide
Consultez `docs/BUILD.md` pour les détails complets. Raccourci principal :
```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
idf.py size
```

### Build/flash/monitor standard
- Nettoyage complet + recompilation pour ESP-IDF 6.1 :
  ```bash
  idf.py fullclean
  idf.py build
  ```
- Flash + monitor sur Windows (adapter `COM3`) en conservant le décodage d’adresse :
  ```bash
  idf.py -p COM3 flash monitor
  ```
- Si le décodage d’adresses (ex: `0x40000000` pendant CMD41) bruit le log, désactiver côté monitor :
  ```bash
  idf.py -p COM3 flash monitor --disable-address-decoding
  ```
  (alternativement `idf.py -p COM3 monitor --disable-address-decoding` pour une session monitor seule).

## Sécurité
- HTTP n'est pas chiffré : placer derrière un proxy TLS (voir `CONFIG_ARS_WEB_REQUIRE_TLS_PROXY`).
- Jeton web généré au premier boot (NVS namespace `web`, clé `auth_token`), transmis via `Authorization: Bearer <token>`.
- Client HTTP embarqué désactivé par défaut ; n'accepte que HTTPS si activé.

## Tests / CI
Une CI minimale (`.github/workflows/ci.yml`) lance un build et un size check via `idf.py` pour garantir la reproductibilité.
