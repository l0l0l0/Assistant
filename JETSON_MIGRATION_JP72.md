# Migration JetPack 6.2 → 7.2 — MicroscopeIBOM (Jetson AGX Orin 32GB)

**Date :** 2026-07-07
**Cible matérielle :** Jetson AGX Orin 32GB (Seeed) — **reflash complet obligatoire**
**Depuis :** JetPack 6.2 — L4T r36.4, Ubuntu 22.04, CUDA 12.x, TensorRT 8.6–10.3
**Vers :** JetPack 7.2 — L4T r39.2 (sortie 02/06/2026), Ubuntu 24.04, CUDA 13.2.1, cuDNN 9.20, TensorRT 10.16.2, driver **R595** minimum

---

## 1. TL;DR

JP6.2 → JP7.2 est un **saut de plateforme majeur**, pas une mise à jour de paquets : nouvel Ubuntu, nouveau CUDA, nouveau TensorRT, passage par défaut de Xorg à Wayland. **Aucun upgrade en place** : il faut reflasher, donc **sauvegarder hors machine** au préalable.

Le vrai travail se concentre sur **l'image Docker de base** : NVIDIA **ne publie plus** de conteneur `l4t-jetpack` pour JP7. On bascule sur le conteneur **TensorRT SBSA** officiel, qui embarque déjà CUDA + cuDNN + TensorRT. Le reste du pipeline du dépôt (`bootstrap_jetson.sh`, `build_jetson.sh`) ne change pas.

| Composant | JP6.2 (avant) | JP7.2 (cible) | Remarque |
|---|---|---|---|
| Base conteneur | `nvcr.io/nvidia/l4t-jetpack:r36.4.0` | `nvcr.io/nvidia/tensorrt:26.05-py3` | l4t-jetpack **retiré** sur JP7 |
| OS conteneur | Ubuntu 22.04 | Ubuntu 24.04 | transition paquets (t64) |
| CUDA | 12.x | 13.2.1 | Orin bascule sur **CUDA SBSA** |
| cuDNN | fourni par l4t | 9.20 (via conteneur) | à vérifier présent |
| TensorRT | 8.6–10.3 | 10.16.1 (conteneur) / 10.16.2 (hôte) | même minor 10.16, compatible |
| OpenCV | 4.10.0 | **4.13.0** | la 4.12 casse avec CUDA 13 |
| ONNX Runtime | v1.19.2 | **v1.23.0** | 1re version avec CUDA 13 + TRT EP |
| librealsense | v2.55.1 | v2.55.1 | bumper si build casse (noyau 6.8) |
| CUDA_ARCH_BIN | 8.7 | **8.7 (inchangé)** | Orin = Ampere sm_87 |

---

## 2. Nature de la migration

- **Reflash complet** : le NVMe est effacé. La sauvegarde doit donc partir vers le NAS.
- **Ubuntu 22.04 → 24.04** : les paquets apt ne se restaurent pas tels quels (distro différente + transition `t64`).
- **CUDA 12 → 13**, **TensorRT bump** : tout binaire compilé contre CUDA 12 est à recompiler ; les moteurs TensorRT sont à régénérer.
- **Xorg → Wayland par défaut** : l'app force `QT_QPA_PLATFORM=xcb`, à surveiller.
- **Orin passe sur CUDA SBSA** en 13.2 (unification serveur/edge) — c'est ce qui rend utilisables les conteneurs Arm standard (`cuda`, `tensorrt`) sur le Jetson.

---

## 3. Ce qui se restaure vs ce qui se rebuild

| Catégorie | Exemples | Action |
|---|---|---|
| **Rien à faire** | Code Python pur, données, configs, modèles `.onnx` | Réinstaller les deps pip, copier les fichiers |
| **À régénérer (pas de la compil)** | Moteurs TensorRT `.engine` | **Automatique** : cache runtime (voir §7). Il suffit de vider `data/tensorrt-cache/` |
| **À rebuild (automatisé)** | Images Docker `base`/`dev`/`runtime` | `docker compose build` via `bootstrap_jetson.sh` |
| **Vraie recompilation** | Code natif C++/CUDA (l'app) | `build_jetson.sh` **dans le conteneur dev** (CMake/Ninja) |

> Note : le projet **ne dépend pas** de `dusty-nv/jetson-containers` pour les images — il build ses propres `microscope-ibom:base/dev/runtime` depuis `docker/*.Dockerfile`. La bascule de base se fait donc dans **ces** fichiers.

---

## 4. Point critique : l'image de base Docker

### 4.1 Le constat

- Le conteneur `nvcr.io/nvidia/l4t-jetpack` **n'a aucun tag r39/JP7** sur NGC (tag le plus élevé publié : `r36.4.0`, vérifié directement sur le registre).
- **Confirmé par NVIDIA** (forum développeurs, modérateur DaneLLL) : le conteneur `l4t-jetpack` **n'est pas maintenu** sur cette génération. Alternatives officielles : `nvcr.io/nvidia/cuda`, `nvcr.io/nvidia/tensorrt`, `nvcr.io/nvidia/deepstream-l4t`.
- **Distinction importante** : le **SDK/BSP/ISO** JetPack 7.2 existe bel et bien (c'est ce qui sert à flasher). C'est un canal **différent** de l'image conteneur de base sur NGC.

### 4.2 La solution retenue

Base = **`nvcr.io/nvidia/tensorrt:26.05-py3`** (arm64 / SBSA, Ubuntu 24.04). Elle embarque **CUDA 13.2.1 + cuDNN + TensorRT 10.16.1**, ce qui remplace exactement ce que fournissait `l4t-jetpack` — et **supprime l'étape d'installation de TensorRT** qui aurait été nécessaire avec une base `cuda` nue.

### 4.3 Choix du tag (release notes du conteneur TensorRT)

| Tag | TensorRT | CUDA | Verdict |
|---|---|---|---|
| `26.03-py3` | 10.16.0.72 | 13.2.0 | OK (patch antérieur) |
| `26.04-py3` | 10.16.1.11 | 13.2.1 | OK |
| **`26.05-py3`** | **10.16.1.11** | **13.2.1** | ✅ **recommandé** — aligné sur JP7.2 |
| `26.06-py3` | — | **13.3.0** | ❌ **à éviter** : CUDA 13.3 désaligné du driver R595 (CUDA 13.2) |
| `*-py3-igpu` | — | — | variante iGPU **retirée après `26.02`** (unification SBSA) → utiliser `-py3` |

> Le conteneur est autonome pour CUDA/TensorRT (seul le driver vient de l'hôte). La 10.16.1 du conteneur tourne donc sans souci sur le driver R595 de JP7.2, et les moteurs sont compilés en interne avec la TRT du conteneur.

---

## 5. Modifications des fichiers

### 5.1 `docker/base.Dockerfile`

- Bloc `ARG` : `L4T_VERSION` remplacé par `BASE_IMAGE` + bump des versions.

```dockerfile
ARG BASE_IMAGE=nvcr.io/nvidia/tensorrt:26.05-py3
ARG OPENCV_VERSION=4.13.0
ARG REALSENSE_VERSION=v2.55.1
ARG ONNXRUNTIME_VERSION=v1.23.0
ARG CUDA_ARCH_BIN=8.7          # Orin = Ampere sm_87, inchangé
```

- Les **4** `FROM nvcr.io/nvidia/l4t-jetpack:${L4T_VERSION}` → `FROM ${BASE_IMAGE}`.
- Stage ONNX Runtime :
  - `--cuda_version 12.6` → `--cuda_version 13.2`
  - `--cudnn_home /usr/lib/aarch64-linux-gnu` → `--cudnn_home /usr` *(à confirmer selon l'emplacement dans le conteneur)*
  - `--tensorrt_home /usr` conservé (TensorRT est dans la base).
- **Suppression** de toute étape d'installation de TensorRT (fournie par la base).
- Stage final : noms de paquets runtime adaptés à Ubuntu 24.04 (voir §6).
- Commentaire Qt : « Ubuntu 22.04 — Qt 6.2 » → « Ubuntu 24.04 — Qt 6.4 ».

### 5.2 `docker/compose.yml`

```yaml
    build:
      context: ..
      dockerfile: docker/base.Dockerfile
      args:
        BASE_IMAGE: nvcr.io/nvidia/tensorrt:26.05-py3   # remplace L4T_VERSION
        OPENCV_VERSION: 4.13.0
        REALSENSE_VERSION: v2.55.1
        ONNXRUNTIME_VERSION: v1.23.0
        CUDA_ARCH_BIN: "8.7"
```

> Le tag de base était **codé en dur** dans `compose.yml` — c'est **ici** le vrai levier. Modifier la variable d'env du bootstrap ne suffit pas : le build de la base lit l'`arg` du compose.

### 5.3 `scripts/bootstrap_jetson.sh`

- Défaut `L4T_VERSION=r36.4.0` : ne concerne plus que l'image de test runtime (`docker run … l4t-jetpack`). Comme cette image n'existe pas en r39, **retirer/remplacer** ce test ou le pointer vers `tensorrt:26.05-py3`.
- Le reste (MAXN, docker + nvidia-container-toolkit, clone/pull, udev RealSense, build `base`/`dev`) **reste valable**.

---

## 6. Points de vigilance (`## CHECK`)

1. **cuDNN présent ?** Le conteneur `tensorrt` hérite du CUDA-DL → cuDNN devrait être là. Vérifier :
   ```bash
   ls /usr/include/cudnn_version.h && dpkg -l | grep -i cudnn
   ```
   Ajuster `--cudnn_home` si nécessaire.

2. **Noms de paquets apt Ubuntu 24.04 (noble)** — c'est le point le plus susceptible de casser au build. Sonames FFmpeg `58→60`, `libswscale5→7`, `libtiff5→6`, et **transition `t64`** : `libssl3 → libssl3t64`, `libpng16-16 → libpng16-16t64` (?). Vérifier chaque lib :
   ```bash
   apt-cache search libavcodec ; apt-cache search libssl3
   ```

3. **Hash Eigen (ONNX Runtime)** : le workaround `sed` sur `cmake/deps.txt` visait les versions 1.19–1.22. Revérifier la ligne `eigen;` pour la **v1.23.0** (le SHA1 servi par GitLab peut différer).

4. **Décodage HW GStreamer (`nvvidconv`)** : **absent** de la base SBSA (multimédia Jetson non inclus). Si l'app en dépend, installer la **Jetson Multimedia API R39.2.0** dans le conteneur (`Jetson_Multimedia_API_R39.2.0_aarch64.tbz2`, depuis la page de téléchargement JP7.2).

5. **`network: host` au build** : contournait le bug `iptable_raw` du noyau Tegra + Docker (JP6.2). Le noyau 6.8 de JP7 devrait le régler → probablement **inutile**, mais inoffensif si conservé.

6. **Wayland par défaut** : l'app force `QT_QPA_PLATFORM=xcb`. Vérifier l'affichage/le tactile après flash.

---

## 7. Moteurs TensorRT — cache runtime (pas de `trtexec`)

`src/ai/InferenceEngine.cpp` active le cache moteur de l'EP TensorRT d'ONNX Runtime :
`trt_engine_cache_enable = 1`, chemin `dataDir()/tensorrt-cache`. Les moteurs sont donc **compilés à l'exécution** depuis les `.onnx` de `models/` et mis en cache sous `IBOM_DATA_DIR`.

**Conséquence pour la migration** : aucun script `trtexec` à écrire. Après le flash (TensorRT 10.16), il suffit de **vider le cache** pour forcer la recompilation des moteurs :

```bash
rm -rf data/tensorrt-cache/*
```

Les `.onnx` (`component_detector`, `solder_inspector`, `ocr_model`) restent la source ; les `.engine` se régénèrent au premier lancement.

---

## 8. Sauvegarde pré-reflash

À sauvegarder **vers le NAS** avant de flasher (scripts `jetson-backup.sh` / `jetson-restore.sh`) :

| Se restaure proprement | Référence seulement (à réinstaller) |
|---|---|
| `~/.ssh` (clés, config, known_hosts) | Liste apt (`apt-mark showmanual`) |
| Historique shell, dotfiles | Liste pip (`pip freeze`) |
| Données du home, projets | Liste des images Docker |
| Volumes Docker (données) | Config Jetson (`nvpmodel`, netplan, wireguard, cron) |
| `/etc/docker/daemon.json` (runtime nvidia) | |

> Les **images Docker** ne sont pas sauvegardées : liées à CUDA 12 / L4T 36, inutilisables sur CUDA 13. Elles se reconstruisent via le pipeline du dépôt.

---

## 9. Procédure de migration pas-à-pas

1. **Sauvegarde** vers le NAS (`jetson-backup.sh`), avec `STOP_DOCKER=true` si des volumes stateful sont actifs.
2. **Flash JP7.2** (SDK Manager / ISO), driver **R595** minimum. Vérifier après boot : `head -1 /etc/nv_tegra_release`, `nvcc --version`.
3. **Restauration portable** (`jetson-restore.sh`) : ssh, dotfiles, home, volumes, `daemon.json`.
4. **Éditer `docker/compose.yml`** : `BASE_IMAGE` + versions (§5.2). Idem `base.Dockerfile` (§5.1).
5. **Relancer le bootstrap** :
   ```bash
   cd ~/Assistant-git && bash scripts/bootstrap_jetson.sh
   ```
   (build `base` ~90–120 min, puis `dev`).
6. **Compiler l'app** dans le conteneur :
   ```bash
   bash docker/run-dev.sh
   # dans le conteneur :
   bash scripts/build_jetson.sh
   ```
7. **Vider le cache TensorRT** : `rm -rf data/tensorrt-cache/*` (régénération sous TRT 10.16 au 1er run).
8. **(Si requis)** installer la **Jetson Multimedia API R39.2.0** pour le décodage HW.
9. Valider : affichage (Wayland/xcb), caméras (RealSense/USB), inférence.

---

## 10. Risques résiduels (par ordre de probabilité)

1. **Noms de paquets Ubuntu 24.04** au build de `base` (transition `t64`, sonames).
2. **cuDNN / ONNX Runtime ↔ CUDA 13** : premier build de l'ORT 1.23 contre CUDA 13 + TRT 10.16.
3. **OpenCV-CUDA ↔ CUDA 13** : la 4.13 compile, mais c'est un build from source lourd.
4. **Multimédia HW** (`nvvidconv`) si l'app en dépend.

---

## 11. Références

- JetPack 7.2 — téléchargements & notes : https://developer.nvidia.com/embedded/jetpack/downloads/archive-7.2
- Forum NVIDIA — l4t-jetpack absent sur NGC (réponse officielle) : https://forums.developer.nvidia.com/t/cant-find-specified-l4t-jetpack-container-in-ngc/374302
- TensorRT — Container Release Notes (mapping tag ↔ CUDA/TRT) : https://docs.nvidia.com/deeplearning/frameworks/container-release-notes/index.html
- Conteneur TensorRT sur NGC : https://catalog.ngc.nvidia.com/orgs/nvidia/containers/tensorrt
- ONNX Runtime — releases (support CUDA 13) : https://github.com/microsoft/onnxruntime/releases
- OpenCV 4.13 + CUDA 13 sur Ubuntu 24.04 (retour d'expérience) : https://medium.com/@scofield44165/ubuntu-24-04-1-install-opencv-4-13-0-with-cuda-13-0-and-cudnn-9-16-0-4a22c2e535ae
