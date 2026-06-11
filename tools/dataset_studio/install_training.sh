#!/usr/bin/env bash
# PCB Dataset Studio — dépendances entraînement
# RTX 50xx (Blackwell sm_120) => PyTorch >= 2.7 / CUDA 12.8
set -euo pipefail
cd "$(dirname "$0")"

if [[ ! -d .venv ]]; then
    echo "[ERREUR] Lancer ./install.sh d'abord."
    exit 1
fi

source .venv/bin/activate

echo "[install] PyTorch CUDA 12.8 (gros téléchargement, ~3 Go)..."
pip install torch torchvision --index-url https://download.pytorch.org/whl/cu128

echo "[install] ultralytics..."
pip install ultralytics

echo ""
echo "[check] Vérification GPU :"
python -m studio.gpu_check
