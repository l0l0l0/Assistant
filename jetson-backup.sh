#!/usr/bin/env bash
#
# jetson-backup.sh — Sauvegarde pré-reflash JetPack (à lancer SUR le Jetson)
#
# Récupère : SSH (clés/config/historique), Docker (compose + volumes + refs),
#            paquets apt (liste), configs système Jetson, dotfiles, cron, home.
# Puis pousse l'archive compressée sur le NAS (rsync, fallback scp).
#
# Ce qui se RESTAURE proprement : ssh, dotfiles, home, volumes Docker, compose.
# Ce qui est REFERENCE seulement (maj majeure) : liste apt, liste pip, images Docker.
#
set -uo pipefail

# ─── Config (adapter) ───────────────────────────────────────────────────────
NAS_USER="lololo"                         # user SSH sur le NAS
NAS_IP="192.168.200.60"
NAS_DEST="/volume3/backups/jetson"        # dossier cible sur le NAS
STAGE="${HOME}/jetson-backup-staging"     # staging local (sur le rootfs/NVMe)

# Dossiers du home à inclure intégralement — METS TES CHEMINS RÉELS ici
HOME_DIRS=( "jetson-containers" "docker" "compose" "projects" "scripts" ".config" )

STOP_DOCKER=false        # true = arrêter Docker avant le tar des volumes (volumes cohérents / bases de données)
SAVE_DOCKER_IMAGES=false # true = docker save de toutes les images (TRÈS lourd, inutile car incompatibles JP7)
# ────────────────────────────────────────────────────────────────────────────

NAS_HOST="${NAS_USER}@${NAS_IP}"
TS="$(date +%Y%m%d-%H%M%S)"
HOSTN="$(hostname)"
OUT="${STAGE}/${TS}"
SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"

# Compresseur : zstd si dispo (rapide + fort), sinon gzip
if command -v zstd >/dev/null 2>&1; then
  COMP=(-I "zstd -19 -T0"); EXT="tar.zst"
else
  COMP=(-z); EXT="tar.gz"
fi
ARCHIVE="jetson-${HOSTN}-${TS}.${EXT}"

log() { printf '\033[1;36m▶\033[0m %s\n' "$*"; }
ok()  { printf '  \033[1;32m✓\033[0m %s\n' "$*"; }

[ -f /etc/nv_tegra_release ] || echo "⚠ /etc/nv_tegra_release absent — pas un Jetson ? je continue quand même."

mkdir -p "$OUT"/{ssh,docker,apt,pip,system,home,cron}
log "Staging : $OUT"

# ─── SSH + historique ───
cp -a "$HOME/.ssh"           "$OUT/ssh/user-ssh"  2>/dev/null && ok "SSH utilisateur"      || true
$SUDO cp -a /root/.ssh       "$OUT/ssh/root-ssh"  2>/dev/null && ok "SSH root"             || true
cp -a "$HOME/.bash_history"  "$OUT/ssh/"          2>/dev/null && ok "bash_history"         || true
cp -a "$HOME/.zsh_history"   "$OUT/ssh/"          2>/dev/null && ok "zsh_history"          || true

# ─── Dotfiles ───
for f in .bashrc .bash_aliases .bash_logout .profile .zshrc .gitconfig .vimrc .tmux.conf .netrc .curlrc; do
  cp -a "$HOME/$f" "$OUT/home/" 2>/dev/null && ok "$f" || true
done

# ─── Dossiers du home sélectionnés ───
for d in "${HOME_DIRS[@]}"; do
  if [ -e "$HOME/$d" ]; then
    rsync -a "$HOME/$d" "$OUT/home/" 2>/dev/null && ok "~/$d" || cp -a "$HOME/$d" "$OUT/home/" 2>/dev/null
  fi
done

# ─── Docker ───
if command -v docker >/dev/null 2>&1; then
  docker images --format '{{.Repository}}:{{.Tag}}\t{{.ID}}\t{{.Size}}' > "$OUT/docker/images.list"     2>/dev/null && ok "images.list"     || true
  docker ps -a  --format '{{.Names}}\t{{.Image}}\t{{.Status}}'          > "$OUT/docker/containers.list" 2>/dev/null && ok "containers.list" || true
  docker volume ls --format '{{.Name}}'                                 > "$OUT/docker/volumes.list"    2>/dev/null && ok "volumes.list"    || true
  $SUDO cp -a /etc/docker/daemon.json "$OUT/docker/" 2>/dev/null && ok "daemon.json (runtime nvidia !)" || true

  [ "$STOP_DOCKER" = true ] && { log "Arrêt de Docker pour volumes cohérents…"; $SUDO systemctl stop docker; }
  if $SUDO test -d /var/lib/docker/volumes; then
    $SUDO tar -C /var/lib/docker -cf "$OUT/docker/docker-volumes.tar" volumes 2>/dev/null && ok "volumes Docker (données)" || true
  fi
  [ "$STOP_DOCKER" = true ] && $SUDO systemctl start docker

  if [ "$SAVE_DOCKER_IMAGES" = true ]; then
    IMG="$(docker images -q | sort -u)"
    [ -n "$IMG" ] && { log "docker save (lourd)…"; docker save $IMG -o "$OUT/docker/images.tar" && ok "images.tar"; }
  fi
else
  echo "  (Docker non détecté)"
fi

# ─── Paquets apt (référence — Ubuntu 22.04 → 24.04, à réinstaller à la main) ───
dpkg --get-selections            > "$OUT/apt/dpkg-selections.list" 2>/dev/null && ok "dpkg-selections" || true
apt-mark showmanual              > "$OUT/apt/apt-manual.list"      2>/dev/null && ok "apt-manual"      || true
$SUDO cp -a /etc/apt/sources.list      "$OUT/apt/"     2>/dev/null || true
$SUDO cp -a /etc/apt/sources.list.d    "$OUT/apt/"     2>/dev/null || true

# ─── pip (référence) ───
command -v pip3 >/dev/null 2>&1 && pip3 freeze > "$OUT/pip/pip3-freeze.list" 2>/dev/null && ok "pip3 freeze" || true

# ─── Config système / Jetson ───
$SUDO cp -a /etc/nvpmodel.conf         "$OUT/system/"          2>/dev/null && ok "nvpmodel.conf" || true
$SUDO nvpmodel -q                      > "$OUT/system/nvpmodel-current.txt" 2>/dev/null || true
$SUDO cp -a /etc/nvfancontrol.conf     "$OUT/system/"          2>/dev/null || true
$SUDO cp -a /etc/hostname /etc/hosts   "$OUT/system/"          2>/dev/null && ok "hostname/hosts" || true
$SUDO cp -a /etc/netplan               "$OUT/system/netplan"   2>/dev/null && ok "netplan" || true
$SUDO cp -a /etc/fstab                 "$OUT/system/"          2>/dev/null && ok "fstab" || true
$SUDO cp -a /etc/wireguard             "$OUT/system/wireguard" 2>/dev/null && ok "wireguard" || true
$SUDO cp -a /etc/systemd/system        "$OUT/system/systemd-system" 2>/dev/null && ok "systemd (services custom)" || true
$SUDO cp -a /boot/extlinux/extlinux.conf "$OUT/system/"        2>/dev/null || true   # référence (regénéré au flash)

# ─── Cron ───
crontab -l        > "$OUT/cron/user-crontab" 2>/dev/null && ok "crontab user" || true
$SUDO crontab -l  > "$OUT/cron/root-crontab" 2>/dev/null && ok "crontab root" || true
$SUDO cp -a /etc/cron.d "$OUT/cron/cron.d"   2>/dev/null || true

# ─── Manifest ───
{
  echo "Backup Jetson   : $HOSTN"
  echo "Date            : $TS"
  echo "nv_tegra_release: $(head -1 /etc/nv_tegra_release 2>/dev/null)"
  echo "L4T (nvidia-l4t-core): $(dpkg-query --show nvidia-l4t-core 2>/dev/null | awk '{print $2}')"
  echo "Kernel          : $(uname -r)"
  echo "Ubuntu          : $(lsb_release -ds 2>/dev/null)"
  command -v jetson_release >/dev/null 2>&1 && { echo "--- jetson_release ---"; jetson_release 2>/dev/null; }
} > "$OUT/MANIFEST.txt"
ok "MANIFEST.txt"

# ─── Compression ───
log "Compression → $ARCHIVE"
$SUDO chown -R "$(id -u):$(id -g)" "$OUT" 2>/dev/null || true
tar -C "$STAGE" "${COMP[@]}" -cf "$STAGE/$ARCHIVE" "$TS" || { echo "❌ tar a échoué"; exit 1; }
SIZE="$(du -h "$STAGE/$ARCHIVE" | cut -f1)"
ok "Archive locale : $STAGE/$ARCHIVE ($SIZE)"

# ─── Envoi vers le NAS ───
log "Envoi vers $NAS_HOST:$NAS_DEST"
if ssh -o ConnectTimeout=8 "$NAS_HOST" "mkdir -p '$NAS_DEST'" 2>/dev/null; then
  if command -v rsync >/dev/null 2>&1; then
    rsync -avh --progress "$STAGE/$ARCHIVE" "$NAS_HOST:$NAS_DEST/" && SENT=1
  else
    scp "$STAGE/$ARCHIVE" "$NAS_HOST:$NAS_DEST/" && SENT=1
  fi
fi

echo
if [ "${SENT:-0}" = 1 ]; then
  echo "✅ Sauvegarde envoyée : $NAS_HOST:$NAS_DEST/$ARCHIVE ($SIZE)"
  echo "   Vérifie puis supprime le staging : rm -rf '$OUT'"
else
  echo "⚠ Envoi auto impossible (pas de clé Jetson→NAS ?). Archive prête ici :"
  echo "   $STAGE/$ARCHIVE"
  echo "   Récupère-la DEPUIS le NAS avec :"
  echo "   rsync -avh '$(whoami)@$(hostname -I | awk '{print $1}'):$STAGE/$ARCHIVE' '$NAS_DEST/'"
fi
