# =============================================================================
#  microscope-ibom:dev
#  Image de developpement — base + outils de build/debug
# =============================================================================
#
#  Build:
#    docker compose -f docker/compose.yml build dev
#
#  Usage interactif:
#    docker compose -f docker/compose.yml up -d dev
#    docker compose -f docker/compose.yml exec dev bash
#
# =============================================================================

FROM microscope-ibom:base

ENV DEBIAN_FRONTEND=noninteractive

# -----------------------------------------------------------------------------
#  Outils developpement
# -----------------------------------------------------------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
    gdb \
    valgrind \
    clang-format \
    clang-tidy \
    cppcheck \
    ccache \
    strace \
    ltrace \
    htop \
    tmux \
    vim nano less \
    bash-completion \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# -----------------------------------------------------------------------------
#  vcpkg (OPT-IN — desactive par defaut sur Jetson)
#
#  Sur Jetson, toutes les deps du projet sont satisfaites par les paquets
#  apt systeme installes dans base.Dockerfile (Qt6, OpenCV CUDA, spdlog, etc.)
#  donc vcpkg n'est pas necessaire et le bootstrap-vcpkg.sh echoue souvent
#  sur la cible ARM64 (binaire vcpkg-tool pre-compile pas dispo, fallback
#  compilation from source qui demande des outils manquants).
#
#  Pour activer vcpkg malgre tout :
#      docker compose build dev --build-arg INSTALL_VCPKG=true
#
#  Le scripts/build_jetson.sh detecte automatiquement la presence/absence
#  de VCPKG_ROOT et active le toolchain en consequence.
# -----------------------------------------------------------------------------
ARG INSTALL_VCPKG=false
RUN if [ "$INSTALL_VCPKG" = "true" ]; then \
        git clone --depth 1 https://github.com/microsoft/vcpkg.git /opt/vcpkg && \
        /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics ; \
    fi
# VCPKG_ROOT n'est exporte QUE si vcpkg a ete installe (sinon
# build_jetson.sh fallback proprement sur les paquets apt systeme).

# -----------------------------------------------------------------------------
#  Configuration ccache (accelere les rebuilds)
# -----------------------------------------------------------------------------
ENV CCACHE_DIR=/root/.ccache
ENV PATH="/usr/lib/ccache:${PATH}"
RUN mkdir -p ${CCACHE_DIR}

# -----------------------------------------------------------------------------
#  Aliases pratiques
# -----------------------------------------------------------------------------
RUN echo 'alias ll="ls -lah"' >> /root/.bashrc && \
    echo 'alias cmake-debug="cmake -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug"' >> /root/.bashrc && \
    echo 'alias cmake-release="cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release"' >> /root/.bashrc && \
    echo 'export PS1="\[\033[01;32m\][dev]\[\033[00m\] \[\033[01;34m\]\w\[\033[00m\] \$ "' >> /root/.bashrc

WORKDIR /opt/microscope-ibom

CMD ["/bin/bash"]
