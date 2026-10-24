#pragma once

#include <QImage>
#include <QString>
#include <opencv2/objdetect.hpp>
#include "facestabilizer.h"
#include "expressionrecognizer.h"
#include <map>

class FaceDetector
{
public:
    bool load();
    int annotate(QImage &image);
    int annotateVideo(QImage &image, qint64 now);
    void reset() { stabilizer.reset(); expressions.clear(); }
    void loadExpression() { expression.load(); expressionError = expression.error; }
    int annotateExpression(QImage &image);
    int expressionMs = 0;
    int expressionCount = 0;
    QString expressionError;
    int detectionMs = 0;
    QString error;
private:
    bool detect(const QImage &image, std::vector<QRectF> &boxes);
    void draw(QImage &image, const std::vector<QRectF> &boxes);
    cv::Ptr<cv::FaceDetectorYN> detector;
    FaceStabilizer stabilizer;
    std::vector<std::array<cv::Point2f, 5>> landmarks;
    ExpressionRecognizer expression;
    std::map<quint64, ExpressionState> expressions;
};
