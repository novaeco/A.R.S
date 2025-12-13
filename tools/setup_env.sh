#!/usr/bin/env bash
set -euo pipefail

if [ -z "${IDF_PATH:-}" ]; then
  echo "[setup_env] IDF_PATH non défini. Chargez l'environnement ESP-IDF (ex: . ~/esp/esp-idf/export.sh)." >&2
  exit 1
fi

. "$IDF_PATH/export.sh"

idf.py --version

echo "[setup_env] Environnement ESP-IDF prêt. Utilisez idf.py set-target esp32s3 && idf.py reconfigure && idf.py build"
