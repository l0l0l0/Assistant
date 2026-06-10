#!/bin/bash
# =============================================================================
#  Entrypoint runtime — verifie l'env et lance MicroscopeIBOM
# =============================================================================

set -euo pipefail

APP_DIR=/opt/microscope-ibom
APP_BIN=${APP_DIR}/MicroscopeIBOM
DATA_DIR=${IBOM_DATA_DIR:-${APP_DIR}/data}
TRT_CACHE=${DATA_DIR}/tensorrt-cache
MODELS_DIR=${APP_DIR}/models
LOGS_DIR=${APP_DIR}/logs

# -----------------------------------------------------------------------------
#  Creation des dossiers de persistance s'ils n'existent pas
#  (config.json / calibration.yml / snapshots / cache TRT vivent tous sous
#   DATA_DIR — cf src/utils/Paths.h et IBOM_DATA_DIR dans compose.yml)
# -----------------------------------------------------------------------------
mkdir -p "${LOGS_DIR}" "${DATA_DIR}" "${TRT_CACHE}"

# -----------------------------------------------------------------------------
#  Sanity check : binaire present
# -----------------------------------------------------------------------------
if [ ! -x "${APP_BIN}" ]; then
    echo "[entrypoint] ERREUR: ${APP_BIN} non trouve ou non executable"
    echo "[entrypoint] Avez-vous compile le binaire dans le container dev avant de builder runtime ?"
    exit 1
fi

# -----------------------------------------------------------------------------
#  Sanity check : modeles presents
# -----------------------------------------------------------------------------
if [ ! -d "${MODELS_DIR}" ] || [ -z "$(ls -A ${MODELS_DIR} 2>/dev/null)" ]; then
    echo "[entrypoint] ATTENTION: ${MODELS_DIR} vide ou absent"
    echo "[entrypoint] Monter le volume avec les .onnx via compose.yml"
fi

# -----------------------------------------------------------------------------
#  Sanity check : DISPLAY (GUI)
# -----------------------------------------------------------------------------
if [ -z "${DISPLAY:-}" ]; then
    echo "[entrypoint] ATTENTION: DISPLAY non defini, GUI ne demarrera pas"
fi

# -----------------------------------------------------------------------------
#  Sanity check : GPU
#  Sur Jetson/L4T, nvidia-smi est souvent absent ou tres limite (iGPU Tegra).
#  On teste donc d'abord les noeuds device Tegra, et nvidia-smi seulement s'il
#  existe — sinon on emettrait un faux warning permanent.
# -----------------------------------------------------------------------------
if ls /dev/nvhost-gpu /dev/nvgpu* >/dev/null 2>&1; then
    echo "[entrypoint] GPU Tegra detecte (noeuds /dev/nvhost-gpu)"
elif command -v nvidia-smi >/dev/null 2>&1 && nvidia-smi >/dev/null 2>&1; then
    echo "[entrypoint] GPU detecte via nvidia-smi"
else
    echo "[entrypoint] ATTENTION: aucun GPU detecte (ni /dev/nvhost-gpu ni nvidia-smi) — runtime nvidia manquant ?"
fi

# -----------------------------------------------------------------------------
#  Cache TensorRT
#  Les engines sont compiles paresseusement par ONNX Runtime au 1er appel
#  d'inference et caches dans TRT_CACHE (chemin absolu cote binaire, cf
#  InferenceEngine.cpp). Ce dossier etant persiste (volume), la recompilation
#  (~5-15 min) ne se produit qu'une fois. Rien a generer ici.
# -----------------------------------------------------------------------------
if [ -d "${MODELS_DIR}" ] && ls "${MODELS_DIR}"/*.onnx >/dev/null 2>&1; then
    if [ -z "$(ls -A "${TRT_CACHE}" 2>/dev/null)" ]; then
        echo "[entrypoint] Cache TRT vide — la 1ere inference compilera les engines (lent une fois)"
    else
        echo "[entrypoint] Cache TRT present ($(ls "${TRT_CACHE}" | wc -l) fichiers)"
    fi
fi

# -----------------------------------------------------------------------------
#  Lancement
# -----------------------------------------------------------------------------
cd "${APP_DIR}"
echo "[entrypoint] Lancement de MicroscopeIBOM..."
exec "${APP_BIN}" "$@"
