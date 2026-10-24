# OV5640 Qt HTTP MJPEG 相机

Windows Qt 5.11.1 / MinGW 5.3 32 位，使用 C++11、Widgets、Network 与 OpenCV 3.4.20。

首次在 Windows PowerShell 执行 `./setup_opencv.ps1`，将 CMake、OpenCV 源码及同工具链编译的 32 位运行库安装到 `D:\OpenCV`。脚本从 CMake 和 OpenCV 官方 GitHub 仓库下载，仅构建所需 OpenCV 模块。随后使用下述构建脚本部署应用、OpenCV DLL 和人脸模型。

在 Windows PowerShell 执行 `./build.ps1` 构建并部署，随后运行 `./qt-camare.exe`。填写板端 IP 和 HTTP 端口（默认 80），点击连接，程序请求 `http://设备IP:端口/stream`。主动断开停止自动重连；网络中断后每秒重试。关闭窗口会结束接收线程。

板端提供 `multipart/x-mixed-replace` HTTP 视频流，Qt 从响应读取 boundary，按每部分的 Content-Length 提取并解码 800×480 JPEG；JPEG 最大 512 KiB，头部最大 8 KiB。完整设计见 `../BaseProject/docs/superpowers/specs/2026-10-23-qt-mjpeg-design.md`。本程序适配当前 HTTP 固件，不再使用原 OCJP 自定义协议。

板端仅允许一个视频观看连接，请先关闭浏览器预览或其他客户端。HTTP 503 表示设备正被观看或 OTA 忙碌，程序显示原因并等待重连。设备必须运行当前 HTTP MJPEG 固件；默认 IP 为 192.168.10.2，请按设备实际地址填写。

接收帧率为完整 JPEG 帧到达速率，解码成功数单独显示。接收、解码和人脸检测在工作线程执行，界面定时读取最新图像并等比例显示。OpenCV Haar 模型自动检测正脸，画绿色矩形框并显示人脸数量；侧脸、遮挡或低照度可能漏检。模型读取失败时状态栏显示原因，视频继续播放。保留整个部署目录，尤其是 OpenCV DLL、`models/haarcascade_frontalface_default.xml`、`imageformats/qjpeg.dll` 和 `platforms/qwindows.dll`，不能只复制 EXE。

`./qt-camare.exe --smoke-test` 打开界面后约一秒退出，仅验证 GUI 启动。双端图像质量、帧率与 OTA 恢复仍需实机验证。

`./qt-camare.exe --protocol-test` 使用本机临时 TCP 端口运行模拟 HTTP 设备，与实际接收线程交互，检查 GET /stream 请求、multipart boundary、800×480 JPEG 解码、拆包、粘包、超限长度拒绝、半帧断线、HTTP 503 与重连。退出码 0 表示模拟测试通过，非零表示失败；不会访问实机。

`./qt-camare.exe --device-test 192.168.10.2` 使用同一 Qt 接收线程连接实机，20 秒内成功解码至少五帧 800×480 图像且人脸检测未出错返回 0，否则返回 1；现场没有人脸时允许检出零张。`./qt-camare.exe --connect 192.168.10.2` 打开预览窗口并自动连接指定设备。

`./qt-camare.exe --face-test 图片路径` 使用部署的人脸模型验证输入图片至少检出一张人脸、输出包含绿色框、空白图检出零张，以及两张并排样图检出至少两张人脸，均满足返回 0；图片应包含足够清晰的正脸。这只验证检测链路，实际摄像头人脸框位置与稳定性需现场确认。
