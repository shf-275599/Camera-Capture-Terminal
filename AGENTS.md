# 仓库操作说明

## 构建与验证

- 这是 STM32H757XIH6 双核固件。Windows PowerShell 中使用 GNU Make 和 `arm-none-eabi-*` 工具链（可通过 `GCC_PATH` 指定）；`make -C Makefile` 先构建 CM4、再构建 CM7，默认仅构建 A 槽。
- 聚焦单核/单槽：`make -C Makefile/CM7 APP_SLOT=B` 或 `make -C Makefile/CM4 APP_SLOT=A`；双槽分别运行 `make -C Makefile/CM7 all-slots`、`make -C Makefile/CM4 all-slots`。各核 A 槽产物在 `Makefile/CMx/build/`，B 槽在 `Makefile/CMx/build_B/`（`CMx` 为 `CM4` 或 `CM7`）；`./clean.ps1` 清理两个核的双槽产物。
- 仓库没有独立测试或 lint 入口；源码变更构建受影响的核，修改 `Common/`、启动或 OpenAMP 时构建两个核。`all-slots` 的配方含 Windows `@echo.`，请在 Windows 环境运行构建。
- `./flash_all.ps1 [-Jobs N]` 先构建两个核的双槽，再按 CM7-A、CM4-A、CM7-B、CM4-B 顺序烧录，最后复位。需要 ST-Link 和 STM32CubeProgrammer；CLI 未在 PATH 时可设置 `STM32_PROGRAMMER_CLI`。不要把烧录脚本当作仅构建命令。

## 启动和修改边界

- `CM7/Core/Src/main.c` 配置 MPU/缓存并清空 SRAM4，然后通过 HSEM 唤醒 CM4；`CM4/Core/Src/main.c` 在 D2 STOP 中等待。双核通过 `CM7/OPENAMP`、`CM4/OPENAMP` 与 `Common/` 中的共享代码协作；修改同步、IPC、启动或内存布局时同时检查两核。
- CM7 初始化外设后通过 `CM7/APP/BOOT/boot_eeprom.c` 读取 EEPROM 启动记录、选择 CM4 应用槽，再调用 `Common/Src/cm4_boot_handoff.c` 将 CM4 应用地址写入 SRAM4，并使能 CM4 启动核；EEPROM 不可用时默认 CM4 A 槽。修改槽位选择、EEPROM 启动记录或 handoff 时，同时核对 `Common/Inc/boot_slot.h`、CM4 Stub 的约定和双核链接脚本。
- 双核链接脚本把 SRAM4 前 1 KiB 定义为 `OPENAMP_RSC_TAB`，63 KiB 共享段从 `0x38000400` 开始；实际 `.resource_table` 节放在共享段起点。CM7 MPU 将 `0x38000000` 起的整个 64 KiB 配置为共享、不可缓存。CM7 A/B 槽分别从 `0x08020000`/`0x080A0000` 链接，CM4 A/B 槽分别从 `0x08120000`/`0x081A0000` 链接；CM4 的 `0x08100000` 留给 Stub。
- `BaseProject.ioc` 是 CubeMX 配置源；`Core/` 中的外设代码为生成代码，修改时保留 `USER CODE` 区域。应用逻辑主要在 `CM7/APP/`、`CM4/APP/` 和 `CM7/BSP/`，不要无故改动 `Drivers/` 或 `Middlewares/`。
- `.vscode/launch.json` 的 ST-Link/STM32CubeProgrammer 路径是本机路径，调试配置仅指向 A 槽 ELF。构建目录和固件产物已被 `.gitignore` 排除。
