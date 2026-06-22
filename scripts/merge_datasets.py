#!/usr/bin/env python3
# =============================================================================
#  merge_datasets.py — fusionne plusieurs sources YOLO en UN dataset prêt à
#  entraîner (piste A : pré-entraînement public + fine-tune sur nos cartes).
#
#  Sources acceptées (mélangeables) :
#   • un dataset YOLO classique (dossier avec .../images/* et .../labels/*),
#     p.ex. la sortie de fetch_roboflow_dataset.py APRÈS remap_classes.py ;
#   • une (ou des) session(s) DatasetCreator : $IBOM_DATA_DIR/dataset/session_*/
#     (images/ + labels/), déjà dans nos 14 classes.
#
#  ⚠️ Toutes les sources doivent partager le MÊME espace de classes (mêmes ids).
#     -> remappe les datasets publics AVANT (remap_classes.py).
#
#  Usage :
#    # 14 classes (défaut) : public déjà remappé + nos sessions
#    python3 scripts/merge_datasets.py \
#        --out datasets/merged \
#        datasets/smd  ~/.local/share/MicroscopeIBOM/dataset/session_2026-06-20
#
#    # espace "présence" (1 classe) :
#    python3 scripts/merge_datasets.py --presence --out datasets/merged_presence \
#        datasets/smd_presence  $IBOM_DATA_DIR/dataset/session_xxx
#
#  Sortie : out/{images,labels}/{train,val} + out/data.yaml
#  Split déterministe (hash du nom de fichier) -> reproductible.
# =============================================================================

import argparse
import hashlib
import shutil
import sys
from pathlib import Path

# Même liste ordonnée que pcb_classes.json / footprint_classes.json.
PROJECT_CLASSES = [
    "resistor", "capacitor", "inductor", "diode", "led", "transistor_sot",
    "ic_soic", "ic_qfp", "ic_qfn", "ic_bga", "connector", "crystal",
    "button", "other",
]

IMG_EXTS = (".jpg", ".jpeg", ".png", ".bmp")


def find_pairs(source: Path):
    """Renvoie une liste de (image_path, label_path) pour une source.

    Convention YOLO : un label .../labels/.../X.txt correspond à une image
    .../images/.../X.<ext>. On déduit l'image en remplaçant le segment
    'labels' par 'images'.
    """
    pairs = []
    for lbl in source.rglob("*.txt"):
        parts = list(lbl.parts)
        if "labels" not in parts:
            continue
        # Construit le chemin image équivalent.
        idx = len(parts) - 1 - parts[::-1].index("labels")
        img_parts = parts.copy()
        img_parts[idx] = "images"
        stem = Path(img_parts[-1]).stem
        img_dir = Path(*img_parts[:-1])
        img = None
        for ext in IMG_EXTS:
            cand = img_dir / (stem + ext)
            if cand.exists():
                img = cand
                break
        if img is None:
            continue
        pairs.append((img, lbl))
    return pairs


def split_is_val(key: str, val_ratio: float) -> bool:
    """Split déterministe : hash stable du nom -> bucket [0,1)."""
    h = hashlib.md5(key.encode("utf-8")).hexdigest()
    frac = int(h[:8], 16) / 0xFFFFFFFF
    return frac < val_ratio


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Fusionne des sources YOLO en un dataset train/val unique.")
    ap.add_argument("sources", nargs="+", help="Dossiers sources (YOLO ou session_*)")
    ap.add_argument("--out", required=True, help="Dossier de sortie")
    ap.add_argument("--val", type=float, default=0.1,
                    help="Fraction de validation (défaut : %(default)s)")
    ap.add_argument("--presence", action="store_true",
                    help="Espace de classes 'présence' (1 classe component)")
    ap.add_argument("--names",
                    help="Liste de noms de classes séparés par des virgules "
                         "(défaut : nos 14 classes, ou 'component' si --presence)")
    args = ap.parse_args()

    if args.presence and args.names:
        print("ERREUR : --presence et --names sont exclusifs.", file=sys.stderr)
        return 2
    if args.presence:
        names = ["component"]
    elif args.names:
        names = [n.strip() for n in args.names.split(",") if n.strip()]
    else:
        names = list(PROJECT_CLASSES)

    out = Path(args.out)
    for split in ("train", "val"):
        (out / "images" / split).mkdir(parents=True, exist_ok=True)
        (out / "labels" / split).mkdir(parents=True, exist_ok=True)

    counts = {"train": 0, "val": 0}
    seen = set()
    for si, src in enumerate(args.sources):
        srcp = Path(src)
        if not srcp.exists():
            print(f"AVERTISSEMENT : source introuvable, ignorée : {srcp}",
                  file=sys.stderr)
            continue
        pairs = find_pairs(srcp)
        print(f"[merge] {srcp} : {len(pairs)} paires image/label")
        # Tag de source pour éviter les collisions de noms entre datasets.
        tag = f"s{si}_{srcp.name}".replace(" ", "_")
        for img, lbl in pairs:
            dst_stem = f"{tag}__{img.stem}"
            if dst_stem in seen:
                continue
            seen.add(dst_stem)
            split = "val" if split_is_val(dst_stem, args.val) else "train"
            shutil.copy2(img, out / "images" / split / (dst_stem + img.suffix))
            shutil.copy2(lbl, out / "labels" / split / (dst_stem + ".txt"))
            counts[split] += 1

    total = counts["train"] + counts["val"]
    if total == 0:
        print("ERREUR : aucune paire image/label trouvée dans les sources.",
              file=sys.stderr)
        return 1

    data_yaml = out / "data.yaml"
    with open(data_yaml, "w", encoding="utf-8") as f:
        f.write(f"path: {out.resolve()}\n")
        f.write("train: images/train\n")
        f.write("val: images/val\n")
        f.write(f"nc: {len(names)}\n")
        f.write("names: [" + ", ".join(f"'{n}'" for n in names) + "]\n")

    print(f"[merge] OK : train={counts['train']} val={counts['val']} "
          f"(total {total})")
    print(f"[merge] data.yaml -> {data_yaml}")
    print("[merge] Entraînement : "
          f"python3 scripts/train_yolo.py {data_yaml} "
          "--model yolov8m.pt --name component_detector")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
