#include "facedetector.h"
#include <QCoreApplication>
#include <QFile>
#include <QPainter>
#include <QElapsedTimer>
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <cmath>
#include <QFontMetrics>
#include <cstdio>

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
    landmarks.clear();
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
            if (box.width() > 0 && box.height() > 0) {
                std::array<cv::Point2f, 5> points;
                for (int j=0;j<5;++j) points[size_t(j)]=cv::Point2f(float(face[4+j*2]*sx),float(face[5+j*2]*sy));
                boxes.push_back(box); landmarks.push_back(points);
            }
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
    expressionMs = 0; expressionCount = 0;
    std::vector<QRectF> boxes;
    if (!detect(image, boxes)) { reset(); return -1; }
    const auto faces = stabilizer.updateFaces(boxes, image.size(), now);
    const QImage source = image;
    std::vector<QString> labels;
    for (const auto &face : faces) {
        QString label=QString::fromUtf8("无法判断");
        bool eligible=face.detection>=0;
        if (eligible) {
            const QRectF &box=boxes[size_t(face.detection)];
            eligible=box.width()>=48 && box.height()>=48;
            for (const auto &point : landmarks[size_t(face.detection)])
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x<0 || point.y<0 ||
                    point.x>=image.width() || point.y>=image.height()) eligible=false;
        }
        if (eligible) {
            ExpressionState &state=expressions[face.id];
            if (state.due(now)) {
                ExpressionScores scores;
                if (expression.infer(source,landmarks[size_t(face.detection)],scores)) {
                    state.accept(scores,now); expressionCount++; expressionError.clear();
                } else {
                    state=ExpressionState(); expressionError=expression.error;
                }
                state.attempted=now;
                expressionMs+=expression.inferenceMs;
            }
            label=state.label(now);
        }
        labels.push_back(label);
    }
    for (auto it=expressions.begin();it!=expressions.end();) {
        bool active=false;
        for (const auto &face : faces) if (face.id==it->first) active=true;
        if (!active || now-it->second.attempted > 600) it=expressions.erase(it);
        else ++it;
    }
    QPainter painter(&image);
    QFont font=painter.font(); font.setPixelSize(16); painter.setFont(font);
    painter.setPen(QPen(QColor(0,255,0),3));
    for (size_t i=0;i<faces.size();++i) {
        painter.drawRect(faces[i].box);
        const QFontMetrics metrics(painter.font());
        const int width=qMin(image.width(),metrics.horizontalAdvance(labels[i])+8);
        const int height=metrics.height()+4;
        const int x=qBound(0,qRound(faces[i].box.x()),qMax(0,image.width()-width));
        const int y=qBound(0,qRound(faces[i].box.y())-height,qMax(0,image.height()-height));
        painter.fillRect(QRect(x,y,width,height),QColor(0,0,0,190));
        painter.drawText(QRect(x+4,y,width-4,height),Qt::AlignVCenter,labels[i]);
    }
    return int(faces.size());
}

int FaceDetector::annotateExpression(QImage &image)
{
    expressionCount=0; expressionMs=0;
    std::vector<QRectF> boxes;
    if (!detect(image,boxes)) return -1;
    const QImage source=image;
    std::vector<QString> labels;
    for (size_t i=0;i<boxes.size();++i) {
        ExpressionScores scores;
        if (!expression.infer(source,landmarks[i],scores)) { expressionError=expression.error; return -1; }
        ExpressionState state; state.accept(scores,0);
        labels.push_back(state.label(0));
        expressionMs+=expression.inferenceMs; expressionCount++;
        fprintf(stdout,"Expression face %d: %s\n",int(i),state.label(0).toUtf8().constData());
    }
    draw(image,boxes); expressionError.clear();
    QPainter painter(&image);
    QFont font=painter.font(); font.setPixelSize(16); painter.setFont(font);
    painter.setPen(QColor(0,255,0));
    for (size_t i=0;i<boxes.size();++i)
        painter.drawText(qBound(0,qRound(boxes[i].x()),qMax(0,image.width()-120)),
                         qBound(16,qRound(boxes[i].y())-4,qMax(16,image.height()-4)),labels[i]);
    return int(boxes.size());
}
