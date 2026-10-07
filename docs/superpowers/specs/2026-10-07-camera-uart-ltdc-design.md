# 摄像头串口中断与 LTDC 状态基线设计

## 目标

避免 USART1 接收完成回调中的阻塞发送延迟摄像头 DMA 中断，并区分 LTDC 初始化阶段遗留状态与运行期间新产生的状态。

## 设计

- USART1 接收回调只记录接收事件并立即重新开启单字节接收。
- `StartDefaultTask` 在任务上下文中取出待发送事件，发送现有的 `recv success!!!` 文本；计数器饱和，防止高频接收溢出。
- `lcd_init` 完成 LTDC、图层、背光和初始清屏后，清除 `FUIF`、`TERRIF`、`RRIF`，建立运行期状态基线。
- 摄像头流任务继续检查 LTDC 状态；基线后的 `TERRIF` 记录为运行期错误，并清除相关标志。

## 验证

- 检查修改后的 diff 和源码引用。
- 按仓库要求通过 Windows PowerShell 构建 CM7；当前 WSL 环境不执行 Windows 固件构建。
