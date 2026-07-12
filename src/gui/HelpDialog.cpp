#include "HelpDialog.h"

#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QLabel>
#include <QShortcut>
#include <QKeySequence>
#include <QTextCursor>
#include <QTextDocument>

namespace ibom::gui {

HelpDialog::HelpDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("MicroscopeIBOM — Help & Reference"));
    resize(860, 640);

    auto* layout = new QVBoxLayout(this);

    // ── Search bar: full-text across every tab ──────────────────
    auto* searchRow = new QHBoxLayout;
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search the whole reference… (Ctrl+F, Enter = next match)"));
    m_search->setClearButtonEnabled(true);
    m_searchInfo = new QLabel;
    m_searchInfo->setMinimumWidth(150);
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(m_searchInfo);
    layout->addLayout(searchRow);

    m_tabs = new QTabWidget;
    layout->addWidget(m_tabs);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(btnBox);

    createTabs();

    connect(m_search, &QLineEdit::textChanged,
            this, [this](const QString& q) { runSearch(q); });
    connect(m_search, &QLineEdit::returnPressed,
            this, [this]() { findNext(false); });
    auto* findSc = new QShortcut(QKeySequence::Find, this);
    connect(findSc, &QShortcut::activated, this, [this]() {
        m_search->setFocus();
        m_search->selectAll();
    });
    auto* prevSc = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Return), this);
    connect(prevSc, &QShortcut::activated, this, [this]() { findNext(true); });
}

void HelpDialog::showTab(int index)
{
    if (index >= 0 && index < m_tabs->count())
        m_tabs->setCurrentIndex(index);
    show();
    raise();
    activateWindow();
}

void HelpDialog::addPage(const QString& title, const QString& html)
{
    auto* browser = new QTextBrowser;
    browser->setOpenExternalLinks(true);
    browser->setHtml(html);
    m_pages.push_back(browser);
    m_tabs->addTab(browser, title);
}

// ── Full-text search ────────────────────────────────────────────

int HelpDialog::highlightAll(QTextBrowser* browser, const QString& query)
{
    QList<QTextEdit::ExtraSelection> selections;
    if (!query.isEmpty()) {
        QTextCursor c(browser->document());
        while (true) {
            c = browser->document()->find(query, c);
            if (c.isNull()) break;
            QTextEdit::ExtraSelection sel;
            sel.cursor = c;
            sel.format.setBackground(QColor(255, 210, 0, 130));
            selections.append(sel);
        }
    }
    browser->setExtraSelections(selections);
    return static_cast<int>(selections.size());
}

void HelpDialog::runSearch(const QString& query)
{
    m_matchCounts.resize(m_pages.size());
    int total = 0, tabsWithHits = 0;
    for (int i = 0; i < m_pages.size(); ++i) {
        m_matchCounts[i] = highlightAll(m_pages[i], query);
        total += m_matchCounts[i];
        if (m_matchCounts[i] > 0) ++tabsWithHits;
    }

    if (query.isEmpty()) {
        m_searchInfo->clear();
        return;
    }
    if (total == 0) {
        m_searchInfo->setText(tr("no match"));
        return;
    }
    m_searchInfo->setText(tr("%1 matches in %2 tab(s)").arg(total).arg(tabsWithHits));

    // Land on a tab that actually has matches, cursor before the first one.
    int idx = m_tabs->currentIndex();
    if (idx < 0 || idx >= m_matchCounts.size() || m_matchCounts[idx] == 0) {
        for (int i = 0; i < m_matchCounts.size(); ++i)
            if (m_matchCounts[i] > 0) { m_tabs->setCurrentIndex(i); idx = i; break; }
    }
    m_pages[idx]->moveCursor(QTextCursor::Start);
    m_pages[idx]->find(query);
}

void HelpDialog::findNext(bool backwards)
{
    const QString query = m_search->text();
    if (query.isEmpty() || m_pages.isEmpty()) return;

    const QTextDocument::FindFlags flags =
        backwards ? QTextDocument::FindBackward : QTextDocument::FindFlags();

    int idx = m_tabs->currentIndex();
    if (idx >= 0 && idx < m_pages.size() && m_pages[idx]->find(query, flags))
        return;

    // Wrap to the next/previous tab that has matches (up to a full cycle).
    for (int step = 1; step <= m_pages.size(); ++step) {
        const int i = ((idx + (backwards ? -step : step)) % m_pages.size()
                       + m_pages.size()) % m_pages.size();
        if (i < m_matchCounts.size() && m_matchCounts[i] > 0) {
            m_tabs->setCurrentIndex(i);
            m_pages[i]->moveCursor(backwards ? QTextCursor::End : QTextCursor::Start);
            m_pages[i]->find(query, flags);
            return;
        }
    }
}

// ── Content ─────────────────────────────────────────────────────

void HelpDialog::createTabs()
{
    // ── Getting Started ─────────────────────────────────────────
    addPage(tr("Getting Started"), tr(
        "<h2>Getting Started</h2>"
        "<ol>"
        "<li><b>Connect your camera</b> — plug in the USB microscope (or RealSense D405) before launching the app.</li>"
        "<li><b>Select your camera</b> — <i>Settings → Camera</i> (Ctrl+,): backend (V4L2 microscope / RealSense), device, resolution, FPS.</li>"
        "<li><b>Start the camera</b> — toolbar toggle or <b>C</b>.</li>"
        "<li><b>Load an iBOM file</b> — <i>File → Open iBOM</i> (Ctrl+O), or drag &amp; drop the HTML file, or <i>File → Open Recent</i>. "
        "This is the HTML exported by KiCad's Interactive BOM plugin (compressed LZ-String files are supported).</li>"
        "<li><b>Align the overlay</b> — the quickest path is <b>Auto-Align</b> in the Control Panel; "
        "all methods are described in the Alignment tab.</li>"
        "<li><b>Inspect</b> — click components in the BOM list, track progress with the Placed checkboxes, "
        "sweep the board with the PCB Map coverage, use the tools in the Inspection Tools tab.</li>"
        "</ol>"
        "<p>The most-used shortcuts: <b>C</b> camera · <b>A</b> anchor · <b>M</b> large PCB map · "
        "<b>X</b> (hold) loupe · <b>F1</b> this help. The complete list is in the <b>Shortcuts</b> tab.</p>"
        "<p><i>Tip: this reference is searchable — press Ctrl+F and type any term "
        "(e.g. \"golden\", \"drift\", \"checkerboard\").</i></p>"));

    // ── Shortcuts (exhaustive) ──────────────────────────────────
    addPage(tr("Shortcuts"), tr(
        "<h2>Keyboard &amp; Mouse Reference</h2>"

        "<h3>Global shortcuts</h3>"
        "<table border='1' cellpadding='4' style='border-collapse:collapse;'>"
        "<tr><td><b>C</b></td><td>Start / stop the camera</td></tr>"
        "<tr><td><b>Ctrl+O</b></td><td>Open iBOM file</td></tr>"
        "<tr><td><b>Ctrl+S</b></td><td>Screenshot (camera view + overlay)</td></tr>"
        "<tr><td><b>Ctrl+E</b></td><td>Export report / data</td></tr>"
        "<tr><td><b>Ctrl+,</b></td><td>Settings dialog</td></tr>"
        "<tr><td><b>K</b></td><td>Calibrate camera (checkerboard)</td></tr>"
        "<tr><td><b>I</b></td><td>Start the inspection wizard</td></tr>"
        "<tr><td><b>A</b></td><td>Anchor the selected component (1-point microscope alignment)</td></tr>"
        "<tr><td><b>M</b></td><td>Large PCB Map window (Esc closes)</td></tr>"
        "<tr><td><b>D</b></td><td>Depth view (RealSense only)</td></tr>"
        "<tr><td><b>3</b></td><td>3D point cloud view (RealSense only)</td></tr>"
        "<tr><td><b>F11</b></td><td>Fullscreen window</td></tr>"
        "<tr><td><b>Esc</b></td><td>Exit fullscreen / close dialogs / cancel picking</td></tr>"
        "<tr><td><b>F1</b></td><td>This help</td></tr>"
        "<tr><td><b>Ctrl+Shift+M</b></td><td>Dev: measure FOV &amp; scale</td></tr>"
        "<tr><td><b>Ctrl+Shift+C</b></td><td>Dev: live calibration monitor</td></tr>"
        "</table>"

        "<h3>Camera view (center)</h3>"
        "<table border='1' cellpadding='4' style='border-collapse:collapse;'>"
        "<tr><td><b>Mouse wheel</b></td><td>Zoom (anchored at the cursor, up to 20×)</td></tr>"
        "<tr><td><b>Middle-drag</b></td><td>Pan when zoomed</td></tr>"
        "<tr><td><b>Double-click</b></td><td>Camera-only fullscreen (Esc returns)</td></tr>"
        "<tr><td><b>Hold X</b></td><td>Loupe under the cursor, rendered from the full-resolution frame "
        "(overlay included). <b>Wheel while held</b> adjusts ×1.5–×6.</td></tr>"
        "<tr><td><b>Left click</b></td><td>Select / alignment point / measurement point (mode-dependent)</td></tr>"
        "<tr><td><b>Right click</b></td><td>Cancel the in-progress measurement</td></tr>"
        "<tr><td><b>Double-click (Area mode)</b></td><td>Close the polygon and commit the area measurement</td></tr>"
        "</table>"

        "<h3>PCB Map (minimap dock and large view)</h3>"
        "<table border='1' cellpadding='4' style='border-collapse:collapse;'>"
        "<tr><td><b>Mouse wheel</b></td><td>Zoom the map (anchored at the cursor, 1–100×)</td></tr>"
        "<tr><td><b>Left-drag</b></td><td>Pan (once zoomed)</td></tr>"
        "<tr><td><b>Drag the FOV rectangle</b></td><td>Re-anchor the overlay at the dropped position</td></tr>"
        "<tr><td><b>Ctrl+click</b></td><td>Select the component under the cursor (BOM scrolls to it)</td></tr>"
        "<tr><td><b>Double-click / ⛶ / M</b></td><td>Open the large resizable map</td></tr>"
        "<tr><td><b>⌂</b></td><td>Fit the whole board</td></tr>"
        "<tr><td><b>◎</b></td><td>Follow-FOV mode (auto-centers on the camera)</td></tr>"
        "<tr><td><b>Right click</b></td><td>Menu: inspection coverage on/off, reset coverage, detach panel</td></tr>"
        "</table>"

        "<h3>BOM panel</h3>"
        "<ul>"
        "<li>Type in the search bar to filter by reference, value or footprint.</li>"
        "<li>Click a row to select and highlight the component in the camera view and PCB Map.</li>"
        "<li>Checkbox columns (Sourced / Placed, configurable) track assembly progress — persisted per board.</li>"
        "<li>The F/B filter follows the active board side automatically.</li>"
        "</ul>"));

    // ── Calibration ─────────────────────────────────────────────
    addPage(tr("Calibration"), tr(
        "<h2>Camera Calibration</h2>"
        "<p>Calibration corrects lens distortion and establishes the <b>pixels-per-mm</b> ratio, "
        "used by overlay alignment and the measurement tools.</p>"
        "<p><b>RealSense D405</b>: factory intrinsics are read from the device — no checkerboard "
        "needed; the depth stream also provides a live px/mm estimate.</p>"

        "<h3>Calibration Checkerboard (USB microscopes)</h3>"
        "<ol>"
        "<li><i>Control Panel → Generate Checkerboard…</i> creates a custom pattern (PNG or print), "
        "or use <i>Open Patterns PDF…</i> for pre-made 0.5 / 1 / 2 mm patterns.</li>"
        "<li><b>Print at 100% scale</b> (no fit-to-page!) and verify dimensions with calipers.</li>"
        "</ol>"

        "<h3>Recommended square sizes</h3>"
        "<table border='1' cellpadding='6' style='border-collapse:collapse;'>"
        "<tr><th>FOV (field of view)</th><th>Square size</th><th>Board size (8×6)</th></tr>"
        "<tr><td>~5-10 mm</td><td>0.5 mm</td><td>4 × 3 mm</td></tr>"
        "<tr><td>~10-20 mm</td><td>1.0 mm</td><td>8 × 6 mm</td></tr>"
        "<tr><td>~20-40 mm</td><td>2.0 mm</td><td>16 × 12 mm</td></tr>"
        "</table>"

        "<h3>Procedure</h3>"
        "<ol>"
        "<li>Set cols / rows / square size in <i>Settings → Camera</i>.</li>"
        "<li>Place the pattern flat and in focus, press <b>K</b>.</li>"
        "<li>Frames are captured and inner corners detected automatically.</li>"
        "<li>On success the reprojection error and px/mm appear in the status bar.</li>"
        "</ol>"

        "<h3>Tips</h3>"
        "<ul>"
        "<li>The whole checkerboard must be visible — every inner corner detected.</li>"
        "<li>Reprojection error &lt; 0.5 px is excellent; &lt; 1.0 px acceptable.</li>"
        "<li>Calibration is saved and reloaded automatically; recalibrate after a resolution change.</li>"
        "<li><i>Dev → Calibration Monitor</i> (Ctrl+Shift+C) shows detection live while you position the pattern.</li>"
        "</ul>"));

    // ── Alignment ───────────────────────────────────────────────
    addPage(tr("Alignment"), tr(
        "<h2>Overlay Alignment</h2>"
        "<p>Alignment maps the iBOM PCB coordinates onto the camera image so pads, silkscreen and "
        "labels land on the real board. All methods live in the Control Panel; the "
        "<b>Alignment Assistant…</b> walks you through choosing one.</p>"

        "<h3>Automatic — Auto-Align</h3>"
        "<p>One click, no correspondences. Tries, in order: matching detected components/pads to the "
        "iBOM constellation (works when the board fills the frame), then the board contour with a "
        "pad-vote for orientation. It <i>refuses honestly</i> (\"ambiguous registration\") rather than "
        "guessing on symmetric layouts — fall back to a manual method in that case.</p>"
        "<ul>"
        "<li>Best results: board lit evenly, no big specular reflection, board mostly in frame. "
        "The status-bar <b>scene advisor</b> warns about exactly these conditions.</li>"
        "<li>Workflow for high magnification: Auto-Align from afar (whole board visible), then "
        "approach — live tracking follows continuously.</li>"
        "<li><i>Dev → Show re-anchor debug view</i> displays what the detector saw on each attempt.</li>"
        "</ul>"

        "<h3>Manual methods</h3>"
        "<ul>"
        "<li><b>Align: 4 Corners</b> — click the PCB corners Top-Left → Top-Right → Bottom-Right → "
        "Bottom-Left. Full homography (handles perspective). Needs the whole board visible.</li>"
        "<li><b>Align: 2 Components</b> — select a component in the BOM, click it in the image; repeat "
        "with a second, far-away component. Similarity transform — the microscope method.</li>"
        "<li><b>Align: Multi-Comp</b> — mark ≥2 components (2 body corners, pin 1, or the 2 farthest "
        "pads). 2 landmarks → similarity, 3 → affine, ≥4 → full homography. Works on any board shape; "
        "clicks are sub-pixel refined.</li>"
        "<li><b>Anchor (A)</b> — 1-point: select a component, press A, click where it is. Uses the "
        "known scale (calibration or depth) and rotation. Fastest at high magnification.</li>"
        "</ul>"

        "<h3>Live Tracking</h3>"
        "<p>Once aligned, <b>Live Tracking Mode</b> keeps the overlay locked while the board or camera "
        "moves: optical flow at camera rate, ORB re-seeding, jump/sanity gates.</p>"
        "<ul>"
        "<li>States in the status bar: <b>Locked</b> / <b>Drifting</b> (press A to re-anchor) / "
        "<b>Lost</b> (automatic recovery kicks in).</li>"
        "<li>The PCB Map FOV rectangle turns <b>orange</b> when quality drops, <b>red</b> when lost.</li>"
        "<li><b>Auto re-anchor</b> (Settings → Tracking) periodically corrects slow drift; corrections "
        "are gated (needs two concordant estimates) so a healthy pose is never yanked.</li>"
        "<li><b>Reset Alignment</b> drops the pose and stops tracking.</li>"
        "<li>A successful alignment is saved per board and offered for restore on reload (front side).</li>"
        "</ul>"

        "<h3>Back side</h3>"
        "<p>Check <b>Back side (board flipped)</b> in the Control Panel after physically flipping the "
        "board: the overlay renders that layer mirrored (labels stay readable), all alignment methods "
        "and the BOM filter follow. Re-align after flipping — the old pose is meaningless.</p>"

        "<h3>Dynamic scale</h3>"
        "<ul>"
        "<li><b>None</b>: fixed calibration.</li>"
        "<li><b>From homography</b>: scale follows live tracking.</li>"
        "<li><b>From iBOM pads</b>: scale computed from known pad distances.</li>"
        "<li><b>Lens adapter</b> (Settings → Camera) multiplies the calibrated scale — see Lens Adapters.</li>"
        "</ul>"));

    // ── Lens Adapters ───────────────────────────────────────────
    addPage(tr("Lens Adapters"), tr(
        "<h2>Lens Adapters (Optical Multiplier)</h2>"
        "<p>Stereo microscopes often use <b>Barlow lenses</b> or <b>reduction rings</b> "
        "to change the field of view and magnification.</p>"

        "<h3>Common adapters</h3>"
        "<table border='1' cellpadding='6' style='border-collapse:collapse;'>"
        "<tr><th>Adapter</th><th>Effect on FOV</th><th>Effect on px/mm</th><th>Use case</th></tr>"
        "<tr><td><b>0.5x</b></td><td>×2 (doubles FOV)</td><td>÷2</td><td>Overview, larger PCBs, calibration</td></tr>"
        "<tr><td><b>0.75x</b></td><td>×1.33</td><td>÷1.33</td><td>Good compromise</td></tr>"
        "<tr><td><b>1x</b></td><td>No change</td><td>No change</td><td>Default (no adapter)</td></tr>"
        "<tr><td><b>1.5x</b></td><td>÷1.5</td><td>×1.5</td><td>Fine-pitch inspection</td></tr>"
        "<tr><td><b>2x</b></td><td>÷2 (halves FOV)</td><td>×2</td><td>BGA / QFP close-up</td></tr>"
        "</table>"

        "<h3>How to use</h3>"
        "<ol>"
        "<li><b>Calibrate once</b> at 1x (no adapter).</li>"
        "<li>Attach the reduction ring (e.g. 0.5x).</li>"
        "<li><i>Settings → Camera → Lens adapter</i> → select <b>0.5x</b>.</li>"
        "<li>The px/mm ratio is adjusted automatically — no recalibration needed.</li>"
        "</ol>"

        "<h3>Camera pointers</h3>"
        "<table border='1' cellpadding='6' style='border-collapse:collapse;'>"
        "<tr><th>Camera</th><th>Resolution</th><th>px/mm at 0.5x (FOV ~20mm)</th><th>Good for</th></tr>"
        "<tr><td>USB 1080p (basic)</td><td>1920×1080</td><td>~96</td><td>0603+ components</td></tr>"
        "<tr><td>USB 5MP (e.g. MU500)</td><td>2592×1944</td><td>~130</td><td>0402, QFP</td></tr>"
        "<tr><td>USB 4K</td><td>3840×2160</td><td>~192</td><td>0201, BGA, ultra-fine</td></tr>"
        "</table>"));

    // ── Inspection workflow ─────────────────────────────────────
    addPage(tr("Inspection"), tr(
        "<h2>PCB Inspection Workflow</h2>"

        "<h3>BOM panel</h3>"
        "<p>All components from the iBOM. Click to highlight in the camera view and PCB Map; "
        "filter with the search bar; F/B follows the active side.</p>"
        "<ul>"
        "<li><b>Placed / Sourced checkboxes</b> track progress; placed components render faded in the overlay.</li>"
        "<li>Progress is <b>persisted per board</b> — closing the app mid-inspection loses nothing.</li>"
        "<li>The Statistics panel shows <b>Inspection Progress</b> (placed / total).</li>"
        "</ul>"

        "<h3>PCB Map coverage</h3>"
        "<p>While tracking, the green trail on the PCB Map accumulates everywhere the camera has "
        "looked — sweep until the whole board is green to guarantee full visual coverage. "
        "Right-click the map to toggle or reset it.</p>"

        "<h3>Inspection wizard</h3>"
        "<p><i>Inspection → Start Inspection</i> (I) walks through components one by one.</p>"

        "<h3>Dataset capture (AI training)</h3>"
        "<p>The Dataset panel records auto-annotated YOLO training images while tracking is locked: "
        "five live quality gates (tracking, reprojection, sharpness, exposure, freshness), "
        "output ready for PCB Dataset Studio. See docs/TUTO_DATASETS.md in the repository.</p>"

        "<h3>AI assistance</h3>"
        "<p>When a detector model (<code>models/*.onnx</code>) is present it loads in the background "
        "(status in the status bar — the first TensorRT launch compiles for several minutes). "
        "It then powers component detection and stabilizes Auto-Align on symmetric layouts.</p>"));

    // ── Inspection tools (scan / golden / depth / advisor / loupe / measure) ──
    addPage(tr("Inspection Tools"), tr(
        "<h2>Inspection Tools</h2>"

        "<h3>Board scan — mosaic (Inspection → Scan Board)</h3>"
        "<p>Builds a full <b>orthorectified image of the board</b> while you sweep it under the "
        "camera, stitched in board space from tracked frames (the sharpest view of each area wins, "
        "so a blurred pass is repaired by a sharp one).</p>"
        "<ol>"
        "<li>Load an iBOM, align, ideally enable live tracking.</li>"
        "<li>Check <i>Inspection → Scan Board (Mosaic)</i> — the status bar shows live coverage.</li>"
        "<li>Sweep the whole board (the PCB Map green trail is your guide).</li>"
        "<li>Uncheck to finish — the PNG is exported under the data directory (<code>scans/</code>).</li>"
        "</ol>"
        "<p>Frames are only merged while the pose is healthy, so a tracking glitch cannot corrupt the scan.</p>"

        "<h3>Golden board comparison</h3>"
        "<p>Detect assembly differences <b>without any AI model</b> by comparing scans of two boards "
        "of the same design:</p>"
        "<ol>"
        "<li>Scan a known-good board → <i>Inspection → Save Last Scan as Golden</i> "
        "(stored per iBOM file and per face).</li>"
        "<li>Scan the board under test the same way.</li>"
        "<li><i>Inspection → Compare Last Scan to Golden…</i> → a table ranks components by anomaly "
        "score (missing, wrong, shifted parts rise to the top), and the <b>defect heatmap</b> is filled.</li>"
        "<li>Enable <b>Show Defect Heatmap</b> in the Control Panel to see hot spots on the live overlay.</li>"
        "</ol>"
        "<p>Lighting changes are compensated (gain + local contrast comparison) — only real content "
        "differences score high. Areas not covered by both scans are skipped, not flagged.</p>"

        "<h3>Depth check (Inspection → Depth-Check Components, D405)</h3>"
        "<p>With a RealSense D405 and an aligned overlay: fits the bare-board plane in the depth "
        "frame, then measures each component's height above it.</p>"
        "<ul>"
        "<li><b>Present</b>: median height ≥ 0.4 mm · <b>ABSENT</b>: ≤ 0.15 mm · "
        "<b>uncertain</b>: in between, or too few depth pixels (tiny parts).</li>"
        "<li>Reliable for parts ≥ ~1 mm tall and a few pixels wide; 0402-class parts honestly "
        "report <i>uncertain</i>.</li>"
        "</ul>"

        "<h3>Scene advisor (status bar)</h3>"
        "<p>Continuously watches the three conditions that ruin Auto-Align and detection: "
        "<b>glare</b> (a compact specular reflection on the board), <b>under-exposure</b>, and "
        "<b>defocus</b>. A persistent problem raises an orange ⚠ banner with the fix "
        "(move the light, add light, refocus); it clears itself once the scene recovers. "
        "Advisory only — it never blocks anything.</p>"

        "<h3>Loupe (hold X)</h3>"
        "<p>Hold <b>X</b> over the camera view: a circular loupe magnifies under the cursor, rendered "
        "from the <b>full-resolution frame</b> (more detail than the fitted view) with the overlay "
        "warped inside. Wheel while held adjusts ×1.5–×6. Release to dismiss.</p>"

        "<h3>Measurements</h3>"
        "<p>From the Control Panel: <b>Distance</b>, <b>Angle</b>, <b>Area</b> (double-click closes "
        "the polygon), <b>Pin pitch</b>. Values in px and mm (when calibrated); right-click cancels; "
        "history stays drawn until cleared.</p>"

        "<h3>Snapshots</h3>"
        "<p>The snapshot history stores timestamped captures under the data directory "
        "(<code>snapshots/</code>) — useful before/after rework evidence.</p>"));

    // ── Overlay ─────────────────────────────────────────────────
    addPage(tr("Overlay"), tr(
        "<h2>Overlay Settings</h2>"
        "<p>The overlay renders the iBOM board data over the live image, warped by the current "
        "alignment on every frame.</p>"

        "<h3>Control Panel toggles</h3>"
        "<ul>"
        "<li><b>Show Pads</b> / <b>Show Silkscreen</b> / <b>Show Fabrication</b>.</li>"
        "<li><b>Opacity</b> slider (0% = invisible, 100% = opaque).</li>"
        "<li><b>Back side (board flipped)</b> — renders the back layer mirrored.</li>"
        "</ul>"

        "<h3>Colors &amp; appearance</h3>"
        "<p><i>Settings → Overlay</i>: selected / placed / normal colors, placed opacity, selected "
        "outline width. Dark/Light theme via <i>View → Dark Mode</i>.</p>"

        "<h3>Defect heatmap</h3>"
        "<p><b>Show Defect Heatmap</b> (Control Panel) overlays a color-coded defect-density map on "
        "the board. It is filled by <i>Inspection → Compare Last Scan to Golden…</i> — hot (red) "
        "areas are where the board differs most from the golden reference.</p>"));

    // ── Export ──────────────────────────────────────────────────
    addPage(tr("Export"), tr(
        "<h2>Export & Reports</h2>"
        "<p>Export inspection results via <i>File → Export</i> (Ctrl+E).</p>"

        "<h3>Formats</h3>"
        "<ul>"
        "<li><b>CSV</b>: reference, value, footprint, status, defect type, confidence.</li>"
        "<li><b>JSON</b>: machine-readable full component details.</li>"
        "<li><b>KiCad Placement</b>: pick-and-place compatible.</li>"
        "<li><b>BOM with checkboxes</b>: re-importable placed/not-placed status.</li>"
        "<li><b>Defects CSV</b>: only flagged components.</li>"
        "<li><b>HTML / PDF report</b>: stats, checklist, yield — the deliverable of an inspection session.</li>"
        "</ul>"

        "<h3>Images</h3>"
        "<ul>"
        "<li><b>Ctrl+S</b>: screenshot of the current view, overlay included.</li>"
        "<li><b>Board scans</b>: full-board orthorectified PNGs under <code>scans/</code> "
        "(see Inspection Tools) — the documentation-grade photo of the inspected board.</li>"
        "<li><b>Snapshots</b>: timestamped history under <code>snapshots/</code>.</li>"
        "</ul>"));

    // ── Troubleshooting ─────────────────────────────────────────
    addPage(tr("Troubleshooting"), tr(
        "<h2>Troubleshooting</h2>"

        "<h3>Camera not detected</h3>"
        "<ul>"
        "<li>Connect the USB cable <b>before</b> launching the app.</li>"
        "<li><i>Settings → Camera</i> → <b>Refresh</b> to re-scan devices; check the backend "
        "(V4L2 vs RealSense) matches the hardware.</li>"
        "</ul>"

        "<h3>Auto-Align fails or lands wrong</h3>"
        "<ul>"
        "<li>Watch the <b>scene advisor</b> banner: glare and under-exposure are the #1 field cause. "
        "Move the light, kill the reflection, frame the board tighter.</li>"
        "<li>\"Ambiguous registration … refusing to guess\" is honest: the pad layout is too "
        "symmetric from this view. Align manually (2 components / Multi-Comp) or from farther away "
        "(whole board visible → the contour disambiguates the orientation).</li>"
        "<li><i>Dev → Show re-anchor debug view</i>: red = raw detections, magenta = pad detections, "
        "green = expected pads under the computed pose. One look tells you whether the detections or "
        "the matching are at fault.</li>"
        "</ul>"

        "<h3>Overlay misaligned / drifting</h3>"
        "<ul>"
        "<li>Re-run an alignment method, or press <b>A</b> on a selected component to re-anchor.</li>"
        "<li>4-corner mode: corners must be clicked TL → TR → BR → BL.</li>"
        "<li>If the board was flipped, toggle <b>Back side</b> — the old pose is invalid.</li>"
        "<li>Slow drift under live tracking: enable <b>Auto re-anchor</b> (Settings → Tracking).</li>"
        "</ul>"

        "<h3>Tracking keeps getting LOST</h3>"
        "<ul>"
        "<li>Move slower; increase lighting; make sure the board has texture in view (bare laminate "
        "corners track poorly).</li>"
        "<li>Fast rotations beyond the jump gate force a re-acquisition — rotate gradually.</li>"
        "</ul>"

        "<h3>Golden compare flags everything</h3>"
        "<ul>"
        "<li>Both scans must cover the same areas — sweep until coverage matches.</li>"
        "<li>Compare the same <b>face</b> the golden was saved for.</li>"
        "<li>Radically different lighting between scans degrades the comparison — the global gain is "
        "compensated, a color change of the light is not.</li>"
        "</ul>"

        "<h3>Depth check says everything is uncertain</h3>"
        "<ul>"
        "<li>The D405 needs to be close enough for depth density — check the depth view (<b>D</b>).</li>"
        "<li>Tiny components (0402-class) are below depth resolution by design.</li>"
        "</ul>"

        "<h3>Calibration fails</h3>"
        "<ul>"
        "<li>The entire checkerboard must be visible and in focus.</li>"
        "<li>Cols / rows / square size in Settings must match the printed pattern.</li>"
        "<li>Try a larger square size if the camera can't resolve the pattern.</li>"
        "</ul>"

        "<h3>AI status stuck on loading</h3>"
        "<p>The first launch with a new model compiles a TensorRT engine — this takes minutes and "
        "happens once. The status bar shows progress; the app stays fully usable meanwhile.</p>"

        "<h3>Logs &amp; diagnostics</h3>"
        "<ul>"
        "<li><i>Dev → Copy log file path</i> puts the exact log location in the clipboard.</li>"
        "<li><i>Dev → Verbose debug logging</i> adds the per-frame [track]/[scene]/[scan] streams.</li>"
        "<li><i>Dev → Dump full state to log</i> records every setting and runtime value at once.</li>"
        "<li>The Statistics panel's Event Log mirrors warnings live.</li>"
        "</ul>"));
}

} // namespace ibom::gui
