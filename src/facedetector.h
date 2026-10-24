#pragma once

#include <QImage>
#include <QString>
#include <opencv2/objdetect.hpp>
#include "facestabilizer.h"

class FaceDetector
{
public:
    bool load();
    int annotate(QImage &image);
    int annotateVideo(QImage &image, qint64 now);
    void reset() { stabilizer.reset(); }
    int detectionMs = 0;
    QString error;
private:
    bool detect(const QImage &image, std::vector<QRectF> &boxes);
    void draw(QImage &image, const std::vector<QRectF> &boxes);
    cv::Ptr<cv::FaceDetectorYN> detector;
    FaceStabilizer stabilizer;
};
