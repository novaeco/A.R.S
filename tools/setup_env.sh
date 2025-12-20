#!/usr/bin/env bash
set -euo pipefail

DEFAULT_IDF_PATH="C:/Espressif/frameworks/esp-idf-6.1-dev"

if [ -z "${IDF_PATH:-}" ]; then
  IDF_PATH="$DEFAULT_IDF_PATH"
  echo "[setup_env] IDF_PATH non défini, utilisation du chemin par défaut: ${IDF_PATH}" >&2
fi

if [ ! -d "$IDF_PATH" ]; then
  echo "[setup_env] IDF_PATH introuvable: ${IDF_PATH}." >&2
  echo "[setup_env] Chargez l'environnement ESP-IDF (ex: . ~/esp/esp-idf/export.sh)." >&2
  exit 1
fi

. "$IDF_PATH/export.sh"

resolved_idf_py="$(command -v idf.py)"
echo "[setup_env] idf.py résolu vers: ${resolved_idf_py}" >&2
idf.py --version

echo "[setup_env] Environnement ESP-IDF prêt. Utilisez idf.py set-target esp32s3 && idf.py reconfigure && idf.py build"
