# HTTP OTA 会话内续传

## 协议与边界

连接中断、请求体未收完整、begin/chunk 响应丢失时保留当前 RAM 会话。重连后用相同的 `X-Ota-Core`、`X-Fw-Size`、`X-Fw-Version`、`X-Fw-Crc32`、`X-Fw-Base` 重发 `POST /ota/begin`，返回 `OTA resume written=N`；首次创建返回 `OTA ready written=0`。任一字段不匹配返回 409，不擦除原会话。

`written` 包含 Flash 已编程字节和 OTA 编程缓冲中的字节。HTTP 层收齐完整请求体后才提交数据：半块断线不推进偏移；完整块已接收但客户端未读响应时，以重发 begin 返回的偏移为准。后续 chunk 的 `X-Offset` 必须等于该值。

空闲上限仍为 60 秒，会话总上限仍为 10 分钟；只有成功写入数据块刷新空闲计时，状态查询和重复 begin 不续期。等待重连期间相机保持暂停，CM4 升级时继续保持 hold；超时、主动 abort、写入或最终校验失败会释放资源。pending 已保存后仍复位，不能续传或取消。掉电和复位会丢失 RAM 会话。

`ota_upload.ps1` 支持重跑同一命令取得偏移继续上传，异常时不再主动 abort；未实现自动重试。为部署到旧固件，首次空闲会话仍接受旧版 `OTA ready` 响应并从 0 上传。

## 已验证

环境：Windows PowerShell，设备 `192.168.10.2`，串口 `COM8`、115200 8-N-1。

- `make -s -C Makefile/CM7 all-slots -j8`：A/B 构建通过，链接器报告 RWX LOAD 段警告。
- `tests/http/uploader_test.ps1`：模拟首次从 0 上传及已有会话从 8192 续传，逐字节比较通过，CRC32 标准向量通过。
- `tests/http/resume_test.ps1`：CM7、CM4 均通过半块断线偏移为 0、五项镜像信息不匹配返回 409、完整块发送后不读取响应再查询得到 16384，正式上传脚本从 16384 继续并成功 end。
- CM7 续传镜像：A 槽，版本 9402，150876 字节，CRC32 `0x718F2A1C`；串口确认 Bootloader CRC PASS、confirmed=A。
- CM4 续传镜像：B 槽，版本 9403，23276 字节，CRC32 `0x76B618AC`；串口确认 CM4 B 启动及 confirmed=B。
- 两次续传升级后用户均确认画面和帧率正常。
- `tests/http/device_test.ps1`：错误偏移、超限请求后会话保留、半块断线、重复 begin、错误 CRC 不设置 pending 等检查通过。
- CM4 空闲接收会话最终自动清理为 busy=0，未设置 pending。该轮未精确记录 begin 到清理的完整时间，不能作为 60 秒阈值精度测试。

## 测试收尾

异常用例覆盖过空闲槽，已通过正常 OTA 用正常镜像覆盖：CM7 B 版本 9404、150876 字节、CRC32 `0x9B3E36DE`；CM4 A 版本 9405、23276 字节、CRC32 `0xDEDA1DEE`。最终 HTTP 查询两核 running 与 active 相同、flags=0、trial=0、busy=0；CM7 运行 B，CM4 运行 A。A/B 两槽均保留正常镜像。

收尾两次正常 OTA 复位后，用户确认当前画面与帧率正常。

“完整块响应未读”测试不等同于确定触发了服务端发送错误；begin 响应丢失分支、总计 10 分钟超时及物理断电未在本轮逐项注入。
