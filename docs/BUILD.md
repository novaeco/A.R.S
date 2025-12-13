# Guide de build ESP-IDF (A.R.S)

## Prérequis
- ESP-IDF 5.1.x ou supérieur (testé avec l'image Docker `espressif/idf:release-v5.1`).
- Python 3.8+ (inclus dans l'environnement ESP-IDF).
- Outils Git et `idf.py` disponibles via `export.sh` de l'ESP-IDF.

## Préparation de l'environnement
```bash
# Option 1 : shell ESP-IDF pré-exporté (recommandé)
. $IDF_PATH/export.sh

# Option 2 : script helper du dépôt
./tools/setup_env.sh
```

## Cible matérielle
Le projet est calibré pour ESP32-S3. Sélectionner explicitement la cible avant le build :
```bash
idf.py set-target esp32s3
```

## Commandes de build reproductibles
Les composants gérés (LVGL, LittleFS, cJSON) sont décrits dans `main/idf_component.yml` et seront téléchargés automatiquement lors de la reconfiguration.
```bash
idf.py reconfigure      # télécharge/valide les managed components
idf.py build            # compilation
idf.py size             # résumé des tailles
idf.py size-components  # détail par composant
```

## Flash et monitor
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Sécurité & réseau
- Le serveur web est protégé par un jeton Bearer stocké en NVS (namespace `web`, clé `auth_token`).
- CORS est désactivé par défaut (`CONFIG_ARS_WEB_CORS_ORIGIN` vide) et le serveur HTTP doit être placé derrière un proxy TLS (`CONFIG_ARS_WEB_REQUIRE_TLS_PROXY=y`).
- Les identifiants Wi-Fi sont stockés dans NVS (namespace `net`) lorsque provisionnés.

## LittleFS / stockage
- La partition `storage` est en LittleFS (8 Mio). Le montage est obligatoire : en cas d'échec, l'initialisation s'arrête avec log d'erreur.
- Les opérations FS sont sérialisées via mutex dans `data_manager`.

## Tests rapides / CI
Un smoke test CI est fourni dans `.github/workflows/ci.yml` :
- `idf.py set-target esp32s3`
- `idf.py reconfigure`
- `idf.py build`
- `idf.py size`

Lancer localement :
```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
idf.py size
```
