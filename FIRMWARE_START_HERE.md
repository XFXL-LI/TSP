# TSP Firmware 从这里开始

当前唯一实际源码仍是：

`D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\Firmware_2.0.3_original\TSP`

后续状态查看、统一编译、发布包校验和受保护烧录全部从以下目录进入：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace`

优先阅读其中的 `README.md`。根目录旧 `TSP`、`worktrees` 以及
`firmware_workspace\archive` 中的内容都不是当前源码或烧录来源。

当前源码版本是Firmware 2.0.6，已通过完整编译但尚未烧录或进行真实OTA验证。
2.0.5已由用户完成约15小时22分钟主业务冒烟运行，但其OTA没有触发。最新构建位于：

`firmware_workspace\build\current`

默认可烧录版本仍是2026-08-10已验证的Firmware 2.0.4：

`firmware_workspace\releases\2.0.4\2026-08-10_hw-verified`

其应用BIN SHA-256为：

`6CE8B44EA4BF509815A7CE57470B1CC79CA84F85355BF6E4F2D1CB6EA13A4773`

未经明确授权不得烧录MCU。
