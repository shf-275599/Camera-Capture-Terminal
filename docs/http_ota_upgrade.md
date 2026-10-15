# STM32H757 双核 HTTP OTA 升级教程

本文说明如何在当前 `BaseProject` 工程中实现 HTTP OTA 升级。方案参考同目录之外的 `RGB_OV5640_RTOS` 双核心摄像头工程，但会按照当前工程的 Flash 分区、CM7/CM4 槽位编号和启动代码重新适配。

本文讨论的是 **PC 或服务器通过 HTTP 向设备上传固件**。如果需要设备根据 URL 主动下载固件，只需将 HTTP Server 的上传 body 换成 HTTP Client 的响应 body，后面的 Flash 写入、镜像校验、pending、试启动和回滚流程仍然适用。

## 1. 先确认升级目标

当前工程是 STM32H757 双核固件，CM7 和 CM4 分别使用独立的 Flash Bank，并且每个核有 A/B 两个应用槽。

OTA 的目标应当是：

1. 设备通过 HTTP 接收新的 CM7 或 CM4 镜像。
2. 新镜像写入当前运行槽之外的空闲槽。
3. 写入结束后校验 Flash 中的完整镜像。
4. 在 EEPROM 中记录待启动槽位。
5. 复位后由固定引导程序选择 pending 槽。
6. 新应用完成健康检查后提交为 verified。
7. 新应用启动失败时自动回到旧槽。

只有完成第 4～7 步，才是具有断电恢复能力的 OTA。单独增加 HTTP 上传接口并不能完成可靠升级。

## 2. 当前工程的 Flash 布局

分区定义位于 [Common/Inc/boot_slot.h](../Common/Inc/boot_slot.h)。STM32H757 的每个 Bank 为 1 MiB，每个 sector 为 128 KiB。

| 核 | 固定引导区 | A 槽 | 预留区 | B 槽 |
| --- | --- | --- | --- | --- |
| CM7 / Bank1 | `0x08000000`，128 KiB | `0x08020000`，384 KiB | `0x08080000`，128 KiB | `0x080A0000`，384 KiB |
| CM4 / Bank2 | `0x08100000`，128 KiB | `0x08120000`，384 KiB | `0x08180000`，128 KiB | `0x081A0000`，384 KiB |

对应的槽索引如下：

```c
/* CM7 */
#define CM7_APP_SLOT_A_INDEX (0U)
#define CM7_APP_SLOT_B_INDEX (1U)

/* CM4 */
#define CM4_APP_SLOT_A_INDEX (2U)
#define CM4_APP_SLOT_B_INDEX (3U)
```

CM4 槽索引是 `2/3`，而参考工程使用的是 `0/1`。移植参考代码时不能直接复制槽位数组下标和范围判断。

每个镜像必须链接到它实际运行的槽地址。CM7 A 槽生成的镜像不能直接写入 CM7 B 槽后运行；CM4 也一样。发布系统应为每个目标核、目标槽生成对应的固件文件，并在 HTTP 请求中携带目标地址或目标槽信息。

## 3. 升级系统的组成

建议新增或移植以下模块：

```text
CM7/APP/net/app_http_server.c   HTTP 监听、请求解析和路由分发
CM7/APP/ota/app_http_ota.c      OTA HTTP 接口和分块接收
CM7/APP/ota/app_ota.c           OTA 状态机、槽位选择和镜像校验
Common/Src/bsp_flash_ram.c      RAM 中执行的 Flash 擦除和编程
Common/Src/cm4_ota_hold.c       CM4 暂停和恢复协调
```

CM7 负责以下工作：

- 启动 HTTP Server。
- 解析 OTA 请求。
- 选择目标核和空闲槽。
- 接收固件数据并写入 Flash。
- 计算并核对镜像摘要。
- 更新 EEPROM 的 pending 信息。
- 在需要时请求 CM4 暂停。

CM4 需要增加一个轻量的 OTA 协调任务，通过 SRAM4 或 OpenAMP 接收 CM7 的暂停请求，并在确认安全停核后进入等待状态。CM4 不能在 CM7 擦写 CM4 所在的 Bank2 时继续从该 Bank 取指或访问相关资源。

## 4. 必须先确认固定引导程序

当前工程有 `boot_eeprom`、`boot_slot` 和 `cm4_boot_handoff` 等辅助代码，但在工程源码和烧录脚本中没有形成完整的固定 Bootloader/CM4 Stub 构建及烧录链路。实施 OTA 前必须确认：

- 板上 `0x08000000` 是否已经烧录独立 CM7 Bootloader。
- 板上 `0x08100000` 是否已经烧录 CM4 Stub。
- Bootloader 和当前应用是否使用相同的 EEPROM 结构版本。
- CM4 Stub 是否理解当前的 CM4 槽索引 `2/3`。
- 两个固定引导区是否被写保护。
- 复位后的 CM4 启动地址是否由 Stub 和 SRAM4 handoff 共同决定。

当前 [flash_all.ps1](../flash_all.ps1) 的烧录流程面向四个应用槽，不能据此证明 Bootloader 和 Stub 已经被构建并烧录。

固定引导程序至少需要完成以下流程：

```text
复位
  → 读取 EEPROM 双备份
  → 校验 EEPROM CRC
  → 选择 pending 或 verified 槽
  → 校验目标槽向量表和完整镜像
  → 持久化试启动状态
  → 跳转到目标应用
  → 应用健康确认后提交 verified
  → 启动失败或试启动耗尽时回退旧槽
```

如果固定引导程序不存在，应先实现并单独验证它，再接入 HTTP OTA。

## 5. 设计 HTTP OTA 协议

参考工程使用多个短请求完成一次升级。建议保留这种方式，避免一个长 HTTP 请求占用连接过久。

### 5.1 查询状态

```http
GET /ota/status HTTP/1.1
Host: device
```

响应至少包含：

```json
{
  "cm7_verified_slot": 0,
  "cm7_running_slot": 0,
  "cm4_verified_slot": 2,
  "cm4_running_slot": 2,
  "ota_state": "idle"
}
```

设备必须返回实际运行槽，不能只返回 EEPROM 中的 verified 槽。实际运行槽可由启动代码记录，也可由 CM4 通过 IPC 报告。

### 5.2 开始升级

```http
POST /ota/begin HTTP/1.1
X-Ota-Core: cm7
X-Fw-Version: 12
X-Fw-Size: 278528
X-Fw-Crc32: 0x12345678
X-Fw-Sha256: <hex digest>
Content-Length: 0
```

`begin` 阶段应完成：

1. 检查目标核是否合法。
2. 查询实际运行槽。
3. 选择另一个槽作为目标槽。
4. 检查镜像大小不超过目标槽容量。
5. 检查镜像版本和硬件型号。
6. 升级 CM4 时请求 CM4 暂停。
7. 擦除目标槽。
8. 初始化当前会话的 offset 和摘要状态。

如果目标槽、版本、大小或 CM4 暂停状态不合法，必须拒绝 `begin`，不能擦写 Flash。

### 5.3 发送数据块

```http
POST /ota/chunk HTTP/1.1
X-Offset: 65536
Content-Length: 8192

<8192 bytes firmware data>
```

每个 chunk 建议不超过 8 KiB。服务端必须检查：

- 当前 OTA 会话已经通过 `begin`。
- `X-Offset` 等于服务端期望的 offset。
- `Content-Length` 不超过单块上限。
- `offset + length` 不超过镜像总长度。
- 接收完整数据后才调用 Flash 写入函数。
- 写入成功后再推进 offset 和摘要状态。

写入失败时不能推进 offset，也不能写 EEPROM pending。客户端应通过 `/ota/status` 查询失败状态并重新开始会话。

### 5.4 结束升级

```http
POST /ota/end HTTP/1.1
Content-Length: 0
```

`end` 阶段必须执行：

1. 检查接收总长度等于声明长度。
2. 计算接收数据或 Flash 内容的摘要。
3. 将 Flash 内容重新读回并计算摘要。
4. 检查初始栈指针和 Reset_Handler 是否位于目标槽范围内。
5. 检查预期 CRC/SHA-256 与实际结果相同。
6. 读回 EEPROM，写入 pending，并再次读回确认。
7. 只有以上步骤全部成功，才返回 HTTP 成功。

不要只检查向量表。向量表合法只能说明入口地址看起来有效，不能证明整份镜像写入完整。

### 5.5 取消升级

```http
POST /ota/abort HTTP/1.1
Content-Length: 0
```

`abort` 必须清理：

- OTA 会话状态。
- CM4 hold 请求。
- 网络和 HTTP 线程使用的缓冲区。
- 未完成的 pending 标记。

所有接收超时、客户端断开和 Flash 错误都应最终进入与 `abort` 相同的清理路径。建议增加会话总超时，避免 CM4 一直停在 hold 状态。

## 6. Flash 擦除和编程

### 6.1 目标槽限制

OTA 只能擦写对应核的空闲槽：

```text
CM7 运行 A → 只允许写 CM7 B
CM7 运行 B → 只允许写 CM7 A
CM4 运行 A → 只允许写 CM4 B
CM4 运行 B → 只允许写 CM4 A
```

固定 Bootloader、CM4 Stub、S4 预留区和当前运行槽永远不能由 OTA 擦除。

### 6.2 同 Bank 限制

CM7 A/B 都在 Bank1，CM4 A/B 都在 Bank2。因此 A/B 槽不是两个完全独立的执行区域：

- CM7 写空闲槽时，CM7 当前代码仍然在同一个 Bank。
- CM4 写空闲槽时，CM4 当前代码仍然在同一个 Bank。
- Flash 擦写期间，相关 Bank 的取指和访问可能产生停顿。

仅给 Flash 函数增加 `__attribute__((section(".RamFunc")))` 不够。还必须确认：

- `.RamFunc` 在链接脚本中放入 AXI SRAM 或 DTCM。
- 启动代码把 `.RamFunc` 从 Flash 复制到 RAM。
- Flash 操作调用的常量和辅助函数也位于可执行 RAM 或安全区域。
- 中断向量、必要 ISR 和时间基准不会访问正在擦写的 Bank。
- Flash 操作超时逻辑不依赖已经停顿的 SysTick 或外部代码。

当前链接脚本没有参考工程中完整的 `.RamFunc` 放置和复制方案，应单独补齐并验证。

### 6.3 首次实现建议

首次实现可以允许网络在短暂 Flash 操作窗口内暂停：

```text
接收完整 chunk
  → 暂停相关业务和 DMA
  → 进入 RAM Flash 操作函数
  → 擦除或编程
  → 清理 D-Cache，必要时失效 I-Cache
  → 恢复业务和网络
  → 返回 HTTP 响应
```

HTTP 客户端需要设置足够长的超时。若要求擦写期间网络完全不中断，则必须把网络处理所需的执行链和中断链放到安全执行区域，并通过板级测试确认。

## 7. CM4 暂停协议

升级 CM4 镜像前，CM4 不能继续运行。推荐在 SRAM4 保留明确的控制结构，例如：

```c
    uint32_t request;
    uint32_t acknowledge;
    uint32_t generation;
} cm4_ota_mailbox_t;
```

协议流程如下：

```text
CM7 写入 REQUEST_HOLD
  → CM7 通知 CM4
  → CM4 停止业务、DMA 和外设访问
  → CM4 写入 ACK_HELD
  → CM4 进入等待状态
  → CM7 校验 ACK_HELD
  → CM7 擦写 CM4 空闲槽
  → CM7 清除请求并唤醒 CM4
```

协议必须具备超时和取消路径：

- CM4 没有在规定时间内确认，CM7 不得擦写。
- CM7 超时或客户端断开时必须清除 request。
- CM4 看到取消请求后恢复业务。
- ACK 必须表示已经停止所有可能访问目标 Bank 的代码、DMA 和外设。

固定延时不能替代状态确认。参考工程中“收到 ACK 后延时一段时间”的做法只能作为辅助措施。

## 8. EEPROM pending、试启动和回滚

当前 [CM7/APP/BOOT/boot_eeprom.h](../CM7/APP/BOOT/boot_eeprom.h) 已定义 CM7 和 CM4 的 verified、pending、试启动次数、版本、长度和 CRC 字段。

建议将接口改成能返回错误状态的形式。尤其是以下操作不能忽略返回值：

- `boot_eeprom_set_pending`。
- `boot_eeprom_commit_pending`。
- `boot_eeprom_set_cm4_pending`。
- `boot_eeprom_commit_cm4_pending`。

推荐状态转换：

```text
verified = A, pending = none
  → 镜像写入 B 并校验
  → pending = B, trial = 3
  → 复位启动 B
  → 应用健康确认
  → verified = B, pending = none
```

失败时：

```text
B 启动失败
  → 看门狗复位
  → trial 减一
  → trial 耗尽
  → 清除 pending
  → 回到 verified = A
```

试启动计数必须在跳转新应用前持久化。应用不能一进入 `main` 就立即提交 pending；应等到必要的时钟、内存、通信、摄像头和业务初始化全部成功，并持续运行一段时间后再确认。

CM4 还应向 CM7 上报：

- 实际运行槽。
- 镜像版本或镜像 ID。
- 初始化完成状态。
- 业务健康状态。

如果 CM7 和 CM4 必须成套升级，则应把两个候选槽作为一组事务管理。分别设置两个 pending 后，可能出现 CM7 已切换而 CM4 仍为旧版本的兼容性问题。

## 9. 镜像安全校验

最低限度应校验：

| 校验项 | 目的 |
| --- | --- |
| 目标核 | 防止 CM7 镜像写入 CM4 槽 |
| 目标槽地址 | 防止 A 槽镜像错误写入 B 槽 |
| 镜像大小 | 防止越界擦写 |
| 初始栈指针 | 确认向量表位于目标 SRAM 范围 |
| Reset_Handler | 确认复位入口位于目标槽范围 |
| CRC32 或 SHA-256 | 确认下载内容和 Flash 内容一致 |
| 版本号 | 防止无意降级 |
| 板型和芯片 ID | 防止错误固件写入其他设备 |

如果设备可能接入不可信网络，还需要增加鉴权和数字签名验证。CRC 只能检测传输或存储损坏，不能证明固件来自可信发布者。

## 10. 构建和发布镜像

当前工程已经支持通过 `APP_SLOT` 选择槽位链接脚本。构建时必须让发布系统明确记录：

```text
目标核：CM7 或 CM4
目标槽：A 或 B
链接基址：对应槽基址
镜像长度
版本号
CRC32 或 SHA-256
构建时间和发布批次
```

按照仓库说明，Windows PowerShell 中的构建命令为：

```powershell
make -C Makefile/CM7 APP_SLOT=A
make -C Makefile/CM7 APP_SLOT=B
make -C Makefile/CM4 APP_SLOT=A
make -C Makefile/CM4 APP_SLOT=B
```

双槽构建可以使用：

```powershell
make -C Makefile/CM7 all-slots
make -C Makefile/CM4 all-slots
```

当前仓库规定固件构建应在 Windows 环境执行。WSL 只用于源码查看和编辑，不应使用 WSL 工具链替代 Windows 构建环境。

## 11. 验证步骤

### 11.1 不接入 HTTP 的启动验证

先手工准备 EEPROM pending，验证固定引导程序：

1. A 槽运行正常。
2. 将合法 B 槽镜像写入 B 槽。
3. 设置 B 为 pending。
4. 复位并确认启动 B。
5. 应用健康确认后确认 verified 已变为 B。
6. 写入一个故意启动失败的 B 槽镜像。
7. 复位多次，确认最终回到 A 槽。
8. 在擦写和 EEPROM 写入的不同阶段断电，确认 verified 槽仍可启动。

### 11.2 HTTP 协议验证

依次验证：

1. 正常 `status → begin → chunk → end`。
2. offset 不连续。
3. chunk 长度超过上限。
4. 镜像长度超过槽容量。
5. 客户端中途断开。
6. 接收超时。
7. Flash 写入失败。
8. CRC 或 SHA-256 不匹配。
9. EEPROM 保存失败。
10. `abort` 后 CM4 是否恢复运行。

### 11.3 双核验证

分别验证：

- 仅升级 CM7，CM4 继续使用旧版本并正常运行。
- 仅升级 CM4，CM7 能可靠暂停 CM4 并恢复。
- 同时升级 CM7 和 CM4 时，版本兼容性检查是否生效。
- CM4 在 hold 请求超时和取消后不会永久停机。
- CM7 和 CM4 的共享 SRAM 区域没有与 OpenAMP resource table 冲突。

## 12. 推荐实施顺序

按下面顺序实现可以降低排查难度：

1. 核实板上的 CM7 Bootloader、CM4 Stub、Option Bytes 和 EEPROM 格式。
2. 完善 A/B 槽启动、试启动、健康确认和回滚。
3. 实现并验证 RAM Flash 擦除、编程、Cache 维护和读回校验。
4. 实现 CM4 hold/ack/cancel 协议。
5. 增加 `/ota/status`、`/ota/begin`、`/ota/chunk`、`/ota/end` 和 `/ota/abort`。
6. 加入版本、硬件型号、目标核、目标槽和摘要校验。
7. 在 Windows 环境构建四种核/槽镜像。
8. 进行断网、断电、错误镜像、启动失败和双核组合测试。

最重要的验收标准是：**新镜像写入失败、设备在升级过程中断电或新应用启动失败时，旧 verified 槽仍然可以启动。**
