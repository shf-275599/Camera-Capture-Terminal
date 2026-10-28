# 工程协作要点

## 工程边界

- 本目录包含四个独立构建单元，没有根级统一构建入口。进入子工程前读取对应的 `BaseProject/AGENTS.md`、`Bootloader/AGENTS.md`、`qt-ota/AGENTS.md` 或 `qt-camare/AGENTS.md`；其中的命令默认相对子工程根目录。
- `Bootloader/` 生成固定 CM7 Bootloader 和 CM4 Stub；`BaseProject/` 生成两核 A/B 应用。两者都有名为 `BaseProject.ioc` 的配置和 `BaseProject_CM*` 产物，不能按文件名判断镜像用途。
- `qt-ota/` 是 OTA 上传客户端，`qt-camare/`（保留此拼写）是 HTTP MJPEG 预览、人脸与表情处理客户端。两者编辑 `src/`；各自 `build.ps1` 同时构建并覆盖根目录部署文件，分发须保留 DLL、插件及相机客户端的模型。

## 构建与聚焦验证

- 构建、测试和烧录使用 Windows PowerShell；从 WSL 调用 `powershell.exe`。固件依赖 Windows GNU Make 和 `arm-none-eabi-*`，工具链不在 PATH 时给 make 传 `GCC_PATH=<bin目录>`。不要在 WSL 中直接构建。
- 以下命令均从本目录在 Windows PowerShell 执行：

  | 目的 | 命令 |
  | --- | --- |
  | 构建 Bootloader 与 Stub | `make -C Bootloader/Makefile` |
  | 构建应用 CM4 双槽 | `make -C BaseProject/Makefile/CM4 all-slots -j4` |
  | 构建应用 CM7 双槽 | `make -C BaseProject/Makefile/CM7 all-slots -j4` |
  | 仅构建 CM7 B 应用 | `make -C BaseProject/Makefile/CM7 APP_SLOT=B` |
  | 仅构建 CM4 A 应用 | `make -C BaseProject/Makefile/CM4 APP_SLOT=A` |
  | 本地烧录升级模拟测试（不连接硬件） | `./BaseProject/tests/local_flash_update_test.ps1` |
  | JPEG RGB565→YCbCr420 转换回归（不连接硬件） | `./BaseProject/tests/jpeg_color_equivalence_test.ps1` |
  | Qt 构建并部署到 qt-ota 目录 | `./qt-ota/build.ps1` |
  | Qt 上传器模拟测试 | `./qt-ota/test-upload.ps1` |
  | Qt 续传模拟测试 | `./qt-ota/test-recovery.ps1` |
  | Qt 相机客户端构建并部署 | `./qt-camare/build.ps1` |

- 两个 Qt OTA 模拟测试须先构建上传器 EXE 和 `BaseProject/Makefile/CM7/build_B/BaseProject_CM7_B.bin`，分别监听本机端口 18758、18759；恢复测试可用 `-Scenarios` 选择场景。通过只证明模拟交互正确，不证明设备擦写或启动正常。
- 两个 Qt 构建脚本固定使用 `D:\QT\5.11.1\mingw53_32\bin` 与 `D:\QT\Tools\mingw530_32\bin`，需保持 Qt 5.11.1、MinGW 5.3 32 位和 C++11 兼容。相机客户端另依赖 `D:\OpenCV\mingw32-4.10.0` 的同工具链 OpenCV 和模型；安装入口见其子工程指引。
- 相机客户端的无设备协议测试：构建后在 Windows PowerShell 执行 `$test = Start-Process ./qt-camare/qt-camare.exe -ArgumentList '--protocol-test' -Wait -PassThru`，检查 `$test.ExitCode` 为 0；测试也依赖 Qt 平台插件。
- `BaseProject/Makefile` 默认只构建 A 槽；清理两核用各子工程的 `clean.ps1`，顶层 Makefile 的 `clean` 不递归清理两核产物。
- `make_flash*.ps1` 会实际烧录并复位。Bootloader 的两个入口分别只写 CM7 Bootloader / CM4 Stub；BaseProject 的四个 `make_flash_cm*_slot*.ps1` 入口只写对应核、对应应用槽并登记 pending，不支持空板初始化，且不包含固定引导程序。
- 实机只读流测试：`./BaseProject/tests/http/mjpeg_test.ps1 -Device <IP> -Frames 5`，不擦写、不复位；LCD 画面仍需现场确认。当前固件允许两个观看连接，第三个或 OTA 期间返回 503；相机客户端旧文档中的“只允许一个观看者”已过时。

## 跨工程协议

- Flash 分区分别定义在两个固件工程的 `Common/Inc/boot_slot.h`：CM7 Boot 为 `0x08000000`，CM4 Stub 为 `0x08100000`，各 128 KiB；CM7 A/B 为 `0x08020000` / `0x080A0000`，CM4 A/B 为 `0x08120000` / `0x081A0000`，每槽 384 KiB。A/B 镜像链接地址不同，不能混用。
- 两个 `Common/` 是各自独立的源码副本。修改分区或启动协议时，同时核对两工程的头文件、链接脚本和 Qt 客户端的地址校验；每核槽索引均为 A=0、B=1。
- EEPROM 启动协议也有两份：`Bootloader/CM7/APP/BOOT/boot_eeprom.h` 与 `BaseProject/CM7/APP/BOOT/boot_eeprom.h`，双备份位于 `0x0000` / `0x0100`，每份 256 字节；修改格式、版本或读写语义须同步核对引导与应用。
- 当前 CM4 启动前提为 `BCM4=0`：CM7 Bootloader 选择 CM7 应用，随后应用选择 CM4 槽、写 SRAM4 handoff 并使能 CM4，由固定 Stub 跳转。修改启动流程须同时检查引导与应用，不能只改一侧。
- HTTP/OTA 服务在应用 CM7；CM4 通过 IPC 和共享内存配合。CM4 OTA 擦写 Bank2 前须保留 RAM 等待握手，相关约束见 `BaseProject/AGENTS.md`。
- Qt OTA 支持设备 RAM 会话内续传并在本地保存恢复任务；设备会话空闲 60 秒、总时长 10 分钟后失效，掉电或复位不保留设备偏移。上传成功仍需复位后确认实际运行槽和业务表现；两核独立确认与回退，不提供双核整体原子回滚。
