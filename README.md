# BaseProject — STM32H757 双核摄像头固件

基于 STM32H757XIH6（Cortex-M7 + Cortex-M4）的双核 FreeRTOS 固件：OV5640 摄像头经 DCMI 采集 800×480 RGB565，CM7 用硬件 JPEG 编码后同时驱动 LTDC LCD 预览与 HTTP MJPEG 推流，并支持 SD 卡抓拍 BMP、USB 读卡器导出和双核 A/B 槽 OTA 升级。

详细的仓库操作规则（构建边界、实机验证、修改注意事项）见 [`AGENTS.md`](AGENTS.md)。

## 硬件配置

| 外设 | 器件 / 说明 |
| --- | --- |
| 摄像头 | OV5640，DCMI 并口采集，SCCB（I2C4）配置，输出 800×480 RGB565 |
| 显示 | RGB 接口 LCD（LTDC + DMA2D），工作分辨率 800×480 |
| 以太网 | LAN8720 PHY，LwIP 协议栈，HTTP 服务器 |
| SDRAM | FMC，帧缓冲与 JPEG 缓冲（MJPEG 输出缓冲自 `0xD0700000`，抓拍帧 `0xD0900000`） |
| 存储 | SD 卡（FatFs，抓拍 `0:/CAM0001.BMP`～`CAM9999.BMP`）、EEPROM（CM4 启动槽选择） |
| USB | OTG_FS MSC 读卡器导出 |
| 其他 | USART1 日志、IWDG1 看门狗、ST-Link 调试 |

## 双核架构与启动

- **CM7（主核）**：摄像头采集、硬件 JPEG、LTDC LCD、ETH/LwIP、HTTP（主页、`/stream` MJPEG 双观看连接、`/status`、OTA 上传）、SD 抓拍、USB 导出、看门狗喂狗。外设初始化在 `CM7/Core/Src/main.c`，任务创建在 `Core/Src/freertos.c`。
- **CM4（从核）**：在 D2 STOP 中等待，由 CM7 经 HSEM 唤醒，通过 OpenAMP（VirtioMailbox）与 CM7 协作；应用逻辑仅 `CM4/APP/IPC/`。
- **启动与槽位**：固定 CM7 Bootloader（`0x08000000`）与 CM4 Stub（`0x08100000`）不在本工程构建。应用各自占用 A/B 双槽（每槽 384 KiB）：CM7 为 `0x08020000` / `0x080A0000`，CM4 为 `0x08120000` / `0x081A0000`。CM7 `main()` 读取 EEPROM 选择 CM4 应用槽，经 SRAM4 handoff 启动 CM4；SRAM4（`0x38000000` 起 64 KiB）还承载 OpenAMP 共享段。
- **双核共享代码**在 `Common/`：槽位元数据（`boot_slot.h`）、CM4 启动交接（`cm4_boot_handoff`）、CM4 OTA 握手（`cm4_ota_hold`）、RAM 函数擦写（`bsp_flash_ram`）、日志（`mylog`）。

## 目录说明

| 目录 | 说明 |
| --- | --- |
| `Makefile/` | 顶层 Makefile（先 CM4 后 CM7）及两核各自构建目录、四份 Flash 链接脚本；产物在 `CMx/build/`（A 槽）与 `CMx/build_B/`（B 槽） |
| `CM7/Core/` | CM7 CubeMX 生成外设代码（修改保留 `USER CODE` 区域） |
| `CM7/APP/` | CM7 应用逻辑：`BOOT`（EEPROM 槽选择）、`CAMERA`（OV5640/SD/抓拍/存储/USB）、`IPC`、`JPEG`（硬件/软件编码）、`NET/HTTP`（服务器与 MJPEG）、`OTA` |
| `CM7/BSP/` | CM7 板级驱动：CAMERA、EEPROM、ETH、LCD、MPU、SDNAND、SDRAM、USBDMSC |
| `CM7/OPENAMP/`、`CM4/OPENAMP/` | OpenAMP / HSEM mailbox 通信 |
| `CM4/Core/`、`CM4/APP/` | CM4 生成代码与 IPC 应用 |
| `Common/` | 双核共享头文件与源码（见上文） |
| `Drivers/`、`Middlewares/` | ST HAL、FreeRTOS、LwIP、FatFs、OpenAMP 等，勿无故改动 |
| `Utilities/` | `JPEG/jpeg_utils.c`（CM7 构建使用）、`TJpgDec`（软件 JPEG 解码） |
| `tests/` | 升级脚本模拟测试、JPEG 色彩回归、HTTP MJPEG 实机测试 |
| `docs/` | `local_flash_update.md`（本地升级协议）、`superpowers/specs/`（设计文档） |
| `BSP/` | SDNAND、USBDMSC 板级代码，当前未接入任一核构建 |

## 构建

构建在 Windows PowerShell 中进行（GNU Make + `arm-none-eabi-*` 工具链，可用 `GCC_PATH` 指定 bin 目录；从 WSL 操作时通过 `powershell.exe` 执行）：

```powershell
# 双核默认 A 槽（先 CM4 后 CM7）
make -C Makefile

# 单核单槽
make -C Makefile/CM7 APP_SLOT=B -j8

# 全量双槽（依次对两核执行）
make -C Makefile/CM4 all-slots -j8
make -C Makefile/CM7 all-slots -j8

# 清理双核双槽产物
./clean.ps1
```

产物：A 槽 `Makefile/CMx/build/BaseProject_CMx.{elf,hex,bin}`，B 槽 `Makefile/CMx/build_B/BaseProject_CMx_B.{elf,hex,bin}`。

## 烧录与 OTA

四个 `make_flash_cm{7,4}_slot{A,B}.ps1` 入口会构建目标核指定槽、经 ST-Link 烧录、登记 pending 并复位（支持 `-Jobs N`、`-Version V`）：

```powershell
./make_flash_cm7_slotA.ps1 -Jobs 8 -Version 1
```

前置条件与协议细节见 `docs/local_flash_update.md`；OTA 上位机位于 `../qt-ota/`（Qt 工程，单独构建）。

## 测试

```powershell
# 升级脚本无硬件模拟
powershell.exe -NoProfile -File ./tests/local_flash_update_test.ps1

# JPEG RGB565→YCbCr420 转换回归（依赖脚本内固定 MinGW 路径）
powershell.exe -NoProfile -File ./tests/jpeg_color_equivalence_test.ps1

# 实机只读 MJPEG 测试（需设备 IP，不擦写、不复位）
powershell.exe -NoProfile -File ./tests/http/mjpeg_test.ps1 -Device <IP> -Frames 5
```

注意：模拟测试通过不代表实机擦写、启动或回退已验证。
