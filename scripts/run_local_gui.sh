#!/bin/bash
# =============================================================================
#  run_local_gui.sh — lance MicroscopeIBOM avec affichage sur l'ecran physique
#  branche au Jetson (HDMI, DisplayPort, ou USB-C ecran tactile).
#
#  Prerequis :
#    - Un ecran branche au Jetson (que tu peux voir)
#    - Tu lances ce script DEPUIS l'ecran local (pas via SSH)
#      OU via SSH mais en ayant deja une session graphique active sur le Jetson
#
#  Si tu es via SSH et que personne n'est connecte graphiquement sur le Jetson,
#  utilise plutot une session graphique directe (clavier + souris branches).
#
#  Usage :
#    bash scripts/run_local_gui.sh
#    bash scripts/run_local_gui.sh debug    # avec ASAN + logs verbeux
# =============================================================================

set -u

RED=$'\033[0;31m'
GRN=$'\033[0;32m'
YEL=$'\033[1;33m'
BLU=$'\033[1;34m'
NC=$'\033[0m'

REPO_DIR="${REPO_DIR:-$HOME/Assistant-git}"
DISPLAY="${DISPLAY:-:0}"
MODE="${1:-release}"

cd "$REPO_DIR" || { echo "${RED}[ERR] Repo introuvable: $REPO_DIR${NC}"; exit 1; }

echo "${BLU}[run-local]${NC} DISPLAY=$DISPLAY mode=$MODE"

# -----------------------------------------------------------------------------
# 1. Verifier qu'on a bien un display X actif
# -----------------------------------------------------------------------------
if ! xset -display "$DISPLAY" q > /dev/null 2>&1; then
    echo "${RED}[ERR] Pas de serveur X actif sur DISPLAY=$DISPLAY${NC}"
    echo "       - Verifier que l'ecran est branche et qu'une session graphique tourne"
    echo "       - Si tu es via SSH : connecte-toi physiquement au Jetson une fois pour demarrer le bureau"
    exit 1
fi

# -----------------------------------------------------------------------------
# 2. Autoriser Docker a afficher sur ce DISPLAY
# -----------------------------------------------------------------------------
xhost +local:docker > /dev/null 2>&1 || echo "${YEL}[warn] xhost echec (peut etre OK)${NC}"

# -----------------------------------------------------------------------------
# 3. Verifier que les devices necessaires sont decommentes dans compose.yml
# -----------------------------------------------------------------------------
if grep -q "^[[:space:]]*#.*\/dev\/dri:\/dev\/dri" docker/compose.yml; then
    echo "${YEL}[warn] Le mount /dev/dri:/dev/dri est commente dans docker/compose.yml${NC}"
    echo "       Pour l'acceleration GPU display, decommenter le bloc devices: du service dev."
fi

# -----------------------------------------------------------------------------
# 4. Demarrer le container dev s'il ne tourne pas
# -----------------------------------------------------------------------------
# S'assurer que /tmp/.docker.xauth existe en fichier (pas dossier)
if [ -d /tmp/.docker.xauth ]; then
    sudo rm -rf /tmp/.docker.xauth
fi
touch /tmp/.docker.xauth 2>/dev/null || sudo touch /tmp/.docker.xauth
chmod 644 /tmp/.docker.xauth 2>/dev/null || sudo chmod 644 /tmp/.docker.xauth

if ! docker compose -f docker/compose.yml ps --status running -q dev | grep -q .; then
    echo "${BLU}[run-local]${NC} Demarrage du container dev..."
    docker compose -f docker/compose.yml up -d dev
fi

# -----------------------------------------------------------------------------
# 5. Lancer le binaire dans le container avec le DISPLAY local
# -----------------------------------------------------------------------------
BIN=build/bin/MicroscopeIBOM
if [ ! -f "$BIN" ]; then
    echo "${RED}[ERR] Binaire introuvable: $BIN${NC}"
    echo "       Lancer d'abord : bash scripts/build_jetson.sh"
    exit 1
fi

EXTRA_ENV=""
if [ "$MODE" = "debug" ] || [ "$MODE" = "Debug" ]; then
    EXTRA_ENV="-e QT_LOGGING_RULES=*.debug=true -e ASAN_OPTIONS=detect_leaks=0"
fi

echo "${GRN}[run-local]${NC} Lancement de MicroscopeIBOM dans le container..."
echo "${GRN}[run-local]${NC} Fenetre Qt6 devrait apparaitre sur ton ecran."
echo

# shellcheck disable=SC2086
docker compose -f docker/compose.yml exec \
    -e DISPLAY="$DISPLAY" \
    -e XAUTHORITY=/tmp/.docker.xauth \
    $EXTRA_ENV \
    dev ./build/bin/MicroscopeIBOM
