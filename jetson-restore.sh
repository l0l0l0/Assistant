#!/usr/bin/env bash
#
# jetson-restore.sh — Restauration post-reflash JetPack 7.2 (à lancer SUR le nouveau Jetson)
#
# Restaure automatiquement ce qui est portable (ssh, dotfiles, home, volumes Docker,
# daemon.json) et LISTE ce qui doit être réinstallé/rebuild (apt, pip, images Docker),
# car Ubuntu 24.04 + CUDA 13 ≠ l'ancienne install.
#
#   Usage : ./jetson-restore.sh NAS                 # prend la dernière archive sur le NAS
#           ./jetson-restore.sh /chemin/archive.tar.zst
#
set -uo pipefail

# ─── Config (mêmes valeurs que le backup) ───
NAS_USER="lololo"
NAS_IP="192.168.200.60"
NAS_DEST="/volume3/backups/jetson"
NAS_HOST="${NAS_USER}@${NAS_IP}"
WORK="/tmp/jetson-restore-$$"
SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"

ARG="${1:-}"
[ -n "$ARG" ] || { echo "Usage: $0 <NAS | /chemin/archive.tar.zst|.tar.gz>"; exit 1; }

log() { printf '\033[1;36m▶\033[0m %s\n' "$*"; }
ok()  { printf '  \033[1;32m✓\033[0m %s\n' "$*"; }

mkdir -p "$WORK/extract"

# ─── Récupération de l'archive ───
if [ "$ARG" = "NAS" ]; then
  LATEST="$(ssh "$NAS_HOST" "ls -1t '$NAS_DEST'/*.tar.* 2>/dev/null | head -1")"
  [ -n "$LATEST" ] || { echo "❌ Aucune archive dans $NAS_HOST:$NAS_DEST"; exit 1; }
  log "Dernière archive NAS : $LATEST"
  rsync -avh --progress "$NAS_HOST:$LATEST" "$WORK/" 2>/dev/null || scp "$NAS_HOST:$LATEST" "$WORK/"
  ARCHIVE="$WORK/$(basename "$LATEST")"
else
  ARCHIVE="$ARG"
  [ -f "$ARCHIVE" ] || { echo "❌ Introuvable : $ARCHIVE"; exit 1; }
fi

log "Extraction…"
tar -C "$WORK/extract" -xf "$ARCHIVE" || { echo "❌ Extraction échouée"; exit 1; }
SRC="$WORK/extract/$(ls "$WORK/extract" | head -1)"
[ -f "$SRC/MANIFEST.txt" ] && { echo; cat "$SRC/MANIFEST.txt"; echo; }

echo "════════ Restauration automatique (portable) ════════"

# ─── SSH (permissions strictes) ───
if [ -d "$SRC/ssh/user-ssh" ]; then
  mkdir -p "$HOME/.ssh"
  cp -a "$SRC/ssh/user-ssh/." "$HOME/.ssh/"
  chmod 700 "$HOME/.ssh"
  chmod 600 "$HOME/.ssh/"*      2>/dev/null || true
  chmod 644 "$HOME/.ssh/"*.pub  2>/dev/null || true
  ok "SSH utilisateur (~/.ssh)"
fi
cp -a "$SRC/ssh/.bash_history" "$HOME/" 2>/dev/null && ok "bash_history" || true
cp -a "$SRC/ssh/.zsh_history"  "$HOME/" 2>/dev/null && ok "zsh_history"  || true

# ─── Dotfiles + dossiers du home ───
for f in "$SRC/home/".*; do
  bn="$(basename "$f")"; { [ "$bn" = "." ] || [ "$bn" = ".." ]; } && continue
  [ -f "$f" ] && cp -a "$f" "$HOME/" && ok "$bn" || true
done
for d in "$SRC/home/"*/; do
  [ -d "$d" ] && { rsync -a "$d" "$HOME/$(basename "$d")/" 2>/dev/null || cp -a "$d" "$HOME/"; ok "~/$(basename "$d")"; }
done

# ─── Docker : daemon.json + volumes (nécessite Docker déjà installé) ───
if command -v docker >/dev/null 2>&1; then
  if [ -f "$SRC/docker/daemon.json" ]; then
    $SUDO cp -a "$SRC/docker/daemon.json" /etc/docker/ && ok "daemon.json (redémarrage Docker requis)"
  fi
  if [ -f "$SRC/docker/docker-volumes.tar" ]; then
    log "Restauration des volumes Docker…"
    $SUDO systemctl stop docker 2>/dev/null || true
    $SUDO tar -C /var/lib/docker -xf "$SRC/docker/docker-volumes.tar" && ok "volumes Docker"
    $SUDO systemctl start docker 2>/dev/null || true
  fi
else
  echo "  ⚠ Docker absent : installe-le (nvidia-container-toolkit), puis relance ce script pour les volumes."
fi

echo
echo "════════ À réinstaller / rebuild manuellement (référence) ════════"
echo "  • Paquets apt   : $SRC/apt/apt-manual.list"
echo "  • pip           : $SRC/pip/pip3-freeze.list"
echo "  • Images Docker : $SRC/docker/images.list   → rebuild via jetson-containers (autotag)"
echo "  • Config Jetson : $SRC/system/   (nvpmodel.conf, nvfancontrol, netplan, wireguard, systemd, crontabs)"
echo
echo "  Réinstall paquets marqués manuels (VÉRIFIE d'abord — distro 24.04 ≠ 22.04) :"
echo "    xargs -a '$SRC/apt/apt-manual.list' $SUDO apt-get install -y"
echo
echo "  Restaurer un service systemd custom, ex :"
echo "    $SUDO cp '$SRC/system/systemd-system/mon-service.service' /etc/systemd/system/ && $SUDO systemctl daemon-reload"
echo
ok "Terminé. Archive extraite dans : $SRC  (supprime $WORK après vérif)"
