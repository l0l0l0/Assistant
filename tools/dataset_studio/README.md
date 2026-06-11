# PCB Dataset Studio

Wizard de préparation de dataset + entraînement du détecteur de composants
pour MicroscopeIBOM. Fonctionne sur **Windows et Linux (Ubuntu)**. Basé sur
les modules génériques YOLO de
[Pokemon-Dataset-Creator](https://github.com/lo26lo/Pokemon-Dataset-Creator)
(vendorisés dans `studio/vendor/`).

> Plan complet : [docs/DATASET_STUDIO_PLAN.md](../../docs/DATASET_STUDIO_PLAN.md)

## Installation

### Windows (PC RTX 5070 Ti)

1. Python 3.10+ installé depuis python.org (cocher "Add to PATH" **ou** laisser le launcher `py`)
2. Double-clic `INSTALL.bat` (crée `.venv` + dépendances de base)
3. Double-clic `START.bat`

### Linux / Ubuntu (PC RTX 5070 Ti)

```bash
sudo apt install python3 python3-venv python3-tk   # tkinter requis pour la GUI
cd tools/dataset_studio
chmod +x install.sh start.sh install_training.sh
./install.sh
./start.sh
```

⚠️ Pour l'entraînement (Lot 2) : la RTX 5070 Ti (Blackwell, sm_120) exige
**PyTorch ≥ 2.7 en CUDA 12.8** :

| OS | Commande |
|----|---------|
| Windows | `install_training.bat` |
| Linux | `./install_training.sh` |

## État — Lots 1+2 (actuels)

| Étape | Statut |
|-------|--------|
| 0 · Projet (workdir, accès Jetson) | ✅ |
| 1 · Import sessions (dossier local) + générateur factice | ✅ |
| 2 · Validation (rapports HTML, stats classes, aperçu bboxes) | ✅ |
| 3 · Split **par session** (train.txt/val.txt/data.yaml) | ✅ |
| 4 · Entraînement (presets rapide/standard/précis, check GPU Blackwell, log par epoch) | ✅ |
| 5 · Test, export ONNX, déploiement Jetson | 🚧 Lot 3 |

## Test rapide sans vraies données

Étape 1 → « Générer un dataset factice » (2 sessions × 30 images) →
Étape 2 → « Lancer la validation » → ouvrir l'aperçu bboxes : les boîtes
doivent coller exactement aux rectangles colorés.

## Layout attendu des données (produit par la capture Jetson, Phase A)

```
<source>/
  session_xxx/
    images/frame_000001.jpg ...
    labels/frame_000001.txt ...   # YOLO: class cx cy w h normalisés
    manifest.jsonl                # optionnel: tags zoom/éclairage/board
```

Les classes sont définies dans `config/pcb_classes.json` — **même liste
ordonnée** que `resources/footprint_classes.json` côté Jetson (ne pas réordonner).
