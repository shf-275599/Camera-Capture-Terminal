#include "expressionrecognizer.h"
#include <QCoreApplication>
#include <QFile>
#include <QElapsedTimer>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

void ExpressionState::accept(const ExpressionScores &values, qint64 now)
{
    const double alpha = updated < 0 || now < updated || now - updated > 600 ?
                1.0 : 1.0 - std::exp(-double(now - updated) / 400.0);
    for (size_t i = 0; i < scores.size(); ++i) scores[i] += float(alpha * (values[i] - scores[i]));
    updated = now;
    attempted = now;
}

QString ExpressionState::label(qint64 now) const
{
    if (updated < 0 || now < updated || now - updated > 600) return QString::fromUtf8("无法判断");
    const size_t index = size_t(std::max_element(scores.begin(), scores.end()) - scores.begin());
    if (scores[index] < 0.6f) return QString::fromUtf8("无法判断");
    const char *names[] = {"愤怒", "厌恶", "恐惧", "高兴", "中性", "悲伤", "惊讶"};
    return QString::fromUtf8("%1 %2%").arg(QString::fromUtf8(names[index])).arg(qRound(scores[index] * 100));
}

bool ExpressionRecognizer::load()
{
    net = cv::dnn::Net();
    try {
        QFile model(QCoreApplication::applicationDirPath() + "/models/facial_expression_recognition_mobilefacenet_2022july.onnx");
        if (!model.open(QIODevice::ReadOnly)) { error = QString::fromUtf8("表情模型无法打开"); return false; }
        const QByteArray data = model.readAll();
        if (data.isEmpty() || model.error() != QFileDevice::NoError) {
            error = QString::fromUtf8("表情模型读取失败或内容为空"); return false;
        }
        net = cv::dnn::readNetFromONNX(std::vector<uchar>(data.begin(), data.end()));
        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        error.clear(); return !net.empty();
    } catch (const cv::Exception &exception) {
        error = QString::fromUtf8("表情模型加载失败：%1").arg(QString::fromLocal8Bit(exception.what()));
        return false;
    }
}

bool ExpressionRecognizer::probabilities(const cv::Mat &output, ExpressionScores &scores)
{
    if (output.total() != 7 || output.type() != CV_32F || !output.isContinuous()) return false;
    const float *values = output.ptr<float>();
    float maximum = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < 7; ++i) {
        if (!std::isfinite(values[i])) return false;
        maximum = std::max(maximum, values[i]);
    }
    double total = 0;
    for (int i = 0; i < 7; ++i) { scores[size_t(i)] = std::exp(values[i] - maximum); total += scores[size_t(i)]; }
    for (float &score : scores) score = float(score / total);
    return true;
}

bool ExpressionRecognizer::align(const cv::Mat &image, const std::array<cv::Point2f, 5> &points, cv::Mat &aligned)
{
    const cv::Point2f standard[] = {{38.2946f,51.6963f},{73.5318f,51.5014f},{56.0252f,71.7366f},
                                   {41.5493f,92.3655f},{70.7299f,92.2041f}};
    if (image.empty()) return false;
    cv::Mat equations(10, 4, CV_64F, cv::Scalar(0)), targets(10, 1, CV_64F);
    for (int i = 0; i < 5; ++i) {
        if (!std::isfinite(points[size_t(i)].x) || !std::isfinite(points[size_t(i)].y)) return false;
        // 按官方方法求标准五点到观测五点的相似变换，再求逆用于对齐。
        const double x = standard[i].x, y = standard[i].y;
        equations.at<double>(i,0)=x; equations.at<double>(i,1)=y; equations.at<double>(i,2)=1;
        equations.at<double>(i+5,0)=y; equations.at<double>(i+5,1)=-x; equations.at<double>(i+5,3)=1;
        targets.at<double>(i)=points[size_t(i)].x; targets.at<double>(i+5)=points[size_t(i)].y;
    }
    cv::Mat solution;
    if (!cv::solve(equations, targets, solution, cv::DECOMP_SVD)) return false;
    const double a=solution.at<double>(0), b=solution.at<double>(1);
    if (a*a+b*b < 1e-8) return false;
    cv::Mat transform=(cv::Mat_<double>(2,3) << a,b,solution.at<double>(2),-b,a,solution.at<double>(3));
    cv::Mat inverse; cv::invertAffineTransform(transform, inverse);
    cv::warpAffine(image, aligned, inverse, cv::Size(112,112));
    return true;
}

bool ExpressionRecognizer::infer(const QImage &image, const std::array<cv::Point2f, 5> &points, ExpressionScores &scores)
{
    inferenceMs = 0;
    if (net.empty()) return false;
    QElapsedTimer timer; timer.start();
    try {
        QImage rgb=image.convertToFormat(QImage::Format_RGB888);
    cv::Mat input(rgb.height(),rgb.width(),CV_8UC3,rgb.bits(),size_t(rgb.bytesPerLine())), aligned;
        if (!align(input, points, aligned)) { error=QString::fromUtf8("表情关键点无效"); return false; }
        cv::Mat blob=cv::dnn::blobFromImage(aligned,1.0/127.5,cv::Size(112,112),cv::Scalar(127.5,127.5,127.5));
        net.setInput(blob,"data");
        if (!probabilities(net.forward("label"),scores)) {
            error=QString::fromUtf8("表情模型输出无效"); return false;
        }
        inferenceMs=int(timer.elapsed()); error.clear(); return true;
    } catch (const cv::Exception &exception) {
        inferenceMs=int(timer.elapsed());
        error=QString::fromUtf8("表情推理失败：%1").arg(QString::fromLocal8Bit(exception.what()));
        return false;
    }
}

int runExpressionStateTest()
{
    cv::Mat logits(1,7,CV_32F,cv::Scalar(0)); logits.at<float>(3)=10;
    ExpressionScores scores;
    bool ok=ExpressionRecognizer::probabilities(logits,scores) && scores[3]>0.99f;
    ExpressionState state;
    ok=ok && state.due(0); state.accept(scores,0);
    ok=ok && !state.due(199) && state.due(200) && state.label(0).startsWith(QString::fromUtf8("高兴"));
    ExpressionScores neutral{}; neutral[4]=1;
    state.accept(neutral,200);
    ok=ok && state.scores[3]>state.scores[4] && state.label(801)==QString::fromUtf8("无法判断");
    ExpressionState other; other.accept(neutral,0);
    ok=ok && other.label(0).startsWith(QString::fromUtf8("中性"));
    state=ExpressionState(); ok=ok && state.due(0) && state.label(0)==QString::fromUtf8("无法判断");
    logits.setTo(0); ExpressionRecognizer::probabilities(logits,scores); state.accept(scores,0);
    ok=ok && state.label(0)==QString::fromUtf8("无法判断");
    logits.at<float>(0)=std::numeric_limits<float>::quiet_NaN();
    ok=ok && !ExpressionRecognizer::probabilities(logits,scores);
    std::array<cv::Point2f,5> points={{{38.2946f,51.6963f},{73.5318f,51.5014f},{56.0252f,71.7366f},
                                     {41.5493f,92.3655f},{70.7299f,92.2041f}}};
    cv::Mat image(112,112,CV_8UC3,cv::Scalar(10,20,30)), aligned;
    ok=ok && ExpressionRecognizer::align(image,points,aligned) && cv::norm(image,aligned,cv::NORM_INF)==0;
    points.fill(cv::Point2f(0,0)); ok=ok && !ExpressionRecognizer::align(image,points,aligned);
    for (int i=0;i<7;++i) {
        logits.setTo(0); logits.at<float>(i)=10;
        ok=ok && ExpressionRecognizer::probabilities(logits,scores);
        ExpressionState category; category.accept(scores,0);
        const char *names[]={"愤怒","厌恶","恐惧","高兴","中性","悲伤","惊讶"};
        ok=ok && category.label(0).startsWith(QString::fromUtf8(names[i]));
    }
    ExpressionState failed; failed.attempted=0;
    ok=ok && !failed.due(199) && failed.due(200);
    fprintf(stdout,"Expression state/alignment: %s\n",ok?"PASS":"FAIL"); fflush(stdout);
    return ok?0:1;
}
