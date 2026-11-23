# AQMS-MCU 2.2.3.1 认证混合机型

以 `2.2.2.1` B 版为定点基线，源码差异为三处版本号。HJ212 上报使用 UART1，远程命令与 OTA 使用 UART2，传感器 RS‑485 使用 UART0，CN2 颗粒物使用软件串口。应用日志走原生 USB，保留 B 版启动映射和 HJ212 诊断。

## 实机验证范围

- `2.2.2.1` B 版基线从 2026-11-20 18:00 至 2026-11-23 10:07 的约 64 小时长测中，4G 模块 RX 的 2,377 帧均在云端原始接收中找到，逐字节一致且 CRC 合格。
- 2026-11-23 单台认证混合机型从 `2.2.2.1` B 版远程 OTA 到 `2.2.3.1` 成功。重启后版本、UART1 映射、SD 挂载和 LCD 恢复均已确认。后续 25 批实时数据写盘成功，4G RX 的 30 帧均通过 CRC。
- 本包状态为 `hardware-smoke-tested`。`2.2.3.1` OTA 后的云端到数仍待核对。

## 文件用途

- 应用 OTA 取 `firmware.bin`，大小 717088 字节。SHA-256 为 `D7D47F1F3DF5271DED07259D0124334D01C107407AB12A7C7DBE167AEDCBE61C`。
- 串口烧录时，同包的 `bootloader.bin`、`partitions.bin`、`boot_app0.bin` 和 `firmware.bin` 对应 `flash_args.txt` 中的四段地址。文件哈希见 `SHA256SUMS.txt`。
- `certified-2.2.3.1-source.zip` 保存本包 108 个构建输入的原始字节，逐项对应 `source-files.sha256`。源码指纹为 `26DD049F87791DA0DF31A916E51BC1C6D8315D63595295291D96ADDD92809A12`。

在 `firmware_workspace` 中用 ESP32 Arduino Core `3.3.7` 和显式 UART1 配置复建：

```powershell
.\build-certified.cmd -Hj212UartComparison uart1 -BuildPath .\build\verification\certified-2.2.3.1-rebuild
```
