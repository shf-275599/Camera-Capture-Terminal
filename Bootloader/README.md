# Bootloader — STM32H757 固定引导程序

STM32H757XIH6 双核固定引导：CM7 Bootloader 复位后依据 EEPROM 元数据选择 CM7 应用槽并跳转，同时处理试启动回退、SWD 本地升级请求和 KEY0 重新登记；CM4 Stub 依据 SRAM4 handoff 跳转 CM4 应用。A/B 应用镜像不在本工程构建，见 [`../BaseProject/`](../BaseProject/README.md)；操作规则见 [`AGENTS.md`](AGENTS.md)。

## Flash 占用

| 镜像 | 基址 | 大小 |
| --- | --- | --- |
| CM7 Bootloader | `0x08000000` | 128 KiB（末尾 `0x0801FFF0` 16 字节为本地升级描述符） |
| CM4 Stub | `0x08100000` | 128 KiB |

完整分区（含两核 A/B 应用槽）以 `Common/Inc/boot_slot.h` 为准；`docs/STM32H757双核MCU内存地址分配.md` 中 CM4 1024K、旧 APP1/setting 规划已过时，不能作为布局依据。

## 构建

在 Windows PowerShell 中执行（GNU Make + `arm-none-eabi-*`，不在 PATH 时给 make 传 `GCC_PATH=<bin目录>`）：

```powershell
make -C Makefile        # 双核，先 CM4 后 CM7
make -C Makefile/CM7    # 仅单核
./clean.ps1             # 清理双核产物（顶层 make clean 不递归）
```

产物为 `Makefile/CM*/build/BaseProject_CM*.{elf,hex,bin}`。链接脚本的 `(READONLY)` 段语法要求 GCC 11 及以上；改 `.ld` 后先清理再构建。

## 烧录

```powershell
./make_flash_cm7.ps1 -Jobs 8   # 仅构建并烧录 CM7 Bootloader
./make_flash_cm4.ps1 -Jobs 8   # 仅构建并烧录 CM4 Stub
```

两个入口经 SWD 写 ELF、校验并复位，不包含 A/B 应用；应用槽烧录入口在 `../BaseProject/make_flash_cm{7,4}_slot{A,B}.ps1`。本地升级协议（应用侧发起、本工程接收）见 `../BaseProject/docs/local_flash_update.md`。

## 启动要点

- EEPROM 元数据为两个 256 字节备份区（`0x0000` / `0x0100`），协议见 `CM7/APP/BOOT/boot_eeprom.h`；pending 最多试启动 3 次，成功后由应用 commit。
- 人工重刷 CM7 镜像后旧 CRC 可能拒绝启动：启动时按住 KEY0（PA8，高有效）2 秒可重新登记（要求 EEPROM 有效且两槽向量表有效）。
- CM4 Stub 从 D3 SRAM `0x3800F010` / `0x3800F014` 读取 handoff；无有效 magic 时尝试 CM4 A 槽。

本工程没有仓库内测试套件；修改 `Common/` 等共用源码时须构建两个核，修改启动或升级协议时须同步核对 `../BaseProject` 应用侧实现。
