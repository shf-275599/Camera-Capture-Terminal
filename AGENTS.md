# 仓库操作说明

## 构建与验证

- 这是 STM32H757XIH6 双核固件。在 Windows PowerShell 中使用 GNU Make 和 `arm-none-eabi-*` 工具链（可通过 `make ... GCC_PATH=工具链bin目录` 指定）；从 WSL 操作时通过 `powershell.exe` 执行构建。`make -C Makefile` 先构建 CM4、再构建 CM7，默认仅构建 A 槽。
- 聚焦单核/单槽：`make -C Makefile/CM7 APP_SLOT=B` 或 `make -C Makefile/CM4 APP_SLOT=A`；双槽分别运行 `make -C Makefile/CM7 all-slots`、`make -C Makefile/CM4 all-slots`。各核 A 槽产物在 `Makefile/CMx/build/`，B 槽在 `Makefile/CMx/build_B/`（`CMx` 为 `CM4` 或 `CM7`）。
- 源码变更构建受影响的核；修改 `Common/`、启动或 OpenAMP 时构建两个核，修改槽位或链接布局时验证 A/B。`all-slots` 的配方含 Windows `@echo.`，请在 Windows 环境运行构建。
- `./clean.ps1` 清理两个核的双槽产物；顶层 `make -C Makefile clean` 不递归清理两核产物。
- 源文件和头文件目录需显式加入各核 Makefile 的 `C_SOURCES` / `C_INCLUDES`。目标文件按源文件 basename 扁平化；CM7 为 LwIP 与 libmetal 的 `init.c` / `sys.c` 保留了专用规则，添加同名源文件时检查目标冲突。
- `./make_all.ps1 [-Jobs N]` 仅构建两个核的双槽；从 WSL 可运行 `powershell.exe -NoProfile -File ./make_all.ps1 -Jobs 8`。`./make_flash_slotA.ps1 [-Jobs N]` / `./make_flash_slotB.ps1 [-Jobs N]` 构建对应槽，按 CM7、CM4 顺序烧录并在最后复位；不要当作仅构建命令。烧录需要 ST-Link 和 STM32CubeProgrammer，CLI 可通过 `STM32_PROGRAMMER_CLI` 指定。
- 烧录脚本仅写应用槽，不构建或烧录 `0x08000000` 的 CM7 Bootloader、`0x08100000` 的 CM4 Stub；启动/OTA 验证需另行确认板上的固定引导程序。

## HTTP / 摄像头验证

- 当前仓库测试入口为 `powershell.exe -NoProfile -File ./tests/http/mjpeg_test.ps1 -Device <IP> [-Frames 5]`。需要运行固件的实机，检查主页、multipart、多帧 800×480 JPEG 解码、推流期间 `/status` 响应及断线重连；不擦写、不复位。LCD 实际画面仍需现场确认。
- OTA 上位机位于 `../qt-ota/`，使用前读取其 `AGENTS.md`。其 `test-upload.ps1` / `test-recovery.ps1` 依赖已构建的 Qt EXE 和本工程 CM7 B BIN；模拟通过不代表实机擦写或启动验证通过。本仓库已无旧 OTA 上传器和旧 HTTP/OTA 测试入口。
- `/stream` 由独立任务持有连接，HTTP 主任务继续处理状态和 OTA；只允许一个观看连接，繁忙时返回 503。`/status` 的 `mjpeg_fps_x10` 是板端完整帧发送速率的十倍，不是浏览器渲染帧率。
- 相机快照通过 `ov5640_dcmi_copy_frame()` 暂缓三缓冲换帧并失效 D-Cache；不要直接读取正在采集的帧。SDRAM 固定地址分散在 `app_ov5640.c`、`ov5640_dcmi.c`、`app_hw_jpeg.c` 和 `app_http_mjpeg.c`，改缓冲布局时一起核对，保留 CPU/DMA 交接时的缓存维护。
- OTA 擦写前先停止 MJPEG 并等待任务退出，再暂停摄像头；失败/abort 路径须恢复。协议和会话实现见 `CM7/APP/OTA/`，不能跳过停流流程直接擦写。

## 启动和修改边界

- `CM7/Core/Src/main.c` 配置 MPU/缓存并清空 SRAM4，然后通过 HSEM 唤醒 CM4；`CM4/Core/Src/main.c` 在 D2 STOP 中等待。双核通过 `CM7/OPENAMP`、`CM4/OPENAMP` 与 `Common/` 中的共享代码协作；修改同步、IPC、启动或内存布局时同时检查两核。
- CM7 初始化外设后通过 `CM7/APP/BOOT/boot_eeprom.c` 读取 EEPROM 启动记录、选择 CM4 应用槽，再调用 `Common/Src/cm4_boot_handoff.c` 将 CM4 应用地址写入 SRAM4，并使能 CM4 启动核；EEPROM 不可用时默认 CM4 A 槽。修改槽位选择、EEPROM 启动记录或 handoff 时，同时核对 `Common/Inc/boot_slot.h`、CM4 Stub 的约定和双核链接脚本。
- 四份 Flash 链接脚本将 `.resource_table`（`NOLOAD`）放在 SRAM4 前 1 KiB 的 `0x38000000`，59 KiB OpenAMP 共享段从 `0x38000400` 开始，末尾 `0x3800F000..0x3800FFFF` 留给 handoff、OTA hold 和运行槽报告。CM7 MPU 将整个 64 KiB 配置为共享、不可缓存；改布局时同时核对两核 OpenAMP、四份链接脚本、MPU 与 `Common/Inc/cm4_boot_handoff.h`、`cm4_ota_hold.h`。
- CM4 从 Bank2 执行，擦写其空闲槽前必须经 `cm4_ota_hold_*` 握手，让 CM4 在 RAM 中关中断等待；保留 `.RamFunc` 复制和 abort 释放流程，不能只保护目标 Flash 地址。
- CM7 A/B 槽链接地址为 `0x08020000` / `0x080A0000`，CM4 为 `0x08120000` / `0x081A0000`，每槽 384 KiB。`Common/Inc/boot_slot.h` 的两核槽索引各自为 `0/1`，可直接用作各核元数据数组下标；CM7 使用 `boot_slot_*` 接口，CM4 使用 `cm4_boot_slot_*` 接口。A 槽镜像不能直接当作 B 槽镜像运行。
- CM7 的 `Core/Src/freertos.c` 创建 IPC、摄像头任务；默认任务先执行 `MX_LWIP_Init()` 再启动 HTTP，并周期喂 IWDG。`main()` 负责 ETH/PHY 外设初始化；ETH/LwIP、HTTP 与 OTA 服务仅在 CM7，CM4 通过 IPC/共享 mailbox 配合。
- `BaseProject.ioc` 是 CubeMX 配置源；`Core/` 中的外设代码为生成代码，修改时保留 `USER CODE` 区域。应用逻辑主要在 `CM7/APP/`、`CM4/APP/` 和 `CM7/BSP/`，不要无故改动 `Drivers/` 或 `Middlewares/`。
- `.vscode/launch.json` 使用本机 ST-Link/STM32CubeProgrammer 路径，仅指向 A 槽 ELF；CM7 为 launch，CM4 为 attach。
- `docs/superpowers/specs/` 保存设计与验证范围；判断已实现行为以当前源码和 Makefile 为准，设计中的目标帧率不代表实测结果。
