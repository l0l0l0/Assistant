#!/usr/bin/env bash
# =============================================================================
#  train_on_server.sh — pipeline complet sur un SERVEUR UBUNTU avec GPU
#  (testé pour RTX 5070 Ti = Blackwell sm_120). Produit le modèle de
#  re-ancrage (Piste B "présence") : models/component_detector.onnx
#
#  Différence avec build_reanchor_model.sh (qui est pour le Docker Jetson) :
#   - crée un environnement Python isolé (venv) pour ne rien casser ;
#   - installe PyTorch avec les wheels CUDA 12.8 (cu128) REQUIS par les GPU
#     Blackwell (RTX 50xx) — un torch trop ancien donne "no kernel image".
#
#  Avant de lancer :
#    1. Mets ta clé Roboflow :  export ROBOFLOW_API_KEY=ta_cle
#       (ou édite ROBOFLOW_API_KEY ci-dessous)
#    2. Lance :  bash scripts/train_on_server.sh
#
#  Le .onnx final est dans models/. Copie-le (+ le .txt) sur le Jetson.
# =============================================================================

set -euo pipefail

# ======================= CONFIG — édite si besoin =======================
ROBOFLOW_API_KEY="${ROBOFLOW_API_KEY:-COLLE_TA_CLE_ICI}"

DATASET_URL="https://universe.roboflow.com/marco-filippozzi-siwjn/smd-component-detection"
DATASET_DIR="datasets/smd"
MODEL_OUT="models/component_detector.onnx"

YOLO_MODEL="yolov8n.pt"   # présence = petit suffit ; yolov8s/m si tu veux + précis
EPOCHS=100
BATCH=32                  # 5070 Ti = 16 Go ; baisse si "CUDA out of memory"
GPU_DEVICE="0"            # id du GPU (nvidia-smi pour voir), ou "0,1" multi-GPU
RUN_NAME="reanchor_presence"

# Environnement Python isolé.
USE_VENV=1
VENV_DIR=".venv-train"

# Index des wheels PyTorch. cu128 = CUDA 12.8 = OK Blackwell (RTX 50xx).
# Si ton GPU est plus ancien (RTX 40xx/30xx) cu121 marche aussi, mais cu128
# reste compatible.
TORCH_INDEX_URL="https://download.pytorch.org/whl/cu128"
# ========================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$REPO_ROOT"

echo "============================================================"
echo " train_on_server.sh  —  pipeline Roboflow + entraînement GPU"
echo " repo   : $REPO_ROOT"
echo " dataset: $DATASET_URL"
echo " sortie : $MODEL_OUT"
echo "============================================================"

# ---- garde-fou : clé API ----
if [ "$ROBOFLOW_API_KEY" = "COLLE_TA_CLE_ICI" ] || [ -z "$ROBOFLOW_API_KEY" ]; then
    echo "ERREUR : clé API absente."
    echo "  -> export ROBOFLOW_API_KEY=ta_cle   (ou édite le script), puis relance."
    exit 2
fi
export ROBOFLOW_API_KEY

# ---- [0] venv ----
PY="python3"
if [ "$USE_VENV" = "1" ]; then
    echo
    echo "==> [0/6] Environnement Python isolé : $VENV_DIR"
    if [ ! -d "$VENV_DIR" ]; then
        python3 -m venv "$VENV_DIR"
    fi
    # shellcheck disable=SC1091
    source "$VENV_DIR/bin/activate"
    PY="python"
    $PY -m pip install --upgrade pip
fi

# ---- [1] PyTorch (cu128 pour Blackwell) ----
echo
echo "==> [1/6] Installation de PyTorch (CUDA 12.8 / Blackwell)"
$PY -m pip install --upgrade torch torchvision --index-url "$TORCH_INDEX_URL"

echo
echo "==> [2/6] Installation d'Ultralytics + Roboflow"
$PY -m pip install --upgrade ultralytics roboflow pyyaml

# ---- contrôle GPU ----
echo
echo "==> Vérification du GPU vu par PyTorch :"
$PY - <<'PYCHECK'
import torch
ok = torch.cuda.is_available()
print("  CUDA disponible :", ok)
if ok:
    print("  GPU :", torch.cuda.get_device_name(0))
    print("  Capacité :", torch.cuda.get_device_capability(0))
else:
    print("  !! Aucun GPU vu par PyTorch — l'entraînement tournera sur CPU (très lent).")
    print("     Vérifie le driver NVIDIA (nvidia-smi) et la version de torch.")
PYCHECK

# ---- [3] téléchargement ----
echo
echo "==> [3/6] Téléchargement du dataset Roboflow"
$PY scripts/fetch_roboflow_dataset.py "$DATASET_URL" --out "$DATASET_DIR"

# ---- [4] remap présence ----
echo
echo "==> [4/6] Remap en 1 classe ('présence')"
$PY scripts/remap_classes.py "$DATASET_DIR" --presence

# ---- [5] entraînement ----
echo
echo "==> [5/6] Entraînement ($YOLO_MODEL, $EPOCHS epochs, batch $BATCH, GPU $GPU_DEVICE)"
$PY scripts/train_yolo.py "$DATASET_DIR/data.yaml" \
    --model "$YOLO_MODEL" --epochs "$EPOCHS" --batch "$BATCH" \
    --device "$GPU_DEVICE" --name "$RUN_NAME"

# ---- [6] export ONNX ----
echo
echo "==> [6/6] Export ONNX -> $MODEL_OUT"
$PY scripts/export_yolov8_onnx.py \
    "runs/detect/$RUN_NAME/weights/best.pt" --out "$MODEL_OUT"

echo
echo "============================================================"
echo " ✅ TERMINÉ"
echo "    Modèle : $MODEL_OUT (+ le .txt à côté)"
echo "    -> copie ces 2 fichiers dans models/ sur le Jetson, relance l'app."
echo "    -> 1er lancement : compilation TensorRT (quelques min, une fois)."
echo "    -> test : menu Dev > 'Component re-anchor now'."
echo "============================================================"
