# STM32H757 双核普通 HTTP 实施指导

本文指导在当前 `BaseProject` 中先实现一个稳定、可验证的普通 HTTP 服务，为后续本地升级闭环和 HTTP OTA 提供网络入口。

本文只讨论 CM7 上的 HTTP 服务。CM4 继续运行现有业务，通过 OpenAMP 与 CM7 通信；不要在 CM4 重复接入 ETH 或 LwIP。

## 1. 目标和完成标准

第一阶段建议只实现一个只读状态接口：

```http
GET /status HTTP/1.1
Host: device
Connection: close
```

成功响应示例：

```http
HTTP/1.1 200 OK
Content-Type: application/json
Content-Length: 150
Connection: close

{"device":"STM32H757","ip":"192.168.1.100","cm7_running_slot":0,"cm4_running_slot":2,"network":"up"}
```

普通 HTTP 阶段完成后，应满足：

1. CM7 能稳定获得 DHCP 地址，或使用已确认的静态地址。
2. 电脑可以重复访问 `/status`，连续请求不崩溃、不泄漏 pbuf 或 netconn。
3. 网线拔插后，链路状态能正确变化并恢复收发。
4. HTTP 工作期间，CM4、OpenAMP、摄像头和 LCD 业务仍能运行。
5. 错误请求、超长请求、半包请求和客户端断开都能释放连接。
6. Windows PowerShell 构建 CM7 A/B 槽均通过，并能检查 map 文件中的内存布局。

以下内容以“先完成 `/status`，再增加图像接口”为顺序。不要在 HTTP 基础未稳定前加入上传、Flash 操作或长连接推流。

## 2. 当前工程基线

当前工程已经具备以下网络基础：

| 组件 | 当前状态 | 主要位置 |
| --- | --- | --- |
| ETH MAC | 已有初始化代码 | `CM7/Core/Src/eth.c` |
| RMII | 已在 CubeMX 配置 | `BaseProject.ioc` |
| LAN8720 | 已有 PHY 读写和链路驱动 | `CM7/BSP/ETH/`、`CM7/LWIP/Target/lan8720.c` |
| LwIP | 已加入 CM7 构建 | `Makefile/CM7/Makefile` |
| DHCP | `MX_LWIP_Init()` 中已启动 | `CM7/LWIP/App/lwip.c` |
| ETH 接收线程 | 已有 | `CM7/LWIP/Target/ethernetif.c` |
| HTTP 业务 | 尚未接入 | 需要新增 |

当前需要先核对两项基础问题：

1. `ethernet_link_thread()` 当前只有周期延时，尚未看到完整 PHY 链路状态读取和 `netif_set_link_up()` / `netif_set_link_down()` 调用。
2. CM7 链接脚本声明了 `RAM_D2`，但需要通过最终 `.map` 文件确认 `.RxDescripSection`、`.TxDescripSection`、`.Rx_PoolSection` 和 LwIP heap 的实际地址，确保它们不落入 CM4 使用的 D2 区域。

普通 HTTP 只放在 CM7。CM4 的链接脚本和 OpenAMP 共享区不要因为增加 HTTP 任务而修改。

## 3. 推荐架构

```text
CM7 main
  ├─ ETH / LAN8720 初始化
  └─ FreeRTOS 默认任务
       ├─ MX_LWIP_Init()
       │    ├─ tcpip_thread
       │    ├─ EthIf 接收线程
       │    └─ ethernet_link_thread
       └─ HttpServerTask
            ├─ netconn_accept
            ├─ 读取 HTTP 请求
            ├─ 解析方法和路径
            ├─ 生成响应
            └─ 关闭连接
```

推荐使用 LwIP netconn API。它适合 FreeRTOS 中的阻塞式应用线程，可以将接收超时、连接关闭和响应发送写成直观的任务代码。不要在 HTTP 任务中直接调用只允许在 tcpip 线程中的 raw API。

建议新增文件：

```text
CM7/APP/NET/app_http_server.c
CM7/APP/NET/app_http_server.h
CM7/APP/NET/app_http_util.c
CM7/APP/NET/app_http_util.h
```

职责边界如下：

| 模块 | 责任 |
| --- | --- |
| `app_http_server` | 监听端口、接收连接、路由分发、连接生命周期 |
| `app_http_util` | 请求头读取、方法/路径解析、body 长度、响应发送 |
| LwIP | TCP、pbuf、netconn、协议栈线程 |
| 摄像头模块 | 提供已完成的 JPEG 或帧缓冲接口 |
| OTA 模块 | 后续负责升级状态机和 Flash，不由 HTTP Server 直接操作 Flash |

## 4. 第一步：确认网络基础

### 4.1 记录实际地址和槽位

当前分区约定为：

| 核 | A 槽 | B 槽 | 槽索引 |
| --- | --- | --- | --- |
| CM7 | `0x08020000` | `0x080A0000` | `0` / `1` |
| CM4 | `0x08120000` | `0x081A0000` | `2` / `3` |

HTTP `/status` 必须区分：

- EEPROM 中记录的 active 槽。
- 当前实际运行的槽。
- pending 槽和试启动状态。
- DHCP 获得的 IP 地址。

不要只返回固定字符串，也不要把 EEPROM 的 active 槽误报为当前运行槽。

### 4.2 核对 ETH DMA 内存

CM7 的 ETH DMA 缓冲区应放在 ETH 可访问的 D2 SRAM，并与 D-Cache 维护规则一致。当前工程已经在 RX 回调中执行 cache invalidate，但最终地址仍应以 map 文件为准。

在链接脚本中明确规划以下输出段，避免依赖 linker orphan section：

```ld
.RxDescripSection (NOLOAD) :
{
  . = ALIGN(32);
  *(.RxDescripSection)
  . = ALIGN(32);
} >RAM_D2

.TxDescripSection (NOLOAD) :
{
  . = ALIGN(32);
  *(.TxDescripSection)
  . = ALIGN(32);
} >RAM_D2

.Rx_PoolSection (NOLOAD) :
{
  . = ALIGN(32);
  *(.Rx_PoolSection)
  . = ALIGN(32);
} >RAM_D2
```

实际段名称和输入 section 以 `ethernetif.c` 中的定义为准。修改链接脚本前先保存当前用户改动，并同时检查 CM7 A/B 两份脚本。

### 4.3 补充链路状态线程

链路线程应周期性完成以下步骤：

1. 读取 LAN8720 链路状态。
2. 链路断开时停止或标记网卡 down。
3. 链路恢复时更新 MAC 速率和双工模式。
4. 调用 `netif_set_link_up()` 或 `netif_set_link_down()`。
5. 只在状态变化时打印日志，避免串口刷屏。

链路状态线程不应在每个周期重新初始化 ETH，也不应直接从 HTTP 任务修改 netif。

## 5. 第二步：实现 HTTP Server

### 5.1 监听任务

HTTP 任务的基本流程如下：

```text
创建 TCP netconn
  → bind 到端口 80
  → listen
  → 等待 accept
  → 为当前连接设置接收超时
  → 读取并解析请求头
  → 路由分发
  → 发送响应
  → 关闭并删除连接
  → 回到 accept
```

建议初始参数：

| 参数 | 建议值 | 说明 |
| --- | ---: | --- |
| 监听端口 | 80 | 普通 HTTP 首版使用 |
| 请求头最大长度 | 2048 字节 | 超过直接返回 400 |
| 接收超时 | 5 秒 | 防止半连接永久占用任务 |
| 响应方式 | `NETCONN_COPY` | 发送后由 netconn 管理复制数据 |
| Keep-Alive | 首版关闭 | 每个请求响应后关闭连接 |
| 最大并发 | 1 | 首版串行处理，便于验证资源使用 |

任务栈大小要通过 map 和实测调整。参考工程的 HTTP 线程使用了较大的栈，但不能直接照搬；首次实现应保留余量，并在任务中记录最小剩余栈空间。

### 5.2 请求读取

请求读取必须支持 TCP 分包：

1. 循环调用 `netconn_recv()`。
2. 将 netbuf 中的数据复制到固定请求缓冲区。
3. 搜索 `\r\n\r\n`，找到请求头结束位置后停止读取头部。
4. 处理当前 netbuf 中已经预读的 body 数据。
5. 如果头部超过上限，返回 `431 Request Header Fields Too Large` 或 `400 Bad Request`，然后关闭连接。

首版只需要解析：

- 方法：`GET`、`HEAD`。
- 路径：去掉 query string 后的路径。
- `Connection`。
- `Content-Length`，为后续 OTA 预留。

不要使用无限制的字符串拼接，也不要把来自网络的数据直接作为格式字符串传给日志函数。

### 5.3 `/status` 响应

建议状态结构至少包含：

```json
{
  "device": "STM32H757",
  "network": "up",
  "ip": "0.0.0.0",
  "cm7_running_slot": 0,
  "cm7_active_slot": 0,
  "cm4_running_slot": 2,
  "cm4_active_slot": 2,
  "ota_state": "idle"
}
```

字段来源要明确：

| 字段 | 来源 |
| --- | --- |
| `ip` | LwIP netif 当前地址 |
| `network` | netif link 状态和地址状态 |
| `*_running_slot` | 启动阶段记录的实际槽位 |
| `*_active_slot` | EEPROM 读取结果 |
| `ota_state` | 预留给后续 OTA 状态机 |

如果当前没有可靠的实际运行槽记录，应在实现前增加启动阶段记录，而不是在 HTTP 层猜测。

### 5.4 状态并发保护

HTTP 任务只读状态，但状态可能由启动、OpenAMP、EEPROM 或网络线程更新。建议：

- 对多字段状态使用互斥锁或一次性快照。
- 不要在持锁期间执行 netconn 发送。
- 不要在 HTTP 任务中直接访问正在被其他任务修改的字符串缓冲区。
- EEPROM 读取应在启动阶段完成，HTTP 只读取内存快照。

## 6. 可选的第二阶段 HTTP 接口

`/status` 稳定后，再按需求增加：

| 接口 | 用途 | 依赖 |
| --- | --- | --- |
| `GET /` | 固定 HTML 页面 | 无 |
| `GET /snap.jpg` | 获取单帧 JPEG | 相机捕获和 JPEG 缓冲区 |
| `GET /stream` | MJPEG 长连接 | 独立推流任务或连接处理 |
| `GET /health` | 简短健康检查 | 双核 IPC 健康状态 |

`/stream` 不应与 OTA 共用一个永不返回的串行处理路径。若实现推流，至少要保证管理请求仍能被处理；可以为推流和控制分别设置任务或监听端口。

## 7. Makefile 和构建接入

将新增 CM7 源文件加入 `Makefile/CM7/Makefile` 或当前工程使用的用户源文件列表，同时确认 include 路径包含 `CM7/APP/NET`。

建议按以下顺序在 **Windows PowerShell** 中构建：

```powershell
make -C Makefile/CM7 APP_SLOT=A
make -C Makefile/CM7 APP_SLOT=B
```

然后检查：

1. A/B 的 ELF、HEX/BIN 和 map 文件均生成。
2. `.text`、`.rodata` 没有超过 384 KiB 槽容量。
3. HTTP 任务栈、LwIP heap、ETH descriptor、RX pool 没有覆盖 CM4 使用区。
4. OpenAMP resource table 仍位于 `0x38000000` 规划区域，不能被普通数据覆盖。
5. 没有新增未解析符号或重复初始化符号。

本仓库规定固件构建使用 Windows 工具链；不要在 WSL 中替代执行固件构建或烧录。

## 8. 板级验证步骤

### 8.1 网络基础验证

1. 上电，记录 CM7、CM4 启动日志。
2. 确认 PHY 被识别，记录 PHY 地址和协商结果。
3. 网线连接到测试交换机，确认 link up。
4. 确认 DHCP 地址或记录静态地址。
5. 从 PC 执行 `ping <设备IP>`。
6. 拔网线，确认 link down。
7. 重新插入，确认 link up 并重新获得通信能力。

### 8.2 HTTP 验证

PowerShell 示例：

```powershell
Invoke-WebRequest -Uri http://<设备IP>/status -Method Get
```

依次验证：

1. 正常 GET 返回 200 和合法 JSON。
2. 未知路径返回 404。
3. 非 GET 方法返回 405。
4. 请求头分成多个 TCP 包时仍能正确解析。
5. 客户端发送半个请求后超时，服务端能释放连接。
6. 连续访问至少数百次，任务栈和 heap 没有持续下降。
7. HTTP 访问期间 OpenAMP 心跳、摄像头和 LCD 业务保持正常。

### 8.3 资源验证

记录以下数据：

- HTTP 任务最小剩余栈。
- FreeRTOS heap 剩余量。
- LwIP heap 使用峰值。
- RX pool 分配失败次数。
- netconn 创建/删除是否成对。
- 连接异常关闭后的 pbuf 释放情况。

## 9. 常见问题

| 现象 | 优先检查 |
| --- | --- |
| DHCP 一直失败 | PHY link、RMII 时钟、DMA buffer 地址、MAC 初始化次数 |
| 能 ping 但 HTTP 无响应 | HTTP 任务是否创建、监听端口是否成功、netconn 错误码 |
| 第一个请求成功，后续失败 | netbuf/pbuf/netconn 是否完整释放 |
| 长请求卡死 | 接收超时和请求头上限 |
| 网线重插后无法恢复 | `ethernet_link_thread()` 是否真正更新 netif 链路状态 |
| CM4 偶发异常 | CM7 D2 ETH 区是否覆盖 CM4 别名 RAM |
| 开启 D-Cache 后数据异常 | RX invalidate、TX clean、DMA buffer 对齐 |
| 加入 HTTP 后启动失败 | 任务栈、LwIP heap、链接脚本和 map 文件 |

## 10. #questions

以下问题需要在真正实现前由项目现场确认：

1. 设备最终使用 DHCP 还是固定 IP？若使用固定 IP，需要确定 IP、掩码、网关和 DNS。
2. 目标板实际使用的 LAN8720 PHY 地址、RMII 参考时钟来源和复位脚是否与当前源码一致？
3. `/snap.jpg` 和 `/stream` 是否属于普通 HTTP 第一阶段，还是留到 HTTP 基础稳定后再做？本文默认后者。
4. 是否允许首版 HTTP 只监听局域网明文 80 端口？若设备会接入不可信网络，应增加鉴权，必要时使用 HTTPS 网关或 TLS 方案。
5. 当前板上是否已经存在统一的日志输出格式和日志等级约定？

## 11. 完成标准

只有同时满足以下条件，才进入本地升级闭环：

- `/status` 连续访问稳定。
- 网线拔插可恢复。
- CM7/CM4 和摄像头业务无回归。
- map 文件确认 ETH/LwIP 内存不越界、不覆盖 CM4。
- 任务栈、heap、pbuf 和 netconn 资源经过异常请求测试。
- Windows PowerShell 已构建 CM7 A/B 槽并检查产物。
