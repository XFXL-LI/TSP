# Firmware 2.0.21 通用版

状态：**禁止使用。**

2026-09-04现场日志确认，本包的HJ212发送流程会因完整帧QN起始位置识别错误而产生
`request_identity_missing`，报文在写入串口前退出，平台收不到数据。

请使用同版本号的替代包：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\releases\2.0.21\2026-09-04_hj212-send-hotfix_compiled-not-hardware-verified`

本目录是通用原始气体数据版，不包含O3/NO2/SO2的500 ppb认证封顶。完整变更见项目根目录`CHANGELOG_2.0.21.md`。

本目录只用于问题追溯，不得用于烧录或OTA。
