# 抓拍与 USB 导出重新实现验证

本次工作从 `936703fcf7c95ba7757facf18ac5017f0da3594c` 建立 `re-snapshot-usb` 分支，按配置、实际 CubeMX 生成、应用实现、再次生成验证的顺序实施。

## 配置与代码边界

- SDMMC2 使用 PA0 CMD、PD6 CK、PB14/PB15/PB3/PB4 D0..D3，PLL2R 为 100 MHz，ClockDiv 为 2，开启硬件流控。
- USB OTG FS 使用 PA11/PA12，HSI48 和 USB2 SOF CRS，同步关闭 VBUS 检测；通过 PA8 长按切换，不按插拔自动切换。
- FatFs 使用 CubeMX 的 User-defined 驱动和标准 diskio/ff_gen_drv，不重新定义标准 disk_* 符号。
- SD、FatFs 和 USB 的自动初始化调用在 IOC 中关闭，由应用任务控制。
- PA8 使用输入电平任务去抖；松开短按保存、持续两秒长按切换。保存任务与按键任务分离。
- 新增应用代码位于 `CM7/APP/CAMERA/`。生成文件的接入逻辑均位于 USER CODE 区域。
- USB 中断仅屏蔽并唤醒任务；PCD、MSC、同步 SD 读取和停止均在 USB 任务中串行执行。
- USB 通过官方写保护回调报告只读，并拒绝实际块写回调，未修改 MSC 中间件。
- `SDMMC_SWDATATIMEOUT=1000U` 通过应用 GNUmakefile 的编译宏覆盖，未修改 HAL。
- 两核应用构建入口迁至 `GNUmakefile`，四个升级脚本从该入口读取 TARGET_BASE。对象及 ELF 规则均依赖 GNUmakefile，确保编译配置变化触发重编译。CubeMX 默认 Makefile 保留独立入口。

## 实际生成

使用本机 STM32CubeMX 6.18.1，路径为 `D:\STM32CubeMX\STM32CubeMX\STM32CubeMX.exe`，H7 固件包为 V1.13.0。

批处理脚本内容如下，实际执行时使用工程的绝对 Windows 路径：

```text
config load "F:/27559/Projects/STM32-Projects/STM32H7xx/STM32H757XIH6/Camera-Double-CPU/BaseProject/BaseProject.ioc"
project generate
exit
```

Windows PowerShell 调用 CubeMX `-q <脚本>` 实际生成。最终往返验证使用安装目录内 `jre/bin/java.exe` 启动同一个 CubeMX EXE/JAR，以完整捕获标准输出。日志末尾为 `OK`、`exit`、`Bye bye`。生成时确认了基线已有的 LwIP 配置警告。

第一次生成因移走默认 Makefile 导致 CubeMX 更新器空指针失败；保留默认入口后重试。未把该失败计为成功验证。

应用实现后再次生成，比较 141 个应用/生成源文件、头文件、链接脚本及 GNUmakefile 的 SHA-256（统一 CRLF/LF），变化文件数为 **0**。SD 时钟、USB CRS 配置及关键 USER CODE 接入均保留。随后补充标准驱动重新链接的故障恢复处理并修正 GNUmakefile 依赖，连续两次实际生成均返回 OK。扩大为 146 个文件比较，145 个应用与 USER CODE、链接脚本、GNUmakefile、四个升级脚本完全不变；IOC 唯一差异为末行 `rtos.0.ip` 在 `FREERTOS_M4` 与 `FREERTOS_M7` 间切换，其余配置完全一致。

## 构建和测试

Windows PowerShell 下执行：

```powershell
make -C Makefile/CM4 all-slots -j8
make -C Makefile/CM7 all-slots -j8
powershell.exe -NoProfile -File ./tests/local_flash_update_test.ps1
powershell.exe -NoProfile -File ./tests/http/mjpeg_test.ps1 -Device 192.168.10.2 -Frames 5
```

- 故障恢复补充及 GNUmakefile 依赖修正后的最终两核 A/B 全量构建通过。CM7 两槽 text 均为 201528 字节、bss 为 131916 字节；CM4 两槽 text 均为 23268 字节、bss 为 21156 字节，每槽容量 393216 字节。
- 本地升级脚本模拟测试通过，覆盖两核双槽、请求字段、协议版本拒绝、顺序和失败路径。
- 实机升级前串口核实两核均 A confirmed、均无 pending。
- 使用 `make_flash_cm7_slotB.ps1 -Jobs 8` 升级 CM7 B，下载校验通过。
- 复位后 COM7 115200 8-N-1 读取确认 CM7 B running/confirmed、无 pending；CM4 仍 A confirmed、无 pending。
- 新固件 SD 挂载 FR=0，相机 800×480 首帧检查通过，HTTP 服务启动。
- MJPEG 实机测试通过：双连接 JPEG 解码、第三连接 503、状态查询、断开后继续和重连。
- 最新恢复补充后的升级前，再次读取确认 CM7 B confirmed、CM4 A confirmed，双核无 pending。使用 `make_flash_cm7_slotA.ps1 -Jobs 8` 将最新镜像写入空闲 CM7 A，下载校验通过，未覆盖当时 confirmed B。
- 最新固件复位后确认 CM7 A running/confirmed、CM4 A confirmed，双核无 pending。Bootloader 对 A 镜像报告 size=201536、CRC32=0x6AF5ECE4，与本地最新 `BaseProject_CM7.bin` 完全一致，板上已包含恢复补充。
- 最新 A 固件 SD 挂载 FR=0、相机首帧通过、HTTP 服务启动；再次运行同一 MJPEG 实机测试通过。
- 用户确认最新 A 固件 LCD 画面正常，并明确只短按两次。主会话串口捕获 CAM0013.BMP 保存 3319 ms、CAM0014.BMP 保存 3259 ms，均为 1152054 字节；两张照片对应两次短按，不视为长按额外拍照。
- 随后用户明确长按，主会话捕获 `BUTTON_LONG accepted=1` → `STORAGE_SWITCH_START previous=0` → `USB_EXPORT_READY readonly=1`。
- Windows 枚举为磁盘 2、H:，USB VID=0483、PID=572A、序列号 355B38553432，Get-Disk 容量 503316480 字节（983040×512），IsReadOnly=True。身份和容量确认后才访问该盘。
- 从该盘复制 CAM0013/0014，两张均为 1152054 字节，源盘和副本 SHA-256 一致，Windows System.Drawing 均成功解码为 800×480，并生成 PNG 预览。CAM0013 SHA-256：`CC6A068AA03AAF3D94F7849F678B174A0A389F2625F1ADB84321A6D1CDCB671C`；CAM0014：`02DE3FB5B9EB68D0F64448AFCC9C86537C1C5CC756B7AFB2B0B55C29CB5CABD2`。
- 写保护测试仅在该已核实盘尝试 CreateNew 唯一文件 `OC_54b98.TMP`，返回 ERROR_WRITE_PROTECT=19，确认文件不存在。没有删除照片、修改现有文件或写入其他磁盘。
- 完整闭环由主会话捕获：`BUTTON_LONG accepted=1`、`STORAGE_SWITCH_START previous=2` → `SD_INIT status=0 sectors=983040 HAL=0` → `STORAGE_LOCAL FR=0 mode=0` → `SNAPSHOT_PASS CAM0015.BMP size=1152054 ms=3289` → 再次长按 `previous=0` → `USB_EXPORT_READY readonly=1`。
- 重新核实磁盘 2/H: 的 USB 身份、序列号、容量及只读状态一致后，确认 CAM0015 可见并复制。源与副本 SHA-256 均为 `9F8D97C2AC52B75BCFFBA13FAC7EB8DD8CEBDE6FBEA5C80161DCF93B33340961`，1152054 字节，Windows 解码 800×480。主会话已读取 CAM0014 预览。

## 尚未验证

本轮尚未覆盖复制中退出、复制中拔线、电脑休眠/唤醒、大量反复切换、介质故障注入，以及保存中长按和 OTA 并发的实机压力测试。历史实现的实测结果不作为本次重新实现的验收结果。

当前板上为最新已确认的 CM7 A，CM4 为 A。后续升级仍须重新核对 confirmed/pending，选择目标核当前非 confirmed 槽。

本任务创建的 CubeMX `-h` GUI 进程 PID 273856 已在核实完整命令行后关闭，未关闭其他进程。临时日志、照片副本和预览在关键证据固化后按本任务清单逐项清理，不涉及其他用户临时文件。验证完成后按用户要求在 `re-snapshot-usb` 分段提交配置与生成层、驱动、中间件、应用、构建集成和验证文档；分支起点为 `936703fcf7c95ba7757facf18ac5017f0da3594c`。

最终修改范围检查发现 45 个已跟踪文件仅为 CubeMX 生成的 CRLF 行尾变化。本轮保持实际生成输出，不以还原文件方式消除这些差异；审阅功能变化应结合 `git diff --ignore-space-at-eol`，避免把生成器行尾变化误当作功能改动。

最终审查：本轮新增或变化的 43 个 HAL/中间件文件与本机官方 `D:/STM32CubeMX/Packs/STM32Cube_FW_H7_V1.13.0` 对应文件逐一比较，统一行尾后全部一致。官方 SDMMC 头文件对 `SDMMC_SWDATATIMEOUT` 使用 `#ifndef`，应用编译宏覆盖有效。两核 GNUmakefile 未残留指向默认 Makefile 的对象/ELF 依赖。

`.gitignore` 未忽略默认 Makefile 或 GNUmakefile；两核默认 Makefile 和新增 GNUmakefile 均纳入版本控制。隔离生成器覆盖依靠 GNU Make 优先读取 GNUmakefile，而不是依靠忽略规则；`.mxproject` 保持既有忽略规则。没有在最终审查阶段改变此策略。

原工作区保留引用为 `stash@{0}`，对象 `22812ff44236bc89a2bd99571f144e264f7a5ee3`（切换分支前的行尾改动）；另一个既有 stash `8d17da0886bc17fc16a9158912e22c54eed75937` 同样未动。当前设备保持 USB 只读导出模式，CM7 A 镜像与源码构建产物一致。
