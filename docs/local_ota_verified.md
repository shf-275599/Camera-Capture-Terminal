# 本地 HTTP OTA 使用与验证记录

## 启动前提

- 固定 CM7 Bootloader 位于 `0x08000000`，CM4 Stub 位于 `0x08100000`。
- **本轮已将板上 Option Bytes 的 BCM4 设置为 0 并校验成功**。CM7 写好 SRAM4 handoff 后，通过 `HAL_RCCEx_EnableBootCore(RCC_BOOT_C2)` 启动 CM4。
- BCM4=1 时，当前 Stub 可能在本轮 handoff 写入前默认进入 A，导致 CM4 B 升级无法切换。
- 两核槽号均为核内 A=0、B=1。每槽 384 KiB。
- CM7 A/B 地址为 `0x08020000` / `0x080A0000`；CM4 为 `0x08120000` / `0x081A0000`。

## 构建和上传

在工程根目录的 Windows PowerShell 中执行：

```powershell
.\make_all.ps1 -Jobs 8
```

首次部署需要通过 ST-Link 写入包含 HTTP OTA 的应用。之后根据目标核实际运行槽选择另一槽的 BIN：

```powershell
# CM7 当前运行 A 时上传 B
.\ota_upload.ps1 -Device 192.168.10.2 -Core cm7 `
    -Firmware Makefile/CM7/build_B/BaseProject_CM7_B.bin -Version 101

# CM7 当前运行 B 时上传 A
.\ota_upload.ps1 -Device 192.168.10.2 -Core cm7 `
    -Firmware Makefile/CM7/build/BaseProject_CM7.bin -Version 102

# CM4 当前运行 A 时上传 B
.\ota_upload.ps1 -Device 192.168.10.2 -Core cm4 `
    -Firmware Makefile/CM4/build_B/BaseProject_CM4_B.bin -Version 201
```

IP 和版本参数按实际设备修改。脚本先查询运行槽，再检查 BIN 复位入口、计算 CRC32，并顺序上传最多 8192 字节一块的数据。镜像必须链接到目标槽，不能仅更改烧录地址。

## 协议

| 方法和路径 | 必需请求头 | body |
| --- | --- | --- |
| GET `/ota/status` | `X-Ota-Core: cm7` 或 `cm4` | 空 |
| POST `/ota/begin` | `X-Ota-Core`、`X-Fw-Size`、`X-Fw-Version`、`X-Fw-Crc32`、`X-Fw-Base` | 空 |
| POST `/ota/chunk` | `X-Offset` | 1～8192 字节原始数据 |
| POST `/ota/end` | 无 | 空 |
| POST `/ota/abort` | 无 | 空 |

POST 必须有 Content-Length；不支持 Transfer-Encoding 和 Expect: 100-continue。整数头允许十进制或 `0x` 前缀十六进制。状态返回纯文本键值对；`active` 表示 EEPROM 的 confirmed 槽，是否存在 pending 要结合 flags 判断。

同时只允许一个会话；offset 必须连续。错误 offset 保留会话；接收失败和 Flash/CRC 错误清理会话。60 秒没有有效写入或会话超过 10 分钟时取消，恢复相机并释放 CM4。首版失败后重新 begin，不支持掉电续传。

成功 end 后 pending 已保存，不能通过 abort 撤销，设备返回响应后复位。响应丢失时先查询启动状态，不能把网络异常直接当作升级失败。

KEY0 切槽与 OTA 使用同一控制锁。任何一核存在 pending 时拒绝新升级。CM4 实际运行槽与 pending 匹配后，由 HTTP 管理任务执行最小自动提交；此判据仅证明 CM4 任务已运行并上报槽号，不代表完整业务自检。

## 本轮实测

- Windows 下两核 A/B 构建通过；链接器仍报告 RAM 可执行段对应的 RWX LOAD 警告。
- HTTP 解析测试通过：所有两段分包位置、逐字节、二进制数据、重复长度、溢出长度和超限请求。
- PowerShell 上传脚本通过本机 HTTP 模拟端的完整 BIN 字节比对；CRC32 标准向量通过。
- 实板 CM7 A→B、B→A 上传、Flash CRC、pending 和复位启动通过。
- 修复 `cm4_boot_get_running_slot()` 错用 CM7 地址解析函数后，CM4 运行槽上报正常。
- BCM4=0 后，实板 CM4 A→B、B→A 上传、hold、CRC、启动和自动提交通过。
- 异常请求实测通过：错误核、当前槽地址、超容量、并发 begin、错误 offset、超大块、错误 CRC。错误 CRC 未设置 pending。
- CM4 空闲超时后，两核 busy=0、pending 标志清零，REQ/ACK 均读回 0；当前运行镜像保留。
- 尚未实施物理断电测试，以及故意构造不健康候选镜像的完整回滚测试。

超时或错误升级可能已擦除空闲槽，其旧版 size/version 元数据不代表该槽仍有有效镜像。下次升级重新擦写并完整校验。

## 可重复测试

```powershell
# 本机模拟上传和 CRC 标准向量（需要先构建 CM7 B BIN）
.\tests\http\uploader_test.ps1

# 实板异常测试：要求 CM7 已确认运行 A；会擦写空闲 CM7 B
.\tests\http\device_test.ps1 -Device 192.168.10.2
```

解析单元测试使用 Windows 本机 C 编译器编译 `tests/http/parser_test.c` 和 `CM7/APP/NET/HTTP/app_http_util.c`，include 路径为 `tests/http`、`CM7/APP/NET/HTTP`。测试替身不参与固件构建。
