# Firmware 2.0.4 源码来源

本目录是从2026-08-10 16:33正式编译审计缓存恢复的独立可编译源码。

本地来源审计构建：

`firmware_workspace\archive\legacy-builds\current-source\.build_204_audit_20260810`

来源审计 BIN 与正式发布包 `firmware.bin` 的 SHA-256 均为：

`6CE8B44EA4BF509815A7CE57470B1CC79CA84F85355BF6E4F2D1CB6EA13A4773`

GitHub分支不包含构建缓存或固件BIN；上面的哈希用于绑定本地保存的正式发布包。

审计缓存中的少量历史中文注释已经发生编码显示异常，部分文件还保留原始行尾空白。
这些字节不影响业务逻辑，但清理后会改变ESP32镜像构建摘要并扩大与正式BIN的差异；
因此本精确恢复分支有意保留它们。根目录文档和`test.json`中的Arduino生成`#line`
前缀已经删除，因为它们不是原始项目内容，也不参与固件执行代码。

2026-08-12从本Git工作树重新完整编译通过：

- ESP32 Core：3.3.7；
- Sketch：623296字节；
- 全局变量：25936字节；
- 应用BIN：623440字节；
- 本次构建BIN SHA-256：
  `B4CCFB1A53E31240F5009BC9715451679C4B3FFBC525F9D5958221D5161CA7AA`；
- 源码输入指纹：
  `C2B5FABF165AD00BBEA137CE3AB7DD9E949108C2462006F7773569859CF283C2`。

重新编译BIN与正式BIN仅有70字节构建元数据差异，代码区一致。恢复后源码文件字节
指纹与旧发布清单记录值不同，这是构建缓存恢复后的文件格式字节差异；因此重新编译
产物不自动继承实机验证状态。需要稳定回退时，应使用本地
`firmware_workspace\releases\2.0.4\2026-08-10_hw-verified` 中的固定四段发布包。

本目录用于查看和重新编译2.0.4，不作为2.0.6或后续版本的开发目录。

打开Arduino IDE前必须按根目录 `ARDUINO_IDE_2.0.4_SETTINGS.md` 重新选择ESP32 Core
3.3.7、ESP32S3 Dev Module、16MB Flash和3MB APP/9.9MB FATFS分区等完整参数。
