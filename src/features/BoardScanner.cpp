#include "BoardScanner.h"

#include <QDateTime>
#include <QDir>

#include <opencv2/imgcodecs.hpp>
#include <spdlog/spdlog.h>

namespace ibom::features {

BoardScanner::BoardScanner(QObject* parent)
    : QObject(parent)
{
}

void BoardScanner::startScan(double pxPerMm,
                             double minXmm, double minYmm,
                             double maxXmm, double maxYmm,
                             int layerInt, QString exportDir)
{
    BBox bbox;
    bbox.minX = minXmm; bbox.minY = minYmm;
    bbox.maxX = maxXmm; bbox.maxY = maxYmm;
    m_mosaic.initialize(bbox, pxPerMm);
    if (!m_mosaic.isInitialized()) {
        m_active = false;
        emit scanError(tr("Board scan: invalid board size (%1×%2 mm)")
                           .arg(bbox.width()).arg(bbox.height()));
        return;
    }
    m_active         = true;
    m_layerInt       = layerInt;
    m_exportDir      = exportDir;
    m_lastProgressMs = 0;
    spdlog::info("[scan] started: canvas {}x{} px ({:.2f} px/mm), face {}",
                 m_mosaic.geometry().widthPx, m_mosaic.geometry().heightPx,
                 m_mosaic.geometry().pxPerMm, layerInt == 0 ? "Front" : "Back");
    emit scanStarted();
}

void BoardScanner::processFrame(ibom::camera::FrameRef frame, cv::Mat pcbToImage)
{
    if (!m_active || !frame || frame->empty()) return;

    const int updated = m_mosaic.accumulate(*frame, pcbToImage);
    if (updated <= 0) return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastProgressMs >= 500) {
        m_lastProgressMs = now;
        emit scanProgress(m_mosaic.coverageFraction(), m_mosaic.framesAccumulated());
    }
}

void BoardScanner::stopScan()
{
    if (!m_active) return;
    m_active = false;

    const auto& geo = m_mosaic.geometry();
    const double coverage = m_mosaic.coverageFraction();

    QString pngPath;
    if (m_mosaic.framesAccumulated() > 0) {
        QDir().mkpath(m_exportDir);
        pngPath = m_exportDir + "/scan_"
                + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")
                + (m_layerInt == 0 ? "_front.png" : "_back.png");
        if (!cv::imwrite(pngPath.toStdString(), m_mosaic.image())) {
            emit scanError(tr("Board scan: failed to write %1").arg(pngPath));
            pngPath.clear();
        } else {
            spdlog::info("[scan] exported {} ({} frames, {:.0f}% coverage)",
                         pngPath.toStdString(), m_mosaic.framesAccumulated(),
                         coverage * 100.0);
        }
    } else {
        spdlog::info("[scan] stopped with no frames accumulated");
    }

    emit scanFinished(pngPath, m_mosaic.image().clone(),
                      m_mosaic.writtenMask().clone(), coverage,
                      geo.pxPerMm, geo.minXmm, geo.minYmm, m_layerInt);
    m_mosaic.reset();
}

} // namespace ibom::features
