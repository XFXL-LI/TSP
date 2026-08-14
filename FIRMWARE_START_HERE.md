# TSP Firmware 从这里开始

首先阅读根目录的版本位置总表：

`D:\ChatGPT-Pro\TSP-ESP32-S3\FIRMWARE_VERSION_LOCATIONS.md`

使用Arduino IDE编译或选择烧录参数前，阅读：

`D:\ChatGPT-Pro\TSP-ESP32-S3\ARDUINO_IDE_2.0.4_SETTINGS.md`

当前继续开发的源码是：

`D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\Firmware_2.0.3_original\TSP`

后续状态查看、统一编译、发布包校验和受保护烧录全部从以下目录进入：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace`

优先阅读其中的 `README.md`。根目录旧 `TSP`、`worktrees` 以及
`firmware_workspace\archive` 中的内容都不是日常源码或烧录入口。

当前源码版本是Firmware 2.0.7，主板侧LCD OTA交互已实现，并完成一次完整远程OTA
实机测试：准备ACK、5%进度、校验、重启和2.0.7启动均成功。主板重启后发送了两次
`normal`，LCD未立即返回主页，约25分钟后由LCD自身超时机制恢复；该恢复确认仍待
后续完善。最新构建位于：

`firmware_workspace\build\current`

默认可烧录版本仍是2026-08-10已验证的Firmware 2.0.4：

`firmware_workspace\releases\2.0.4\2026-08-10_hw-verified`

对应的独立可编译2.0.4源码位于：

`firmware_workspace\sources\Firmware_2.0.4_hw-verified\TSP`

其应用BIN SHA-256为：

`6CE8B44EA4BF509815A7CE57470B1CC79CA84F85355BF6E4F2D1CB6EA13A4773`

Firmware 2.0.7主板与LCD对接协议：

`D:\ChatGPT-Pro\TSP-ESP32-S3\LCD_OTA_INTERACTION_PROTOCOL.md`

未经明确授权不得烧录MCU。
