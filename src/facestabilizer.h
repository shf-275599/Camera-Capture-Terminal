#pragma once

#include <QRectF>
#include <QSize>
#include <vector>

class FaceStabilizer
{
public:
    void reset();
    std::vector<QRectF> update(const std::vector<QRectF> &detections, const QSize &size, qint64 now);
private:
    struct Track {
        QRectF box;
        qint64 seen;
        bool confirmed;
    };
    std::vector<Track> tracks;
    QSize imageSize;
    qint64 lastUpdate = -1;
};

int runFaceStabilizerTest();
