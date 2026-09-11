# 本地工具链

- `arduino-cli.exe`：1.5.2-rc.1，用于统一命令行编译；来自Arduino官方下载地址。
- `esptool.exe`：5.1.0，来自本机ESP32 Arduino Core 3.3.7工具目录。

ESP32 Core仍使用本机Arduino数据目录中的 `esp32:esp32 3.3.7`，第三方库仍使用
Arduino 用户库目录（通常为 `%USERPROFILE%\Documents\Arduino\libraries`）。工具可执行文件由根目录 `.gitignore` 排除，
不提交到Git。

整理前的便携CLI压缩包已损坏，保存在 `archive/toolchain` 供问题追溯，不再使用。
