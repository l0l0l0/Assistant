#pragma once

#include <QObject>
#include <QString>
#include <opencv2/core.hpp>

#include "camera/ICameraSource.h"   // ibom::camera::FrameRef
#include "features/BoardMosaic.h"

namespace ibom::features {

/**
 * @brief Board-scan worker (A1): accumulates tracked camera frames into a
 *        BoardMosaic on a dedicated QThread — warpPerspective on a 1080p
 *        frame must not run on the GUI thread.
 *
 * Same ownership pattern as DatasetCreator: owned by Application, moved to
 * its own QThread, fed via queued invokeMethod. Application is the gatekeeper
 * (it only forwards frames while a scan is active AND the pose is healthy)
 * and throttles the feed; the worker just accumulates.
 *
 * Geometry parameters travel as flat doubles (no custom metatypes); the
 * mosaic result is returned by scanFinished as cv::Mat (metatype already
 * registered for the tracking pipeline).
 */
class BoardScanner : public QObject {
    Q_OBJECT

public:
    explicit BoardScanner(QObject* parent = nullptr);
    ~BoardScanner() override = default;

public slots:
    /// Begin a scan for a board bbox (raw PCB mm). layerInt is the
    /// ibom::Layer of the face being scanned (carried through to the result).
    /// exportDir: where stopScan writes the PNG (created if needed).
    void startScan(double pxPerMm,
                   double minXmm, double minYmm, double maxXmm, double maxYmm,
                   int layerInt, QString exportDir);

    /// Finish the scan: export the mosaic as PNG under exportDir and emit
    /// scanFinished. No-op if no scan is active.
    void stopScan();

    /// One tracked camera frame + the pose it was displayed under
    /// (raw PCB mm → image px). Ignored while no scan is active.
    void processFrame(ibom::camera::FrameRef frame, cv::Mat pcbToImage);

signals:
    void scanStarted();
    /// Coarse progress: written-pixel fraction of the canvas + frames merged.
    void scanProgress(double coverageFrac, int framesUsed);
    /// Scan complete. mosaic CV_8UC3 + written mask CV_8U (clones, safe to
    /// keep). pngPath empty if the export failed (error already signalled).
    void scanFinished(QString pngPath, cv::Mat mosaic, cv::Mat writtenMask,
                      double coverageFrac, double pxPerMm,
                      double minXmm, double minYmm, int layerInt);
    void scanError(QString message);

private:
    BoardMosaic m_mosaic;
    bool        m_active = false;
    int         m_layerInt = 0;
    QString     m_exportDir;
    qint64      m_lastProgressMs = 0;
};

} // namespace ibom::features
