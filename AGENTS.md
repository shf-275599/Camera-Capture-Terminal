# qt-ota 工作约定

## 构建与部署

- 在 Windows PowerShell 中执行 `./build.ps1`；从 WSL 调用时使用 `powershell.exe -NoProfile -File ./build.ps1`，不要改用 WSL 工具链构建。
- 脚本固定使用 `D:\QT\5.11.1\mingw53_32\bin` 和 `D:\QT\Tools\mingw530_32\bin`，即 Qt 5.11.1 / MinGW 5.3 **32 位**。修改时保持该版本和 C++11 兼容性。
- `build.ps1` 在 `build/` 内运行 qmake 和 `mingw32-make -j4`，随后覆盖根目录 `qt-ota.exe` 并执行 windeployqt、复制 MinGW 运行库；它同时完成构建和部署。
- 编辑源码 `src/main.cpp` 和 qmake 工程 `src/qt-ota.pro`；`build/` 是构建产物。根目录是可运行部署目录，分发时必须保留 DLL 和 `platforms/` 等插件目录，不能只复制 EXE。

## 验证

- 重新构建后，在 Windows PowerShell 执行 `./test-upload.ps1`。它启动 `127.0.0.1:18758` 模拟设备，调用根目录 EXE，核对 CM7 B 槽元数据、分块偏移、块大小和上传字节；成功输出 `QT UPLOAD PASS`。
- 该测试依赖 `../BaseProject/Makefile/CM7/build_B/BaseProject_CM7_B.bin`，并非独立测试夹具；缺少文件时不能直接运行。它只覆盖 CM7 B 槽上传成功路径，不验证真实设备擦写、复位或启动。
- GUI 启动检查：`./qt-ota.exe --smoke-test`，约一秒后退出。测试入口仍创建 `QApplication` 和窗口，需要可用的 Windows GUI / Qt 平台插件。
- `--test-query` 会访问默认设备 `192.168.10.2`；当前实现不读取命令行地址。`--test-upload <URL> <BIN>` 会实际发起上传，固定选择默认 CM7、版本号 301；日常验证优先使用模拟设备脚本。

## OTA 协议约束

- UI、HTTP 请求和上传状态机都在 `src/main.cpp` 的 `Window` 内；流程为查询 `/ota/status` → `/ota/begin` → 顺序发送 `/ota/chunk` → `/ota/end`。每个请求携带 `X-Ota-Core`，每次只升级一个核。
- 状态是逐行 `key=value` 文本。开始上传要求 `busy=0`、`other_core_busy=0`、`flags=0x00` 且 `running` 为 0 或 1；当前对 `flags` 使用字符串精确匹配，调整解析时注意模拟设备响应。
- 目标槽是运行槽的另一槽。CM7 A/B 基址为 `0x08020000` / `0x080A0000`，CM4 A/B 为 `0x08120000` / `0x081A0000`。BIN 长度限制为 8～393216 字节，偏移 4 的小端复位向量必须带 Thumb 位且位于目标镜像范围内；不能用同一 BIN 随意切换槽位。
- CRC32 使用初值 `0xffffffff`、反射多项式 `0xedb88320`、最终取反；上传块最大 8192 字节，`X-Offset` 在成功响应后推进，不支持断点续传。
- `/ota/end` 开始后禁用取消；若结束请求失败，设备可能已保存 pending，必须提示结果待确认，不自动重传或发送 abort。更早阶段的失败或取消通过 `/ota/abort` 清理会话。
- 上传成功只表示上传及 pending 保存成功，仍需设备复位后查询状态并核对实际业务；不能据此宣称新固件启动验证通过。
