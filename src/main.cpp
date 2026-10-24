#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QTcpSocket>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QImageReader>
#include <QBuffer>
#include <QTimer>
#include <QElapsedTimer>
#include <QPixmap>
#include <QDebug>
#include <QTcpServer>
#include <cstdio>
#include <atomic>
#include <QPainter>
#include "facedetector.h"

static const int MaxJpeg = 512 * 1024;
static const int MaxHeader = 8192;

static QByteArray headerValue(const QByteArray &header, const QByteArray &name)
{
    QByteArray result;
    for (const QByteArray &line : header.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().toLower() == name) {
            if (!result.isEmpty()) return QByteArray();
            result = line.mid(colon + 1).trimmed();
        }
    }
    return result;
}

class CameraThread : public QThread
{
    class Processor : public QThread {
        CameraThread *owner;
    public:
        explicit Processor(CameraThread *camera) : owner(camera) {}
        void run() override;
    };
    QWaitCondition frameReady;
    QByteArray pendingJpeg;
    quint64 connectionEpoch = 0;
public:
    QString host;
    quint16 port = 80;
    QMutex mutex;
    QImage latest;
    QString state;
    quint64 received = 0;
    quint64 decoded = 0;
    quint64 dropped = 0;
    int processMs = 0;
    int detectionMs = 0;
    std::atomic<int> testProcessingDelayMs{0};
    quint32 sequence = 0;
    int faceCount = 0;
    QString faceError;

    void setState(const QString &text) { QMutexLocker lock(&mutex); state = text; }
    void run() override
    {
        Processor processor(this);
        processor.start();
        while (!isInterruptionRequested()) {
            { QMutexLocker lock(&mutex); ++connectionEpoch; pendingJpeg.clear(); }
            QTcpSocket socket; // socket 与阻塞接收都归此工作线程所有。
            socket.setReadBufferSize(MaxJpeg + 32);
            setState(QString::fromUtf8("正在连接"));
            socket.connectToHost(host, port);
            QElapsedTimer connecting;
            connecting.start();
            while (socket.state() != QAbstractSocket::ConnectedState &&
                   !isInterruptionRequested() && connecting.elapsed() < 3000) {
                if (socket.waitForConnected(100)) break;
                if (socket.state() == QAbstractSocket::UnconnectedState) break;
            }
            if (socket.state() == QAbstractSocket::ConnectedState) {
                const QByteArray request = "GET /stream HTTP/1.1\r\nHost: " + host.toUtf8() +
                        ":" + QByteArray::number(port) +
                        "\r\nAccept: multipart/x-mixed-replace\r\nConnection: close\r\n\r\n";
                socket.write(request);
                QByteArray pending;
                QByteArray boundary;
                bool responseRead = false;
                int frameSize = -1;
                bool valid = true;
                QElapsedTimer progress;
                progress.start();
                while (!isInterruptionRequested() && valid) {
                    if (!socket.bytesAvailable()) socket.waitForReadyRead(100);
                    const QByteArray chunk = socket.read(MaxHeader + MaxJpeg - pending.size());
                    if (!chunk.isEmpty()) { pending += chunk; progress.restart(); }
                    while (valid) {
                        if (!responseRead || frameSize < 0) {
                            if (responseRead && pending.startsWith("\r\n")) pending.remove(0, 2);
                            const int end = pending.indexOf("\r\n\r\n");
                            if (end < 0) {
                                if (pending.size() > MaxHeader) {
                                    valid = false; setState(QString::fromUtf8("协议错误：头部过长"));
                                }
                                break;
                            }
                            if (end > MaxHeader) {
                                valid = false; setState(QString::fromUtf8("协议错误：头部过长")); break;
                            }
                            const QByteArray header = pending.left(end);
                            pending.remove(0, end + 4);
                            if (!responseRead) {
                                const QList<QByteArray> status = header.left(header.indexOf('\r')).split(' ');
                                if (status.size() < 2 || (status[0] != "HTTP/1.1" && status[0] != "HTTP/1.0") || status[1] != "200") {
                                    valid = false;
                                    setState(status.size() > 1 && status[1] == "503"
                                             ? QString::fromUtf8("设备忙碌或 OTA 中（HTTP 503），等待重连")
                                             : QString::fromUtf8("HTTP 响应错误：%1").arg(QString::fromLatin1(header.split('\n').first().trimmed())));
                                    break;
                                }
                                const QByteArray type = headerValue(header, "content-type");
                                const QList<QByteArray> parameters = type.split(';');
                                for (int i = 1; i < parameters.size(); ++i) {
                                    const QByteArray parameter = parameters[i].trimmed();
                                    const int equal = parameter.indexOf('=');
                                    if (parameter.left(equal).trimmed().toLower() == "boundary") {
                                        boundary = parameter.mid(equal + 1).trimmed();
                                        if (boundary.startsWith('"') && boundary.endsWith('"')) boundary = boundary.mid(1, boundary.size() - 2);
                                    }
                                }
                                if (parameters.first().trimmed().toLower() != "multipart/x-mixed-replace" ||
                                    boundary.isEmpty() || boundary.size() > 70 ||
                                    boundary.contains('\r') || boundary.contains('\n') ||
                                    !headerValue(header, "transfer-encoding").isEmpty()) {
                                    valid = false; setState(QString::fromUtf8("协议错误：无效的 MJPEG 响应")); break;
                                }
                                boundary.prepend("--");
                                responseRead = true;
                                setState(QString::fromUtf8("已连接，等待画面"));
                                continue;
                            }
                            bool sizeOk = false;
                            const QByteArray length = headerValue(header, "content-length");
                            const uint size = length.toUInt(&sizeOk);
                            if (header.split('\n').first().trimmed() != boundary ||
                                headerValue(header, "content-type").toLower() != "image/jpeg" ||
                                !sizeOk || size < 4 || size > uint(MaxJpeg)) {
                                valid = false; setState(QString::fromUtf8("协议错误：无效的 JPEG 帧头或长度")); break;
                            }
                            frameSize = int(size);
                        }
                        if (pending.size() < frameSize + 2) break;
                        if (pending.mid(frameSize, 2) != "\r\n") {
                            valid = false; setState(QString::fromUtf8("协议错误：JPEG 帧结束符无效")); break;
                        }
                        QByteArray jpeg = pending.left(frameSize);
                        pending.remove(0, frameSize + 2);
                        frameSize = -1;
                        QMutexLocker lock(&mutex);
                        received++;
                        if (!pendingJpeg.isEmpty()) dropped++;
                        pendingJpeg = jpeg;
                        frameReady.wakeOne();
                    }
                    if (socket.state() == QAbstractSocket::UnconnectedState || progress.elapsed() > 5000) {
                        if (valid) setState(QString::fromUtf8("连接中断或接收超时，等待重连"));
                        break;
                    }
                }
            } else setState(QString::fromUtf8("连接失败：%1，等待重连").arg(socket.errorString()));
            socket.abort();
            { QMutexLocker lock(&mutex); ++connectionEpoch; pendingJpeg.clear(); }
            if (!isInterruptionRequested()) {
                for (int i = 0; i < 10 && !isInterruptionRequested(); ++i) msleep(100);
            }
        }
        processor.requestInterruption();
        frameReady.wakeOne();
        processor.wait();
        setState(QString::fromUtf8("已断开"));
    }
};

void CameraThread::Processor::run()
{
    FaceDetector detector;
    detector.load();
    QElapsedTimer clock; clock.start();
    quint64 detectorEpoch = 0;
    while (!isInterruptionRequested()) {
        QByteArray jpeg;
        quint64 epoch;
        {
            QMutexLocker lock(&owner->mutex);
            while (owner->pendingJpeg.isEmpty() && !isInterruptionRequested())
                owner->frameReady.wait(&owner->mutex, 100);
            if (isInterruptionRequested()) break;
            jpeg.swap(owner->pendingJpeg);
            epoch = owner->connectionEpoch;
        }
        QElapsedTimer processing;
        processing.start();
        const int testDelay = owner->testProcessingDelayMs.load();
        if (testDelay) msleep(testDelay);
        QImage image;
        if (jpeg.startsWith(QByteArray::fromHex("ffd8")) && jpeg.endsWith(QByteArray::fromHex("ffd9"))) {
            QBuffer buffer(&jpeg);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer, "JPEG");
            if (reader.size() == QSize(800, 480)) image = reader.read();
        }
        if (epoch != detectorEpoch) { detector.reset(); detectorEpoch = epoch; }
        const int faces = image.isNull() ? -1 : detector.annotateVideo(image, clock.elapsed());
        if (image.isNull()) detector.reset();
        QMutexLocker lock(&owner->mutex);
        if (epoch != owner->connectionEpoch) continue;
        owner->processMs = int(processing.elapsed());
        owner->detectionMs = image.isNull() ? 0 : detector.detectionMs;
        owner->faceCount = faces;
        owner->faceError = detector.error;
        if (!image.isNull()) {
            owner->latest = image; owner->decoded++; owner->sequence++;
            owner->state = QString::fromUtf8("正在播放");
        } else owner->state = QString::fromUtf8("JPEG 解码失败，等待下一帧");
    }
}

class Window : public QWidget
{
    CameraThread worker;
    QLineEdit *address;
    QSpinBox *port;
    QPushButton *button;
    QLabel *picture;
    QLabel *status;
    QElapsedTimer elapsed;
    quint64 lastReceived = 0;
    quint64 shown = 0;
    QString rate = "0.0";
public:
    void connectDevice(const QString &host) { address->setText(host); button->click(); }
    Window()
    {
        setWindowTitle(QString::fromUtf8("OV5640 YuNet 人脸检测"));
        resize(960, 650);
        address = new QLineEdit("192.168.10.2");
        port = new QSpinBox; port->setRange(1, 65535); port->setValue(80);
        button = new QPushButton(QString::fromUtf8("连接"));
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel("IP")); row->addWidget(address);
        row->addWidget(new QLabel(QString::fromUtf8("端口"))); row->addWidget(port); row->addWidget(button);
        picture = new QLabel(QString::fromUtf8("等待 JPEG 画面"));
        picture->setAlignment(Qt::AlignCenter); picture->setMinimumSize(400, 240);
        picture->setStyleSheet("background:#18212b;color:white");
        status = new QLabel(QString::fromUtf8("未连接"));
        auto *layout = new QVBoxLayout(this);
        layout->addLayout(row); layout->addWidget(picture, 1); layout->addWidget(status);
        connect(button, &QPushButton::clicked, this, [this] {
            if (worker.isRunning()) {
                worker.requestInterruption(); worker.wait();
                button->setText(QString::fromUtf8("连接")); address->setEnabled(true); port->setEnabled(true);
            } else {
                worker.host = address->text().trimmed();
                if (worker.host.isEmpty() || worker.host.contains('\r') || worker.host.contains('\n')) {
                    status->setText(QString::fromUtf8("请输入有效的设备 IP 或主机名")); return;
                }
                worker.port = quint16(port->value());
                { QMutexLocker lock(&worker.mutex);
                   worker.latest = QImage(); worker.received = 0; worker.decoded = 0; worker.sequence = 0;
                   worker.faceCount = 0; worker.faceError.clear(); worker.dropped = 0; worker.processMs = 0; worker.detectionMs = 0; }
                shown = 0; lastReceived = 0; rate = "0.0"; elapsed.restart();
                picture->clear(); picture->setText(QString::fromUtf8("等待 JPEG 画面"));
                worker.start(); button->setText(QString::fromUtf8("断开"));
                address->setEnabled(false); port->setEnabled(false);
            }
        });
        auto *timer = new QTimer(this);
        elapsed.start();
        connect(timer, &QTimer::timeout, this, [this] {
            QMutexLocker lock(&worker.mutex);
            if (elapsed.elapsed() >= 1000) {
                rate = QString::number((worker.received - lastReceived) * 1000.0 / elapsed.elapsed(), 'f', 1);
                lastReceived = worker.received; elapsed.restart();
            }
            // 定时取单一最新图像，避免跨线程信号无限积压。
            if (!worker.latest.isNull() && (shown != worker.decoded ||
                picture->property("imageSize").toSize() != picture->size())) {
                picture->setPixmap(QPixmap::fromImage(worker.latest).scaled(
                    picture->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                picture->setProperty("imageSize", picture->size()); shown = worker.decoded;
            }
            status->setText(QString::fromUtf8("%1 | 接收 %2 fps | 完整帧 %3 | 解码成功 %4 | 人脸 %5%6 | 跳过 %7 | 处理 %8 ms | 检测 %9 ms")
                            .arg(worker.state).arg(rate).arg(worker.received).arg(worker.decoded)
                            .arg(qMax(0, worker.faceCount))
                             .arg(worker.faceError.isEmpty() ? QString() : " | " + worker.faceError)
                              .arg(worker.dropped).arg(worker.processMs).arg(worker.detectionMs));
        });
        timer->start(33);
    }
    ~Window() override { worker.requestInterruption(); worker.wait(); }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.arguments().contains("--stabilizer-test")) return runFaceStabilizerTest();
    if (app.arguments().contains("--face-test")) {
        const int index = app.arguments().indexOf("--face-test");
        if (index + 1 >= app.arguments().size()) return 2;
        FaceDetector detector;
        if (!detector.load()) { qCritical() << detector.error; return 3; }
        QImage face(app.arguments()[index + 1]);
        if (face.isNull()) return 4;
        const QImage source = face;
        const int found = detector.annotate(face);
        bool greenBox = false;
        for (int y = 0; y < face.height() && !greenBox; ++y)
            for (int x = 0; x < face.width(); ++x)
                if (face.pixelColor(x, y) == QColor(0, 255, 0)) { greenBox = true; break; }
        QImage blank(800, 480, QImage::Format_RGB32); blank.fill(Qt::white);
        const int empty = detector.annotate(blank);
        QImage pair(source.width() * 2, source.height(), QImage::Format_RGB32);
        { QPainter painter(&pair); painter.drawImage(0, 0, source); painter.drawImage(source.width(), 0, source); }
        const int multiple = detector.annotate(pair);
        QImage wide(800, 480, QImage::Format_RGB32); wide.fill(Qt::white);
        { QPainter painter(&wide);
          painter.drawImage(QRect(160, 0, 480, 480), source); }
        const int resizedFaces = detector.annotate(wide);
        detector.reset();
        QImage firstVideo = source, secondVideo = source;
        const int pendingFaces = detector.annotateVideo(firstVideo, 0);
        const int stableFaces = detector.annotateVideo(secondVideo, 33);
        detector.reset();
        QImage resetVideo = source;
        const int resetFaces = detector.annotateVideo(resetVideo, 66);
        QImage confirmedVideo = source;
        detector.annotateVideo(confirmedVideo, 99);
        QImage invalidVideo;
        const int invalidFaces = detector.annotateVideo(invalidVideo, 132);
        QImage recoveredVideo = source;
        const int recoveredFaces = detector.annotateVideo(recoveredVideo, 165);
        qint64 totalMs = 0;
        int minimumMs = 1000000, maximumMs = 0;
        bool repeatedDetection = true;
        for (int i = 0; i < 10; ++i) {
            QImage sample = wide;
            if (detector.annotate(sample) < 1) repeatedDetection = false;
            totalMs += detector.detectionMs;
            minimumMs = qMin(minimumMs, detector.detectionMs);
            maximumMs = qMax(maximumMs, detector.detectionMs);
        }
        fprintf(stdout, "YuNet detection (800x480, 10 runs): mean=%.1f ms min=%d ms max=%d ms\n",
                totalMs / 10.0, minimumMs, maximumMs);
        fprintf(stdout, "Face test: faces=%d green_box=%d blank=%d multiple=%d pending=%d stable=%d reset=%d invalid=%d recovered=%d resized=%d\n",
                found, int(greenBox), empty, multiple, pendingFaces, stableFaces, resetFaces,
                invalidFaces, recoveredFaces, resizedFaces); fflush(stdout);
        return found >= 1 && greenBox && empty == 0 && multiple >= 2 &&
                pendingFaces == 0 && stableFaces >= 1 && resetFaces == 0 &&
                invalidFaces == -1 && recoveredFaces == 0 && resizedFaces >= 1 && repeatedDetection ? 0 : 1;
    }
    if (app.arguments().contains("--device-test")) {
        const int index = app.arguments().indexOf("--device-test");
        if (index + 1 >= app.arguments().size()) return 2;
        CameraThread worker;
        worker.host = app.arguments()[index + 1]; worker.start();
        QElapsedTimer deadline; deadline.start(); bool ok = false;
        while (deadline.elapsed() < 20000) {
            { QMutexLocker lock(&worker.mutex);
              ok = worker.decoded >= 5 && worker.latest.size() == QSize(800, 480) &&
                      worker.faceCount >= 0 && worker.faceError.isEmpty(); }
            if (ok) break;
            QThread::msleep(20);
        }
        { QMutexLocker lock(&worker.mutex);
          fprintf(stdout, "Qt device test: decoded=%llu faces=%d state=%s detection=%s\n",
                  static_cast<unsigned long long>(worker.decoded), worker.faceCount,
                  worker.state.toUtf8().constData(), worker.faceError.toUtf8().constData());
          fflush(stdout); }
        worker.requestInterruption(); worker.wait();
        qInfo() << (ok ? "QT CAMERA DEVICE PASS" : "QT CAMERA DEVICE FAIL") << worker.decoded;
        return ok ? 0 : 1;
    }
    if (app.arguments().contains("--protocol-test")) {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) return 2;
        CameraThread worker;
        worker.host = "127.0.0.1"; worker.port = server.serverPort(); worker.start();
        QImage source(800, 480, QImage::Format_RGB32); source.fill(Qt::green);
        QByteArray jpeg; QBuffer buffer(&jpeg); buffer.open(QIODevice::WriteOnly);
        if (!source.save(&buffer, "JPEG")) { worker.requestInterruption(); worker.wait(); return 3; }
        const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=\"test-camera\"\r\nConnection: close\r\n\r\n";
        const QByteArray frameHeader = "--test-camera\r\nContent-Type: image/jpeg\r\nContent-Length: ";
        const QByteArray frame = frameHeader + QByteArray::number(jpeg.size()) + "\r\n\r\n" + jpeg + "\r\n";
        auto checkRequest = [](QTcpSocket *peer) {
            QByteArray request;
            QElapsedTimer deadline; deadline.start();
            while (!request.contains("\r\n\r\n") && deadline.elapsed() < 3000) {
                if (!peer->bytesAvailable()) peer->waitForReadyRead(100);
                request += peer->readAll();
            }
            return request.startsWith("GET /stream HTTP/1.1\r\n") && request.contains("\r\nHost: ");
        };
        auto waitFrames = [&worker](quint64 count) {
            QElapsedTimer deadline; deadline.start();
            while (deadline.elapsed() < 3000) {
                { QMutexLocker lock(&worker.mutex);
                  if (worker.decoded >= count) return worker.decoded == count && worker.latest.size() == QSize(800, 480); }
                QThread::msleep(10);
            }
            return false;
        };
        bool ok = server.waitForNewConnection(4000);
        QTcpSocket *peer = ok ? server.nextPendingConnection() : nullptr;
        if (peer) {
            ok = checkRequest(peer);
            // HTTP 头、帧头和 JPEG 均拆包，随后粘连下一完整帧。
            const QByteArray first = response + frame;
            for (int offset = 0; offset < first.size(); offset += 137) {
                peer->write(first.mid(offset, 137)); peer->waitForBytesWritten(1000);
                QThread::msleep(1);
            }
            ok = waitFrames(1) && ok;
            peer->write(frame); peer->waitForBytesWritten(1000);
            ok = waitFrames(2) && ok;
            // 超限头应被拒绝并断线，随后自动重连不携带旧解析状态。
            const QByteArray invalid = frameHeader + QByteArray::number(MaxJpeg + 1) + "\r\n\r\n";
            peer->write(invalid); peer->waitForBytesWritten(1000);
            peer->waitForDisconnected(3000);
            ok = ok && peer->state() == QAbstractSocket::UnconnectedState;
            delete peer;
            ok = ok && server.waitForNewConnection(4000);
            if (ok) {
                peer = server.nextPendingConnection(); ok = checkRequest(peer) && ok;
                peer->write(response + frame); peer->waitForBytesWritten(1000);
                ok = waitFrames(3) && ok;
                // 半帧断线不能发布损坏图像，下一次连接必须重新读取头。
                peer->write(frame.left(frame.size()/2)); peer->waitForBytesWritten(1000);
                peer->abort(); delete peer;
                ok = ok && server.waitForNewConnection(4000);
                if (ok) {
                    peer = server.nextPendingConnection(); ok = checkRequest(peer) && ok;
                    peer->write(response + frame); peer->waitForBytesWritten(1000);
                    ok = waitFrames(4) && ok;
                    { QMutexLocker lock(&worker.mutex); ok = ok && worker.decoded == 4 && worker.received == 4; }
                    peer->abort(); delete peer;
                }
            }
        }
        if (ok && server.waitForNewConnection(4000)) {
            peer = server.nextPendingConnection(); ok = checkRequest(peer) && ok;
            peer->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n");
            peer->waitForBytesWritten(1000);
            QElapsedTimer deadline; deadline.start(); bool busy = false;
            while (deadline.elapsed() < 1000) {
                { QMutexLocker lock(&worker.mutex); busy = worker.state.contains("503"); }
                if (busy) break;
                QThread::msleep(10);
            }
            ok = busy && ok;
            peer->abort(); delete peer;
            ok = server.waitForNewConnection(4000) && ok;
            if (ok) {
                peer = server.nextPendingConnection(); ok = checkRequest(peer) && ok;
                peer->write(response + frame); peer->waitForBytesWritten(1000);
                ok = waitFrames(5) && ok;
                // 模拟处理慢于接收：网络仍须收齐，待处理帧只能保留最新一张。
                worker.testProcessingDelayMs = 200;
                for (int i = 0; i < 20; ++i) peer->write(frame);
                peer->waitForBytesWritten(1000);
                QElapsedTimer burst; burst.start();
                bool drained = false;
                while (burst.elapsed() < 1500) {
                    if (peer->bytesToWrite()) peer->waitForBytesWritten(50);
                    { QMutexLocker lock(&worker.mutex);
                      drained = worker.received == 25 && worker.dropped > 0;
                      if (drained) break; }
                    QThread::msleep(10);
                }
                ok = drained && ok;
                peer->abort(); delete peer;
            }
        } else ok = false;
        worker.requestInterruption(); worker.wait();
        fprintf(stdout, "Qt protocol: ok=%d received=%llu decoded=%llu dropped=%llu state=%s\n",
                int(ok), static_cast<unsigned long long>(worker.received),
                static_cast<unsigned long long>(worker.decoded),
                static_cast<unsigned long long>(worker.dropped), worker.state.toUtf8().constData());
        fflush(stdout);
        qInfo() << (ok ? "QT CAMERA PROTOCOL PASS" : "QT CAMERA PROTOCOL FAIL");
        return ok ? 0 : 1;
    }
    Window window;
    window.show();
    const int connectIndex = app.arguments().indexOf("--connect");
    if (connectIndex >= 0 && connectIndex + 1 < app.arguments().size())
        window.connectDevice(app.arguments()[connectIndex + 1]);
    if (app.arguments().contains("--smoke-test")) QTimer::singleShot(1000, &app, &QApplication::quit);
    return app.exec();
}
