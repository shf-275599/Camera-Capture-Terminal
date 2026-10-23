# 仓库操作说明

## 构建与验证

- 这是 STM32H757XIH6 双核固件。在 Windows PowerShell 中使用 GNU Make 和 `arm-none-eabi-*` 工具链（可通过 `make ... GCC_PATH=工具链bin目录` 指定）；从 WSL 操作时通过 `powershell.exe` 执行构建。`make -C Makefile` 先构建 CM4、再构建 CM7，默认仅构建 A 槽。
- 聚焦单核/单槽：`make -C Makefile/CM7 APP_SLOT=B` 或 `make -C Makefile/CM4 APP_SLOT=A`；双槽分别运行 `make -C Makefile/CM7 all-slots`、`make -C Makefile/CM4 all-slots`。各核 A 槽产物在 `Makefile/CMx/build/`，B 槽在 `Makefile/CMx/build_B/`（`CMx` 为 `CM4` 或 `CM7`）。
- 源码变更构建受影响的核；修改 `Common/`、启动或 OpenAMP 时构建两个核，修改槽位或链接布局时验证 A/B。`all-slots` 的配方含 Windows `@echo.`，请在 Windows 环境运行构建。
- `./clean.ps1` 清理两个核的双槽产物；顶层 `make -C Makefile clean` 不递归清理两核产物。
- 源文件和头文件目录需显式加入各核 Makefile 的 `C_SOURCES` / `C_INCLUDES`。目标文件按源文件 basename 扁平化；CM7 为 LwIP 与 libmetal 的 `init.c` / `sys.c` 保留了专用规则，添加同名源文件时检查目标冲突。
- `./make_all.ps1 [-Jobs N]` 构建两个核的双槽；`./make_flash_slotA.ps1 [-Jobs N]` 构建并按 CM7-A、CM4-A 顺序烧录 A 槽；`./make_flash_all.ps1 [-Jobs N]` 构建两个核的双槽，再按 CM7-A、CM4-A、CM7-B、CM4-B 顺序烧录，最后复位。需要 ST-Link 和 STM32CubeProgrammer；CLI 未在 PATH 时可设置 `STM32_PROGRAMMER_CLI`。不要把烧录脚本当作仅构建命令。
- 烧录脚本仅写应用槽，不构建或烧录 `0x08000000` 的 CM7 Bootloader、`0x08100000` 的 CM4 Stub；启动/OTA 验证需另行确认板上的固定引导程序。

## HTTP / OTA 验证

- 本工程的正式 OTA 上位机为 `../qt-ota/`，使用前读取其 `AGENTS.md`。本机模拟测试在 Windows PowerShell 中运行：`../qt-ota/test-upload.ps1` 验证正常上传，`../qt-ota/test-recovery.ps1` 验证 begin/chunk 回包失败、网络中断、未提交块、客户端重启恢复、固件身份不符、会话失效和 end 结果不明等 8 个场景；须先生成 CM7 B BIN 并构建 Qt EXE。模拟测试仅验证客户端与模拟服务交互，不验证真实设备擦写、复位、启动或硬件断线续传。
- 原 PowerShell 上传器 `ota_upload.ps1` 已删除；`tests/http/uploader_test.ps1` 和 `tests/http/resume_test.ps1` 仍依赖该脚本，当前不能直接运行，不得作为可用测试入口。
- `./tests/http/device_test.ps1 -Device <IP>` 要求 CM7 已确认运行 A 且无 pending，会擦写空闲 B 槽，不是只读检查。
- `tests/http/response_timeout_test.ps1` 的回包失败用例依赖已撤除的临时故障注入，不能直接用于正常固件；`-TotalOnly` 路径不依赖注入，但需设备和串口，实等约 10 分钟并写入 CM4 空闲槽。
- `tests/http/parser_test.c` 没有构建入口；其头文件依赖 `lwip/api.h`，旧文档提及的 `tests/http` 本地替身当前不存在，不要将它视为可直接独立编译的测试。
- 正式上传使用 `../qt-ota/` 上位机；目标为该核实际运行槽的另一槽，BIN 必须链接到目标地址。设备端支持 RAM 会话内续传：相同目标核、长度、版本、CRC32、链接地址重复 begin 返回 `OTA resume written=N`，不重擦、不重置 CRC，也不延长会话时限；written 包含 RAM 缓冲，chunk 偏移必须精确匹配。空闲上限 60 秒、总时长上限 10 分钟，掉电/复位不保留进度。
- Qt 客户端已实现上传会话内自动重连续传和本地任务恢复：网络错误、HTTP 408/5xx 后按 1/2/4/5 秒退避，查询状态并重发匹配的 begin，以设备偏移继续；连续恢复失败 60 秒后暂停，时限在请求结束时检查，单请求超时 45 秒。重开程序提示恢复并校验原 BIN 身份；设备会话失效须确认擦除目标槽并从头上传，本地任务不代表设备进度已持久化。初始状态查询失败不自动恢复；end 结果不明时只查询目标运行槽、版本和长度，不自动重发 end 或 abort，仍需核对启动日志和业务。历史协议与验证记录见 `docs/ota_resume_verified_20261021.md`，其中 PowerShell 上传器命令已不适用。

## 启动和修改边界

- `CM7/Core/Src/main.c` 配置 MPU/缓存并清空 SRAM4，然后通过 HSEM 唤醒 CM4；`CM4/Core/Src/main.c` 在 D2 STOP 中等待。双核通过 `CM7/OPENAMP`、`CM4/OPENAMP` 与 `Common/` 中的共享代码协作；修改同步、IPC、启动或内存布局时同时检查两核。
- CM7 初始化外设后通过 `CM7/APP/BOOT/boot_eeprom.c` 读取 EEPROM 启动记录、选择 CM4 应用槽，再调用 `Common/Src/cm4_boot_handoff.c` 将 CM4 应用地址写入 SRAM4，并使能 CM4 启动核；EEPROM 不可用时默认 CM4 A 槽。修改槽位选择、EEPROM 启动记录或 handoff 时，同时核对 `Common/Inc/boot_slot.h`、CM4 Stub 的约定和双核链接脚本。
- 四份 Flash 链接脚本将 `.resource_table`（`NOLOAD`）放在 SRAM4 前 1 KiB 的 `0x38000000`，59 KiB OpenAMP 共享段从 `0x38000400` 开始，末尾 `0x3800F000..0x3800FFFF` 留给 handoff、OTA hold 和运行槽报告。CM7 MPU 将整个 64 KiB 配置为共享、不可缓存；改布局时同时核对两核 OpenAMP、四份链接脚本、MPU 与 `Common/Inc/cm4_boot_handoff.h`、`cm4_ota_hold.h`。
- CM4 从 Bank2 执行，擦写其空闲槽前必须经 `cm4_ota_hold_*` 握手，让 CM4 在 RAM 中关中断等待；保留 `.RamFunc` 复制和 abort 释放流程，不能只保护目标 Flash 地址。
- CM7 A/B 槽链接地址为 `0x08020000` / `0x080A0000`，CM4 为 `0x08120000` / `0x081A0000`，每槽 384 KiB。`Common/Inc/boot_slot.h` 的两核槽索引各自为 `0/1`，可直接用作各核元数据数组下标；CM7 使用 `boot_slot_*` 接口，CM4 使用 `cm4_boot_slot_*` 接口。A 槽镜像不能直接当作 B 槽镜像运行。
- CM7 的 `Core/Src/freertos.c` 创建 IPC、摄像头任务；默认任务先执行 `MX_LWIP_Init()` 再启动 HTTP，并周期喂 IWDG。`main()` 负责 ETH/PHY 外设初始化；ETH/LwIP、HTTP 与 OTA 服务仅在 CM7，CM4 通过 IPC/共享 mailbox 配合。
- `BaseProject.ioc` 是 CubeMX 配置源；`Core/` 中的外设代码为生成代码，修改时保留 `USER CODE` 区域。应用逻辑主要在 `CM7/APP/`、`CM4/APP/` 和 `CM7/BSP/`，不要无故改动 `Drivers/` 或 `Middlewares/`。
- `.vscode/launch.json` 使用本机 ST-Link/STM32CubeProgrammer 路径，仅指向 A 槽 ELF；CM7 为 launch，CM4 为 attach。
- `docs/` 中 HTTP、OTA 与本地升级文档包含实施方案和旧基线（例如 HTTP“尚未接入”）；判断已实现功能以当前源码和 Makefile 为准。
