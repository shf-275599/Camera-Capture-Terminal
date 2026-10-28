# qt-ota 工作约定

## 构建与部署

- 在 Windows PowerShell 中执行 `./build.ps1`；从 WSL 调用时使用 `powershell.exe -NoProfile -File ./build.ps1`，不要改用 WSL 工具链构建。
- 脚本固定使用 `D:\QT\5.11.1\mingw53_32\bin` 和 `D:\QT\Tools\mingw530_32\bin`，即 Qt 5.11.1 / MinGW 5.3 **32 位**。修改时保持该版本和 C++11 兼容性。
- `build.ps1` 在 `build/` 内运行 qmake 和 `mingw32-make -j4`，随后覆盖根目录 `qt-ota.exe` 并执行 windeployqt、复制 MinGW 运行库；它同时完成构建和部署。
- 源码入口和全部 UI、HTTP、上传状态机在 `src/main.cpp` 的 `Window`，qmake 配置在 `src/qt-ota.pro`；不要修改 `build/` 内的生成文件。保留 qmake 的两个 UTF-8 字符集选项，否则中文界面可能乱码。
- 根目录是可运行部署目录，分发时必须保留 DLL 和 `platforms/` 等插件目录，不能只复制 EXE。

## 验证

- 先构建，再运行测试：测试脚本调用根目录部署的 EXE，不会自动编译。以下命令均在 Windows PowerShell 执行；从 WSL 运行脚本用 `powershell.exe -NoProfile -File ./脚本名.ps1`。
- 两个模拟测试都依赖 `../BaseProject/Makefile/CM7/build_B/BaseProject_CM7_B.bin`；先确认文件存在。测试使用独立的 `QT_OTA_TASK_FILE`，正常进入清理流程后删除测试目录并恢复原环境变量；`test-upload.ps1` 的固件路径解析在 `try/finally` 之前，缺少 BIN 时不会自动清理。
- `./test-upload.ps1`：监听 `127.0.0.1:18758`，核对 CM7 B 槽元数据、分块偏移、块大小和上传字节；成功输出 `QT UPLOAD PASS`。
- `./test-recovery.ps1`：监听 `127.0.0.1:18759`，默认覆盖 `begin/chunk/network/partial/restart/changed/expired/end`；单场景用 `./test-recovery.ps1 -Scenarios partial`，多个场景在 PowerShell 内用 `./test-recovery.ps1 -Scenarios chunk,partial`（从 WSL 传数组用 `powershell.exe -NoProfile -Command '& ./test-recovery.ps1 -Scenarios chunk,partial'`）。成功输出 `RECOVERY PASS`。
- `partial` 收完整块但不提交，并中断回包；`changed` 修改任务 SHA256，并不修改 BIN；`changed/expired` 预期非零退出且保留任务。这些测试只证明客户端模拟交互通过，不证明真实设备半块收包、擦写、掉电恢复或启动正常。
- GUI 启动检查：`./qt-ota.exe --smoke-test`，约一秒后退出。所有测试入口仍创建 `QApplication` 和窗口，需要可用的 Windows GUI / Qt 平台插件。
- `--test-query` 会访问默认设备 `192.168.10.2`；当前实现不读取命令行地址。`--test-upload <URL> <BIN>` 会实际发起上传，固定选择默认 CM7、版本号 301；日常验证优先使用模拟设备脚本。

## OTA 协议约束

- 流程为查询 `/ota/status` → `/ota/begin` → 顺序发送 `/ota/chunk` → `/ota/end`。每个请求携带 `X-Ota-Core`，每次只升级一个核；客户端只接受不带路径、认证信息、查询串或片段的 HTTP 地址，并显式禁用代理。
- 状态是逐行 `key=value` 文本。开始上传要求 `busy=0`、`other_core_busy=0`、`flags=0x00` 且 `running` 为 0 或 1；当前对 `flags` 使用字符串精确匹配，调整解析时注意模拟设备响应。
- 目标槽是运行槽的另一槽。CM7 A/B 基址为 `0x08020000` / `0x080A0000`，CM4 A/B 为 `0x08120000` / `0x081A0000`。BIN 长度限制为 8～393216 字节，偏移 4 的小端复位向量必须带 Thumb 位且位于目标镜像范围内；不能用同一 BIN 随意切换槽位。
- CRC32 使用初值 `0xffffffff`、反射多项式 `0xedb88320`、最终取反；上传块最大 8192 字节。上传会话内遇网络错误、HTTP 408/5xx 后查询状态并重发匹配的 begin，以设备返回的 written 续传；1/2/4/5 秒退避，连续恢复失败 60 秒后暂停，时限在请求结束时检查，单请求超时 45 秒。初始状态查询失败不自动恢复。
- 本地使用 QSaveFile 在 begin 前及 end 前保存 `upload.json` 到用户本地应用数据目录，不逐块保存进度；恢复偏移以设备 begin 回包为准。关闭不 abort，重开提示恢复并校验原路径 BIN 的 SHA256、长度、CRC32 及目标槽/基址；已有任务时“开始升级”会先恢复该任务。会话失效时须确认擦除目标槽并从头上传，本地任务不保证设备掉电续传。
- 手动测试须用 `QT_OTA_TASK_FILE` 隔离任务文件；`--test-resume` 非交互恢复保存的任务，地址来自任务文件。`--test-upload` 遇已有任务也会优先恢复，可能忽略命令行 URL/BIN。禁止用测试模式操作真实设备。
- `/ota/end` 开始后禁用取消；结束失败只查询目标运行槽、版本和长度，不自动重传或发送 abort。网络失败保留任务，显式取消才通过 `/ota/abort` 清理会话。
- 上传成功只表示上传及 pending 保存成功，仍需设备复位后查询状态并核对实际业务；不能据此宣称新固件启动验证通过。
