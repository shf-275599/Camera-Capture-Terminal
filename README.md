# Camera-Capture-Terminal

基于 STM32H757XIH6 双核 MCU（Cortex-M7 + Cortex-M4）的摄像头采集终端：OV5640 采集 800×480 图像，CM7 硬件 JPEG 编码后同时驱动 LCD 预览与 HTTP MJPEG 推流，配合双核 A/B 槽 OTA 升级和两个 Windows Qt 上位机客户端，构成完整的"固件 + 引导 + 上位机"方案。

## 项目组成

| 目录 | 说明 | 文档 |
| --- | --- | --- |
| `Bootloader/` | 固定 CM7 Bootloader 与 CM4 Stub，负责槽位选择、跳转和本地升级协议 | [README.md](Bootloader/README.md) |
| `BaseProject/` | 双核 A/B 应用固件：摄像头采集、JPEG 编码、LCD、HTTP MJPEG、SD 抓拍、USB 导出 | [README.md](BaseProject/README.md) |
| `qt-ota/` | Windows OTA 上传客户端：分块上传、CRC32 校验、断点续传与任务恢复 | [README.md](qt-ota/README.md) |
| `qt-camare/` | Windows 相机客户端：MJPEG 预览、YuNet 人脸检测、表情分类（目录名保留此拼写） | [README.md](qt-camare/README.md) |

跨工程的协作规则（构建边界、协议同步约束、验证要求）见 [AGENTS.md](AGENTS.md)。

## 系统架构

- **启动链**：复位后 CM7 Bootloader（`0x08000000`）选择 CM7 应用槽，应用读取 EEPROM 选择 CM4 槽、写 SRAM4 handoff 并使能 CM4，由固定 CM4 Stub（`0x08100000`）跳转到 CM4 应用。
- **双核分工**：CM7 运行摄像头、硬件 JPEG、LTDC LCD、以太网 LwIP、HTTP 服务器、SD 抓拍、USB 读卡器导出；CM4 经 OpenAMP/HSEM 与 CM7 协作。
- **A/B 双槽 OTA**：每核各自占用 A/B 双槽（每槽 384 KiB），链接地址不同不能混用；CM7 槽位为 `0x08020000` / `0x080A0000`，CM4 为 `0x08120000` / `0x081A0000`。两核独立升级与回退，不提供双核整体原子回滚。
- **HTTP MJPEG**：应用提供主页、`/status` 和 `/stream` MJPEG 视频流，允许两个观看连接，第三个连接或 OTA 期间返回 503。
- **上位机**：`qt-ota` 按 8192 字节分块上传并支持设备 RAM 会话内断点续传（空闲 60 秒、总时长 10 分钟失效）；`qt-camare` 解码 MJPEG 流并叠加人脸框与表情分类结果。

## 构建与测试

构建、测试和烧录统一在 Windows PowerShell 执行（从本目录）：

| 目的 | 命令 |
| --- | --- |
| 构建 Bootloader 与 Stub | `make -C Bootloader/Makefile` |
| 构建应用 CM4 双槽 | `make -C BaseProject/Makefile/CM4 all-slots -j4` |
| 构建应用 CM7 双槽 | `make -C BaseProject/Makefile/CM7 all-slots -j4` |
| 构建 Qt OTA 上传器 | `./qt-ota/build.ps1` |
| 构建相机客户端 | `./qt-camare/build.ps1` |

环境要求：

- 固件：Windows GNU Make 与 `arm-none-eabi-*` 工具链（不在 PATH 时给 make 传 `GCC_PATH=<bin目录>`）
- Qt 客户端：Qt 5.11.1 + MinGW 5.3 32 位（`D:\QT\5.11.1\mingw53_32`）
- 相机客户端另依赖 `D:\OpenCV\mingw32-4.10.0` 的同工具链 OpenCV，首次使用先执行 `./qt-camare/setup_opencv.ps1`

更多测试入口（OTA 模拟测试、JPEG 色彩回归、实机 MJPEG 流测试等）见 [AGENTS.md](AGENTS.md) 与各子工程文档。
