#!/bin/bash
# =============================================================================
#  cleanup_vnc_setup.sh — desinstalle tout ce qui a ete setup pour tester le
#  GUI via SSH/VNC (Xvfb + x11vnc + openbox + dependances).
#
#  PRESERVE :
#    - Images Docker microscope-ibom:base et :dev
#    - Code source, build/bin/MicroscopeIBOM
#    - Configuration Docker (compose.yml, Dockerfiles)
#    - Journaux Jetson (docs/JETSON_*)
#
#  SUPPRIME :
#    - Process Xvfb / x11vnc / openbox qui tournent
#    - Fichiers /tmp/.docker.xauth, /tmp/xvfb.log, /tmp/vnc.log, etc.
#    - Container microscope-ibom-dev (l'image reste, le container redemarrable)
#    - Paquets apt : xvfb, x11vnc, openbox, x11-utils, x11-apps, xterm
#
#  Usage :
#    bash scripts/cleanup_vnc_setup.sh
# =============================================================================

set -u

RED=$'\033[0;31m'
GRN=$'\033[0;32m'
YEL=$'\033[1;33m'
BLU=$'\033[1;34m'
NC=$'\033[0m'

log()  { echo "${BLU}[cleanup]${NC} $*"; }
ok()   { echo "${GRN}[OK]${NC} $*"; }
warn() { echo "${YEL}[skip]${NC} $*"; }

# -----------------------------------------------------------------------------
echo
log "1/4 — Killer les process VNC/Xvfb/openbox..."
# -----------------------------------------------------------------------------

if pgrep -f "Xvfb :99" > /dev/null; then
    pkill -f "Xvfb :99" && ok "Xvfb :99 tue"
else
    warn "Xvfb :99 pas actif"
fi

if pgrep -f "x11vnc" > /dev/null; then
    pkill -f "x11vnc" && ok "x11vnc tue"
else
    warn "x11vnc pas actif"
fi

if pgrep -f "openbox" > /dev/null; then
    pkill -f "openbox" && ok "openbox tue"
else
    warn "openbox pas actif"
fi

# -----------------------------------------------------------------------------
echo
log "2/4 — Nettoyer les fichiers /tmp..."
# -----------------------------------------------------------------------------

sudo rm -f /tmp/xvfb.log /tmp/vnc.log /tmp/openbox.log
sudo rm -rf /tmp/.docker.xauth
sudo rm -f /tmp/.X99-lock
ok "Fichiers /tmp nettoyes"

# -----------------------------------------------------------------------------
echo
log "3/4 — Stopper le container dev (l'image reste)..."
# -----------------------------------------------------------------------------

REPO_DIR="${REPO_DIR:-$HOME/Assistant-git}"
if [ -f "$REPO_DIR/docker/compose.yml" ]; then
    cd "$REPO_DIR"
    if docker compose -f docker/compose.yml ps --status running -q dev > /dev/null 2>&1; then
        docker compose -f docker/compose.yml down 2>/dev/null
        ok "Container microscope-ibom-dev arrete"
    else
        warn "Container deja arrete"
    fi
else
    warn "Repo non trouve a $REPO_DIR"
fi

# -----------------------------------------------------------------------------
echo
log "4/4 — Desinstaller les paquets apt du test VNC..."
# -----------------------------------------------------------------------------

PKGS_TO_REMOVE=(xvfb x11vnc openbox x11-utils x11-apps xterm)
INSTALLED=()
for pkg in "${PKGS_TO_REMOVE[@]}"; do
    if dpkg -l "$pkg" 2>/dev/null | grep -q "^ii"; then
        INSTALLED+=("$pkg")
    fi
done

if [ ${#INSTALLED[@]} -gt 0 ]; then
    log "Paquets a supprimer : ${INSTALLED[*]}"
    sudo apt remove --purge -y "${INSTALLED[@]}" 2>&1 | tail -5
    sudo apt autoremove -y 2>&1 | tail -3
    ok "Paquets desinstalles"
else
    warn "Aucun paquet du test VNC trouve"
fi

# -----------------------------------------------------------------------------
echo
echo "${GRN}================================================================${NC}"
echo "${GRN} CLEANUP TERMINE${NC}"
echo "${GRN}================================================================${NC}"
echo
echo "Ce qui reste (preserve) :"
echo "  - Images Docker : microscope-ibom:base + :dev"
echo "  - Code source : ~/Assistant-git/"
echo "  - Binaire compile : ~/Assistant-git/build/bin/MicroscopeIBOM"
echo "  - Journaux : ~/Assistant-git/docs/JETSON_*.md"
echo
echo "${YEL}Pour lancer le GUI avec un ecran branche en local :${NC}"
echo "  bash ~/Assistant-git/scripts/run_local_gui.sh"
echo
