#pragma once

#include <QRectF>
#include <QSize>
#include <vector>

class FaceStabilizer
{
public:
    struct Face { QRectF box; quint64 id; int detection; };
    void reset();
    std::vector<QRectF> update(const std::vector<QRectF> &detections, const QSize &size, qint64 now);
    std::vector<Face> updateFaces(const std::vector<QRectF> &detections, const QSize &size, qint64 now);
private:
    struct Track {
        QRectF box;
        qint64 seen;
        bool confirmed;
        quint64 id;
        int detection;
    };
    std::vector<Track> tracks;
    QSize imageSize;
    qint64 lastUpdate = -1;
    quint64 nextId = 1;
};

int runFaceStabilizerTest();
