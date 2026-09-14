# 嵌入式 JPEG 解码教程：从软件解码到 STM32H7 硬件加速

## 1. 教程目标

JPEG 解码不是简单地把 `.jpg` 文件复制到 LCD 帧缓冲。JPEG 文件保存的是经过熵编码、反量化和频域变换的数据，解码器输出的颜色格式也未必与 LCD 一致。

本文围绕同一条目标数据流，介绍两种实现路线：

```text
JPEG 压缩数据 -> JPEG 解码 -> 像素格式转换 -> LCD 帧缓冲 -> LCD
```

- 软件路线：使用 TJpgDec 在 CPU 上完成全部 JPEG 解码，并直接输出 RGB565。
- 硬件路线：使用 STM32H7 JPEG Codec 解码为 YCbCr 块，再由 DMA2D 转换成 LCD 使用的像素格式。

读完本文后，应能回答以下问题：

1. 一张 JPEG 图片进入 MCU 后经历了哪些步骤？
2. TJpgDec 的输入和输出回调分别解决什么问题？
3. STM32H7 的 JPEG 外设为什么不能直接替代 LCD 驱动？
4. SDRAM、DMA、D-Cache 和帧缓冲为什么容易导致花屏？
5. 如何判断一张图片是否适合嵌入式 JPEG 解码？

本文的软件示例基于当前 STM32H757 工程。当前工程已经接入 TJpgDec，但尚未启用 STM32 HAL JPEG 驱动，因此硬件路线用于说明正确的配置和数据流，不表示当前代码已经使用硬件 JPEG Codec。

## 2. JPEG 解码必备基础

### 2.1 先确认文件真的是 JPEG

扩展名不能证明文件格式。把 PNG 文件改名为 `image.jpg`，文件内容仍然是 PNG，JPEG 解码器必然失败。

JPEG 文件通常以 SOI 标记 `FF D8` 开始，以 EOI 标记 `FF D9` 结束。PC 上可以使用以下命令检查：

```bash
file image.jpg
xxd -l 16 image.jpg
```

预期结果应包含 `JPEG image data`，文件前两个字节应为 `ff d8`。

当前工程最初的 `test.jpg` 实际是 PNG 数据，测试代码使用的是转换后的 100 x 100 Baseline JPEG 数组。这类问题应在数据进入 MCU 前排除。

### 2.2 Baseline 与 Progressive

嵌入式轻量解码器通常优先支持 Baseline JPEG：图片数据按一次扫描组织，状态和内存需求相对简单。

Progressive JPEG 会分多次扫描逐步恢复图像细节，浏览器支持良好，但轻量 MCU 解码器未必支持。当前 TJpgDec 遇到不支持的 JPEG 标准时会返回 `JDR_FMT3`，Progressive JPEG 是常见原因之一。

建议先把测试图片转换为 Baseline JPEG：

```bash
ffmpeg -i input.png -pix_fmt yuvj420p -q:v 3 output.jpg
```

### 2.3 JPEG Marker

JPEG 文件由多个 Marker 段组成，常见标记如下：

| Marker | 名称 | 作用 |
| --- | --- | --- |
| `FFD8` | SOI | 图片开始 |
| `FFE0`~`FFEF` | APPn | JFIF、Exif 等应用信息 |
| `FFDB` | DQT | 量化表 |
| `FFC0` | SOF0 | Baseline 图片尺寸、分量和采样信息 |
| `FFC2` | SOF2 | Progressive JPEG |
| `FFC4` | DHT | Huffman 表 |
| `FFDA` | SOS | 压缩扫描数据开始 |
| `FFD9` | EOI | 图片结束 |

解码器在准备阶段会解析这些段，建立量化表和 Huffman 表，并获得图片宽高、颜色分量和采样方式。

### 2.4 MCU、DCT 与色度采样

JPEG 不按 LCD 的逐像素方式保存图片，而是按 MCU（Minimum Coded Unit）组织数据。每个 MCU 包含一个或多个 8 x 8 数据块，具体数量取决于色度采样方式。

常见采样格式：

| 采样方式 | 含义 | 特点 |
| --- | --- | --- |
| 4:4:4 | 色度与亮度分辨率相同 | 质量高、数据量大 |
| 4:2:2 | 水平方向降低色度分辨率 | 常见折中方案 |
| 4:2:0 | 水平和垂直方向都降低色度分辨率 | 照片中最常见 |
| 灰度 | 只有亮度分量 | 无 Cb、Cr |

简化后的 Baseline JPEG 解码步骤为：

```text
解析 Marker
  -> Huffman 解码
  -> 反量化
  -> IDCT（逆离散余弦变换）
  -> 恢复 Y/Cb/Cr 分量
  -> 色度上采样
  -> 转换为 RGB
  -> 写入帧缓冲
```

轻量软件解码器通常一次只保留一个 MCU 的工作数据，因此不必在 RAM 中保存整张解码图片。

## 3. 解码结果为什么不能直接显示

LCD 控制器读取的是固定布局的像素帧缓冲，而 JPEG 解码器的输出可能是 YCbCr、RGB888、RGB565 或灰度数据。

当前工程的 LTDC 帧缓冲参数为：

```text
分辨率：800 x 480
格式：RGB565
地址：0xD0000000
每像素：2 字节
单帧大小：800 x 480 x 2 = 768000 字节
```

RGB565 用 16 位表示一个像素：

```text
RRRRR GGGGGG BBBBB
 5 位   6 位   5 位
```

如果解码输出是 RGB565，只需处理目标地址、行跨度和 Cache。如果输出是 YCbCr，还必须先做颜色空间和采样格式转换。

典型软件数据流：

```text
JPEG -> TJpgDec -> RGB565 MCU 块 -> LTDC RGB565 帧缓冲
```

典型 STM32H7 硬件数据流：

```text
JPEG -> JPEG Codec -> YCbCr MCU 块 -> DMA2D -> RGB565/ARGB8888 帧缓冲
```

因此，“JPEG 解码完成”和“LCD 已经能正确显示”是两个不同的完成条件。

## 4. 实验平台与统一测试方法

### 4.1 当前工程资源

| 资源 | 配置 |
| --- | --- |
| MCU | STM32H757XIH6，CM7 执行图像和 LCD 逻辑 |
| RTOS | FreeRTOS |
| LCD | LTDC RGB LCD，800 x 480，RGB565 |
| 外部 RAM | 32 MB SDRAM，基地址 `0xD0000000` |
| 图像软件库 | `Utilities/TJpgDec/` |
| 测试图片 | `CM7/APP/JPEG/jpeg_test_data.h` |
| 测试入口 | `CM7/APP/JPEG/app_jpeg_test.c` |

### 4.2 建议的最小测试顺序

1. 用小尺寸 Baseline JPEG 的 `const uint8_t[]` 测试解码器。
2. 串口打印图片宽高和解码结果。
3. 将图片显示到固定坐标，确认颜色和方向正确。
4. 再增加图片居中、缩放和边界判断。
5. 最后把输入源替换为 FatFS、串口或网络流。

这个顺序可以把“存储驱动问题”和“JPEG 解码问题”分开。如果一开始就从 SD 卡读取，挂载失败、文件读取失败和 JPEG 格式错误会混在一起。

### 4.3 统一成功标准

- JPEG 头解析成功，宽高与原图一致。
- 解码函数返回成功状态。
- LCD 显示区域尺寸、方向和颜色正确。
- 图片外的帧缓冲没有被破坏。
- 多次运行没有 HardFault、内存泄漏或随机花屏。
- Cache 开启与关闭时结果一致。

## 5. 路线 A：TJpgDec 软件解码

### 5.1 TJpgDec 的定位

TJpgDec 是面向小型嵌入式系统的软件 JPEG 解码器。它不依赖文件系统和 LCD 驱动，而是通过两个回调连接外部环境：

- 输入回调：向解码器提供 JPEG 字节流。
- 输出回调：接收解码后的矩形像素块。

当前配置位于 `Utilities/TJpgDec/tjpgd_utils_conf.h`：

```c
#define JD_SZBUF       512
#define JD_FORMAT      1
#define JD_USE_SCALE   1
#define JD_TBLCLIP     1
#define JD_FASTDECODE  1
```

关键配置含义：

| 宏 | 当前值 | 含义 |
| --- | --- | --- |
| `JD_SZBUF` | 512 | 流输入缓冲大小 |
| `JD_FORMAT` | 1 | 输出 RGB565 |
| `JD_USE_SCALE` | 1 | 支持 1、1/2、1/4、1/8 缩放 |
| `JD_TBLCLIP` | 1 | 用查表加速颜色限幅 |
| `JD_FASTDECODE` | 1 | 启用适合 32 位 MCU 的优化 |

### 5.2 把 JPEG 转换为 C 数组

数组应保留 JPEG 文件的原始字节，不能保存解码后的像素数据。可使用：

```bash
xxd -i image.jpg > image_jpg.h
```

建议整理成以下接口：

```c
const uint8_t jpeg_test_data[] = {
    0xFF, 0xD8, /* ... */ 0xFF, 0xD9
};

const unsigned int jpeg_test_data_size = sizeof(jpeg_test_data);
```

数组是压缩数据，适合放在 Flash 的只读段。不要把几 MB 的原始 RGB 数据误认为 JPEG 数组。

### 5.3 解码会话上下文

输入回调需要保存当前读取位置，输出回调需要知道图片在 LCD 上的位置。可以通过 `JDEC::device` 共享这些状态：

```c
typedef struct
{
    const uint8_t *data;
    size_t size;
    size_t position;
    uint16_t x;
    uint16_t y;
} jpeg_context_t;
```

每次解码都应创建独立上下文。如果多个任务并发解码，不要共用同一个 `position`。

### 5.4 内存输入回调

```c
static size_t jpeg_input(JDEC *decoder, uint8_t *buffer, size_t count)
{
    jpeg_context_t *ctx = decoder->device;
    size_t remaining = ctx->size - ctx->position;

    if (count > remaining)
    {
        count = remaining;
    }

    if (buffer != NULL)
    {
        memcpy(buffer, ctx->data + ctx->position, count);
    }

    ctx->position += count;
    return count;
}
```

这里最容易漏掉的规则是：`buffer == NULL` 表示解码器希望跳过一段数据。此时不执行复制，但仍要推进 `position` 并返回实际跳过的字节数。

同一个接口可以换成 FatFS：

```c
static size_t jpeg_file_input(JDEC *decoder, uint8_t *buffer, size_t count)
{
    FIL *file = decoder->device;
    UINT processed = 0;

    if (buffer != NULL)
    {
        if (f_read(file, buffer, count, &processed) != FR_OK)
        {
            return 0;
        }
    }
    else if (f_lseek(file, f_tell(file) + count) == FR_OK)
    {
        processed = count;
    }

    return processed;
}
```

串口输入也可以使用，但必须先解决分包、总长度、超时和生产者/消费者速度差异。推荐先把一帧 JPEG 收入连续缓冲，再复用内存输入回调；需要边接收边解码时，应使用环形缓冲和阻塞读取，而不是在回调里忙等。

### 5.5 RGB565 输出回调

TJpgDec 按 MCU 分块调用输出回调，`JRECT` 的四个边界都是闭区间：

```c
static int jpeg_output(JDEC *decoder, void *bitmap, JRECT *rect)
{
    jpeg_context_t *ctx = decoder->device;
    const uint16_t *src = bitmap;
    uint16_t width = rect->right - rect->left + 1U;
    uint16_t height = rect->bottom - rect->top + 1U;

    for (uint16_t row = 0; row < height; row++)
    {
        uint16_t *dst = (uint16_t *)LTDC_FRAME_BUF_ADDR
                      + (ctx->y + rect->top + row) * lcddev.width
                      + ctx->x + rect->left;

        memcpy(dst, src + row * width, width * sizeof(uint16_t));
        clean_framebuffer_cache(dst, width * sizeof(uint16_t));
    }

    return 1;
}
```

为什么要逐行复制：

- 当前 MCU 输出块是紧密排列的。
- LCD 帧缓冲每行有 `lcddev.width` 个像素。
- MCU 块通常比整屏窄，连续复制整个块会错误跨越目标行边界。

返回 0 会让 `jd_decomp()` 以 `JDR_INTR` 停止，可用于裁剪、取消操作或输出错误。

### 5.6 准备并启动解码

```c
static uint8_t work_pool[8192];

JDEC decoder;
jpeg_context_t ctx = {
    .data = jpeg_test_data,
    .size = jpeg_test_data_size,
    .position = 0,
};

JRESULT result = jd_prepare(&decoder, jpeg_input,
                            work_pool, sizeof(work_pool), &ctx);
if (result != JDR_OK)
{
    printf("JPEG prepare failed: %d\n", result);
    return;
}

ctx.x = (lcddev.width - decoder.width) / 2U;
ctx.y = (lcddev.height - decoder.height) / 2U;

result = jd_decomp(&decoder, jpeg_output, 0U);
printf("JPEG decode result: %d\n", result);
```

`jd_prepare()` 解析 JPEG 头并从工作池分配内部结构；`jd_decomp()` 才执行完整解码。工作池不是整张图片的输出缓冲，它主要保存流缓冲、Huffman 表、量化表和 MCU 工作区。

缩放参数：

| `scale` | 输出尺寸 |
| --- | --- |
| 0 | 原尺寸 |
| 1 | 1/2 |
| 2 | 1/4 |
| 3 | 1/8 |

使用缩放时，居中计算必须使用缩放后的宽高：

```c
uint16_t out_width = decoder.width >> scale;
uint16_t out_height = decoder.height >> scale;
```

### 5.7 TJpgDec 错误码

| 错误码 | 含义 | 常见原因 |
| --- | --- | --- |
| `JDR_OK` | 成功 | 无 |
| `JDR_INTR` | 输出回调中止 | 输出回调返回 0 |
| `JDR_INP` | 输入错误 | 数据不完整、读取失败 |
| `JDR_MEM1` | 工作池不足 | `work_pool` 太小 |
| `JDR_MEM2` | 输入缓冲太小 | `JD_SZBUF` 配置不足 |
| `JDR_PAR` | 参数错误 | 非法缩放值等 |
| `JDR_FMT1` | 数据格式损坏 | JPEG 字节流被破坏 |
| `JDR_FMT2` | 格式正确但功能不支持 | 表或格式超出实现能力 |
| `JDR_FMT3` | JPEG 标准不支持 | Progressive JPEG、采样格式不支持 |

遇到错误时，应先在 PC 上验证文件，再检查输入回调返回长度，最后调整解码配置和工作池。

## 6. 路线 B：STM32H7 硬件 JPEGDEC

### 6.1 硬件路线的边界

STM32H757 包含 JPEG Codec 外设，既支持 JPEG 解码，也支持编码。芯片头文件中可以找到 `JPEG_IRQn`、`JPEG_TypeDef` 和 `JPGDEC_BASE`。

当前工程没有包含 `stm32h7xx_hal_jpeg.c/.h`，`HAL_JPEG_MODULE_ENABLED` 也未启用。因此要使用硬件路线，应先通过 CubeMX 为 CM7 启用 JPEG 外设并重新生成代码，同时保留所有 `USER CODE` 区域。

硬件解码不会直接输出 RGB565。STM32CubeH7 官方示例采用以下流程：

```text
Flash/FatFS 中的 JPEG
  -> MDMA 输入
  -> JPEG Codec
  -> MDMA 输出 YCbCr MCU 块
  -> DMA2D YCbCr 转 RGB
  -> LTDC 帧缓冲
```

### 6.2 CubeMX 配置要点

1. 在 CM7 上启用 JPEG 外设。
2. 启用 JPEG 全局中断。
3. 为输入和输出配置 MDMA 请求。
4. 启用 MDMA 中断。
5. 确认 DMA2D 输出格式与 LTDC 层一致。
6. 在 `stm32h7xx_hal_conf.h` 中确认 `HAL_JPEG_MODULE_ENABLED`。
7. 确认构建系统包含 `stm32h7xx_hal_jpeg.c`。

官方 DMA 示例使用 JPEG 输入 FIFO 和输出 FIFO 阈值作为 MDMA 请求。输入方向通常为内存字节递增到固定 JPEG 输入寄存器，输出方向为固定 JPEG 输出寄存器到内存字节递增。

### 6.3 初始化与启动

核心初始化形式如下：

```c
JPEG_HandleTypeDef hjpeg;

hjpeg.Instance = JPEG;
if (HAL_JPEG_Init(&hjpeg) != HAL_OK)
{
    Error_Handler();
}
```

DMA 解码通常按块提供输入和输出缓冲：

```c
HAL_JPEG_Decode_DMA(&hjpeg,
                    jpeg_input_buffer,
                    input_chunk_size,
                    ycbcr_output_buffer,
                    output_chunk_size);
```

该调用是非阻塞的。应用需要在回调中继续提供输入缓冲、处理已产生的输出，并在解码完成回调中设置完成标志或释放信号量。

不同 STM32CubeH7 版本的函数参数类型和回调注册方式可能存在差异，应以工程中实际生成的 `stm32h7xx_hal_jpeg.h` 为准。

### 6.4 DMA 回调的数据流

典型输出回调：

```c
void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef *hjpeg,
                                uint8_t *data,
                                uint32_t length)
{
    process_ycbcr_block(data, length);
    HAL_JPEG_ConfigOutputBuffer(hjpeg,
                                next_output_buffer,
                                OUTPUT_CHUNK_SIZE);
}
```

典型输入续传逻辑是在输入缓冲消费后读取下一块 JPEG 数据，再调用 HAL 的输入缓冲配置接口。FatFS 场景应采用双缓冲：JPEG 外设消费缓冲 A 时，CPU 或 DMA 填充缓冲 B，然后交换角色。

需要处理的状态至少包括：

- 输入文件剩余字节数。
- 当前输入缓冲及有效长度。
- 当前输出缓冲及有效长度。
- JPEG 头信息是否已解析。
- 解码完成、错误和暂停状态。
- 已处理 MCU 数量或输出偏移。

### 6.5 获取图片信息

解码过程中或完成后，可读取 JPEG 信息：

```c
JPEG_ConfTypeDef info;

HAL_JPEG_GetInfo(&hjpeg, &info);
```

`info` 中的重要字段包括：

- `ImageWidth`
- `ImageHeight`
- `ImageQuality`
- `ColorSpace`
- `ChromaSubsampling`

宽高用于目标地址和边界计算；色彩空间与采样方式决定 DMA2D 如何解释 JPEG 外设的 YCbCr 块。

### 6.6 使用 DMA2D 转换到 LCD 格式

硬件 JPEG 输出按 YCbCr MCU 块组织。DMA2D 的前景输入应配置为 YCbCr，并设置与 JPEG 信息一致的 Chroma Subsampling：

```c
DMA2D_HandleTypeDef hdma2d;

hdma2d.Instance = DMA2D;
hdma2d.Init.Mode = DMA2D_M2M_PFC;
hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
hdma2d.Init.OutputOffset = lcd_width - image_width;

hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_YCBCR;
hdma2d.LayerCfg[1].ChromaSubSampling = css_mode;
hdma2d.LayerCfg[1].InputOffset = input_line_offset;

HAL_DMA2D_Init(&hdma2d);
HAL_DMA2D_ConfigLayer(&hdma2d, 1);
HAL_DMA2D_Start(&hdma2d,
                (uint32_t)ycbcr_buffer,
                framebuffer_address,
                image_width,
                image_height);
HAL_DMA2D_PollForTransfer(&hdma2d, timeout_ms);
```

`css_mode` 需要根据 `ChromaSubsampling` 映射为 `DMA2D_CSS_420`、`DMA2D_CSS_422` 或 `DMA2D_NO_CSS`。YCbCr 输入行可能还需要按 MCU 宽度补齐，不能只把图片宽度直接当作紧密行跨度。

官方 STM32CubeH7 示例常输出 ARGB8888；本工程 LTDC 为 RGB565，因此示例中的输出格式、每像素字节数和目标地址公式必须一起改成 RGB565，不能只修改一个宏。

### 6.7 硬件路线的缓冲策略

硬件解码更快，但数据搬运和同步更复杂。常用策略：

| 策略 | 优点 | 缺点 |
| --- | --- | --- |
| 整图 YCbCr 缓冲 | 流程简单 | RAM 占用大 |
| 分块输出后立即转换 | 峰值 RAM 小 | 状态管理复杂 |
| 双输入、双输出缓冲 | 吞吐高，可流水处理 | 需要严格同步 |

静态图片首次验证可使用整图缓冲；连续图片或 MJPEG 应使用分块双缓冲，使文件读取、JPEG 解码和 DMA2D 转换形成流水线。

## 7. LCD、SDRAM 与 D-Cache 一致性

### 7.1 为什么会花屏

CM7 写入可缓存 SDRAM 时，最新数据可能仍在 D-Cache 中；LTDC、DMA2D、MDMA 等总线主设备读取的是 SDRAM。若不维护 Cache，一方看到的是新数据，另一方看到的可能仍是旧数据。

当前工程 MPU 将 `0xD0000000` 开始的 32 MB SDRAM 配置为 Cacheable 和 Bufferable，因此必须明确每块缓冲的所有者。

### 7.2 Cache 操作方向

| 数据流 | DMA 前 | DMA 后 |
| --- | --- | --- |
| CPU 写，DMA/LTDC 读 | Clean | 通常不需要 |
| DMA 写，CPU 读 | 写前避免脏 Cache 覆盖；按设计 Clean/Invalidate | Invalidate |
| CPU 与 DMA 都写 | 必须设计所有权切换 | 必须设计所有权切换 |

Cortex-M7 D-Cache Line 为 32 字节，地址和长度必须覆盖完整 Cache Line：

```c
static void clean_framebuffer_cache(void *address, size_t length)
{
    uintptr_t start = (uintptr_t)address & ~(uintptr_t)31U;
    uintptr_t end = ((uintptr_t)address + length + 31U) & ~(uintptr_t)31U;

    SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
    __DSB();
}
```

如果两个无关对象位于同一个 Cache Line，清理或失效可能影响另一个对象。因此 DMA 缓冲应使用 32 字节对齐，并尽量让长度也按 32 字节补齐。

### 7.3 DTCM 不是通用 DMA 缓冲区

CM7 的 DTCM 适合 CPU 高频访问，但多数 DMA 主设备无法访问。局部数组和任务栈可能被链接到 DTCM，不能默认交给 DMA2D 或 MDMA。

软件解码输出由 CPU 直接复制到 SDRAM时，TJpgDec 工作缓冲放在 DTCM 没有问题。硬件解码或 DMA2D 输入缓冲必须放在对应 DMA 可访问的 SRAM 或 SDRAM 中。

## 8. 性能测试方法

只比较“能不能显示”不足以选型。软件和硬件路线应使用同一张 JPEG、同一优化级别和同一目标像素格式测试。

### 8.1 解码耗时

可以用 DWT 周期计数器测量：

```c
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

uint32_t start = DWT->CYCCNT;
decode_image();
uint32_t cycles = DWT->CYCCNT - start;

float milliseconds = cycles * 1000.0f / SystemCoreClock;
```

分别记录：

- JPEG 输入读取时间。
- JPEG 核心解码时间。
- YCbCr 到 RGB 转换时间。
- 帧缓冲写入和 Cache 维护时间。
- 端到端显示准备时间。

### 8.2 CPU 占用

软件解码期间 CM7 基本被计算占用。硬件路线虽然降低 CPU 计算量，但中断、缓冲管理和 Cache 操作仍会消耗 CPU。

可比较：

- 解码期间 Idle Task 运行比例。
- 其他实时任务的最大调度延迟。
- 连续解码时是否丢帧。

### 8.3 内存与固件尺寸

建议记录以下指标：

| 指标 | 软件路线 | 硬件路线 |
| --- | --- | --- |
| JPEG 输入缓冲 | 取决于输入方式 | 通常需要双缓冲 |
| 解码工作区 | TJpgDec 内存池 | HAL 状态和 YCbCr 输出缓冲 |
| 整图像素缓冲 | 可省略 | 可省略，但分块逻辑更复杂 |
| 帧缓冲 | 两者相同 | 两者相同 |
| Flash 增量 | 软件解码代码和表 | HAL JPEG、工具代码、DMA 管理 |

通过 ELF 的 `size` 输出比较 `text`、`data` 和 `bss`，不要只看 BIN 文件大小。

## 9. 软件与硬件方案对比

| 维度 | TJpgDec 软件解码 | STM32H7 硬件 JPEGDEC |
| --- | --- | --- |
| 可移植性 | 高，适用于多种 MCU | 仅适用于带 JPEG 外设的型号 |
| 初始接入 | 简单，两个回调即可 | 复杂，涉及 JPEG、MDMA、DMA2D 和中断 |
| CPU 占用 | 高 | 低 |
| 峰值吞吐 | 较低 | 高 |
| RAM 设计 | 可按 MCU 流式输出 | 分块和双缓冲设计要求更高 |
| 输出到 RGB565 | TJpgDec 可直接输出 | 通常需 DMA2D 转换 YCbCr |
| 调试难度 | 状态少，容易单步 | 异步回调和 Cache 问题较多 |
| 适用场景 | UI 小图、偶尔显示、跨平台 | 相册、MJPEG、大图和高帧率 |

选型建议：

- 只显示少量静态图片：优先 TJpgDec，先把完整链路做正确。
- CPU 已接近满载或需要连续解码：选择硬件 JPEG Codec。
- 产品需要跨多款 MCU：保留统一的软件接口，把硬件加速作为后端实现。
- 需要 MJPEG：优先硬件路线，并采用输入、解码、转换三级流水线。

## 10. 常见问题及排查

### 10.1 `jd_prepare()` 立即失败

检查顺序：

1. 文件头是不是 `FF D8`。
2. 文件实际格式是否为 JPEG，而不是改名的 PNG/WebP。
3. 输入回调是否正确返回实际读取长度。
4. `buffer == NULL` 时是否正确跳过数据。
5. 工作池是否足够。
6. 图片是否为 Progressive JPEG。

### 10.2 `JDR_MEM1`

说明 TJpgDec 工作池不足。增大传给 `jd_prepare()` 的内存池，或减少需要分配的解码配置。不要把它误认为整图帧缓冲不足。

### 10.3 `JDR_FMT3`

常见原因：

- Progressive JPEG。
- 不支持的色彩分量。
- 不支持的采样因子。
- JPEG 表结构超出轻量解码器能力。

先在 PC 上转换为 Baseline、YCbCr 4:2:0 或 4:2:2 再测试。

### 10.4 图片颜色不正确

检查：

- TJpgDec 的 `JD_FORMAT` 是否为 RGB565。
- LTDC 层是否也是 RGB565。
- RGB565 字节序是否被交换。
- DMA2D 输入是不是错误地配置成 RGB 而实际输入为 YCbCr。
- 硬件 JPEG 的 Chroma Subsampling 是否正确传给 DMA2D。
- 红蓝通道交换配置是否一致。

### 10.5 图片倾斜或每行错位

通常是行跨度错误：

```text
目标下一行地址 = 当前行地址 + 屏幕宽度 x 每像素字节数
```

不能使用图片宽度代替屏幕宽度，也不能把 TJpgDec MCU 块一次连续复制到比它更宽的帧缓冲。

### 10.6 只显示部分图片

检查：

- 输出回调是否意外返回 0。
- 图片边界判断是否使用了缩放后的尺寸。
- 输入流是否提前返回 0。
- 硬件解码是否遗漏下一输入或输出缓冲配置。
- 输出缓冲是否小于一个完整处理单元且没有续传。

### 10.7 偶尔花屏，关闭 Cache 后正常

这是典型 Cache 一致性问题。检查：

- CPU 写给 LTDC 或 DMA 的数据前是否 Clean。
- DMA 写给 CPU 的数据后是否 Invalidate。
- Cache 操作地址和长度是否按 32 字节对齐。
- 缓冲是否位于 DMA 可访问内存。
- 是否存在 CPU 与 DMA 同时写同一缓冲。

### 10.8 软件解码正常，DMA2D 转换失败

首先检查 TJpgDec 工作缓冲或任务栈是否在 DTCM。CPU 能访问 DTCM，不代表 DMA2D 能访问。将 DMA 输入缓冲放到 AXI SRAM 或 SDRAM，并正确维护 Cache。

## 11. 代码索引与检查清单

### 11.1 当前工程代码索引

| 内容 | 路径 |
| --- | --- |
| 软件解码器 | `Utilities/TJpgDec/tjpgd_utils.c` |
| 软件解码器 API | `Utilities/TJpgDec/tjpgd_utils.h` |
| 软件解码配置 | `Utilities/TJpgDec/tjpgd_utils_conf.h` |
| JPEG 测试数据 | `CM7/APP/JPEG/jpeg_test_data.h` |
| 内存输入和 LCD 输出回调 | `CM7/APP/JPEG/app_jpeg_test.c` |
| JPEG 测试接口 | `CM7/APP/JPEG/app_jpeg_test.h` |
| LCD 应用接口 | `BSP/LCD/lcd.h` |
| LTDC 帧缓冲配置 | `BSP/LCD/lcd_ltdc.h` |
| SDRAM MPU 配置 | `BSP/MPU/mpu.c` |
| 测试任务入口 | `CM7/Core/Src/freertos.c` |

### 11.2 图片输入检查

- [ ] 文件实际格式是 JPEG。
- [ ] 使用 Baseline JPEG。
- [ ] 解码器支持图片的颜色分量和采样格式。
- [ ] JPEG 数据完整，包含 SOI 和 EOI。
- [ ] 输入回调不会越界。
- [ ] FatFS 或串口输入能返回准确的有效字节数。

### 11.3 软件解码检查

- [ ] `JD_FORMAT` 与 LCD 像素格式匹配。
- [ ] 工作池生命周期覆盖整个解码过程。
- [ ] `buffer == NULL` 时正确跳过数据。
- [ ] 输出回调按 MCU 矩形的实际宽高处理。
- [ ] 图片边界检查包含目标显示偏移。
- [ ] 缩放后重新计算显示尺寸。

### 11.4 硬件解码检查

- [ ] 芯片型号包含 JPEG Codec。
- [ ] HAL JPEG 模块、源文件、时钟和中断已启用。
- [ ] MDMA 输入和输出请求配置正确。
- [ ] 输入、输出缓冲位于 DMA 可访问内存。
- [ ] 回调能够持续提供下一块输入和输出缓冲。
- [ ] `HAL_JPEG_GetInfo()` 返回的采样格式用于 DMA2D 配置。
- [ ] YCbCr 已转换为 LTDC 使用的 RGB 格式。

### 11.5 显示和 Cache 检查

- [ ] 帧缓冲地址与 LTDC 当前层一致。
- [ ] 每像素字节数与 LTDC 格式一致。
- [ ] 目标行跨度使用屏幕宽度。
- [ ] CPU 与 DMA 的缓冲所有权清晰。
- [ ] Cache 操作按 32 字节边界覆盖全部数据。
- [ ] DMA 不访问 DTCM 中的缓冲。

## 12. 总结

JPEG 解码项目应拆成三个独立问题：

```text
如何获得正确的 JPEG 字节流
如何把 JPEG 解码成明确格式的图像数据
如何把该格式安全地送入 LCD 帧缓冲
```

TJpgDec 把这三个问题通过输入和输出回调分离，适合先建立可靠的软件基线。STM32H7 JPEG Codec 能显著提高吞吐并降低 CPU 占用，但必须增加 MDMA 缓冲管理、YCbCr 到 RGB 转换和 Cache 一致性处理。

实际开发中，建议先用 `const uint8_t[]` 跑通 TJpgDec 和 LCD，再接入 FatFS 或串口；确认输入和显示链路可靠后，再用相同测试图片实现硬件后端并进行性能对比。这样每一步都有明确的成功标准，出现问题时也能快速判断故障属于文件输入、JPEG 解码、颜色转换还是帧缓冲同步。

## 参考资料

- ChaN, TJpgDec R0.03 源码及配置说明：本工程 `Utilities/TJpgDec/`。
- STMicroelectronics, STM32CubeH7 JPEG decoding examples：`JPEG_DecodingFromFLASH_DMA`、`JPEG_DecodingUsingFs_DMA`、`JPEG_DecodingUsingFs_Polling`。
- STMicroelectronics, STM32H7 HAL JPEG Driver API：`stm32h7xx_hal_jpeg.h/.c`，应以当前使用的 STM32CubeH7 版本为准。
- Arm CMSIS Cortex-M7 Cache API：`SCB_CleanDCache_by_Addr()`、`SCB_InvalidateDCache_by_Addr()`。
