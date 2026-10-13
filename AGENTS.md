# 仓库要点

## 构建与验证

- 在 Windows PowerShell 中从仓库根目录执行 `make -C Makefile`（先 CM4 后 CM7）；单核用 `make -C Makefile/CM7` 或 `make -C Makefile/CM4`。需 GNU Make 和 `arm-none-eabi-*`；工具链不在 PATH 时传 `GCC_PATH=<bin目录>`。产物为 `Makefile/CM*/build/BaseProject_CM*.{elf,hex,bin}`。
- 清理双核用 `./clean.ps1`；顶层 `make -C Makefile clean` 不会清理两核产物。Windows 下 `clean.ps1` 直接删除 build 目录，不依赖子 Makefile 的 `rm`。
- 没有仓库内测试套件；固件改动至少构建受影响的核，修改共用源码须构建双核。链接脚本未列入 ELF 目标依赖，改 `.ld` 后先清理再构建，避免沿用旧 ELF。
- `./flash_all.ps1 -Jobs N` 先构建双核，再经 SWD（`mode=UR`）依次写入并校验 CM7、CM4 ELF，最后复位。依赖 Windows `STM32_Programmer_CLI.exe`，可设 `STM32_PROGRAMMER_CLI` 指定路径；此脚本只烧录 Bootloader/Stub，不包含 A/B 应用镜像。构建和烧录校验不能替代应用启动的硬件验证。

## 代码生成与启动流程

- `BaseProject.ioc` 是 CubeMX 配置（STM32Cube FW_H7 V1.13.0，Makefile 工具链）。修改生成代码须保留 `USER CODE BEGIN/END`；重新生成后核对自定义启动逻辑、链接脚本及两核 Makefile 的 `C_SOURCES` / `C_INCLUDES`，自定义 APP、BSP、Log 源码采用显式列表。
- 两核共编译 `Common/Src/boot_slot.c` 和 `Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c`。CM7 入口在 `CM7/Core/Src/main.c`，通过 `CM7/APP/BOOT/boot_eeprom.c` 选择 A/B 槽并处理试启动回退，再调用 `boot_jump_to_app()`；槽有效性仅检查向量表，不校验整镜像 CRC。
- CM7 跳转实现在 `CM7/APP/BOOT/boot_jump.c`：清除 CM4 的 `BOOT_C2`，清理中断、切 VTOR/MSP，并在 `0x2001FFF0` 写入 `BOOT_JUMP_MAGIC`（见同目录 `boot_config.h`）。改启动逻辑需核对应用侧 SystemInit 对此标记及 CM4 启动的配套处理。
- CM4 入口 `CM4/Core/Src/main.c` 是固定 Stub，不执行 STOP/HSEM 等待；`CM4/APP/BOOT/cm4_boot_handoff.c` 从 D3 SRAM 的 `0x3800F010` / `0x3800F014` 读取 magic/base，无有效 magic 时尝试 A 槽，跳转失败则关闭中断停留。改共享内存或启动协议需核对应用侧写入者与共享区占用。

## Flash 布局

- 分区定义以 `Common/Inc/boot_slot.h` 为准：CM7 Boot 在 `0x08000000`、CM4 Stub 在 `0x08100000`，各 128KB；CM7 A/B 在 `0x08020000` / `0x080A0000`，CM4 A/B 在 `0x08120000` / `0x081A0000`，每槽 384KB，两槽间各预留 128KB。全局槽索引为 CM7 0/1、CM4 2/3；CM4 元数据数组下标需减去 CM4 A 槽索引。
- 改布局时同步核对 `boot_slot.h`、两核 `Makefile/CM*/stm32h757xx_flash_CM*.ld`、应用镜像链接地址及向量表。默认构建使用 Flash 链接脚本，两核 FLASH 长度均为 128K；`docs/STM32H757双核MCU内存地址分配.md` 中的 CM4 1024K、旧 APP1/setting 规划已过时，不能作为当前布局依据。
