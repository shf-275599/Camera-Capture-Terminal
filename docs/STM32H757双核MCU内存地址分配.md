# STM32H757 双核 MCU 内存地址分配

本文说明 STM32H757XIH6 的主要内存地址空间、CM7/CM4 双核使用方式、共享内存和当前 Bootloader 工程的地址规划。

> 文档中的“芯片地址空间”是 MCU 的硬件地址映射；“当前工程分区”是本仓库链接脚本和 `boot_config.h` 中的项目规划。两者需要分别理解。

## 1. 地址空间总览

STM32H757 集成 Cortex-M7 和 Cortex-M4 两个内核，并通过不同总线域连接 Flash、片上 SRAM 和外设。常用地址空间如下：

| 存储区域 | 起始地址 | 典型容量 | 主要用途 |
|---|---:|---:|---|
| ITCM RAM | `0x00000000` | 64 KB | CM7 的低延迟指令存储 |
| DTCM RAM | `0x20000000` | 128 KB | CM7 的低延迟数据、栈和变量 |
| Flash Bank 1 | `0x08000000` | 1 MB | CM7 程序和数据 |
| Flash Bank 2 | `0x08100000` | 1 MB | CM4 程序和数据，或其他 Flash 分区 |
| CM4/D2 SRAM 访问区 | `0x10000000` | 288 KB | CM4 程序、数据和栈的常用地址别名 |
| AXI SRAM / D1 SRAM | `0x24000000` | 512 KB | CM7 的大容量高速 RAM |
| D2 SRAM1 | `0x30000000` | 128 KB | 双核可访问的数据 SRAM |
| D2 SRAM2 | `0x30020000` | 128 KB | 双核可访问的数据 SRAM |
| D2 SRAM3 | `0x30040000` | 32 KB | D2 域共享数据或通信缓冲区 |
| D3 SRAM4 | `0x38000000` | 64 KB | 低功耗域 RAM、双核共享内存 |
| Backup SRAM | `0x38800000` | 4 KB | 备份域掉电保持数据 |

不同内核对这些区域的访问性能和权限可能不同。实际使用时，应以具体芯片参考手册的总线矩阵、内存保护配置和当前工程链接脚本为准。

## 2. Flash 地址分配

### 2.1 芯片 Flash 范围

STM32H757XIH6 的内部 Flash 总容量为 2 MB，通常按两个 1 MB Bank 观察：

```text
Flash Bank 1: 0x08000000 ~ 0x080FFFFF
Flash Bank 2: 0x08100000 ~ 0x081FFFFF
```

Flash 的擦除粒度、写入对齐和 Bank/sector 规则必须按照具体型号参考手册执行。应用分区的起始地址应满足擦除粒度和升级策略的要求。

### 2.2 当前工程的链接地址

当前 Makefile 默认使用以下链接脚本：

```make
# Makefile/CM7/Makefile
LDSCRIPT = stm32h757xx_flash_CM7.ld

# Makefile/CM4/Makefile
LDSCRIPT = stm32h757xx_flash_CM4.ld
```

CM7 Bootloader 链接脚本：

```ld
FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
```

因此 CM7 Bootloader 的链接范围是：

```text
0x08000000 ~ 0x0801FFFF
```

CM4 链接脚本：

```ld
FLASH (rx) : ORIGIN = 0x08100000, LENGTH = 1024K
```

因此 CM4 固件的链接范围是：

```text
0x08100000 ~ 0x081FFFFF
```

## 3. CM7 内存分配

### 3.1 CM7 Bootloader

当前 CM7 Flash 链接脚本定义：

```ld
FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
RAM   (xrw): ORIGIN = 0x20000000, LENGTH = 128K
```

对应关系如下：

| 区域 | 起始地址 | 结束边界 | 大小 |
|---|---:|---:|---:|
| CM7 Bootloader Flash | `0x08000000` | `0x08020000` | 128 KB |
| CM7 Bootloader RAM | `0x20000000` | `0x20020000` | 128 KB |

链接脚本中的：

```ld
_estack = 0x20020000;
```

表示初始栈顶位于 DTCM RAM 的结束边界。

### 3.2 CM7 AXI SRAM 运行布局

仓库中还存在 `Makefile/CM7/stm32h757xx_sram1_CM7.ld`，其运行地址为：

```ld
RAM_EXEC (rx) : ORIGIN = 0x24000000, LENGTH = 256K
RAM      (xrw): ORIGIN = 0x24040000, LENGTH = 256K
_estack = 0x24080000;
```

该脚本适合将代码和只读数据放在 AXI SRAM 中运行：

| 区域 | 起始地址 | 结束边界 | 大小 |
|---|---:|---:|---:|
| CM7 AXI SRAM 执行区 | `0x24000000` | `0x24040000` | 256 KB |
| CM7 AXI SRAM 数据区 | `0x24040000` | `0x24080000` | 256 KB |

如果 APP1 使用该 SRAM 链接脚本，其向量表第 0 项可能是：

```text
0x24080000
```

这就是 APP1 的初始 MSP 栈顶，不是 Flash 地址。

## 4. CM4 内存分配

当前 CM4 Flash 链接脚本定义：

```ld
FLASH (rx) : ORIGIN = 0x08100000, LENGTH = 1024K
RAM   (xrw): ORIGIN = 0x10000000, LENGTH = 288K
_estack = 0x10048000;
```

对应关系如下：

| 区域 | 起始地址 | 结束边界 | 大小 |
|---|---:|---:|---:|
| CM4 Flash | `0x08100000` | `0x08200000` | 1024 KB |
| CM4 RAM | `0x10000000` | `0x10048000` | 288 KB |

仓库中的 `stm32h757xx_sram2_CM4.ld` 也使用 `0x10000000` 作为 `RAM_EXEC` 起始地址：

```ld
RAM_EXEC (rx) : ORIGIN = 0x10000000, LENGTH = 128K
RAM      (xrw): ORIGIN = 0x10020000, LENGTH = 160K
_estack = 0x10048000;
```

因此 CM4 的代码区和数据区在地址上连续，总容量仍为 288 KB。

`0x10000000` 是 CM4 工程常用的 D2 SRAM 访问地址。与 CM7 侧使用的 `0x30000000` 等 D2 SRAM 地址之间存在内核/总线访问别名关系，不能仅根据两个地址数值判断为两块独立物理 RAM。

## 5. 双核共享内存

当前两个 Flash 链接脚本都定义了 OpenAMP 共享区域：

```ld
OPEN_AMP_SHMEM (xrw) : ORIGIN = 0x38000400, LENGTH = 63K
```

范围为：

```text
0x38000400 ~ 0x3800FFFF
```

其中：

- `0x38000000 ~ 0x380003FF`：未包含在当前 OpenAMP 区域中，可用于保留或其他 D3 数据；
- `0x38000400 ~ 0x3800FFFF`：当前链接脚本声明的 OpenAMP 共享区域；
- `.resource_table` 被放置在 `OPEN_AMP_SHMEM` 区域起始位置，并声明为 `NOLOAD`。

双核共享数据还需要同时考虑：

1. 两个内核对缓存的一致性处理；
2. 内存屏障和同步机制，例如 HSEM、WFE/SEV；
3. 共享缓冲区的所有权和生命周期；
4. 链接脚本是否为共享区域预留了足够空间。

## 6. 启动地址、向量表和栈

### 6.1 向量表

每个内核镜像的向量表位于对应链接脚本的 `FLASH ORIGIN`：

| 内核/镜像 | 向量表地址 |
|---|---:|
| CM7 Bootloader | `0x08000000` |
| CM4 固件 | `0x08100000` |
| CM7 APP1 | 应为 APP1 的链接起始地址 `0x08020000` |

当前生成的 map 文件中可看到：

```text
CM7 .isr_vector = 0x08000000
CM4 .isr_vector = 0x08100000
```

CM7 Bootloader 跳转 APP1 时，代码会执行：

```c
SCB->VTOR = CM7_APP_FLASH_BASE;
__set_MSP(app_stack);
```

因此 APP1 必须满足：

1. 向量表位于 `CM7_APP_FLASH_BASE`；
2. 向量表第 0 项是合法且对齐的 RAM 栈顶；
3. 向量表第 1 项是 Thumb 状态的复位入口；
4. APP 自身的链接脚本与 Bootloader 的校验范围一致。

### 6.2 APP 栈地址校验

如果 APP1 使用 AXI SRAM 链接脚本，初始栈顶可能为：

```text
0x24080000
```

此时 Bootloader 不能只校验 `0x20000000` 一段 DTCM RAM，否则会把有效 APP 判断为无效。校验逻辑应根据 APP 的实际链接布局允许相应 RAM 区间，并保留跳转标记使用的 RAM 地址。

## 7. 当前 Bootloader 分区

当前 `CM7/Core/Inc/boot_config.h` 定义的 CM7 Flash 分区如下：

| 分区 | 起始地址 | 大小 | 结束边界 |
|---|---:|---:|---:|
| Bootloader | `0x08000000` | 128 KB | `0x08020000` |
| APP1 | `0x08020000` | 512 KB | `0x080A0000` |
| APP2 | `0x080A0000` | 512 KB | `0x08120000` |
| Setting | `0x08120000` | 128 KB | `0x08140000` |

其中 APP1 的基地址为：

```c
#define CM7_APP_FLASH_BASE    APP_SECTOR_ADDR
```

即：

```text
CM7_APP_FLASH_BASE = 0x08020000
```

## 8. 当前工程的地址冲突

当前 CM4 链接脚本使用：

```text
0x08100000 ~ 0x081FFFFF
```

而 CM7 分区表使用：

```text
APP2    0x080A0000 ~ 0x0811FFFF
Setting 0x08120000 ~ 0x0813FFFF
```

因此存在重叠：

| 重叠范围 | 冲突双方 |
|---|---|
| `0x08100000 ~ 0x0811FFFF` | CM4 Flash 与 CM7 APP2 |
| `0x08120000 ~ 0x0813FFFF` | CM4 Flash 与 CM7 Setting |

如果 CM4 镜像确实需要完整使用 1 MB Flash，则 CM7 APP2 和 Setting 不能继续使用这些地址。必须重新规划两核固件和 CM7 应用分区，使实际烧录范围互不覆盖。

## 9. 推荐的规划原则

### 9.1 每个可擦除区域只能归属一个用途

Bootloader、CM7 APP、CM4 固件、Setting 和升级暂存区应按 Flash 擦除粒度对齐，并且不能重叠。分区表、链接脚本、烧录脚本和升级代码必须使用同一份地址定义。

### 9.2 链接脚本决定镜像地址

`.ld` 文件中的 `FLASH ORIGIN` 决定向量表和代码的链接地址。仅修改烧录工具的下载地址，而不修改链接脚本，会导致向量表中的函数地址仍指向旧区域。

### 9.3 APP 跳转必须同步修改三处

修改 APP 起始地址时，至少要同步检查：

1. APP 链接脚本的 `FLASH ORIGIN`；
2. Bootloader 的 APP 基地址和有效性校验；
3. APP 的 `SCB->VTOR`、启动文件和实际烧录镜像。

### 9.4 共享内存不能被普通数据覆盖

OpenAMP 区域和双核通信缓冲区必须从普通堆、栈、`.bss` 和升级缓存中隔离出来，并在两个内核的链接脚本中保持一致。

## 10. 当前工程检查结论

| 检查项 | 结果 |
|---|---|
| CM7 Bootloader Flash 起始地址 | 与分区表一致 |
| CM7 Bootloader Flash 容量 | 128 KB，与分区表一致 |
| CM4 Flash 单独链接范围 | `0x08100000 ~ 0x081FFFFF`，链接配置自洽 |
| CM4 Flash 与 CM7 分区关系 | 与 APP2、Setting 存在重叠 |
| CM7 Bootloader RAM | `0x20000000 ~ 0x2001FFFF` |
| CM7 AXI SRAM APP 栈顶 | 可为 `0x24080000`，Bootloader 校验必须支持 |
| CM4 RAM | `0x10000000 ~ 0x10047FFF` |
| OpenAMP 共享区域 | 两个 Flash 链接脚本地址一致 |
| SRAM 链接脚本 | 存在，但不是当前默认 Makefile 的链接脚本 |

**最终结论：当前 CM7 Bootloader 的地址定义与自身链接脚本基本一致，但整个双核工程的 Flash 分区尚未统一。优先修正 CM4 Flash 与 CM7 APP2/Setting 的重叠，再根据 APP1 的实际 RAM 链接脚本修正 Bootloader 的栈地址有效性校验。**
