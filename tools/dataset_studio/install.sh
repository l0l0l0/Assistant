#!/usr/bin/env bash
# PCB Dataset Studio — installation (venv + dépendances de base)
set -euo pipefail
cd "$(dirname "$0")"

# ── Python ───────────────────────────────────────────────────────────────────
PY=""
for candidate in python3.12 python3.11 python3.10 python3; do
    if command -v "$candidate" &>/dev/null; then
        ver=$("$candidate" -c "import sys; print(sys.version_info >= (3,10))" 2>/dev/null)
        if [[ "$ver" == "True" ]]; then
            PY="$candidate"
            break
        fi
    fi
done

if [[ -z "$PY" ]]; then
    echo "[ERREUR] Python 3.10+ introuvable. Installer via :"
    echo "  sudo apt install python3 python3-venv python3-tk"
    exit 1
fi

echo "[info] Python : $($PY --version)"

# ── Tkinter ──────────────────────────────────────────────────────────────────
if ! "$PY" -c "import tkinter" 2>/dev/null; then
    echo "[ERREUR] tkinter manquant — nécessaire pour l'interface graphique."
    echo "  sudo apt install python3-tk"
    exit 1
fi

# ── Venv ─────────────────────────────────────────────────────────────────────
if [[ ! -d .venv ]]; then
    echo "[install] Création du venv..."
    "$PY" -m venv .venv
fi

source .venv/bin/activate
python -m pip install --upgrade pip
pip install -r requirements.txt

echo ""
echo "============================================================"
echo " Installation de base OK. Lancer avec : ./start.sh"
echo ""
echo " Pour l'ENTRAÎNEMENT (étape 4) : ./install_training.sh"
echo " (PyTorch CUDA 12.8 ~3 Go + ultralytics, GPU RTX 50xx Blackwell)"
echo "============================================================"
