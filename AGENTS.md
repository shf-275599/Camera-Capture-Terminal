# 仓库操作说明

## 项目结构

- 这是一个基于 STM32H757XIH6 双核的项目，由 STM32CubeMX 6.18.1 生成，使用 Makefile 工具链。`CM7/` 为主核，拥有 SDRAM、LCD/LTDC、DMA2D 以及 OpenAMP 主端；`CM4/` 拥有 USART1 和 OpenAMP 从端。
- 双核均使用 FreeRTOS，并通过 OpenAMP/RPMsg 经由 SRAM4 通信。CM7 在启动时通过 HSEM 释放 CM4，因此对启动同步、`CM4/OPENAMP`、`CM7/OPENAMP`、链接脚本或 MPU/缓存配置的更改必须成对考虑。
- `BaseProject.ioc` 是 CubeMX 的源文件（真相来源）。生成的外设文件包含 `USER CODE` 区域；请保留这些区域，并注意 CubeMX 重新生成时会覆盖生成的部分。

## 构建与清理

- 在仓库根目录下，使用 `make -C Makefile` 构建两个固件镜像；根 Makefile 先构建 CM4，再构建 CM7。
- 单独构建某个核：`./flash_cm7.ps1`、`./flash_cm4.ps1` 或 `./flash_all.ps1`。生成的 ELF/HEX/BIN/MAP 文件位于对应的 `Makefile/CM4/build` 或 `Makefile/CM7/build` 目录下。
- Makefile 直接通过 `./flash_cm7.ps1`、`./flash_cm4.ps1` 或 `./flash_all.ps1` 构建。
- 清理生成的核输出：在 PowerShell 中运行 `./clean.ps1`，或直接执行 `make -C Makefile/CM4 clean` 和 `make -C Makefile/CM7 clean`。
- 仓库没有测试或 lint 套件。对源码更改的验证重点是受影响核的构建成功；涉及共享启动/OpenAMP 代码的更改应同时构建两个核。

## 烧录与调试

- 烧录需要 Windows PowerShell、GNU Make、ST-Link 连接以及 STM32CubeProgrammer。使用 `./flash_cm7.ps1`、`./flash_cm4.ps1` 或 `./flash_all.ps1`；每个脚本在烧录前会先构建，并验证 ELF 文件是否存在。
- 如果 `STM32_Programmer_CLI.exe` 不在 `PATH` 中，也不在脚本的 Windows 后备路径之一，请设置 `STM32_PROGRAMMER_CLI` 环境变量。`flash_all.ps1` 先烧录 CM7，再烧录 CM4，并在 CM4 烧录后执行复位。
- VS Code Cortex-Debug 配置期望生成的 ELF 路径以及 `.vscode/launch.json` 中硬编码的本地 STM32CubeIDE/ST-Link 路径；请将这些路径视为机器特定，不要假设它们能在其他主机上工作。

## 变更边界

- 除非更改确实必要，否则不要编辑 `Drivers/`、`Middlewares/` 或 CubeMX 生成的外设代码段。优先在 `BSP/`、`CM4/APP/` 和 `CM7/APP/` 或 `USER CODE` 块中编写应用代码。
- 保持 CM7/CM4 的 OpenAMP 角色和共享内存布局一致。链接脚本将 OpenAMP 共享内存放置在 `0x38000400`，大小为 63 KiB，而 MPU/缓存配置将 `0x38000000` 处的 SRAM4 视为共享非缓存内存。
- 不要提交构建产物或调试器元数据。`.gitignore` 已排除 `*.elf`、`*.hex`、`*.bin`、`*.map`、`*.lst`、`*.d`、构建输出以及 `.vscode` 内容。