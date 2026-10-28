# OV5640 Qt HTTP MJPEG 相机

Windows Qt 5.11.1 / MinGW 5.3 32 位，使用 C++11、Widgets、Network 与 OpenCV 4.10.0。

首次在 Windows PowerShell 执行 `./setup_opencv.ps1`，将 CMake、OpenCV 源码及同工具链编译的 32 位运行库安装到 `D:\OpenCV`。OpenCV 4.10.0 独立安装到 `D:\OpenCV\mingw32-4.10.0`，保留旧版安装。脚本从官方 GitHub 仓库下载，仅构建所需模块（包含 DNN），下载 YuNet 2023mar 模型并校验 SHA-256，保留模型许可证。随后使用下述构建脚本部署应用、OpenCV DLL 和人脸模型。

安装脚本包含 GCC 5.3 兼容补丁：将内置 Protobuf `WrappedMutex` 的空函数体构造函数改为 `constexpr WrappedMutex() = default;`，保留常量初始化及互斥实现。该补丁只作用于独立下载的 4.10.0 源码，不修改原 OpenCV 3.4.20 安装。

OpenCV 的 C/C++ 编译增加 `-mstackrealign`，用于旧版 32 位 MinGW 与 Windows 线程入口之间的 SSE 栈对齐兼容；不要混用其他编译方式生成的同名 DLL。

在 Windows PowerShell 执行 `./build.ps1` 构建并部署，随后运行 `./qt-camare.exe`。填写板端 IP 和 HTTP 端口（默认 80），点击连接，程序请求 `http://设备IP:端口/stream`。主动断开停止自动重连；网络中断后每秒重试。关闭窗口会结束接收线程。

已有 OpenCV 4.10.0 的环境先运行 `./setup_expression.ps1`，下载约 4.8 MB 的 OpenCV Zoo MobileFaceNet 2022july 表情模型并校验 SHA-256，无需重编 OpenCV；新环境的 `setup_opencv.ps1` 会自动调用该脚本。部署目录还需保留 `models/facial_expression_recognition_mobilefacenet_2022july.onnx`、`models/LICENSE-OpenCV-Zoo.txt` 及模型来源说明。

板端提供 `multipart/x-mixed-replace` HTTP 视频流，Qt 从响应读取 boundary，按每部分的 Content-Length 提取并解码 800×480 JPEG；JPEG 最大 512 KiB，头部最大 8 KiB。完整设计见 `../BaseProject/docs/superpowers/specs/2026-10-23-qt-mjpeg-design.md`。本程序适配当前 HTTP 固件，不再使用原 OCJP 自定义协议。

板端允许两个视频观看连接；两个连接都被占用或 OTA 忙碌时返回 HTTP 503，程序显示原因并等待重连。设备必须运行当前 HTTP MJPEG 固件；默认 IP 为 192.168.10.2，请按设备实际地址填写。

接收帧率为完整 JPEG 帧到达速率，解码成功数单独显示。接收、解码和人脸检测在工作线程执行，界面定时读取最新图像并等比例显示。YuNet 模型使用 CPU 检测人脸，检测图像宽度最多 640，置信度阈值 0.8，NMS 阈值 0.3。视频框经一对一匹配和时间平滑后显示：新目标连续两次检出确认，短时漏检最多保留 150 ms，超过 300 ms 无观测删除；断线重连、分辨率变化或检测异常清空轨迹。人数为当前显示框数量，包含短时保留框。这不是身份识别，多人交叉不保证身份保持，侧脸、遮挡或低照度仍可能漏检。状态栏分别显示总处理和检测耗时。模型读取失败时状态栏显示原因，视频继续播放。保留整个部署目录，尤其是 OpenCV DLL、`models/face_detection_yunet_2023mar.onnx`、`models/LICENSE-YuNet.txt`、`imageformats/qjpeg.dll` 和 `platforms/qwindows.dll`，不能只复制 EXE。

框旁显示面部表情分类：愤怒、厌恶、恐惧、高兴、中性、悲伤、惊讶，以及模型分数。YuNet 五点用于 112×112 对齐，每条轨迹最多每 200 ms 推理一次，分数按 400 ms 时间常数平滑；最高分不足 0.6、脸宽高不足 48 像素、结果超过 600 ms 或当前未检出该脸时显示“无法判断”。分数不是经过校准的概率，分类不代表真实心理状态。重连及轨迹重置会清空表情历史；多人交叉不保证身份保持。模型失败时继续视频和人脸框，并在状态栏显示错误及独立表情耗时。

`./qt-camare.exe --smoke-test` 打开界面后约一秒退出，仅验证 GUI 启动。双端图像质量、帧率与 OTA 恢复仍需实机验证。

`./qt-camare.exe --protocol-test` 使用本机临时 TCP 端口运行模拟 HTTP 设备，与实际接收线程交互，检查 GET /stream 请求、multipart boundary、800×480 JPEG 解码、拆包、粘包、超限长度拒绝、半帧断线、HTTP 503 与重连。退出码 0 表示模拟测试通过，非零表示失败；不会访问实机。

`./qt-camare.exe --device-test 192.168.10.2` 使用同一 Qt 接收线程连接实机，20 秒内成功解码至少五帧 800×480 图像且人脸检测未出错返回 0，否则返回 1；现场没有人脸时允许检出零张。`./qt-camare.exe --connect 192.168.10.2` 打开预览窗口并自动连接指定设备。

`./qt-camare.exe --face-test 图片路径` 使用部署的人脸模型验证输入图片至少检出一张人脸、输出包含绿色框、空白图检出零张，以及两张并排样图检出至少两张人脸，均满足返回 0；图片应包含足够清晰的正脸。此测试使用原始检测，不保留历史框，并输出预热后十次检测耗时。这只验证检测链路，实际摄像头人脸框位置与稳定性需现场确认。

`./qt-camare.exe --stabilizer-test` 使用合成矩形验证确认、匹配、平滑、漏检过期和重置，不依赖人脸模型。设计及验收范围见 [YuNet 与框稳定设计](docs/superpowers/specs/2026-10-24-yunet-stabilization-design.md)。所有测试均在 Windows PowerShell 使用 `Start-Process ./qt-camare.exe -ArgumentList '测试参数' -Wait -PassThru` 等待程序退出并检查 `ExitCode`。

`--expression-state-test` 验证七类映射、分数处理、低分拒绝、节流、平滑、过期及五点对齐；`--expression-test 图片路径` 验证真实模型、多人推理和视频节流并输出预热后耗时，可加 `--output 输出.png` 保存标框及中文标签的测试图片。这些测试验证实现链路，不代表七种表情准确率已经过标注数据集评估。设计见 [表情分类方案](docs/superpowers/specs/2026-10-24-expression-recognition-design.md)。
