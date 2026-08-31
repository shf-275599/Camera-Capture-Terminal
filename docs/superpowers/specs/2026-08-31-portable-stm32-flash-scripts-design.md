# STM32 烧录脚本通用化设计

## 目标

将三个 PowerShell 烧录脚本改为可复制到同结构 STM32 双核 GNU Make 项目中直接使用的脚本。目标环境为 Windows PowerShell 5.1 或 PowerShell 7，工程布局仍采用顶层 `Makefile`、`Makefile/CM7/Makefile` 和 `Makefile/CM4/Makefile`。

## 设计

- `flash_cm7.ps1` 和 `flash_cm4.ps1` 分别处理对应核心。
- `flash_all.ps1` 构建顶层 Makefile，并按 CM7、CM4 顺序烧录。
- 每个脚本保持自包含，复制三个文件即可使用，不依赖公共脚本。
- 固件路径不再使用项目名常量；脚本从核心 Makefile 严格读取唯一的 `TARGET` 与 `BUILD_DIR` 赋值，并组成 `<BUILD_DIR>/<TARGET>.elf`。
- `-Jobs` 参数保持不变，并传给 GNU Make。
- STM32CubeProgrammer CLI 按环境变量、PATH、常见安装目录顺序查找。
- 缺少 Makefile、关键变量缺失或重复、构建失败、固件不存在、CLI 不存在或烧录失败时立即报错并返回失败码。
- 全量烧录先写 CM7，再写 CM4，只有最后一次写入执行复位。

## 验证

- 对三个脚本执行 PowerShell 语法解析。
- 在当前项目中验证解析得到的构建目录、目标名和 ELF 路径。
- 检查脚本只引用脚本所在目录，不依赖当前工作目录或项目名称。
- 不连接硬件执行静态检查；实际烧录仍需要 Windows、GNU Make、ARM GCC、STM32CubeProgrammer 和已连接的目标板。
