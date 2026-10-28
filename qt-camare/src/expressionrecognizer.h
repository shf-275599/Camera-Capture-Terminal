#pragma once

#include <QImage>
#include <QString>
#include <array>
#include <opencv2/dnn.hpp>

typedef std::array<float, 7> ExpressionScores;

class ExpressionState
{
public:
    bool due(qint64 now) const { return attempted < 0 || now - attempted >= 200; }
    void accept(const ExpressionScores &scores, qint64 now);
    QString label(qint64 now) const;
    qint64 updated = -1;
    qint64 attempted = -1;
    ExpressionScores scores{};
};

class ExpressionRecognizer
{
public:
    bool load();
    bool infer(const QImage &image, const std::array<cv::Point2f, 5> &points, ExpressionScores &scores);
    static bool probabilities(const cv::Mat &output, ExpressionScores &scores);
    static bool align(const cv::Mat &image, const std::array<cv::Point2f, 5> &points, cv::Mat &aligned);
    QString error;
    int inferenceMs = 0;
private:
    cv::dnn::Net net;
};

int runExpressionStateTest();
