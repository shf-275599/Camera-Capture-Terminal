#pragma once

#include <QImage>
#include <QString>
#include <opencv2/objdetect.hpp>

class FaceDetector
{
public:
    bool load();
    int annotate(QImage &image);
    QString error;
private:
    cv::CascadeClassifier cascade;
};
