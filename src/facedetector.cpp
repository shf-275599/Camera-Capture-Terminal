#include "facedetector.h"
#include <QCoreApplication>
#include <QFile>
#include <QPainter>
#include <opencv2/imgproc.hpp>

bool FaceDetector::load()
{
    try {
        // 从内存读取 XML，兼容包含中文的程序安装路径。
        QFile model(QCoreApplication::applicationDirPath() + "/models/haarcascade_frontalface_default.xml");
        if (!model.open(QIODevice::ReadOnly)) {
            error = QString::fromUtf8("人脸模型无法打开"); return false;
        }
        const QByteArray data = model.readAll();
        cv::FileStorage storage(std::string(data.constData(), size_t(data.size())),
                                cv::FileStorage::READ | cv::FileStorage::MEMORY);
        if (!storage.isOpened() || !cascade.read(storage.getFirstTopLevelNode()) || cascade.empty()) {
            error = QString::fromUtf8("人脸模型无效"); return false;
        }
        error.clear(); return true;
    } catch (const cv::Exception &exception) {
        error = QString::fromUtf8("人脸模型加载失败：%1").arg(QString::fromLocal8Bit(exception.what()));
        return false;
    }
}

int FaceDetector::annotate(QImage &image)
{
    if (cascade.empty()) return -1;
    try {
        QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
        cv::Mat input(gray.height(), gray.width(), CV_8UC1, gray.bits(), size_t(gray.bytesPerLine()));
        cv::Mat small;
        cv::resize(input, small, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
        cv::equalizeHist(small, small);
        std::vector<cv::Rect> faces;
        cascade.detectMultiScale(small, faces, 1.1, 5, 0, cv::Size(24, 24));
        QPainter painter(&image);
        painter.setPen(QPen(QColor(0, 255, 0), 3));
        for (const cv::Rect &face : faces)
            painter.drawRect(QRect(face.x * 2, face.y * 2, face.width * 2, face.height * 2));
        error.clear();
        return int(faces.size());
    } catch (const cv::Exception &exception) {
        error = QString::fromUtf8("人脸检测失败：%1").arg(QString::fromLocal8Bit(exception.what()));
        return -1;
    }
}
