# OV5640 一帧图像 LCD 测试设计

## 目标

创建一个 CM7 FreeRTOS 摄像头测试任务，依次验证 OV5640 颜色条和真实画面的采集链路，并将最终真实画面显示在 LCD 上。测试结果通过 CM7 的 USART1 输出，使用 VS Code serial terminal 连接 COM8、115200 8-N-1 读取。

## 数据流

```text
OV5640 -> DCMI + DMA -> 0xD0000000 SDRAM 帧缓冲 -> LTDC layer 0 -> LCD
                    └-> USART1 / COM8: 测试阶段和结果日志
```

## 实现

- 保留现有 `app_ov5640_init()` 初始化入口。
- 在摄像头应用层提供一次性测试函数，配置 RGB565、800x480，并把帧写入现有 `LTDC_FRAME_BUF_ADDR`。
- 新增独立 `CameraTestTask`，由 `freertos.c` 创建，任务启动后只执行一次。
- 第一阶段打开 OV5640 颜色条测试模式，采集成功后输出 `CAMERA_TEST_COLORBAR_PASS`。
- 第二阶段关闭测试模式，采集真实画面；成功后输出 `CAMERA_TEST_REAL_PASS` 和 `CAMERA_TEST_PASS`，真实画面留在 LCD 上。
- 任一阶段失败都输出带错误码的 `FAIL` 标记并结束任务，任务栈不保存图像数据。

## 验证

1. 使用 `flash_all.ps1` 在 Windows PowerShell 中构建 CM4/CM7 的 A、B 槽并烧录。
2. 通过 VS Code serial terminal 连接 COM8，参数设置为 115200、8-N-1。
3. 复位开发板后确认串口依次出现颜色条阶段和真实画面阶段的 PASS 标记，以及最终 `CAMERA_TEST_PASS`。
4. 检查 LCD 最终显示为摄像头真实画面。
