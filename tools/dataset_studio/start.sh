#!/usr/bin/env bash
# PCB Dataset Studio — lancement
set -euo pipefail
cd "$(dirname "$0")"

if [[ ! -d .venv ]]; then
    echo "[ERREUR] Lancer ./install.sh d'abord."
    exit 1
fi

source .venv/bin/activate
python app.py
