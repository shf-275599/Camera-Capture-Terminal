#include <QtWidgets>
#include <QtNetwork>
#include <functional>

static quint32 crc32(const QByteArray &data)
{
    quint32 crc = 0xffffffffU;
    for (char byte : data) {
        crc ^= quint8(byte);
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0U);
    }
    return ~crc;
}

class Window : public QWidget
{
    QNetworkAccessManager network;
    QNetworkReply *reply = nullptr;
    QLineEdit *address, *fileName, *version;
    QComboBox *core;
    QPushButton *browse, *query, *upload, *cancel;
    QLabel *status;
    QProgressBar *progress;
    QPlainTextEdit *log;
    QByteArray image;
    QUrl baseUrl;
    QByteArray selectedCore;
    quint32 selectedVersion = 0;
    int offset = 0;
    int targetSlot = -1;
    bool working = false, session = false, ending = false, cancelling = false;
    bool closing = false, testMode = false, allowRestart = false;
    quint32 imageCrc = 0, imageBase = 0;
    QByteArray imageHash;
    QTimer retryTimer;
    QElapsedTimer recoveryTime;
    int retryCount = 0, lastCode = 0;
    bool transportFailed = false;

    QString taskPath() const
    {
        const QString overridePath = qEnvironmentVariable("QT_OTA_TASK_FILE");
        return overridePath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/upload.json" : overridePath;
    }

    bool saveTask()
    {
        QJsonObject task;
        task["url"] = baseUrl.toString();
        task["file"] = QFileInfo(fileName->text()).absoluteFilePath();
        task["core"] = QString::fromLatin1(selectedCore);
        task["version"] = double(selectedVersion);
        task["size"] = image.size();
        task["crc"] = double(imageCrc);
        task["base"] = double(imageBase);
        task["slot"] = targetSlot;
        task["sha256"] = QString::fromLatin1(imageHash.toHex());
        task["ending"] = ending;
        if (!QDir().mkpath(QFileInfo(taskPath()).absolutePath())) return false;
        QSaveFile file(taskPath());
        const QByteArray data = QJsonDocument(task).toJson();
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
    }

    void pauseUpload(const QString &reason)
    {
        retryTimer.stop();
        append(reason);
        status->setText(QStringLiteral("上传已暂停，任务已保留"));
        setWorking(false);
    }

    void recover()
    {
        if (!recoveryTime.isValid()) recoveryTime.start();
        if (recoveryTime.elapsed() >= 60000) {
            pauseUpload(QStringLiteral("连续恢复失败已达 60 秒，请恢复网络后重开程序继续。"));
            return;
        }
        const int delay = qMin(5000, 1000 * (1 << qMin(retryCount++, 3)));
        status->setText(QStringLiteral("连接中断，%1 秒后重连…").arg(delay / 1000));
        retryTimer.start(delay);
    }

    void beginUpload()
    {
        request("POST", "/ota/begin", {{"X-Fw-Size", QByteArray::number(image.size())},
                {"X-Fw-Version", QByteArray::number(selectedVersion)},
                {"X-Fw-Crc32", "0x" + QByteArray::number(imageCrc, 16)},
                {"X-Fw-Base", "0x" + QByteArray::number(imageBase, 16)}}, {},
                [this](bool ok, QByteArray data) {
            if (!ok) { failed(); return; }
            QRegularExpression expression("^OTA (?:ready|resume) written=([0-9]+)\\s*$");
            const auto match = expression.match(QString::fromLatin1(data));
            bool valid = false;
            const qulonglong written = match.captured(1).toULongLong(&valid);
            if (!match.hasMatch() || !valid || written > quint64(image.size())) {
                pauseUpload(QStringLiteral("设备返回了无效续传偏移，已停止。")); return;
            }
            // ready=0 表示设备原会话已消失，可能刚被本次 begin 重新创建。
            if (match.captured(0).startsWith("OTA ready") && !allowRestart) {
                if (testMode || QMessageBox::question(this, QStringLiteral("会话已失效"),
                        QStringLiteral("设备在查询后丢失了会话，begin 已重新初始化目标槽，是否从头上传？")) != QMessageBox::Yes) {
                    pauseUpload(QStringLiteral("设备原会话已失效，等待确认从头上传。")); return;
                }
            }
            allowRestart = false;
            offset = int(written);
            progress->setValue(offset);
            recoveryTime.invalidate(); retryCount = 0;
            append(QStringLiteral("设备确认续传偏移：%1").arg(offset));
            sendChunk();
        });
    }

    void checkRecovery()
    {
        request("GET", "/ota/status", {}, {}, [this](bool ok, QByteArray data) {
            if (!ok) { failed(); return; }
            const auto values = parseStatus(data);
            if (ending) {
                bool versionOk = false, sizeOk = false;
                const QByteArray suffix = targetSlot ? "b" : "a";
                const quint32 ver = values.value("version_" + suffix).toUInt(&versionOk, 0);
                const int size = values.value("size_" + suffix).toInt(&sizeOk, 0);
                if (versionOk && sizeOk && ver == selectedVersion && size == image.size() &&
                    values.value("running") == QByteArray::number(targetSlot) && values.value("flags") == "0x00") {
                    QFile::remove(taskPath()); session = false;
                    append(QStringLiteral("设备报告目标槽已运行，版本与长度匹配；请核对实际业务。"));
                    status->setText(QStringLiteral("设备已报告目标固件运行")); setWorking(false);
                } else recover();
                return;
            }
            if (values.value("flags") != "0x00" || values.value("other_core_busy") != "0" ||
                values.value("running") != QByteArray::number(1 - targetSlot)) {
                pauseUpload(QStringLiteral("设备槽位或 pending 状态已变化，请查询状态。")); return;
            }
            if (values.value("busy") == "0" && !allowRestart) {
                if (testMode || QMessageBox::question(this, QStringLiteral("会话已失效"),
                        QStringLiteral("设备接收会话已超时或已复位，是否擦除目标槽并从头上传？")) != QMessageBox::Yes) {
                    pauseUpload(QStringLiteral("设备会话已失效，等待确认从头上传。")); return;
                }
                allowRestart = true;
            } else if (values.value("busy") != "0" && values.value("busy") != "1") {
                pauseUpload(QStringLiteral("设备状态无效。")); return;
            }
            beginUpload();
        });
    }

    void restoreTask()
    {
        QFile taskFile(taskPath());
        if (!taskFile.exists()) return;
        if (!testMode && QMessageBox::question(this, QStringLiteral("恢复上传"),
                QStringLiteral("发现未完成任务，是否校验固件并恢复上传？")) != QMessageBox::Yes) return;
        if (!taskFile.open(QIODevice::ReadOnly)) { pauseUpload(QStringLiteral("无法读取保存任务。")); return; }
        const QJsonObject task = QJsonDocument::fromJson(taskFile.readAll()).object();
        address->setText(task.value("url").toString());
        fileName->setText(task.value("file").toString());
        version->setText(QString::number(quint32(task.value("version").toDouble())));
        const QString coreName = task.value("core").toString();
        if (coreName != "cm7" && coreName != "cm4") { pauseUpload(QStringLiteral("保存任务核心无效。")); return; }
        core->setCurrentIndex(coreName == "cm4" ? 1 : 0);
        if (!setup()) return;
        QFile file(fileName->text());
        if (!file.open(QIODevice::ReadOnly) || file.size() < 8 || file.size() > 393216) {
            pauseUpload(QStringLiteral("原固件无法读取，请恢复原文件后重试。")); return;
        }
        image = file.readAll();
        imageHash = QCryptographicHash::hash(image, QCryptographicHash::Sha256);
        imageCrc = crc32(image);
        targetSlot = task.value("slot").toInt(-1);
        imageBase = quint32(task.value("base").toDouble());
        const quint32 expectedBase = (selectedCore == "cm7" ? 0x08020000U : 0x08120000U) + (targetSlot == 1 ? 0x80000U : 0U);
        if ((targetSlot != 0 && targetSlot != 1) || imageBase != expectedBase ||
            task.value("sha256").toString().toLatin1() != imageHash.toHex() ||
            task.value("size").toInt() != image.size() || quint32(task.value("crc").toDouble()) != imageCrc) {
            pauseUpload(QStringLiteral("固件已改变或任务记录无效，拒绝恢复。")); return;
        }
        selectedVersion = quint32(task.value("version").toDouble());
        ending = task.value("ending").toBool(); session = true; cancelling = false; allowRestart = false;
        progress->setRange(0, image.size());
        setWorking(true); recoveryTime.start(); retryCount = 0;
        checkRecovery();
    }

    void append(const QString &text)
    {
        log->appendPlainText(QTime::currentTime().toString("HH:mm:ss ") + text);
    }

    void setWorking(bool value)
    {
        working = value;
        for (QWidget *w : QList<QWidget *>{address, fileName, version, core, browse, query, upload})
            w->setEnabled(!value);
        cancel->setEnabled((value || session) && !ending);
    }

    bool setup()
    {
        QString url = address->text().trimmed();
        if (!url.contains("://")) url.prepend("http://");
        baseUrl = QUrl(url);
        if (!baseUrl.isValid() || baseUrl.host().isEmpty() || baseUrl.scheme() != "http" ||
            !baseUrl.userInfo().isEmpty() || baseUrl.hasQuery() || baseUrl.hasFragment() ||
            (baseUrl.path() != "" && baseUrl.path() != "/")) {
            append(QStringLiteral("请输入设备 IP 或 http://IP:端口。"));
            return false;
        }
        selectedCore = core->currentData().toByteArray();
        return true;
    }

    typedef QMap<QByteArray, QByteArray> Headers;
    void request(const QByteArray &method, const QString &path, const Headers &headers,
                 const QByteArray &body, std::function<void(bool, QByteArray)> done)
    {
        QUrl url = baseUrl;
        url.setPath(path);
        QNetworkRequest req(url);
        req.setRawHeader("Connection", "close");
        req.setRawHeader("X-Ota-Core", selectedCore);
        for (auto it = headers.begin(); it != headers.end(); ++it) req.setRawHeader(it.key(), it.value());
        if (method == "POST") {
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
            req.setHeader(QNetworkRequest::ContentLengthHeader, body.size());
        }
        reply = method == "GET" ? network.get(req) : network.post(req, body);
        QNetworkReply *current = reply;
        QTimer *timer = new QTimer(current);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, current, [current]() {
            current->setProperty("timedOut", true);
            current->abort();
        });
        timer->start(45000);
        connect(current, &QNetworkReply::finished, this, [=]() {
            timer->stop();
            if (closing) { current->deleteLater(); return; }
            const int code = current->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            lastCode = code;
            const QByteArray data = current->readAll();
            const bool ok = current->error() == QNetworkReply::NoError && code >= 200 && code < 300;
            transportFailed = current->error() != QNetworkReply::NoError && (code == 0 || (code >= 200 && code < 300));
            if (!ok && !cancelling) {
                append(QStringLiteral("请求失败：HTTP %1，%2\n%3").arg(code)
                       .arg(current->property("timedOut").toBool() ? QStringLiteral("请求超时") : current->errorString())
                       .arg(QString::fromUtf8(data)));
            }
            reply = nullptr;
            current->deleteLater();
            if (cancelling) { abortSession(); return; }
            done(ok, data);
        });
    }

    static QMap<QByteArray, QByteArray> parseStatus(const QByteArray &data)
    {
        QMap<QByteArray, QByteArray> values;
        for (QByteArray line : data.split('\n')) {
            line = line.trimmed();
            int separator = line.indexOf('=');
            if (separator > 0) values.insert(line.left(separator), line.mid(separator + 1));
        }
        return values;
    }

    static QString formatStatus(const QByteArray &data)
    {
        const auto values = parseStatus(data);
        const QString unknown = QStringLiteral("未知");
        auto slot = [&](const char *key) {
            const QByteArray value = values.value(key);
            return value == "0" ? QStringLiteral("A 槽") : value == "1" ? QStringLiteral("B 槽") : unknown;
        };
        auto number = [&](const char *key, const QString &unit) {
            bool ok = false;
            const qulonglong value = values.value(key).toULongLong(&ok, 0);
            return ok ? QString::number(value) + unit : unknown;
        };
        auto coreName = [&](const char *key) {
            const QByteArray value = values.value(key);
            return value == "cm7" ? QStringLiteral("CM7") : value == "cm4" ? QStringLiteral("CM4") :
                   value == "none" ? QStringLiteral("无") : unknown;
        };
        auto state = [&](const char *key, const QString &idle, const QString &busy) {
            const QByteArray value = values.value(key);
            return value == "0" ? idle : value == "1" ? busy : unknown;
        };
        bool flagsOk = false;
        const uint flags = values.value("flags").toUInt(&flagsOk, 0);
        const QString pending = !flagsOk ? unknown : (flags & 1U) ? slot("pending") : QStringLiteral("无");
        QStringList lines;
        lines << QStringLiteral("目标核心：%1").arg(coreName("core"))
              << QStringLiteral("升级状态：%1").arg(state("busy", QStringLiteral("空闲"), QStringLiteral("升级处理中（含等待复位）")))
              << QStringLiteral("另一核心升级状态：%1").arg(state("other_core_busy", QStringLiteral("空闲"), QStringLiteral("升级处理中")))
              << QStringLiteral("当前运行：%1").arg(slot("running"))
              << QStringLiteral("已确认槽位：%1").arg(slot("active"))
              << QStringLiteral("待确认固件：%1").arg(pending)
              << QStringLiteral("剩余试启动次数：%1").arg(number("trial", QString()))
              << QStringLiteral("下次升级目标：%1").arg(slot("target_slot"))
              << QStringLiteral("目标地址：%1").arg(values.contains("target_base") ? QString::fromLatin1(values.value("target_base")) : unknown)
              << QStringLiteral("最大固件容量：%1").arg(number("max_image", QStringLiteral(" 字节")))
              << QStringLiteral("A 槽版本：%1；固件长度：%2").arg(number("version_a", QString()), number("size_a", QStringLiteral(" 字节")))
              << QStringLiteral("B 槽版本：%1；固件长度：%2").arg(number("version_b", QString()), number("size_b", QStringLiteral(" 字节")))
              << QStringLiteral("当前升级会话核心：%1").arg(coreName("session_core"))
              << QStringLiteral("已接收（含缓冲）：%1 / %2").arg(number("written", QStringLiteral(" 字节")), number("image_size", QStringLiteral(" 字节")))
              << QStringLiteral("会话版本：%1").arg(number("session_ver", QString()));
        if (values.contains("stub_base"))
            lines << QStringLiteral("CM4 固定引导地址：%1").arg(QString::fromLatin1(values.value("stub_base")));
        lines << QStringLiteral("提示：升级文件必须匹配目标槽链接地址；版本和长度为设备登记值，不代表空闲槽已通过校验。");
        return lines.join('\n');
    }

    void abortSession()
    {
        cancelling = false;
        if (!session) { setWorking(false); return; }
        status->setText(QStringLiteral("正在取消升级…"));
        request("POST", "/ota/abort", {}, {}, [this](bool ok, QByteArray) {
            session = false;
            if (ok) QFile::remove(taskPath());
            append(ok ? QStringLiteral("会话已取消，可重新开始。") : QStringLiteral("取消未确认，请查询状态；设备会话会按超时清理。"));
            status->setText(QStringLiteral("上传已停止"));
            setWorking(false);
        });
    }

    void failed()
    {
        if (session && (ending || transportFailed || lastCode == 0 || lastCode == 408 || lastCode >= 500)) recover();
        else pauseUpload(QStringLiteral("请求被拒绝，任务已保留，请查询设备状态。"));
    }

    void sendChunk()
    {
        if (offset >= image.size()) {
            ending = true;
            if (!saveTask()) { ending = false; pauseUpload(QStringLiteral("无法保存结束阶段，已停止上传。")); return; }
            cancel->setEnabled(false);
            status->setText(QStringLiteral("校验镜像、保存 pending…"));
            request("POST", "/ota/end", {}, {}, [this](bool ok, QByteArray data) {
                if (!ok) { failed(); return; }
                session = false;
                QFile::remove(taskPath());
                append(QString::fromUtf8(data).trimmed());
                append(QStringLiteral("上传与 pending 保存成功，等待设备复位；请查询状态并核对实际画面。"));
                status->setText(QStringLiteral("上传成功，设备正在复位"));
                setWorking(false);
            });
            return;
        }
        const QByteArray chunk = image.mid(offset, 8192);
        request("POST", "/ota/chunk", {{"X-Offset", QByteArray::number(offset)}}, chunk,
                [this, chunk](bool ok, QByteArray) {
            if (!ok) { failed(); return; }
            offset += chunk.size();
            progress->setValue(offset);
            status->setText(QStringLiteral("已上传 %1 / %2 字节").arg(offset).arg(image.size()));
            sendChunk();
        });
    }

    void startUpload()
    {
        if (QFile::exists(taskPath())) { restoreTask(); return; }
        if (!setup()) return;
        bool valid;
        const qulonglong number = version->text().trimmed().toULongLong(&valid, 10);
        if (!valid || number > 0xffffffffULL) { append(QStringLiteral("版本号须为 0～4294967295 的十进制整数。")); return; }
        QFile file(fileName->text());
        if (!file.open(QIODevice::ReadOnly) || file.size() < 8 || file.size() > 393216) {
            append(QStringLiteral("请选择可读取且长度为 8～393216 字节的 BIN 文件。")); return;
        }
        image = file.readAll();
        if (image.size() != file.size()) { append(QStringLiteral("固件读取不完整。")); return; }
        selectedVersion = quint32(number);
        imageCrc = crc32(image);
        imageHash = QCryptographicHash::hash(image, QCryptographicHash::Sha256);
        recoveryTime.invalidate(); retryCount = 0; retryTimer.stop();
        ending = session = cancelling = false;
        offset = 0;
        progress->setRange(0, image.size());
        progress->setValue(0);
        setWorking(true);
        status->setText(QStringLiteral("查询设备与目标槽…"));
        request("GET", "/ota/status", {}, {}, [this](bool ok, QByteArray data) {
            if (!ok) { failed(); return; }
            const auto values = parseStatus(data);
            if (values.value("busy") != "0" || values.value("other_core_busy") != "0" ||
                values.value("flags") != "0x00" ||
                (values.value("running") != "0" && values.value("running") != "1")) {
                append(QStringLiteral("设备繁忙、有 pending 或运行槽未知，请先查询状态。"));
                failed(); return;
            }
            targetSlot = 1 - values.value("running").toInt();
            const quint32 base = (selectedCore == "cm7" ? 0x08020000U : 0x08120000U) + (targetSlot ? 0x80000U : 0U);
            const uchar *bytes = reinterpret_cast<const uchar *>(image.constData());
            const quint32 entry = quint32(bytes[4]) | (quint32(bytes[5]) << 8) | (quint32(bytes[6]) << 16) | (quint32(bytes[7]) << 24);
            if (!(entry & 1U) || entry < base || quint64(entry) >= quint64(base) + quint64(image.size())) {
                append(QStringLiteral("BIN 链接入口错误，需要 %1 %2 槽镜像（0x%3）。")
                       .arg(QString::fromLatin1(selectedCore)).arg(targetSlot ? "B" : "A").arg(base, 8, 16, QChar('0')));
                failed(); return;
            }
            const quint32 crc = crc32(image);
            imageBase = base;
            append(QStringLiteral("目标 %1 %2，%3 字节，CRC32 0x%4")
                   .arg(QString::fromLatin1(selectedCore)).arg(targetSlot ? "B" : "A").arg(image.size()).arg(crc, 8, 16, QChar('0')));
            status->setText(QStringLiteral("准备升级，正在擦除空闲槽…"));
            session = true;
            if (!saveTask()) { session = false; pauseUpload(QStringLiteral("无法保存上传任务，已停止。")); return; }
            allowRestart = true;
            beginUpload();
        });
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        closing = true;
        retryTimer.stop();
        if (reply) reply->abort();
        event->accept();
    }

public:
    void runTest(const QStringList &args)
    {
        testMode = args.contains("--test-upload") || args.contains("--test-resume");
        if (args.contains("--test-resume")) {
            restoreTask();
            QTimer *watch = new QTimer(this);
            connect(watch, &QTimer::timeout, this, [this]() {
                if (!working) { qInfo().noquote() << log->toPlainText(); qApp->exit(status->text() == QStringLiteral("上传成功，设备正在复位") || status->text() == QStringLiteral("设备已报告目标固件运行") ? 0 : 1); }
            });
            watch->start(100);
            return;
        }
        if (!args.contains("--test-query") && !args.contains("--smoke-test") && !testMode) restoreTask();
        if (args.contains("--test-query")) {
            setup();
            request("GET", "/ota/status", {}, {}, [this](bool ok, QByteArray data) {
                qInfo().noquote() << data;
                qApp->exit(ok && parseStatus(data).contains("running") ? 0 : 1);
            });
        } else if (args.contains("--test-upload")) {
            int index = args.indexOf("--test-upload");
            if (args.size() < index + 3) { qApp->exit(2); return; }
            address->setText(args[index + 1]);
            fileName->setText(args[index + 2]);
            version->setText("301");
            startUpload();
            QTimer *watch = new QTimer(this);
            connect(watch, &QTimer::timeout, this, [this]() {
                if (!working) {
                    qInfo().noquote() << log->toPlainText();
                    qApp->exit(status->text() == QStringLiteral("上传成功，设备正在复位") || status->text() == QStringLiteral("设备已报告目标固件运行") ? 0 : 1);
                }
            });
            watch->start(100);
        }
    }

    Window()
    {
        setWindowTitle(QStringLiteral("qt-ota · STM32 双核固件升级"));
        retryTimer.setSingleShot(true);
        connect(&retryTimer, &QTimer::timeout, this, [this]() { checkRecovery(); });
        resize(700, 520);
        network.setProxy(QNetworkProxy::NoProxy);
        address = new QLineEdit("192.168.10.2");
        fileName = new QLineEdit;
        fileName->setPlaceholderText(QStringLiteral("选择目标空闲槽对应的 .bin 文件"));
        version = new QLineEdit("1");
        core = new QComboBox;
        core->addItem("CM7", QByteArray("cm7"));
        core->addItem("CM4", QByteArray("cm4"));
        browse = new QPushButton(QStringLiteral("选择 BIN"));
        query = new QPushButton(QStringLiteral("查询状态"));
        auto clearQuery = new QPushButton(QStringLiteral("清除查询"));
        upload = new QPushButton(QStringLiteral("开始升级"));
        cancel = new QPushButton(QStringLiteral("取消上传"));
        cancel->setEnabled(false);
        status = new QLabel(QStringLiteral("就绪：先查询运行槽，再选择另一槽的 BIN。"));
        status->setWordWrap(true);
        progress = new QProgressBar;
        progress->setRange(0, 100);
        progress->setValue(0);
        log = new QPlainTextEdit;
        log->setReadOnly(true);
        log->setMaximumBlockCount(1000);
        auto form = new QFormLayout;
        form->addRow(QStringLiteral("设备地址"), address);
        form->addRow(QStringLiteral("目标核心"), core);
        form->addRow(QStringLiteral("版本号"), version);
        auto fileRow = new QHBoxLayout;
        fileRow->addWidget(fileName); fileRow->addWidget(browse);
        form->addRow(QStringLiteral("固件文件"), fileRow);
        auto buttons = new QHBoxLayout;
        buttons->addWidget(query); buttons->addWidget(clearQuery); buttons->addStretch(); buttons->addWidget(upload); buttons->addWidget(cancel);
        auto layout = new QVBoxLayout(this);
        layout->addLayout(form); layout->addLayout(buttons);
        layout->addWidget(status); layout->addWidget(progress); layout->addWidget(log);
        connect(browse, &QPushButton::clicked, this, [this]() {
            QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择固件"), fileName->text(), "BIN (*.bin)");
            if (!path.isEmpty()) fileName->setText(path);
        });
        connect(query, &QPushButton::clicked, this, [this]() {
            if (!setup()) return;
            ending = false;
            setWorking(true);
            cancel->setEnabled(false);
            request("GET", "/ota/status", {}, {}, [this](bool ok, QByteArray data) {
                append((ok ? formatStatus(data) : QStringLiteral("状态查询失败。")) + QStringLiteral("\n===================="));
                status->setText(ok ? QStringLiteral("状态查询成功") : QStringLiteral("状态查询失败"));
                setWorking(false);
            });
        });
        connect(upload, &QPushButton::clicked, this, [this]() { startUpload(); });
        connect(clearQuery, &QPushButton::clicked, log, &QPlainTextEdit::clear);
        connect(cancel, &QPushButton::clicked, this, [this]() {
            retryTimer.stop();
            cancelling = true;
            cancel->setEnabled(false);
            if (reply) reply->abort(); else abortSession();
        });
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion");
    Window window;
    window.show();
    QTimer::singleShot(0, &window, [&]() { window.runTest(app.arguments()); });
    if (app.arguments().contains("--smoke-test")) QTimer::singleShot(1000, &app, &QApplication::quit);
    return app.exec();
}
