#include "facedetector.h"
#include <QCoreApplication>
#include <QFile>
#include <QPainter>
#include <QElapsedTimer>
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <cmath>

bool FaceDetector::load()
{
    try {
        // 从内存读取 ONNX，兼容包含中文的程序安装路径。
        detector.release(); reset();
        QFile model(QCoreApplication::applicationDirPath() + "/models/face_detection_yunet_2023mar.onnx");
        if (!model.open(QIODevice::ReadOnly)) {
            error = QString::fromUtf8("人脸模型无法打开"); return false;
        }
        const QByteArray data = model.readAll();
        if (model.error() != QFileDevice::NoError || data.isEmpty()) {
            error = QString::fromUtf8("人脸模型读取失败或内容为空"); return false;
        }
        const std::vector<uchar> bytes(data.begin(), data.end());
        detector = cv::FaceDetectorYN::create("onnx", bytes, std::vector<uchar>(), cv::Size(640, 384),
                                             0.8f, 0.3f, 5000, cv::dnn::DNN_BACKEND_OPENCV,
                                             cv::dnn::DNN_TARGET_CPU);
        if (detector.empty()) {
            error = QString::fromUtf8("人脸模型无效"); return false;
        }
        error.clear(); return true;
    } catch (const cv::Exception &exception) {
        error = QString::fromUtf8("人脸模型加载失败：%1").arg(QString::fromLocal8Bit(exception.what()));
        return false;
    }
}

bool FaceDetector::detect(const QImage &image, std::vector<QRectF> &boxes)
{
    detectionMs = 0;
    if (detector.empty()) return false;
    QElapsedTimer timer; timer.start();
    try {
        if (image.isNull()) { error = QString::fromUtf8("人脸检测图像为空"); return false; }
        QImage rgb = image.convertToFormat(QImage::Format_RGB888);
        cv::Mat input(rgb.height(), rgb.width(), CV_8UC3, rgb.bits(), size_t(rgb.bytesPerLine()));
        cv::Mat bgr, small;
        cv::cvtColor(input, bgr, cv::COLOR_RGB2BGR);
        const double scale = qMin(1.0, 640.0 / image.width());
        cv::resize(bgr, small, cv::Size(qMax(1, qRound(image.width() * scale)),
                                       qMax(1, qRound(image.height() * scale))), 0, 0, cv::INTER_AREA);
        detector->setInputSize(small.size());
        cv::Mat faces;
        detector->detect(small, faces);
        const double sx = double(image.width()) / small.cols, sy = double(image.height()) / small.rows;
        for (int i = 0; i < faces.rows; ++i) {
            const float *face = faces.ptr<float>(i);
            if (!std::isfinite(face[0]) || !std::isfinite(face[1]) ||
                !std::isfinite(face[2]) || !std::isfinite(face[3]) || face[2] <= 0 || face[3] <= 0) continue;
            const QRectF box = QRectF(face[0] * sx, face[1] * sy, face[2] * sx, face[3] * sy)
                    .intersected(QRectF(image.rect()));
            if (box.width() > 0 && box.height() > 0) boxes.push_back(box);
        }
        detectionMs = int(timer.elapsed());
        error.clear();
        return true;
    } catch (const cv::Exception &exception) {
        error = QString::fromUtf8("人脸检测失败：%1").arg(QString::fromLocal8Bit(exception.what()));
        detectionMs = int(timer.elapsed());
        return false;
    }
}

void FaceDetector::draw(QImage &image, const std::vector<QRectF> &boxes)
{
    QPainter painter(&image);
    painter.setPen(QPen(QColor(0, 255, 0), 3));
    for (const QRectF &box : boxes) painter.drawRect(box);
}

int FaceDetector::annotate(QImage &image)
{
    std::vector<QRectF> boxes;
    if (!detect(image, boxes)) { reset(); return -1; }
    draw(image, boxes);
    return int(boxes.size());
}

int FaceDetector::annotateVideo(QImage &image, qint64 now)
{
    std::vector<QRectF> boxes;
    if (!detect(image, boxes)) { reset(); return -1; }
    boxes = stabilizer.update(boxes, image.size(), now);
    draw(image, boxes);
    return int(boxes.size());
}
