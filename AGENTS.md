# 仓库操作说明

## 构建与验证

- 这是 STM32H757XIH6 双核固件。在 Windows PowerShell 中使用 GNU Make 和 `arm-none-eabi-*` 工具链（可通过 `make ... GCC_PATH=工具链bin目录` 指定）；从 WSL 操作时通过 `powershell.exe` 执行构建和测试。以下路径和命令均相对本目录；`make -C Makefile` 先构建 CM4、再构建 CM7，默认仅构建 A 槽。
- 聚焦单核/单槽：`make -C Makefile/CM7 APP_SLOT=B -j8`（核名与槽号按需替换）。全量仅构建：依次运行 `make -C Makefile/CM4 all-slots -j8`、`make -C Makefile/CM7 all-slots -j8`。A 槽产物为 `Makefile/CMx/build/BaseProject_CMx.{elf,hex,bin}`，B 槽为 `Makefile/CMx/build_B/BaseProject_CMx_B.{elf,hex,bin}`。
- 源码变更构建受影响的核；修改 `Common/`、启动或 OpenAMP 时构建两个核，修改槽位或链接布局时验证 A/B。`all-slots` 的配方含 Windows `@echo.`，请在 Windows 环境运行构建。
- `./clean.ps1` 清理两个核的双槽产物；顶层 `make -C Makefile clean` 不递归清理两核产物。
- 源文件和头文件目录需显式加入各核 Makefile 的 `C_SOURCES` / `C_INCLUDES`。目标文件按源文件 basename 扁平化；CM7 为 LwIP 与 libmetal 的 `init.c` / `sys.c` 保留了专用规则，添加同名源文件时检查目标冲突。
- 四个 `make_flash_cm{7,4}_slot{A,B}.ps1` 入口均支持 `[-Jobs N] [-Version V]`（版本默认 0），会构建、烧录目标核指定槽、登记 pending 并复位；不能当作仅构建命令。升级协议实现各自独立，修改时同步维护四份。
- 本地升级需要 ST-Link、STM32CubeProgrammer（可用 `STM32_PROGRAMMER_CLI` 指定 CLI）及支持本地协议 v2 的 Bootloader；要求双核无 pending、confirmed 镜像有效且目标槽不是目标核 confirmed。不支持空板初始化。脚本不构建或烧录 `0x08000000` 的 CM7 Bootloader、`0x08100000` 的 CM4 Stub；固定引导程序更新与协议说明见 `docs/local_flash_update.md`。
- 本地升级脚本的无硬件模拟测试：`powershell.exe -NoProfile -File ./tests/local_flash_update_test.ps1`。模拟通过或烧录输出 `PENDING` 均不代表实机启动、应用确认或回退已验证；两核独立确认和回退，不提供双核版本整体原子回滚。
- JPEG RGB565→YCbCr420 转换回归：`powershell.exe -NoProfile -File ./tests/jpeg_color_equivalence_test.ps1`，提取实际转换函数与旧实现逐字节比较；依赖脚本内固定的 `D:\QT\Tools\mingw530_32\bin\gcc.exe`，不使用 ARM 交叉编译器。

## HTTP / 摄像头验证

- 实机只读测试：`powershell.exe -NoProfile -File ./tests/http/mjpeg_test.ps1 -Device <IP> -Frames 5`。检查主页、双观看连接、第三连接 503、multipart、多帧 800×480 JPEG 解码、推流期间 `/status` 响应、单端断开后另一端继续及重连；不擦写、不复位。LCD 实际画面仍需现场确认。
- OTA 上位机位于 `../qt-ota/`，使用前读取其 `AGENTS.md`。其 `test-upload.ps1` / `test-recovery.ps1` 依赖已构建的 Qt EXE 和 `Makefile/CM7/build_B/BaseProject_CM7_B.bin`；模拟通过不代表实机擦写或启动验证通过。
- `/stream` 支持两个观看连接，共用一个编码任务与三块 JPEG 缓冲，每个连接由独立任务发送；慢连接跳过旧帧，满员或 OTA 期间返回 503。HTTP 主任务继续处理状态和 OTA；`/status` 的 `mjpeg_fps_x10` 为最近仍在发送的最快观看连接帧率的十倍，不累加两个连接，也不是浏览器渲染帧率。JPEG 缓冲占用 `0xD0700000..0xD087FFFF`。
- 相机快照通过 `ov5640_dcmi_copy_frame()` 暂缓三缓冲换帧并失效 D-Cache；不要直接读取正在采集的帧。改 SDRAM 布局时一起核对 `app_ov5640.c`、`ov5640_dcmi.c`、`app_hw_jpeg.c`、`app_http_mjpeg.c` 和 `app_snapshot.c`（抓拍帧固定在 `0xD0900000`），保留 CPU/DMA 交接时的缓存维护。
- OTA（`CM7/APP/OTA/`）先取得 `app_storage_ota_lock()`，再停止 MJPEG 并等待任务退出、暂停摄像头，之后才能擦写；失败/abort 路径须恢复并释放存储占用。

## SD 卡与 USB 所有权

- `CM7/APP/CAMERA/app_snapshot.c` 在 PA8 短按松开时保存 `0:/CAM0001.BMP`～`CAM9999.BMP`，长按两秒切换本地 FatFs / USB MSC 读写导出。挂载失败不自动格式化；USB 模式需先返回本地才能抓拍或开始 OTA。
- 所有权交接由 `app_storage.c` 在抓拍任务中执行：等待保存结束、卸载 FatFs 后才开放 USB，确认 USB 停止后才重新挂载。文件关闭失败锁存故障，需复位；不能让 FatFs 与 USB 主机同时访问卡。
- `app_usb_export.c` 将 PCD/BOT、同步扇区 I/O 和断开串行放在 USB 任务中；`OTG_FS` IRQ 仅屏蔽中断并唤醒任务。不要恢复成在 IRQ 内直接调用 `HAL_PCD_IRQHandler()` 或读写 SD 卡。

## 启动和修改边界

- `CM7/Core/Src/main.c` 配置 MPU/缓存并清空 SRAM4，然后通过 HSEM 唤醒 CM4；`CM4/Core/Src/main.c` 在 D2 STOP 中等待。双核通过 `CM7/OPENAMP`、`CM4/OPENAMP` 与 `Common/` 中的共享代码协作；修改同步、IPC、启动或内存布局时同时检查两核。
- CM7 在 `main()` 中读取 EEPROM，通过 `CM7/APP/BOOT/boot_eeprom.c` 选择 CM4 应用槽，再经 `Common/Src/cm4_boot_handoff.c` 写 SRAM4 并使能 CM4 启动核；EEPROM 不可用时默认 CM4 A 槽。修改该流程时同时核对 `Common/Inc/boot_slot.h`、固定 CM4 Stub 的约定和双核链接脚本。
- 四份 Flash 链接脚本将 `.resource_table`（`NOLOAD`）放在 SRAM4 前 1 KiB 的 `0x38000000`，59 KiB OpenAMP 共享段从 `0x38000400` 开始，末尾 `0x3800F000..0x3800FFFF` 留给 handoff、OTA hold 和运行槽报告。CM7 MPU 将整个 64 KiB 配置为共享、不可缓存；改布局时同时核对两核 OpenAMP、四份链接脚本、MPU 与 `Common/Inc/cm4_boot_handoff.h`、`cm4_ota_hold.h`。
- CM4 从 Bank2 执行，擦写其空闲槽前必须经 `cm4_ota_hold_*` 握手，让 CM4 在 RAM 中关中断等待；保留 `.RamFunc` 复制和 abort 释放流程，不能只保护目标 Flash 地址。
- CM7 A/B 槽链接地址为 `0x08020000` / `0x080A0000`，CM4 为 `0x08120000` / `0x081A0000`，每槽 384 KiB。`Common/Inc/boot_slot.h` 的两核槽索引各自为 `0/1`，可直接用作各核元数据数组下标；CM7 使用 `boot_slot_*` 接口，CM4 使用 `cm4_boot_slot_*` 接口。A 槽镜像不能直接当作 B 槽镜像运行。
- CM7 的 `Core/Src/freertos.c` 创建串口、IPC、摄像头及抓拍任务；默认任务先执行 `MX_LWIP_Init()` 再启动 HTTP，并周期喂 IWDG。`main()` 负责 ETH/PHY 外设初始化；ETH/LwIP、HTTP 与 OTA 服务仅在 CM7，CM4 通过 IPC/共享 mailbox 配合。
- `BaseProject.ioc` 是 CubeMX 配置源；`Core/` 中的外设代码为生成代码，修改时保留 `USER CODE` 区域。应用逻辑主要在 `CM7/APP/`、`CM4/APP/` 和 `CM7/BSP/`，不要无故改动 `Drivers/` 或 `Middlewares/`。
- `.vscode/launch.json` 使用本机 ST-Link/STM32CubeProgrammer 路径，仅指向 A 槽 ELF；CM7 为 launch，CM4 为 attach。
- `docs/superpowers/specs/` 保存设计与验证范围；判断已实现行为以当前源码和 Makefile 为准，设计中的目标帧率不代表实测结果。
