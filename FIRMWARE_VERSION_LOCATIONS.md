# TSP 固件版本与目录说明

最后更新：2026-08-14

本文件用于区分“正式实机验证固件”“对应的可编译源码”和“当前开发源码”。
不要只根据目录名或 `TSP.ino` 文件名判断版本。

新安装Arduino IDE或重新选择开发板时，还必须阅读：

`D:\ChatGPT-Pro\TSP-ESP32-S3\ARDUINO_IDE_2.0.4_SETTINGS.md`

## Firmware 2.0.4：正式实机验证版本

正式烧录包目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\releases\2.0.4\2026-08-10_hw-verified`

真正完成过2026-08-10实机验证的应用固件是该目录中的 `firmware.bin`：

- 大小：623440字节；
- SHA-256：`6CE8B44EA4BF509815A7CE57470B1CC79CA84F85355BF6E4F2D1CB6EA13A4773`；
- 状态：`hardware-smoke-tested`；
- 默认正式回退版本仍为2.0.4。

该发布包必须使用四段地址烧录，不使用 merged BIN，不执行全片擦除。未经用户明确
授权不得烧录MCU。

## Firmware 2.0.4：独立可编译源码

独立源码目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\sources\Firmware_2.0.4_hw-verified\TSP`

Arduino IDE应打开：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\sources\Firmware_2.0.4_hw-verified\TSP\TSP.ino`

开发板必须选择 `ESP32S3 Dev Module`，ESP32 Core使用3.3.7，Flash Size选择16MB，
Partition Scheme选择 `16M Flash (3MB APP/9.9MB FATFS)`；完整菜单见上面的Arduino设置文档。

这份源码不是简单从历史 Git 提交复制，而是从2026-08-10正式构建保留的 Arduino
审计缓存恢复。审计缓存中的原始 `TSP.ino.bin` 与正式发布包 SHA-256 完全一致。

2026-08-12已从该独立目录重新完整编译：

- 编译通过；
- Sketch：623296字节；
- 全局变量：25936字节；
- 应用BIN：623440字节，与正式包大小一致；
- 新编译BIN与正式BIN只有70字节不同，差异位于编译时间、构建摘要和镜像末尾哈希；
- 恢复后源码指纹为`126A19A30A99EF661A974505E267E7B405F5833F8E35224F52984051958F76E3`，
  与旧发布清单记录的源码文件字节指纹不同；这是审计缓存恢复后源码文件格式字节的
  差异，最终BIN对比证明编译代码区一致；
- 新编译产物本身不能继承“实机验证”状态，正式回退仍应使用上面的固定发布包。

验证构建输出位于：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\verify-2.0.4-source`

历史 Git 提交 `b1231e9` 可以编译且包含2.0.4主要修复，但它不是生成正式623440字节
BIN的逐文件精确快照，因此不要把它当成正式包的唯一源码依据。

恢复源码已经上传GitHub：

- 分支：`codex/firmware-2.0.4-exact-restored-source`；
- 固定标签：`firmware-2.0.4-hw-verified-source`；
- 提交：`1725122`；
- 草稿PR：`https://github.com/XFXL-LI/TSP/pull/3`。

GitHub分支和标签保存源码及说明，不包含正式BIN。正式实机验证四段BIN仍以本地固定
发布包为准。

## Firmware 2.0.6：单任务 OTA 实机测试基线

Firmware 2.0.6 已完成现场 OTA 测试：

- 一次完整接收后镜像校验失败，旧固件继续运行且业务自动恢复；
- 一次完整 OTA 成功、自动重启并恢复业务；
- 第三次上传请求/Ready 握手未建立，因此主板没有进入 OTA；
- 2.0.6 尚未包含 LCD `ota_status` 状态协议和 OTA 期间 LCD 输入排空。

GitHub 开发分支：`codex/firmware-2.0.6-single-ota`；草稿 PR #2。

## Firmware 2.0.7：当前开发源码

当前继续开发的源码目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\Firmware_2.0.3_original\TSP`

状态：主板侧 LCD OTA 交互已实现，并于 2026-08-14 完成一次完整远程 OTA 实机
测试。准备 ACK、5% 进度、校验、重启和 2.0.7 启动均成功；主板重启后发送了两次
`normal`，LCD 未立即返回主页，约 25 分钟后由 LCD 自身超时机制恢复，因此 LCD
恢复确认仍待后续完善。它完整保留 2.0.4 的 8 月 10 日修复、2.0.5 业务优先级
保护和 2.0.6 单任务 OTA，并新增：

- `preparing/transferring/verifying/restarting/failed/normal` 状态 JSON；
- 2 秒非阻塞 LCD 准备 ACK；
- OTA 期间 LCD 输入持续排空和半包解析状态清理；
- 每跨过 5% 的进度通知；
- LCD/DTU 单行响应互斥发送。

最新应用 BIN：627072 字节；SHA-256：
`4F37E77B9F52B0A154CD2F94B20ABB71E1C55D3EB723D9911DB1DECD4B30BB86`。

源码输入指纹：
`AF23FBD1DE343752459A6144794599A428C499AC13E5CEAE6316E545E11E1596`。

构建目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\current`

LCD 对接协议：

`D:\ChatGPT-Pro\TSP-ESP32-S3\LCD_OTA_INTERACTION_PROTOCOL.md`

Firmware 2.0.7 改动在独立分支 `codex/firmware-2.0.7-lcd-ota` 中整理；默认正式
回退版本仍是 2.0.4 固定四段发布包。

## 使用规则

1. 需要恢复已验证固件：使用2.0.4固定四段发布包。
2. 需要查看或重新编译2.0.4：打开独立2.0.4源码目录中的 `TSP.ino`。
3. 需要继续开发：只修改当前2.0.7开发源码目录。
4. 不在2.0.4独立源码目录中继续开发；需要改动时先复制或建立新的Git分支。
5. 构建目录和归档目录均不作为日常源码入口。
