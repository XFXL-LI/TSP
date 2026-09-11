# Firmware 2.0.21.1 认证限制版

状态：**禁止使用。**

2026-09-04现场日志确认，本包的HJ212发送流程会因完整帧QN起始位置识别错误而产生
`request_identity_missing`，报文在写入串口前退出，平台收不到数据。

请使用同版本号的替代包：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\releases\2.0.21.1\2026-09-04_hj212-send-hotfix_compiled-not-hardware-verified`

本目录同步2.0.21公共优化，并保留O3/NO2/SO2最高500 ppb、CO不封顶和独立认证校准配置。完整变更见项目根目录`CHANGELOG_2.0.21.1.md`。

本目录只用于问题追溯，不得用于烧录或OTA。
