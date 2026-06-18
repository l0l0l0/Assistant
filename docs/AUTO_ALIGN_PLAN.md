# Auto-Align — automatic board outline detection

> Status: MVP shipped (2026-06-18). 6th alignment path, feeds the same
> `Homography::compute(pcbCorners, imageCorners)` sink used by the 5 existing
> manual paths (4-point click, 2-component align, microscope 1-point anchor).

## Goal

Today the overlay only orients itself once the user manually clicks
points or selects components. This plan adds a one-click "Auto-Align"
button that locates the physical board in the current camera frame and
computes the homography automatically — no clicking required.

## Approach

1. **Locate the board outline** in the current frame, via two strategies
   tried in order:
   - **Depth-plane segmentation** (RealSense D405 only): the board is a
     raised plane closer to the camera than the table behind it. Take the
     median distance over a central ROI ± a 15mm band, threshold the depth
     image to isolate that plane, then `minAreaRect` the resulting blob.
   - **2D contour fallback** (works on any camera, incl. plain USB
     microscope): Canny edges → `findContours` → keep the largest
     quad-like contour (`area / rectArea ≥ 0.55`).
2. **Validate size** of the candidate rectangle against the iBOM's own
   board outline size, using whatever pixels-per-mm estimate is currently
   known (live homography scale, or calibration fallback). Implausible
   candidates (wrong size/aspect ratio) are rejected early — this is the
   main false-positive guard for the contour fallback.
3. **Disambiguate orientation.** A bare rectangle has 8 possible corner
   orderings (4 cyclic rotations × 2 windings) that all satisfy "this is a
   board-sized rectangle." Render the iBOM's board outline + component
   bounding boxes through each candidate homography and score how well
   that predicted geometry overlaps real Canny edges in the frame
   (`countNonZero(predicted & dilatedEdges) / countNonZero(predicted)`).
   The best-scoring candidate wins.
4. **Apply the result** the same way every other alignment path does:
   `Homography::compute(pcbCorners, imageCorners)` with the standard
   PCB corner convention `{TL(minX,minY), TR(maxX,minY), BR(maxX,maxY),
   BL(minX,maxY)}` of `boardInfo.boardBBox`, then propagate via
   `OverlayRenderer::setHomography()`, `updateDynamicScale()`,
   `BoardMinimap::update()`, and reset the live-tracking reference frame.

## Files

| File | Role |
|------|------|
| `src/overlay/BoardLocator.h/.cpp` | Pure-CV detection + disambiguation. Qt-free, testable standalone. |
| `src/gui/ControlPanel.h/.cpp` | "Auto-Align (Beta)" button → `autoAlignRequested` signal. |
| `src/app/Application.h/.cpp` | `autoAlignBoard()` — runs `BoardLocator::locate()` off the GUI thread via `QtConcurrent::run` + `QFutureWatcher`, applies the result on the main thread. |

## Threading

Detection (Canny/contours/edge-overlap scoring across 8 candidates) can
take tens of ms — run on a worker thread (`QtConcurrent::run`), mirroring
the pattern used by `CalibrationMonitorDialog`. The current color/depth
frames are cached as `Application::m_lastColorFrame`/`m_lastDepthFrame`
(zero-copy `shared_ptr<const cv::Mat>`, updated every `frameReady`/
`depthFrameReady`) and cloned once before handing off to the worker, since
OpenCV ops on the worker thread must not race the next incoming frame.
A `m_autoAligning` guard prevents re-entrant clicks while a detection is
in flight.

## MVP scope / deviations from the original sketch

- `BoardLocator` does its own lightweight rendering (lines/polylines) for
  per-candidate scoring rather than reusing `OverlayRenderer::render()` —
  simpler, and keeps `BoardLocator` Qt-free.
- No persisted `Config::autoAlignOnLoad` option — this ships as a manual
  button trigger only. Auto-triggering on iBOM load can be added later if
  useful in practice.
- The AI detector path (`ComponentDetector`) is not used here — `models/`
  is currently empty and this is out of scope for the MVP. A
  detector-assisted variant (e.g. anchoring on detected component
  silhouettes instead of a depth/contour plane) is plausible future work
  once a trained model exists.

## Risks

- Contour fallback on cluttered/dark backgrounds can find a wrong
  quad — mitigated by the size/aspect gate, but not eliminated. The score
  reported in the status bar is the user's signal to retry/fall back to
  manual alignment if it looks low.
- Symmetric or near-symmetric boards (square outline, rotationally
  symmetric component layout) can have multiple high-scoring candidates —
  inherent ambiguity, not fixable by this algorithm alone.
