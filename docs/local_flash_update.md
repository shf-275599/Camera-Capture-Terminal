# 本地升级与 OTA 的统一试启动流程

本地应用更新与 OTA 使用相同的启动语义：保护 confirmed 镜像，校验新镜像，登记 pending，复位试启动，由应用确认，超出试启动次数则回退。本地通过 SWD 传输，OTA 通过 HTTP 传输，不改变现有 BIN 或 EEPROM v2 格式。

## 使用范围

本地脚本只支持已有有效 EEPROM 启动记录的正常升级。双核都必须没有 pending，目标槽必须同时不同于 CM7 和 CM4 的 confirmed 槽，且两个 confirmed 镜像都通过校验。

不支持空板初始化和覆盖 confirmed 镜像。双核 confirmed 分处 A/B 时，两个双核烧录脚本都会拒绝；需先通过现有单核 OTA 升级流程调整槽位。

首次使用前先用 Windows PowerShell 烧录新版固定引导程序。下列命令相对工程总目录：

```powershell
.\Bootloader\make_flash_all.ps1 -Jobs 4
# 双核已确认 A 时，升级到 B：
.\BaseProject\make_flash_slotB.ps1 -Jobs 4 -Version 42
# 双核已确认 B 时，升级到 A；版本默认 0：
.\BaseProject\make_flash_slotA.ps1 -Jobs 4
```

`-Version` 是无符号 32 位整数，同次双核共用，默认 `0`。Bootloader/Stub 烧录仍只更新固定引导程序，不触发应用升级。

## 执行流程与结果

1. 构建目标槽，检查 BIN 长度与复位入口范围，计算 CRC32。
2. 检查板上 Bootloader 的本地更新协议标识。
3. 发起只读预检查：有效 EEPROM、无 pending、不覆盖 confirmed、原镜像可校验。失败则不擦写应用。
4. 依次写入并校验双核应用 ELF。
5. Bootloader 再次检查启动状态，校验实际 Flash 中的两个新镜像，使用一次 EEPROM 双备份保存登记双核 pending，分别设置 3 次试启动额度。confirmed 和其镜像信息保留。
6. 脚本收到保存及读回成功结果后复位。CM7 Bootloader 扣减 CM7 次数，CM7 应用选择 CM4 并扣减其次数。两核分别使用现有应用确认路径。

脚本输出 `PENDING` 只代表镜像写入和候选登记成功，不代表双核已运行或完成确认。CM7 目前在主初始化流程后确认；CM4 由 IPC 任务上报运行槽后，CM7 HTTP 管理逻辑确认。两核独立确认和回退，不提供双核版本的整体原子回滚。

任何预检查、Flash 写入或 EEPROM 提交失败都会中止，不执行最终试启动复位。Flash 与 EEPROM 不是掉电原子事务；pending 落盘前中断不会主动选择新镜像。提交结果丢失时先核对 EEPROM/启动日志，不能将重复执行被 pending 拒绝误认为原提交失败。

## 本地维护协议

Boot Flash 最后 16 字节 `0x0801FFF0` 为描述符：魔数 `0x4C555044`、版本 `1`、邮箱地址 `0x3800F100`、长度 `32`。SRAM4 邮箱依次保存命令、槽号、CM7 长度、CM7 CRC、CM4 长度、CM4 CRC、版本、状态。

命令 `0x4C555031` 只检查，`0x4C555032` 校验并提交。状态 `0/1/2/3` 分别表示待处理、处理中、成功、失败。先写参数再发布命令并系统复位；Bootloader 清除命令、执行后停留，脚本用 HOTPLUG 读取结果。两阶段之间不进入正常应用。

## 验证

在 BaseProject 目录用 Windows PowerShell 执行无硬件测试：

```powershell
.\tests\local_flash_update_test.ps1
```

模拟覆盖 A/B 请求顺序、版本传递与默认值、旧引导程序拒绝、预检查拒绝、Flash 写入失败及 pending 提交失败。实际 SRAM4 跨复位保留、SWD 状态读取、EEPROM 保存、启动确认和失败回退须板上验证；串口成功标记不替代摄像头/LCD 等业务现象确认。

## 2026-10-23 实机验证记录

- Windows STM32CubeProgrammer 2.22.0、ST-Link SWD、COM8 115200 8N1。
- 初始双核 confirmed=A、无 pending，A 槽镜像校验通过；固定引导程序已烧录并校验。
- B 槽双核 Flash 写入校验通过。首次提交时过早 SWD 查询期间发生 EEPROM 写入失败；请求后增加 2 秒无调试访问窗口，随后提交和读回成功。该等待用于避免调试器暂停内核干扰 I²C 页写，不能视为所有环境下时序的保证。
- 串口显示双核 pending=B、试启动次数各 3；CM7 和 CM4 分别完成 confirmed=1 提交。版本使用默认值 0。
- 再次复位后双核 confirmed=B、无 pending、次数为 0；CM7 B 完整 CRC 通过，摄像头帧检查通过，HTTP 正常启动。
- 针对 confirmed B 发起预检查被拒绝，未执行应用写入；随后复位恢复正常运行。
- 用户确认本轮 LCD 画面正常且持续更新。
- 未进行实机故障镜像试启动耗尽回退、断电注入和 A 槽完整升级测试；非零版本传递由模拟测试覆盖。现有 OTA 传输代码未修改，本轮未重新执行 HTTP OTA 上传。
