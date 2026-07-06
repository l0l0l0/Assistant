#!/usr/bin/env bash
# =============================================================================
#  build_reanchor_model.sh — fait TOUT le pipeline Roboflow en une commande
#  (Piste B : modèle de "présence" pour le re-ancrage). À lancer DANS le Docker.
#
#  Avant de lancer :
#    1. Édite ROBOFLOW_API_KEY ci-dessous (mets ta vraie clé), OU exporte-la :
#         export ROBOFLOW_API_KEY=ta_cle
#    2. Lance :
#         bash scripts/build_reanchor_model.sh
#
#  Ce que ça fait, dans l'ordre :
#    [1] installe les outils Python (ultralytics, roboflow, pyyaml)
#    [2] télécharge le dataset Roboflow
#    [3] le remappe en 1 seule classe ("présence")
#    [4] entraîne un petit modèle YOLOv8n
#    [5] l'exporte en models/component_detector.onnx (+ .txt)
#  -> ensuite : relance l'app, le re-ancrage IA est actif.
#
#  Astuce : pour SEULEMENT télécharger+préparer (et entraîner ailleurs,
#  p.ex. Google Colab ou un PC avec GPU), mets RUN_TRAINING=0 ci-dessous.
# =============================================================================

set -euo pipefail

# ======================= CONFIG — édite si besoin =======================
# Ta clé API Roboflow (ou exporte ROBOFLOW_API_KEY avant de lancer).
ROBOFLOW_API_KEY="${ROBOFLOW_API_KEY:-COLLE_TA_CLE_ICI}"

# Dataset à télécharger (le SMD est le plus adapté à nos cartes).
DATASET_URL="https://universe.roboflow.com/marco-filippozzi-siwjn/smd-component-detection"
DATASET_DIR="datasets/smd"

# Sortie du modèle (là où l'app le cherche).
MODEL_OUT="models/component_detector.onnx"

# Paramètres d'entraînement.
YOLO_MODEL="yolov8n.pt"   # petit + rapide ; yolov8s/m = plus précis, plus lent
EPOCHS=80
BATCH=16                  # baisse à 8 ou 4 si "CUDA out of memory"
RUN_NAME="reanchor_presence"

# 1 = entraîne + exporte ici ; 0 = télécharge + prépare seulement.
RUN_TRAINING=1
# ========================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$REPO_ROOT"
PY="${PYTHON:-python3}"

echo "============================================================"
echo " build_reanchor_model.sh  —  pipeline Roboflow (Piste B)"
echo " repo   : $REPO_ROOT"
echo " dataset: $DATASET_URL"
echo " sortie : $MODEL_OUT"
echo "============================================================"

# ---- garde-fou : clé API renseignée ----
if [ "$ROBOFLOW_API_KEY" = "COLLE_TA_CLE_ICI" ] || [ -z "$ROBOFLOW_API_KEY" ]; then
    echo "ERREUR : clé API absente."
    echo "  -> édite ROBOFLOW_API_KEY en haut de ce script,"
    echo "     ou lance : export ROBOFLOW_API_KEY=ta_cle puis relance."
    exit 2
fi
export ROBOFLOW_API_KEY

# ---- [1] dépendances ----
echo
echo "==> [1/5] Installation des outils Python (ultralytics, roboflow, pyyaml)"
if ! $PY -m pip install --upgrade ultralytics roboflow pyyaml; then
    echo "AVERTISSEMENT : 'pip install' a échoué."
    echo "  Sur Jetson, l'entraînement (torch) peut être délicat à installer."
    echo "  Le téléchargement/préparation peut quand même marcher."
    echo "  Si l'entraînement échoue plus bas, mets RUN_TRAINING=0 et entraîne"
    echo "  sur Google Colab ou un PC GPU (le dataset préparé est réutilisable)."
fi

# ---- [2] téléchargement ----
echo
echo "==> [2/5] Téléchargement du dataset Roboflow"
$PY scripts/fetch_roboflow_dataset.py "$DATASET_URL" --out "$DATASET_DIR"

# ---- [3] remap présence ----
echo
echo "==> [3/5] Remap en 1 classe ('présence')"
$PY scripts/remap_classes.py "$DATASET_DIR" --presence

if [ "$RUN_TRAINING" != "1" ]; then
    echo
    echo "✅ Dataset prêt : $DATASET_DIR (RUN_TRAINING=0, pas d'entraînement)."
    echo "   Entraîne-le ailleurs, ou remets RUN_TRAINING=1 et relance."
    exit 0
fi

# ---- [4] entraînement ----
echo
echo "==> [4/5] Entraînement ($YOLO_MODEL, $EPOCHS epochs) — ça peut être long."
echo "    (En cas d'erreur torch/CUDA ici : mets RUN_TRAINING=0, le dataset"
echo "     est déjà prêt, et entraîne sur Colab/PC GPU.)"
$PY scripts/train_yolo.py "$DATASET_DIR/data.yaml" \
    --model "$YOLO_MODEL" --epochs "$EPOCHS" --batch "$BATCH" --name "$RUN_NAME"

# ---- [5] export ONNX ----
echo
echo "==> [5/5] Export ONNX -> $MODEL_OUT"
$PY scripts/export_yolov8_onnx.py \
    "runs/detect/$RUN_NAME/weights/best.pt" --out "$MODEL_OUT"

echo
echo "============================================================"
echo " ✅ TERMINÉ"
echo "    Modèle : $MODEL_OUT (+ le .txt à côté)"
echo "    -> relance l'app. Au 1er lancement, la compilation TensorRT"
echo "       prend quelques minutes (normal, une seule fois)."
echo "    -> test : menu Dev > 'Component re-anchor now'."
echo "============================================================"
