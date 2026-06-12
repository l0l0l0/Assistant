# Idées d'améliorations & nouvelles features — MicroscopeIBOM

> **Date de l'analyse** : 2026-06-12
> **Contexte** : demande utilisateur « trouve des améliorations et des nouvelles features ». Analyse complète de `src/`, `tools/`, `tests/`, config, GUI — en excluant volontairement tout ce qui est **déjà planifié** ailleurs (Phases B/C/D dataset → [DATASET_CREATOR_PLAN.md](DATASET_CREATOR_PLAN.md), Phase 2d/2.5/3 → [JETSON_MIGRATION.md](JETSON_MIGRATION.md), audit infra → [JETSON_AMELIORATIONS.md](JETSON_AMELIORATIONS.md), roadmap → [PROCHAINE_SESSION.md](PROCHAINE_SESSION.md)).
>
> Tout ce qui suit est donc **nouveau** ou non couvert par les plans existants. Tableau de priorisation en fin de document.

---

## 1. 🔴 Corrections / risques découverts (quick wins)

### 1.1 `IBomParser` : boucle `while(true)` sans garde-fou (`src/ibom/IBomParser.cpp:389`)
La décompression LZ-String tourne en boucle infinie potentielle : aucun plafond d'itérations. Un fichier iBOM HTML corrompu (ou tronqué par un transfert) peut bloquer l'app ou la faire exploser en mémoire. **Fix** : garde max-itérations (ex. taille décompressée ≤ 100 × taille compressée) + erreur propre. ~30 min, avec test sur HTML tronqué.

### 1.2 Le slider de confiance IA n'est connecté à rien
`ControlPanel` a un spinner de confiance (0.05–1.0) mais il n'est **jamais câblé** au `ComponentDetector` (seuils hardcodés 0.5/NMS 0.45 dans `ComponentDetector.h:42-43`). Dès que le modèle v1 existera, le réglage ne fera rien. **Fix** : connexion + persistance dans `Config` (section `ai.*`). ~30 min.

### 1.3 `aiStatusChanged` jamais consommé par la GUI
`Application` émet `aiStatusChanged(bool, QString)` (init IA en arrière-plan) mais aucun widget ne l'affiche — l'utilisateur ne sait pas si le détecteur est prêt, en cours de compilation d'engine TRT (plusieurs minutes au 1er lancement !), ou en erreur. **Fix** : indicateur dans la status bar + `StatsPanel` (« IA : chargement… / prête / absente »). ~30 min, déjà tout câblé côté backend.

### 1.4 Micro-perfs dans le chemin chaud d'affichage
- `Application.cpp:461` : `qimg.copy()` à **chaque frame** (allocation + memcpy 1080p inutile dans la plupart des cas).
- `HeatmapRenderer.cpp:74,79` : `background.clone()` par frame quand la heatmap est active.
- `InferenceEngine.cpp:27` : `SetIntraOpNumThreads(4)` hardcodé — l'Orin a 8 cœurs (et le fallback CPU s'en servira) → `std::thread::hardware_concurrency()`.

À mesurer avant/après avec les timings spdlog existants. ~1 h le tout.

---

## 2. 🟠 Features dormantes — le code existe, il manque ~le câblage

> 7 modules sont **quasi complets** dans le repo mais jamais instanciés dans `Application.cpp`. Classés ici par valeur pour l'usage Jetson/atelier.

### 2.1 RemoteView — flux caméra + overlay dans le navigateur ⭐ recommandé
`src/features/RemoteView.h` : serveur WebSocket MJPEG (port 8080, qualité/FPS configurables, page HTML viewer générée). **Parfait pour le Jetson** : voir le microscope depuis le PC/la tablette de l'atelier sans toucher à X11, montrer la carte à quelqu'un à distance, ou travailler écran éteint. Les clés `remoteViewEnabled`/`remoteViewPort` existent déjà dans `Config` — il manque : instanciation dans `Application`, case + port dans `SettingsDialog`, et `network_mode: host` du compose rend le port directement accessible. **Effort : ½ session.**

### 2.2 ReportGenerator — bouton « Générer rapport »
`src/export/ReportGenerator.h` : rapport PDF/HTML d'inspection (stats, défauts, checklist BOM, yield). `DataExporter` est déjà instancié — il manque juste le bouton/menu et l'appel. C'est la sortie « livrable » d'une session d'inspection. **Effort : ½ session** (dont test du rendu PDF libharu sur ARM64).

### 2.3 BarcodeScanner + association carte → iBOM ⭐ combo nouvelle feature
`src/features/BarcodeScanner.h` : détection QR/Code128/DataMatrix via ZXing-cpp — **déjà compilé dans l'image Docker base**. Câblage simple, mais la vraie valeur est le combo : scanner le QR/code-barres d'une carte → **charger automatiquement le bon fichier iBOM** (table `barcode → chemin iBOM` persistée dans la config, alimentée à la première association). Au poste de travail : poser la carte, scan, overlay prêt. **Effort : 1 session.**

### 2.4 OCREngine / SolderInspector
Complets côté code (`src/ai/`), mais dépendent chacun d'un **modèle** dédié (OCR + classif joints). À garder pour après le detector v1 — noté ici pour mémoire, le câblage suivra le même pattern `initializeAI()`.

### 2.5 StencilAlign / VoiceControl
- `StencilAlign` (fiducials + erreur en mm) : utile seulement si tu fais de la pâte à braser au pochoir — à câbler à la demande.
- `VoiceControl` : squelette seulement (TODO `VoiceControl.cpp:101`). Mais l'idée a du sens **au microscope, les deux mains prises par la carte** : « composant suivant », « marquer placé », « snapshot ». Sur Jetson, whisper.cpp tourne bien en ARM64+CUDA. Gros morceau (≥ 2 sessions), à décider plus tard.

---

## 3. 🟡 Nouvelles features (absentes de tous les plans)

### 3.1 Focus assist — indicateur de netteté live ⭐ très utile au microscope
La métrique de netteté (variance du Laplacien) **existe déjà** dans les gates du `DatasetCreator` — mais elle n'est visible que pendant une capture dataset. L'extraire en indicateur permanent (barre/valeur dans `StatsPanel`, optionnellement bordure colorée dans `CameraView`) transforme la mise au point du microscope : on tourne la molette jusqu'au pic, fini le jugé à l'œil. **Effort : ½ session.**

### 3.2 Fichiers récents + rechargement auto du dernier iBOM
`Config::ibomFilePath()` existe mais n'est jamais pré-rempli : à chaque démarrage il faut re-naviguer vers le HTML. **Fix** : menu « Fichiers récents » (5 entrées) + option « recharger le dernier iBOM au démarrage ». Couplé à 2.3, l'app devient zéro-clic pour les cartes connues. **Effort : ~2 h.**

### 3.3 Restauration de session d'inspection
`MainWindow` sauve la géométrie mais **pas l'état d'inspection** : composants pointés « placés » (`m_placedRefs`), composant courant, filtres BOM — tout est perdu si l'app (ou le Jetson) redémarre au milieu d'une carte de 300 composants. **Fix** : persister dans `$IBOM_DATA_DIR/session_state.json`, clé = chemin iBOM. **Effort : ½ session.**

### 3.4 Onglet Settings « Features » pour les clés config orphelines
6 clés `Config` n'ont **aucune UI** : `remoteViewEnabled`, `remoteViewPort`, `voiceControlEnabled`, `darkMode`, `detectorModel`, `checkboxColumns`. Un 5ᵉ onglet dans `SettingsDialog` les regroupe (le dropdown `detectorModel` listant les `.onnx` trouvés par `ModelManager` est le plus utile). **Effort : ½ session.**

### 3.5 Worker d'inférence asynchrone (prérequis du point F de la roadmap)
`ComponentDetector::detect()` n'est invoqué nulle part, et s'il l'était, ce serait sur le **thread GUI** (blocage à chaque frame). Avant d'afficher les détections live (point F de [PROCHAINE_SESSION.md](PROCHAINE_SESSION.md)), créer un `InferenceWorker` sur le pattern `TrackingWorker` (QThread dédié, `FrameRef` en entrée, throttle configurable, signal `detectionsReady`). C'est aussi la brique du hard-example mining (Phase D). **Effort : 1 session — à faire dès que le modèle v1 existe.**

### 3.6 Cheat-sheet raccourcis clavier
Les raccourcis existent (Ctrl+S, Ctrl+E, Ctrl+,, C, K, I, F11…) mais aucune vue ne les liste. Un onglet dans `HelpDialog` ou un overlay « ? » les rendrait découvrables. **Effort : ~1 h.**

### 3.7 i18n française
Tout le code est déjà `tr()`-wrappé, il n'y a juste **aucun fichier `.ts`/`.qm`**. `lupdate` + traduction FR + sélecteur de langue dans Settings. Effort modéré (~1 session), valeur selon qui utilise l'app.

---

## 4. 🟢 Hygiène / dette (au fil de l'eau)

- **Tests manquants** : `Config` (round-trip load/save — 10 sections, zéro test), `CameraCalibration` (calcul sur images synthétiques), `OverlayRenderer`. Le test `Config` est le plus rentable : c'est lui qui protège les migrations de schéma (cf. piège #16).
- **`ComponentDetector::estimateOrientation()`** : code mort jamais appelé (tolérance 15° hardcodée) — soit le câbler dans le futur affichage détections (mauvaise orientation = défaut détectable !), soit le supprimer.
- **`OverlayRenderer.cpp:206`** : TODO arcs/cercles silkscreen non dessinés — visible sur les cartes avec des marquages circulaires.
- **`TODO.md` racine** : daté 2026-03-20, ère Windows — plusieurs entrées obsolètes (installer NSIS, bugs `%APPDATA%`…). À rafraîchir ou à remplacer par un renvoi vers les docs Jetson.

---

## 5. Priorisation suggérée

| Prio | Réf | Quoi | Effort | Pourquoi maintenant |
|------|-----|------|--------|---------------------|
| 🔴 1 | 1.3 | Statut IA visible dans la GUI | 30 min | L'init TRT prend des minutes — l'utilisateur doit le voir |
| 🔴 2 | 1.1 | Garde-fou décompression iBOM | 30 min | Risque de freeze/OOM sur fichier corrompu |
| 🔴 3 | 1.2 | Câbler le slider de confiance | 30 min | Sera silencieusement inopérant au 1er modèle |
| 🟠 4 | 3.1 | Focus assist (netteté live) | ½ session | Métrique déjà calculée ; aide immédiate au microscope |
| 🟠 5 | 3.2 | Fichiers récents + auto-reload iBOM | 2 h | Confort quotidien, zéro risque |
| 🟠 6 | 2.1 | RemoteView (navigateur) | ½ session | Code complet, idéal atelier Jetson |
| 🟠 7 | 3.4 | Onglet Settings « Features » | ½ session | Débloque les clés config orphelines |
| 🟡 8 | 3.3 | Restauration session d'inspection | ½ session | Précieux sur les grosses cartes |
| 🟡 9 | 3.5 | InferenceWorker async | 1 session | **Dès que le modèle v1 existe** (prérequis point F) |
| 🟡 10 | 2.2 | Bouton rapport PDF/HTML | ½ session | Livrable d'inspection |
| 🟡 11 | 2.3 | BarcodeScanner + assoc. iBOM | 1 session | Workflow zéro-clic multi-cartes |
| 🟢 12 | 1.4, 4 | Micro-perfs + tests Config + nettoyages | au fil de l'eau | Hygiène |
| 🟢 13 | 3.6, 3.7 | Cheat-sheet raccourcis, i18n FR | ~1 session | Polish |
| ⏸ — | 2.4, 2.5 | OCR, SolderInspector, Voice, Stencil | ≥ 2 sessions | Attendent un modèle / un besoin réel |

> **Note** : la priorité produit n°1 reste celle de [PROCHAINE_SESSION.md](PROCHAINE_SESSION.md) — calibration caméra → première capture dataset → entraînement v1. Les items 1-3 ci-dessus (1 h 30 cumulées) valent le coup **avant** la première session de capture ; le reste s'intercale ensuite.

---

*Analyse du 2026-06-12 sur `main` @ `639cc01` (branche `claude/focused-fermi-if21mm`). Aucun code modifié — document de propositions uniquement.*
