#!/usr/bin/env bash
#
# jetson-rebuild.sh — Remise en route post-reflash JetPack 7.2 (à lancer SUR le Jetson)
#
# À exécuter APRÈS jetson-restore.sh. Orchestre les 4 étapes :
#   1) apt (paquets manuels) + pip (hors paquets CUDA)
#   2) images Docker via jetson-containers (autotag/build + contournements JP7.2)
#   3) recompilation de TON code natif C++/CUDA (CUDA 12 → 13)
#   4) régénération des moteurs TensorRT depuis les ONNX (TRT 8/10 → 10.16)
#
# ORCHESTRATEUR : remplis les 3 tableaux DOCKER_PACKAGES / NATIVE_PROJECTS / TRT_MODELS
# (je ne connais pas ta liste exacte). Ré-exécutable, ne s'arrête pas à la 1re erreur.
#
#   ./jetson-rebuild.sh              # tout
#   ./jetson-rebuild.sh docker       # un seul module (preflight|apt|pip|docker|native|trt)
#
set -uo pipefail

# ═══ CONFIG ══════════════════════════════════════════════════════════════════

# Sauvegarde extraite (pour apt-manual.list / pip3-freeze.list). Optionnel.
BACKUP_SRC=""     # ex: /tmp/jetson-restore-1234/extract/20260707-101500

JC_DIR="${HOME}/jetson-containers"        # répertoire jetson-containers

# (Re)construire ces paquets via jetson-containers — À ADAPTER à tes profils
DOCKER_PACKAGES=( "pytorch" "opencv" "ros:jazzy-desktop" )

# Projets natifs : "chemin|commande de build" — À ADAPTER
NATIVE_PROJECTS=(
  # "${HOME}/ros2_ws|colcon build --symlink-install"
  # "${HOME}/opencv/build|cmake --build . -j$(nproc)"
)

# Moteurs TensorRT : "entree.onnx|sortie.engine|flags trtexec sup." — À ADAPTER
TRT_MODELS=(
  # "${HOME}/models/yolo.onnx|${HOME}/models/yolo.engine|--fp16"
)

# ── Contournements JP7.2 (support r39/cu132 encore jeune) ──
SKIP_TESTS=true                              # jetson-containers : éviter le harnais de tests
START_LOCAL_WHEELS=false                     # servir des tarballs SBSA locaux si l'index cu132 est incomplet
DOWNLOADS_DIR="${HOME}/jp72-downloads"       # y déposer les .tar.gz SBSA (depuis developer.nvidia.com)
LOCAL_PORT=8888
TENSORRT_TARBALL=""                          # ex: TensorRT-10.16.0.72.Linux.aarch64-gnu.cuda-13.2.tar.gz

# ── Interrupteurs d'étapes ──
DO_PREFLIGHT=true; DO_APT=true; DO_PIP=true; DO_DOCKER=true; DO_NATIVE=true; DO_TRT=true

# Paquets pip à NE PAS réinstaller (viennent de jetson-containers / index jetson-ai-lab)
PIP_EXCLUDE_RE='^(torch|onnxruntime|tensorrt|cupy|pycuda|numpy|nvidia-|cuda-|cudnn|jetson)'

# ═════════════════════════════════════════════════════════════════════════════

LOG="${HOME}/jetson-rebuild-$(date +%Y%m%d-%H%M%S).log"
exec > >(tee -a "$LOG") 2>&1
OKD=(); FAILED=()
SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"

sec()  { printf '\n\033[1;36m═══ %s ═══\033[0m\n' "$*"; }
ok()   { printf '  \033[1;32m✓\033[0m %s\n' "$*"; OKD+=("$*"); }
warn() { printf '  \033[1;33m⚠\033[0m %s\n' "$*"; }
fail() { printf '  \033[1;31m✗\033[0m %s\n' "$*"; FAILED+=("$*"); }

# ─── 1. Préflight ─────────────────────────────────────────────────────────────
preflight() {
  sec "Préflight JetPack 7.2"
  head -1 /etc/nv_tegra_release 2>/dev/null || warn "nv_tegra_release absent"
  local l4t cu drv
  l4t="$(dpkg-query --show nvidia-l4t-core 2>/dev/null | awk '{print $2}')"
  echo "  L4T    : ${l4t:-?}"
  case "$l4t" in 39.*) ok "L4T r39 (JetPack 7.x)";; *) warn "L4T attendu r39.x — vérifie le flash";; esac
  cu="$(grep -oP '"version"\s*:\s*"\K[0-9.]+' /usr/local/cuda/version.json 2>/dev/null | head -1)"
  echo "  CUDA   : ${cu:-?}"
  case "$cu" in 13.2*) ok "CUDA 13.2";; *) warn "CUDA attendu 13.2.x";; esac
  echo "  Ubuntu : $(lsb_release -ds 2>/dev/null)"
  echo "  Kernel : $(uname -r)"
  if command -v docker >/dev/null 2>&1; then
    if docker info 2>/dev/null | grep -qi 'Default Runtime: nvidia'; then ok "Docker default-runtime = nvidia"
    else warn "Docker default-runtime ≠ nvidia → ajoute \"default-runtime\":\"nvidia\" dans /etc/docker/daemon.json"; fi
  else warn "Docker absent"; fi
  drv="$(grep -oP 'Kernel Module\s+\K[0-9.]+' /proc/driver/nvidia/version 2>/dev/null | head -1)"
  [ -n "$drv" ] && echo "  Driver : $drv  (R595 mini requis pour CUDA 13.2 SBSA sur Orin)"
  df -h / | awk 'NR==2{print "  Disque : "$4" libre sur /"}'
}

# ─── 2. apt ───────────────────────────────────────────────────────────────────
reinstall_apt() {
  sec "Réinstallation apt (paquets manuels)"
  local list="${BACKUP_SRC}/apt/apt-manual.list"
  [ -f "$list" ] || { warn "apt-manual.list introuvable (BACKUP_SRC ?) — ignoré"; return 0; }
  $SUDO apt-get update -y || true
  local miss=() pkg
  while read -r pkg; do
    [ -z "$pkg" ] && continue
    if $SUDO apt-get install -y "$pkg" >/dev/null 2>&1; then printf '.'; else miss+=("$pkg"); fi
  done < "$list"; echo
  if [ ${#miss[@]} -gt 0 ]; then
    warn "${#miss[@]} paquets indisponibles sur 24.04 (à traiter à la main) → ~/apt-manquants.txt"
    printf '%s\n' "${miss[@]}" | tee "${HOME}/apt-manquants.txt" | sed 's/^/     /'
  else ok "Tous les paquets manuels réinstallés"; fi
}

# ─── 3. pip ───────────────────────────────────────────────────────────────────
reinstall_pip() {
  sec "Réinstallation pip (hors torch/onnxruntime/tensorrt…)"
  local list="${BACKUP_SRC}/pip/pip3-freeze.list"
  [ -f "$list" ] || { warn "pip3-freeze.list introuvable — ignoré"; return 0; }
  command -v pip3 >/dev/null 2>&1 || { warn "pip3 absent"; return 0; }
  grep -viE "$PIP_EXCLUDE_RE" "$list" > /tmp/pip-safe.txt || true
  echo "  $(wc -l < /tmp/pip-safe.txt) paquets (les CUDA sont exclus → voir jetson-ai-lab)"
  pip3 install --no-input -r /tmp/pip-safe.txt && ok "pip réinstallé" || warn "certains paquets pip ont échoué (voir log)"
  echo "  torch : uv pip install torch torchvision torchaudio --index-url https://pypi.jetson-ai-lab.io/sbsa/cu132"
}

# ─── 4. Docker (jetson-containers) ───────────────────────────────────────────
SERVER_PID=""
stop_wheels() { [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null; SERVER_PID=""; }
start_wheels() {
  [ "$START_LOCAL_WHEELS" = true ] || return 0
  [ -d "$DOWNLOADS_DIR" ] || { warn "DOWNLOADS_DIR absent — pas de serveur local"; return 0; }
  ( cd "$DOWNLOADS_DIR" && exec python3 -m http.server "$LOCAL_PORT" ) & SERVER_PID=$!
  ok "Serveur local tarballs :$LOCAL_PORT (pid $SERVER_PID)"
}
rebuild_docker() {
  sec "Images Docker (jetson-containers)"
  command -v docker >/dev/null 2>&1 || { fail "Docker absent"; return 1; }
  if [ ! -d "$JC_DIR" ]; then
    git clone --depth 1 https://github.com/dusty-nv/jetson-containers "$JC_DIR" || { fail "clone jetson-containers"; return 1; }
    bash "$JC_DIR/install.sh" || warn "install.sh a renvoyé une erreur"
  else
    git -C "$JC_DIR" pull --ff-only || warn "pull jetson-containers (ignoré)"
  fi
  local JC; JC="$(command -v jetson-containers || echo "$JC_DIR/jetson-containers")"
  export LSB_RELEASE=24.04
  local extra=(); [ "$SKIP_TESTS" = true ] && extra+=(--skip-tests all)
  local buildargs=()
  [ "$START_LOCAL_WHEELS" = true ] && [ -n "$TENSORRT_TARBALL" ] && \
    buildargs+=(--build-arg "TENSORRT_URL=http://localhost:${LOCAL_PORT}/${TENSORRT_TARBALL}")
  start_wheels
  local pkg
  for pkg in "${DOCKER_PACKAGES[@]}"; do
    echo "  → build $pkg"
    if "$JC" build "${extra[@]}" "${buildargs[@]}" "$pkg"; then ok "docker: $pkg"; else fail "docker: $pkg"; fi
  done
  stop_wheels
}

# ─── 5. Code natif ───────────────────────────────────────────────────────────
rebuild_native() {
  sec "Recompilation code natif (CUDA 12 → 13)"
  [ ${#NATIVE_PROJECTS[@]} -gt 0 ] || { warn "NATIVE_PROJECTS vide — rien à compiler"; return 0; }
  local entry path cmd
  for entry in "${NATIVE_PROJECTS[@]}"; do
    path="${entry%%|*}"; cmd="${entry#*|}"
    echo "  → $path : $cmd"
    if [ -d "$path" ] && ( cd "$path" && bash -c "$cmd" ); then ok "natif: $path"; else fail "natif: $path"; fi
  done
}

# ─── 6. Moteurs TensorRT ─────────────────────────────────────────────────────
rebuild_trt() {
  sec "Régénération moteurs TensorRT"
  [ ${#TRT_MODELS[@]} -gt 0 ] || { warn "TRT_MODELS vide — rien à régénérer"; return 0; }
  local trtexec; trtexec="$(command -v trtexec || echo /usr/src/tensorrt/bin/trtexec)"
  [ -x "$trtexec" ] || { fail "trtexec introuvable ($trtexec)"; return 1; }
  local entry onnx eng flags
  for entry in "${TRT_MODELS[@]}"; do
    IFS='|' read -r onnx eng flags <<< "$entry"
    echo "  → $(basename "$onnx") → $(basename "$eng") $flags"
    if [ -f "$onnx" ] && "$trtexec" --onnx="$onnx" --saveEngine="$eng" $flags; then ok "trt: $(basename "$eng")"; else fail "trt: $(basename "$eng")"; fi
  done
}

# ─── Orchestration ───────────────────────────────────────────────────────────
if [ $# -gt 0 ]; then
  DO_PREFLIGHT=false DO_APT=false DO_PIP=false DO_DOCKER=false DO_NATIVE=false DO_TRT=false
  for a in "$@"; do case "$a" in
    preflight) DO_PREFLIGHT=true;; apt) DO_APT=true;; pip) DO_PIP=true;;
    docker) DO_DOCKER=true;; native) DO_NATIVE=true;; trt) DO_TRT=true;;
    *) echo "module inconnu: $a (preflight|apt|pip|docker|native|trt)"; exit 2;; esac; done
fi

trap stop_wheels EXIT
[ "$DO_PREFLIGHT" = true ] && preflight
[ "$DO_APT"       = true ] && reinstall_apt
[ "$DO_PIP"       = true ] && reinstall_pip
[ "$DO_DOCKER"    = true ] && rebuild_docker
[ "$DO_NATIVE"    = true ] && rebuild_native
[ "$DO_TRT"       = true ] && rebuild_trt

sec "Rapport"
echo "  Log : $LOG"
printf '  \033[1;32mOK (%d)\033[0m  ' "${#OKD[@]}"
if [ ${#FAILED[@]} -gt 0 ]; then
  printf '\033[1;31m| ÉCHECS (%d)\033[0m\n' "${#FAILED[@]}"
  printf '     ✗ %s\n' "${FAILED[@]}"
  exit 1
fi
echo "| ✅ tout est passé"
