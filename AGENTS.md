# 仓库要点

- 这是 STM32H757XIH6 的双核 CubeMX 工程：`BaseProject.ioc` 是外设/代码生成配置；`CM7/Core/Src/main.c`、`CM4/Core/Src/main.c` 分别是两核入口，`Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c` 被两核共同编译。修改生成文件时保留 `USER CODE BEGIN/END` 区域；重新生成后核对自定义代码、链接脚本及 Makefile 源文件列表。
- 从仓库根目录执行 `make -C Makefile` 构建双核（先 CM4 后 CM7），或 `make -C Makefile/CM7`、`make -C Makefile/CM4` 单独构建；产物分别在 `Makefile/CM7/build/`、`Makefile/CM4/build/`。需 GNU Make 和 `arm-none-eabi-*`，可通过 `GCC_PATH` 指定工具链 bin 目录。没有仓库内测试套件；固件改动至少构建受影响的核。
- 清理使用根目录 `clean.ps1` 或分别执行 `make -C Makefile/CM4 clean`、`make -C Makefile/CM7 clean`；`make -C Makefile clean` 不会清理两核的 `build/`。
- `flash_all.ps1` 会先构建再经 SWD 依次烧录 CM7、CM4 ELF（最后复位）；`flash_cm7.ps1` / `flash_cm4.ps1` 仅构建并烧录对应核。脚本依赖 Windows 的 `STM32_Programmer_CLI.exe`（可设 `STM32_PROGRAMMER_CLI`）及 `make`，可传 `-Jobs N`。不要把构建误当作已完成硬件验证。
- 改动 Flash 布局时同时核对 `CM7/Core/Inc/boot_config.h`、两核 `Makefile/CM*/stm32h757xx_flash_CM*.ld` 和实际待烧录镜像：当前 CM7 bootloader 链接在 `0x08000000`、限 128K，APP1 预期在 `0x08020000`；CM4 链接从 `0x08100000` 开始，其 1024K 声明范围与 CM7 分区表中 `0x08120000` 的 setting 区重叠，分区表不能直接视为跨核完整规划。
- 当前 `CM7/Core/Src/main.c` 只检查 APP1 向量表有效性，`Boot_JumpToAPP()` 调用被注释；跳转实现及 RAM 标记在 `CM7/Core/Src/boot_jump.c` / `boot_config.h`。CM4 入口启用了 HSEM 等待 CM7 唤醒的双核同步序列；修改启动顺序时先检查两核配套逻辑。
