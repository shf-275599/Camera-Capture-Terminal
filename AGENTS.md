# 仓库操作说明

## 构建与验证

- 这是 STM32H757XIH6 双核固件。在 Windows PowerShell 中使用 GNU Make 和 `arm-none-eabi-*` 工具链（可通过 `make ... GCC_PATH=工具链bin目录` 指定）；从 WSL 操作时通过 `powershell.exe` 执行构建。`make -C Makefile` 先构建 CM4、再构建 CM7，默认仅构建 A 槽。
- 聚焦单核/单槽：`make -C Makefile/CM7 APP_SLOT=B` 或 `make -C Makefile/CM4 APP_SLOT=A`；双槽分别运行 `make -C Makefile/CM7 all-slots`、`make -C Makefile/CM4 all-slots`。各核 A 槽产物在 `Makefile/CMx/build/`，B 槽在 `Makefile/CMx/build_B/`（`CMx` 为 `CM4` 或 `CM7`）。
- 仓库没有独立测试或 lint 入口；源码变更构建受影响的核，修改 `Common/`、启动或 OpenAMP 时构建两个核。`all-slots` 的配方含 Windows `@echo.`，请在 Windows 环境运行构建。
- `./clean.ps1` 清理两个核的双槽产物；顶层 `make -C Makefile clean` 不递归清理两核产物。
- 源文件和头文件目录需显式加入各核 Makefile 的 `C_SOURCES` / `C_INCLUDES`。目标文件按源文件 basename 扁平化；CM7 为 LwIP 与 libmetal 的 `init.c` / `sys.c` 保留了专用规则，添加同名源文件时检查目标冲突。
- `./make_all.ps1 [-Jobs N]` 构建两个核的双槽；`./make_flash_slotA.ps1 [-Jobs N]` 构建并按 CM7-A、CM4-A 顺序烧录 A 槽；`./make_flash_all.ps1 [-Jobs N]` 构建两个核的双槽，再按 CM7-A、CM4-A、CM7-B、CM4-B 顺序烧录，最后复位。需要 ST-Link 和 STM32CubeProgrammer；CLI 未在 PATH 时可设置 `STM32_PROGRAMMER_CLI`。不要把烧录脚本当作仅构建命令。
- 该脚本仅烧录四个应用槽，不构建或烧录 `0x08000000` 的 CM7 Bootloader、`0x08100000` 的 CM4 Stub；启动/OTA 验证需另行确认板上的固定引导程序。

## 启动和修改边界

- `CM7/Core/Src/main.c` 配置 MPU/缓存并清空 SRAM4，然后通过 HSEM 唤醒 CM4；`CM4/Core/Src/main.c` 在 D2 STOP 中等待。双核通过 `CM7/OPENAMP`、`CM4/OPENAMP` 与 `Common/` 中的共享代码协作；修改同步、IPC、启动或内存布局时同时检查两核。
- CM7 初始化外设后通过 `CM7/APP/BOOT/boot_eeprom.c` 读取 EEPROM 启动记录、选择 CM4 应用槽，再调用 `Common/Src/cm4_boot_handoff.c` 将 CM4 应用地址写入 SRAM4，并使能 CM4 启动核；EEPROM 不可用时默认 CM4 A 槽。修改槽位选择、EEPROM 启动记录或 handoff 时，同时核对 `Common/Inc/boot_slot.h`、CM4 Stub 的约定和双核链接脚本。
- 四份 Flash 链接脚本将 `.resource_table`（`NOLOAD`）放在 SRAM4 前 1 KiB 的 `0x38000000`，63 KiB 共享段从 `0x38000400` 开始；CM7 MPU 将整个 64 KiB 配置为共享、不可缓存。handoff 地址由 `Common/Inc/cm4_boot_handoff.h` 定义为 `0x3800F010` / `0x3800F014`；改布局时同时核对两核 OpenAMP 配置、链接脚本与 MPU。
- CM7 A/B 槽链接地址为 `0x08020000` / `0x080A0000`，CM4 为 `0x08120000` / `0x081A0000`，每槽 384 KiB。`Common/Inc/boot_slot.h` 的两核槽索引各自为 `0/1`，可直接用作各核元数据数组下标；CM7 使用 `boot_slot_*` 接口，CM4 使用 `cm4_boot_slot_*` 接口。A 槽镜像不能直接当作 B 槽镜像运行。
- CM7 的 `Core/Src/freertos.c` 启动 IPC、摄像头和 HTTP 任务，`MX_LWIP_Init()` 在默认任务中执行；网络初始化不在 `main()` 中。CM4 负责自身 IPC，ETH/LwIP 与 HTTP 构建仅在 CM7。
- `BaseProject.ioc` 是 CubeMX 配置源；`Core/` 中的外设代码为生成代码，修改时保留 `USER CODE` 区域。应用逻辑主要在 `CM7/APP/`、`CM4/APP/` 和 `CM7/BSP/`，不要无故改动 `Drivers/` 或 `Middlewares/`。
- `.vscode/launch.json` 使用本机 ST-Link/STM32CubeProgrammer 路径，仅指向 A 槽 ELF；CM7 为 launch，CM4 为 attach。
- `docs/` 中 HTTP、OTA 与本地升级文档包含实施方案和旧基线（例如 HTTP“尚未接入”）；判断已实现功能以当前源码和 Makefile 为准。
