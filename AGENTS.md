# 仓库工作指引

## 构建与部署

- 在 Windows PowerShell 中构建、运行和测试；WSL 仅用于编辑。使用 Qt 5.11.1、MinGW 5.3 **32 位**、C++11 和 OpenCV 3.4.20，不要换成系统 Qt、MSVC 或 64 位 OpenCV。
- 构建入口是根目录 `./build.ps1`，工程文件是 `src/qt-camare.pro`。脚本固定使用 `D:\QT\5.11.1\mingw53_32\bin`、`D:\QT\Tools\mingw530_32\bin`，OpenCV 位于 `D:\OpenCV\mingw32`。
- 缺少 OpenCV 时，先运行 `./setup_opencv.ps1`；它会下载 CMake/OpenCV 并用同一 MinGW 工具链编译安装到 `D:\OpenCV`，不是普通的应用构建步骤。
- `build/` 是 qmake/make 产物目录。`build.ps1` 还会将 Release EXE、Qt 插件、MinGW/OpenCV DLL 和人脸模型部署到仓库根目录；验证应运行根目录的 `qt-camare.exe`。
- 部署必须保留 DLL、`platforms/qwindows.dll`、`imageformats/qjpeg.dll` 和 `models/haarcascade_frontalface_default.xml`。模型相对于 EXE 目录定位；不要只复制 EXE。

## 验证入口

测试内置在同一 EXE 的 `main()` 中，没有独立测试工程。先执行 `./build.ps1`；以下命令在仓库根目录的 Windows PowerShell 中执行。程序使用 `QApplication`，测试也需要 Qt 平台插件。

```powershell
# 本机模拟 HTTP 设备，无需连接板卡；等待 GUI 子系统程序退出并检查退出码。
$test = Start-Process ./qt-camare.exe -ArgumentList '--protocol-test' -Wait -PassThru
$test.ExitCode
```

- `--protocol-test`：成功返回 0，检查拆包/粘包、超限长度、半帧断线、HTTP 503、重连和慢处理时丢弃旧待处理帧；修改接收或线程逻辑后优先运行。
- `--smoke-test`：打开窗口约一秒后退出，仅验证 GUI 启动。
- `--device-test 192.168.10.2`：连接实机默认 HTTP 80 端口，20 秒内至少解码五帧 800×480 图像且人脸检测无错误才返回 0；允许零张人脸。先关闭浏览器预览等其他观看连接，板端只允许一个观看者。
- `--face-test 图片路径`：需要清晰正脸图片；检查人脸、绿色框、空白图无检出、并排双图至少两张人脸，成功返回 0。图片读取或模型加载失败也会返回非零。
- `--connect 192.168.10.2`：启动 GUI 并自动连接，用于人工预览。模拟测试不能替代实机画面、帧率和 OTA 恢复验证。

## 修改时必须保留的约束

- `src/main.cpp` 同时包含 HTTP 解析、`CameraThread`、嵌套 `Processor` 线程、窗口及上述测试入口；`src/facedetector.cpp` 负责模型加载和标框。
- 协议是 `GET /stream` 的 HTTP multipart MJPEG，不是旧 OCJP。boundary 来自响应头，每帧按 `Content-Length` 提取；JPEG 上限 512 KiB、头部上限 8 KiB，只解码 800×480 JPEG，不支持 HTTP Transfer-Encoding。
- socket 和阻塞接收属于 `CameraThread`；JPEG 解码与检测属于 `Processor`。待处理 JPEG 仅保留最新一张，GUI 每 33 ms 读取最新图像，避免逐帧排队。共享状态由 mutex 保护，`connectionEpoch` 防止断线前的处理结果在重连后发布。
- `received` 统计收到的完整 JPEG，`decoded` 统计成功解码，`dropped` 统计被新帧替换的待处理帧；不要将接收帧率改成处理帧率。
- 模型用 `QFile` 读入内存再交给 OpenCV，以兼容中文安装路径。模型或检测失败应显示错误并继续播放视频。
