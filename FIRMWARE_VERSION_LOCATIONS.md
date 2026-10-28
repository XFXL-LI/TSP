# TSP 固件版本与目录说明

最后更新：2026-10-23

本文件用于区分“正式实机验证固件”“对应的可编译源码”和“当前开发源码”。
不要只根据目录名或 `TSP.ino` 文件名判断版本。

## 当前版本总览

| Variant | 版本 | 当前正式源码 | 构建输入 | 源码指纹 |
|---|---:|---|---:|---|
| standard | 2.1.7 | `firmware/standard/TSP` | 104 | `2B481FE81D0ECEA02FEA1F81DA523DF422157CCC622A267113D71A2FE32D321A` |
| certified | 2.1.7.1 | `firmware/certified/TSP` | 105 | `4894E25D48D29E62BE4D30EEDF1A393F9AEC563B3BD36926F5CB4D952F59F5B2` |

当前构建入口为 `firmware_workspace/build-standard.cmd`、
`firmware_workspace/build-certified.cmd`和兼容standard的
`firmware_workspace/build-current.cmd`。状态入口为
`status-standard.cmd`、`status-certified.cmd`和默认standard的`status.cmd`。

此前两版迁移基线得到以下验证结论（不是本次新代码的二进制等价结论）：

`MIGRATION BUILD VERIFIED - EXPECTED NONDETERMINISTIC METADATA ONLY`

该结论不是bit-identical；新旧BIN的差异已由编译时间及其派生镜像元数据完整解释。

当前2.1.7/2.1.7.1恢复常规流程的版本已编译通过，均为 `compiled-not-hardware-verified`；
实验版不随本次更新。新构建不覆盖已有INFO测试目录，也未生成新的正式Release。

当前最新已发布的2026-09-07正式Release分别为：

- standard：`firmware_workspace/releases/2.0.22/20260907-gas-pacing-diagnostics`
- certified：`firmware_workspace/releases/2.0.22.1/20260907-gas-pacing-diagnostics-certified`

两包状态均为`packaged-not-hardware-verified`，尚未取得现场硬件验证状态。

当前正式四段烧录地址统一为：

```text
0x000000  bootloader.bin
0x008000  partitions.bin
0x00E000  boot_app0.bin
0x010000  firmware.bin
```

不使用merged BIN替代正式四段包，不执行全片擦除，不写FFat分区。

新安装Arduino IDE或重新选择开发板时，还必须阅读：

[Arduino IDE 2.0.4阶段设置记录](docs/archive/guides/ARDUINO_IDE_2.0.4_SETTINGS.md)

## Firmware 2.0.4：正式实机验证版本

正式烧录包目录：

`firmware_workspace/releases/2.0.4/2026-08-10_hw-verified`

真正完成过2026-08-10实机验证的应用固件是该目录中的 `firmware.bin`：

- 大小：623440字节；
- SHA-256：`6CE8B44EA4BF509815A7CE57470B1CC79CA84F85355BF6E4F2D1CB6EA13A4773`；
- 状态：`hardware-smoke-tested`；
- 默认正式回退版本仍为2.0.4。

该发布包必须使用四段地址烧录，不使用 merged BIN，不执行全片擦除。未经用户明确
授权不得烧录MCU。

## Firmware 2.0.4：独立可编译源码

历史源码已移至仓库外独立历史归档：

`sources/firmware-2.0.4-hw-verified/TSP`

需要追溯时，在独立历史归档中打开：

`sources/firmware-2.0.4-hw-verified/TSP/TSP.ino`

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

`firmware_workspace/build/verify-2.0.4-source`

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

## Firmware 2.0.7：LCD OTA 主流程实机测试基线

2.0.7 精确源码已保存在下述 GitHub 分支；本地当前开发目录已继续升级到2.0.8，
不能再把该目录当作2.0.7源码快照。

状态：主板侧 LCD OTA 交互已实现，并于 2026-08-14 完成一次完整远程 OTA 实机
测试。准备 ACK、5% 进度、校验、重启和 2.0.7 启动均成功；主板重启后发送了两次
`normal`，LCD 未立即返回主页，约 25 分钟后由 LCD 自身超时机制恢复。它完整保留
2.0.4 的 8 月 10 日修复、2.0.5 业务优先级保护和 2.0.6 单任务 OTA，并新增：

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

`firmware_workspace/build/current`

LCD 对接协议：

[LCD OTA交互协议](docs/protocols/lcd/LCD_OTA_INTERACTION_PROTOCOL.md)

GitHub 分支：`codex/firmware-2.0.7-lcd-ota`；提交：`e2c079f`；草稿 PR #4。

## Firmware 2.0.8：LCD normal ACK实机联调基线

Firmware 2.0.8 在 2.0.7 实机测试基线上增加 LCD 恢复确认：

- 初始化完成后立即发送第一条 `session=0,state=normal`；
- 未收到 ACK 时每 2 秒非阻塞重发，最多 30 秒；
- 接受 LCD V1.0.12 的 `state=normal,session=0,code=OK` ACK 后立即停止；
- 超时只记录警告，不判定为 OTA 失败；
- OTA 失败恢复也使用同一套 `normal` ACK；
- 新 OTA 开始时取消旧的 `normal` 重试。

完整编译通过：应用 BIN 627872 字节；SHA-256：
`90E066D25798BBB76752F74E156CAF18E2EE4F58A441D31D9343FD7020FD8DB1`。

源码输入指纹：
`AEB27CA06DEF242E68B1C191F0D8A8CC75CA630AC64D6C9512AED8717396EE7D`。

构建目录：

`firmware_workspace/build/firmware-2.0.8-normal-ack`

2026-08-14实机结果：627872字节远程OTA完成、镜像校验成功、自动重启并启动
Firmware 2.0.8。重启后首条`normal`未获ACK，第二条按2秒机制重发后收到LCD
V1.0.12 ACK；约3.09秒后恢复`get_data`。日志中无normal ACK timeout、UART
overflow、JSON接收错误或OTA失败。

GitHub分支：`codex/firmware-2.0.8-normal-ack`；提交：`4869c6f`；草稿PR #5。

## Firmware 2.0.9：Remote DTU协议开发基线

该版本当时继续开发的历史源码位置为：

迁移前 `tmp` 副本，现由迁移安全备份保存。

Firmware 2.0.9在2.0.8实机基线上增加Remote DTU协议版本1：

- 请求明确携带整数`protocol=1`时返回`upload_status` JSON；
- 非零Remote会话与LCD会话独立；
- 状态覆盖`accepted/ready/transferring/verifying/success/failed/rejected`；
- `ready`返回800字节分包、1000ms间隔和90000ms无活动超时；
- `transferring`按5%输出总体写入进度；
- 请求不带`protocol`时保留2.0.8旧英文legacy流程；
- 不新增任务或动态大型JSON缓冲，DEBUG和冻结约束保持。

完整编译通过：应用BIN 630864字节；SHA-256：
`F202BA3A5784B936F2D089410BED1AA019276F36CFFEB6A3616AF16B2F3B6849`。

源码输入指纹：
`3E7ED8C9EA903B3292C8E8E8189CBCECADEB0AAC97C6474F3FEAC48958CFCD40`。

构建目录：

`firmware_workspace/build/firmware-2.0.9-upload-status`

状态：`compiled-not-hardware-verified`，尚未烧录或与新版发送端实机联调。默认正式
回退版本仍为Firmware 2.0.4。

## Firmware 2.0.10：气体功能首次上机版本

该版本当时继续开发的历史源码位置仍为：

迁移前 `tmp` 副本，现由迁移安全备份保存。

Firmware 2.0.10在2.0.9源码上增加K-7S四气体处理：

- O3/NO2/CO/SO2一次读取`0x6000/0x6001`，只在失败时最多重试3次；
- 按预热、保护、故障、超量程和报警状态决定数据有效性；
- 内部、SD和统计统一保留ppb基准原始值；
- 四气体支持`ppb/ppm/ug/m3/mg/m3`，沿用现有`unit`字段；
- LCD实时/历史、LED和HJ212按配置单位换算；
- 默认O3/NO2/SO2为`ug/m3`、CO为`mg/m3`；
- 校准、小时统计、HJ212 3000ms间隔、SHT30及两套OTA协议保持不变。

完整编译通过：应用BIN 636912字节；SHA-256：
`25EEEEF057BE8EBE55262B008775826EBD12882AB167F43E98ED0CD13CE64E82`。

源码输入指纹：
`027C2B0DB30CE8608B9BCFB1AE73F26A9EA4C3D1CA3CDCDC65FA8B411F498851`。

构建目录：

`firmware_workspace/build/firmware-2.0.10-gas-units`

用户随后自行烧录2.0.10。首次LCD联调发现：下发完整14因子
`set_config/sensors`时权限层首次JSON解析成功，但ConfigManager二次解析失败，旧
代码又没有发布失败响应，最终LCD收到`Response timeout`，配置未保存。该问题由
2.0.11继续修复。气体数值、LCD/LED/HJ212完整联调结论仍待确认。默认正式回退版本
仍为Firmware 2.0.4。

## Firmware 2.0.11：禁止使用的启动重启版本

该版本当时继续开发的历史源码位置仍为：

迁移前 `tmp` 副本，现由迁移安全备份保存。

Firmware 2.0.11在2.0.10基础上修复LCD完整14因子配置链路：

- LCD EspSoftwareSerial RX缓冲由1024提高到2048字节，其他软件串口不变；
- 权限层首次解析取得operation后先释放cJSON树，再转交ConfigManager二次解析；
- `set_config`所有错误分支立即发布明确`NG`响应，不再等待2秒超时；
- 失败诊断包含报文长度、解析错误偏移、空闲堆和最大连续堆块；
- 成功响应中的`config`准确返回`sensors`，不再带前导空格；
- 2.0.10气体采集、单位换算、校准保留策略及全部冻结约束不变。

完整编译通过：应用BIN 638144字节；SHA-256：
`7C423DF2A4287185FDD2D2CD80D02431EE7E674EC91B3BAECF8926B05DE5C3C7`。

源码输入指纹：
`10E4DE462513E7CFD02F15E8B1F52BF33FE911AB89B443A71B06D59FFCF4C24C`。

构建目录：

`firmware_workspace/build/firmware-2.0.11-lcd-config`

用户烧录后设备启动即重启。EspSoftwareSerial的2048字节RX参数会联动生成约80KB
ISR边沿缓冲，比原配置额外占用约41984字节动态堆并要求大块连续分配。因此状态已
改为`rejected-hardware-boot-loop`，构建目录已放置`DO_NOT_FLASH.md`，禁止使用。
后继修正版为2.0.12。

## Firmware 2.0.12：HJ212内存问题实机复现基线

2.0.12恢复2.0.10完全相同的`sw->begin(..., false, 1024)`软件串口设置，不再扩大
LCD RX及其关联ISR缓冲；同时保留以下14因子配置修复：

- 权限层首次解析取得operation后立即释放cJSON树，再进入ConfigManager二次解析；
- `set_config`所有错误分支立即发布明确NG响应；
- 保留请求长度、错误偏移、空闲堆、最大连续堆块和UART overflow诊断；
- 2.0.10气体采集、单位换算及全部冻结约束不变。

完整编译通过：Sketch 637976字节，全局变量26816字节，应用BIN 638128字节；
SHA-256：
`C061BC1485809BEE658B7DCF956BF0BDBAEA522D3A6A7B216C59488CE5517CF9`。

源码输入指纹：
`BC8650DD281604D7110ED9A569E21DD3C055101E21A5DE23FF6FD9C34045CAC6`。

构建目录：

`firmware_workspace/build/firmware-2.0.12-lcd-config-memory-safe`

状态：`compiled-not-hardware-verified`，尚未烧录。首次实机验证应先确认不再启动重启，
再下发完整14因子配置、核对OK响应和重启后的14个采集器。详细内容见
[Firmware 2.0.12历史变更记录](docs/archive/changelog/CHANGELOG_2.0.12.md)。

用户后续已运行2.0.12。2026-08-20日志确认设备可持续采集和保存，但清空旧pending后，
约1210～1217字节的14因子十分钟包及小时包仍会因瞬时堆峰值在校验阶段失败；完整
14因子`set_config`在碎片堆下也仍可能序列化或解析失败。

## Firmware 2.0.13：HJ212内存修复实机验证版本

2.0.13只针对HJ212构包峰值实施第一阶段最小修复：

- 2017/2025最终报文改为同一个`String`原地构造；
- 原地回填四位长度，CRC直接覆盖同一缓冲区中的正文；
- `isValidPacket()`按下标校验，不再创建任何`substring/String`副本；
- 小时统计、3000ms逐包间隔、SHT30、DEBUG、配置接口及两套OTA均保持不变。

完整编译通过：Sketch 637604字节，全局变量26816字节，应用BIN 637760字节；
SHA-256：
`5B54511F84FB55E5A1EFDA024A3156AEFFB835EA86E9BAA3D0F740EE848E9CF2`。

源码输入指纹：
`D628E81BE814EEE854A7EE5A06AD49B57AB9993CE64F5EC9ED1A027B9C55607C`。

构建目录：

`firmware_workspace/build/firmware-2.0.13-hj212-memory-safe`

2026-08-21完成约16小时16分钟实机验证：976轮14因子采集、97个十分钟包、16个
小时包和1个日包连续运行，1090个HJ212报文全部构建成功，`HJ_BUILD_FAIL=0`，无
重启、panic、watchdog或brownout。2.0.13的HJ212内存修复判定实机验证通过。
Codex未烧录MCU。详细内容见
[Firmware 2.0.13历史变更记录](docs/archive/changelog/CHANGELOG_2.0.13.md)。

## Firmware 2.0.14：历史开发构建

2.0.14在2.0.13实机验证基线上升级传感器配置接口：

- 保留现有Remote/LCD旧全量`get_config/set_config sensors`语义和响应结构；
- 新增`view=selection`和`mode=selection`，只用因子ID查询、保存开关；
- 新增最多4因子的`ids`局部查询和`mode=merge`局部修改；
- 根据最终启用因子动态计算颗粒物、扬尘、空气微站或TVOC版本；
- FFAT新增`/sensorCatalog.json`，关闭因子时保留单位和报警配置；
- 大JSON在权限层使用移动传递；旧全量保存使用任务栈序列化缓冲，并在创建完整
  兼容响应前释放请求cJSON树；
- HJ212、小时统计、3000ms逐包间隔、SHT30、DEBUG和两套OTA协议保持不变。

完整编译通过：Sketch 648592字节，全局变量26816字节，应用BIN 648736字节；
SHA-256：
`376DC82371E709350D779901C2D040883665A0C13E41D8EAA17A70FCBB04E9C6`。

源码输入指纹：
`1BDAD06A40F12E29DFF01EBDCFB7456E3C625EAC37075E035DC5E1789D0156FF`。

构建目录：

`firmware_workspace/build/firmware-2.0.14-sensor-config-partial`

状态：`compiled-not-hardware-verified`。Codex未烧录MCU。详细内容见
[Firmware 2.0.14历史变更记录](docs/archive/changelog/CHANGELOG_2.0.14.md)和根目录
[LCD传感器配置协议](docs/protocols/lcd/LCD_SENSOR_CONFIG_PROTOCOL.md)。

## Firmware 2.0.15：历史四气体校准开发版本

该版本当时继续开发的历史源码位置为：

迁移前 `tmp` 副本，现由迁移安全备份保存。

2.0.15在2.0.14及pending维护改动基线上实现K-7S四气体异步两点校准：

- O3/NO2/CO/SO2日常只读`0x6001`，不再以`0x6000`门控有效性；
- 零点写`0x1000`、量程点写`0x1001`和FFAT本地目标，轮询`0x6006`且只有
  `0x0001`成功；
- 最近20秒10样本、±20%量程点防误操作和5%极差稳定提示；
- 正常60秒485采集优先，校准期间四气体维护数据不进入业务链；
- 管理员异步`gas_calibration`接口，禁用旧`gal_data`四气体路径，颗粒物兼容；
- 清洗后显式`finish`恢复，不重启MCU。

完整编译通过：Sketch 663212字节，全局变量26976字节，应用BIN 663360字节；
SHA-256：`F65E5C1686CDCA83FEC44EB33DF9D831F73F2B53FAF8B27725386C83860C0E9D`；
源码输入指纹：`2A38194BC7E08418DF7136F4854BEDE32E3D2293B2FB62B524EC1FB0065ED5ED`。

构建目录：

`firmware_workspace/build/firmware-2.0.15-gas-calibration`

状态：`compiled-not-hardware-verified`。Codex未烧录MCU。LCD接口见
[LCD四气体校准协议](docs/protocols/lcd/LCD_GAS_CALIBRATION_PROTOCOL.md)，历史实机验证清单见
[MCU气体校准升级清单](docs/archive/development/MCU_GAS_CALIBRATION_UPGRADE_CHECKLIST.md)，
代码变更见
[Firmware 2.0.15历史变更记录](docs/archive/changelog/CHANGELOG_2.0.15.md)。

## Firmware 2.0.16：通用正式版本

当前开发目录中的2.0.16已完成颗粒物校准成功/失败路径和HJ212 Flag 4/5第一轮
实机验证，暂定为正常传感器通用正式版本。它继续使用FFat
`/gasCalibration.json`，O3、NO2、SO2量程点目标为500 ppb，CO为5000 ppb；普通
四气体数据不做认证项目封顶。详细记录见
[Firmware 2.0.16历史变更记录](docs/archive/changelog/CHANGELOG_2.0.16.md)。

## Firmware 2.0.17：气体认证特定版历史开发版本

2.0.17以2.0.16为基线，增加认证项目特定气体策略：O3、NO2、SO2业务及校准读数
封顶500 ppb，认证量程目标250 ppb，CO保持不封顶和5000 ppb目标。2.0.17使用独立
FFat`/gasCalibrationCertified.json`；不接触2.0.16的`/gasCalibration.json`，因此
固件降级不会把通用版目标污染成250 ppb。

完整编译通过，应用BIN 664832字节，SHA-256：
`C8F549BE80F666CBB25D0CA081B61E705FEDB7292C0586C4679632435358D9ED`。
构建目录：

`firmware_workspace/build/firmware-2.0.17-certified-gas-20260827`

状态：`compiled-not-hardware-verified`。完整记录见
[Firmware 2.0.17历史变更记录](docs/archive/changelog/CHANGELOG_2.0.17.md)。

2026-08-28该阶段源码已在不提升2.0.17版本号的前提下增加公共`systemInfo`运行版本
响应覆盖，并关闭DEBUG。该源码已完成干净编译：应用BIN 665424字节，SHA-256：
`BD23D44CC864E0A2260C9268C84BC7FDA2465F8D4473A36C04E4FC8FCCEE8393`；源码输入
指纹：`7C07D14414AD1D41162B80678E97FD286F31338600728C8A7F12E0CB31C2B84A`。构建目录：

`firmware_workspace/build/firmware-2.0.17-certified-gas-systeminfo-20260828`

状态：`compiled-not-hardware-verified`，未烧录。上述664832字节、`C8F549...`的旧BIN
属于2026-08-27 DEBUG开启构建，不包含最新`systemInfo`修复，只作为历史记录保留。

## Firmware 2.0.18：历史通用优化版开发源码

该版本当时继续开发的历史源码位置为：

迁移前 `tmp` 副本，现由迁移安全备份保存。

2.0.18保留2.0.17后续传感器读取优化和通用修复，但移除认证项目专用气体设计：

- O3、NO2、SO2日常采集和校准维护读数不再封顶；
- 使用通用`/gasCalibration.json`；
- O3、NO2、SO2量程目标恢复500 ppb，CO继续5000 ppb；
- 不读取或改写2.0.17的`/gasCalibrationCertified.json`；
- 500 ms响应超时、最多3次尝试、300 ms重试/总线间隔等读取优化完整保留；
- 小时统计、HJ212逐包3000 ms、SHT30独立、DEBUG开启和Flag默认5保持不变。

完整编译通过：应用BIN 663696字节，SHA-256：
`7BCE11150DB0CC06F7FF41636C6EDA8FBE763C6972DCC03733A7626B2B3289CA`；源码输入
指纹：`A9FD364B5D330944BE76334050A6B88E869B7FA501B287665945B5D0585E00DD`。构建目录：

`firmware_workspace/build/firmware-2.0.18-general-sensor-read-optimization-20260901`

状态：`compiled-not-hardware-verified`，未烧录、未擦除Flash、未操作设备FFat。完整记录见
[Firmware 2.0.18历史变更记录](docs/archive/changelog/CHANGELOG_2.0.18.md)。

## Firmware 2.0.19：历史HJ212-2017报文修正版开发源码

该版本当时继续开发的历史源码位置为：

迁移前 `tmp` 副本，现由迁移安全备份保存。

2.0.19以2.0.18通用优化版为基线，修正HJ212-2017报文：

- CRC使用HJ 212-2017附录A算法，标准测试向量为`1C80`；
- 非累计监测因子的分钟、小时和日数据不再生成`Cou`；
- 数字使用无字段宽度格式化，修复气体`ppb`零值和一位整数的前导空格；
- CP最后一个字段后不再生成分号；
- 内部`w34011`和`L90`在HJ212出口映射为`a05024`和`LA`；
- 噪声覆盖`CN=2011/2051/2061/2031`；
- 保留四种气体单位配置，不增加分包和1024/950运行时硬拦截；
- 不修改小时统计，不自动改写设备FFat。

完整编译通过：Sketch 664020字节，全局变量26976字节，应用BIN 664176字节；
应用BIN SHA-256：`68E41F6EF212B36DB08E8FD71E48D6320752248B3B754E312C5DA56DF4A81918`；
源码输入指纹：`6496E0F42D93CBB023F9E79165D9653578798080E5D621C64C94FA010B44698F`。
构建目录：

`firmware_workspace/build/firmware-2.0.19-hj212-2017-compliance-20260902`

状态：`compiled-not-hardware-verified`，未烧录、未擦除Flash、未操作设备FFat。完整记录见
[Firmware 2.0.19历史变更记录](docs/archive/changelog/CHANGELOG_2.0.19.md)及
[HJ212-2017当前报文关键修改清单](docs/protocols/hj212/空气微站_HJ212-2017当前报文关键修改清单.md)。

## Firmware 2.0.20：历史补传可靠性优化版开发源码

该版本当时继续开发的历史源码位置为：

迁移前 `tmp` 副本，现由迁移安全备份保存。

2.0.20以2.0.19为基线优化HJ212断点补传：

- 待补候选使用有界循环游标，失败包不再长期阻塞后续包；
- 固定8项RAM状态记录失败，连续3次无ACK后冷却30分钟；
- 每5分钟恢复周期仍最多1次补传发送，逐包3000 ms保持不变；
- `ST/PW/MN/Flag`变化时，轮到旧包才从SD原始记录按当前配置重建；
- 待补完整包改用临时文件写入、flush/fsync、关闭重读校验和重命名提交；
- 修复LCD HJ212配置名前导空格，不再启动即创建空`/sdcard/history`；
- 小时统计、传感器读取优化、SHT30独立、DEBUG开启和通用气体策略不变。

完整编译通过：Sketch 667512字节，全局变量27272字节，应用BIN 667664字节；
应用BIN SHA-256：`654305821DAF6EE3CE8FC040631011C073B394516C2DC3E6D05033A002F300D6`；
源码输入指纹：`B5D0E0791CD52BABCD78A6B651BAAF5A7C84BB6AE883749C89D43247C862C0E1`。
构建目录：

`firmware_workspace/build/firmware-2.0.20-pending-recovery-20260902`

状态：`compiled-not-hardware-verified`，未烧录、未擦除Flash、未操作设备FFat。完整记录见
[Firmware 2.0.20历史变更记录](docs/archive/changelog/CHANGELOG_2.0.20.md)。

## Firmware 2.0.21：历史HJ212加固版本

该版本当时使用的通用源码（historical source location）为：

迁移前 `tmp` 副本，现由迁移安全备份保存。

2.0.21在2.0.20基础上完成真实因子状态、9014应答分级校验、毫秒级唯一QN、
`timeout/retry_times`配置生效和pending双路径重写诊断。气体日常采集从`0x6000`
起一次读取状态与浓度两个寄存器，事务次数不增加；小时有效值取样逻辑、HJ212逐包
3000 ms、SHT30独立和DEBUG开启保持不变。

2026-09-04根据现场日志修复完整帧QN起始位置未被识别、导致发送前出现
`request_identity_missing`的问题，版本号保持2.0.21不变。最新完整编译通过：
应用BIN 675008字节；SHA-256：
`43ED4CBF4A65BBE1929E6C66F5A30930E071B2ADB471B16B0E38E10F2CF8FF5C`。
发布包位于：

`firmware_workspace/releases/2.0.21/2026-09-04_hj212-send-hotfix_compiled-not-hardware-verified`

## Firmware 2.0.21.1：历史同步认证限制版

该版本当时使用的认证源码（historical source location）为：

迁移前 `tmp` 认证副本，现由迁移安全备份保存。

2.0.21.1完整同步2.0.21公共优化，仅额外保留O3/NO2/SO2最高500 ppb、CO不封顶、
独立`/gasCalibrationCertified.json`和250 ppb认证量程目标。已同步2026-09-04发送热修复，
版本号保持2.0.21.1不变。应用BIN 675488字节；SHA-256：
`57C61954F820F160C819295160F17C00D865FF3C82EE79E0631F369C4C1BA048`。
发布包位于：

`firmware_workspace/releases/2.0.21.1/2026-09-04_hj212-send-hotfix_compiled-not-hardware-verified`

两个版本均尚未实机验证，Codex未执行烧录、擦除或设备FFat操作。详细记录见
[Firmware 2.0.21历史变更记录](docs/archive/changelog/CHANGELOG_2.0.21.md)与
[Firmware 2.0.21.1历史变更记录](docs/archive/changelog/CHANGELOG_2.0.21.1.md)。

两个2026-09-03发布包存在QN解析阻断发送的问题，已在包内标记`DO_NOT_USE.md`，不得再用。

## Firmware 2.0.22：最新已发布standard Release的源码基线

历史源码状态已由当前`firmware/standard/TSP`继续演进。

- 构建输入：88；
- 源码指纹：`84CD14958E077AE1FE4294C4C566F8CF39FE89952C5CB929301AF2CF4C553213`；
- 2026-09-07应用BIN：678640字节；
- 应用BIN SHA-256：`A8ECE4A080D82D944468C601513B15D26FBB835A7EC554B4F235081BC1DA6A60`；
- 9月7日历史构建：`firmware_workspace/build/firmware-2.0.22-gas-pacing-diagnostics-20260907`；
- 正式Release：`firmware_workspace/releases/2.0.22/20260907-gas-pacing-diagnostics`；
- Release状态：`packaged-not-hardware-verified`。

迁移测试构建位于
`firmware_workspace/build/firmware-2.0.22-repo-migration-test-20260910`。重新构建BIN
因ESP32 Core 3.3.7编译时间和派生摘要而不与9月7日BIN bit-identical，但业务代码布局、
配置、分区及入口保持一致，结论为
`MIGRATION BUILD VERIFIED - EXPECTED NONDETERMINISTIC METADATA ONLY`。

2.0.22在2.0.21基础上实现HJ212实时队列优先、pending空闲调度及写入保护，并在
2026-09-07同版本中加入气体200 ms统一节流和轻量运行诊断。

## Firmware 2.0.22.1：最新已发布certified Release的源码基线

历史源码状态已由当前`firmware/certified/TSP`继续演进。

- 构建输入：89；
- 源码指纹：`540EE286955E50B552882A8F7639E858DBC724DFECFC9B0571E0AC5D0A128779`；
- 2026-09-07应用BIN：679136字节；
- 应用BIN SHA-256：`D9300FCCB31B5A0C44BEFEC3A37E0F6411C97F8C870429E184BECFE3A52E9EFE`；
- 9月7日历史构建：`firmware_workspace/build/firmware-2.0.22.1-certified-gas-pacing-diagnostics-20260907`；
- 正式Release：`firmware_workspace/releases/2.0.22.1/20260907-gas-pacing-diagnostics-certified`；
- Release状态：`packaged-not-hardware-verified`。

迁移测试构建位于
`firmware_workspace/build/firmware-2.0.22.1-certified-repo-migration-test-20260910`，验证
结论同样为`MIGRATION BUILD VERIFIED - EXPECTED NONDETERMINISTIC METADATA ONLY`。

2.0.22.1完整同步2.0.22公共优化，只额外保留O3/NO2/SO2最高500 ppb、CO不封顶、
独立`/gasCalibrationCertified.json`、`certified_250ppb_v1`和250/250/5000/250 ppb
校准目标。

两个当前Release均已通过文件级和静态校验，但尚未取得现场硬件验证状态。Codex未在
本轮治理中烧录、擦除MCU或操作设备FFat。详细记录见
[Firmware 2.0.22变更记录](docs/changelog/CHANGELOG_2.0.22.md)与
[Firmware 2.0.22.1变更记录](docs/changelog/CHANGELOG_2.0.22.1.md)。

## Firmware 2.0.23：上一版standard编译基线

当前正式源码：`firmware/standard/TSP`

- 构建输入：88；
- 源码指纹：`A97AA91FD298EAA31D3ED2051C1A19FAEB210A98284533267508A947D644C3E9`；
- 应用BIN：680192字节；
- 应用BIN SHA-256：`C10B8B98B052A11C90B1CB0E5DD1DCC9C0831E361590712444642B3A749A486A`；
- 构建目录：`firmware_workspace/build/firmware-2.0.23-pending-sector-write-hardening-20260914`；
- 状态：`compiled-not-hardware-verified`。

2.0.23只处理pending完整HJ212报文的SD写入加固。首次使用`.tmp1`执行stdio写入并关闭
重开校验；失败后保留`.tmp1`，使用独立`.tmp2`和256字节POSIX分块写入，再关闭重开
校验。只有验证成功的临时文件才重命名为最终`.pkt`。两次均失败时继续使用原有rebuild
marker和raw重建兜底。详细记录见
[Firmware 2.0.23变更记录](docs/changelog/CHANGELOG_2.0.23.md)。

## Firmware 2.0.23.1：上一版certified编译基线

当前正式源码：`firmware/certified/TSP`

- 构建输入：89；
- 源码指纹：`090B6AD564ED66941D9DA60EF1ABB1626113E5837A9A77C736A23CAC4A3CAA6B`；
- 应用BIN：680672字节；
- 应用BIN SHA-256：`58F2506A295B2FB3853321BC80E63028C911BE3AA7E29B9884CB0747425A51C5`；
- 构建目录：`firmware_workspace/build/firmware-2.0.23.1-certified-pending-sector-write-hardening-20260914`；
- 状态：`compiled-not-hardware-verified`。

2.0.23.1同步2.0.23的公共pending写入加固，认证专用气体封顶和校准策略未修改。详细
记录见[Firmware 2.0.23.1变更记录](docs/changelog/CHANGELOG_2.0.23.1.md)。

## Firmware 2.1.0：上一版standard编译基线

当前正式源码：`firmware/standard/TSP`

- 构建输入：88；
- 源码指纹：`8D4DE1EA9A501390D4DF906B4B75988C2DAB424594704B9E648074B9E1F7991F`；
- 应用BIN：683744字节；
- 应用BIN SHA-256：`F904DF6E3352453CF639478F6EC3791245014600DC88932EAE85528C1A91833C`；
- 当前构建目录：`firmware_workspace/build/current-standard`；
- 状态：`compiled-not-hardware-verified`。

2.1.0固定120秒实时批次和70秒气泵运行时间。其他传感器从第45秒读取；颗粒物和气体
在持续通气60秒后于气泵仍运行的最后10秒内读取。颗粒物使用一次Modbus请求读取四个
通道；小时统计纳入每一个有效实时批次。详细记录见
[Firmware 2.1.0变更记录](docs/changelog/CHANGELOG_2.1.0.md)。

## Firmware 2.1.0.1：上一版certified编译基线

当前正式源码：`firmware/certified/TSP`

- 构建输入：89；
- 源码指纹：`7612C96BE98E74AC6182F8F9D96E3C4BE836DDACC9F87001418DDBA5A06777F0`；
- 应用BIN：684208字节；
- 应用BIN SHA-256：`D959F8C5CF179C0090BFD4BC002ADF082E30FFD585717730F6136780D9605A39`；
- 当前构建目录：`firmware_workspace/build/current-certified`；
- 状态：`compiled-not-hardware-verified`。

2.1.0.1同步2.1.0的公共时序、颗粒物批量读取、配置时间和统计逻辑，同时保留认证版
O3/NO2/SO2封顶及独立校准策略。详细记录见
[Firmware 2.1.0.1变更记录](docs/changelog/CHANGELOG_2.1.0.1.md)。

## Firmware 2.1.1：上一版standard编译基线

正式源码：`firmware/standard/TSP`

- 构建输入：93；
- 源码指纹：`95CE6E20E3FF7DEB9DE43F76FB505EC55E194C5E354132FAC94DC4E5774605B3`；
- 应用BIN：687264字节；
- SHA-256：`3A18371CF566838E88B5C93936B620C8BCCE93D73AA7D58AF49871942F79479D`；
- 构建目录：`firmware_workspace/build/standard-2.1.1`；
- 状态：`compiled-not-hardware-verified`，运行日志级别仍为DEBUG。

无有效时间时仍更新本地测量快照；正式SD记录、统计和实时HJ212等待有效日期。
`get_data` 新增可选 `with_status:true` 精简状态，旧请求结构不变，扩展回复含换行
限制为800字节。采集120秒、气泵70秒和气体间隔500ms不变，未操作设备FFat。
LCD/云端按[状态接口约定](docs/protocols/lcd/LCD_GET_DATA_STATUS_PROTOCOL.md)联调。

## Firmware 2.1.1.1：上一版certified编译基线

正式源码：`firmware/certified/TSP`

- 构建输入：94；
- 源码指纹：`DCF142C5D1078957237B193A2BB80FBF5B3370E587B9C1CE4853C3C9338DD7F2`；
- 应用BIN：687744字节；
- SHA-256：`B50F569F1C1F348376205082DD2D301590EA7A6056288D1AC45F612061D766AB`；
- 构建目录：`firmware_workspace/build/certified-2.1.1.1`；
- 状态：`compiled-not-hardware-verified`，运行日志级别仍为DEBUG。

同步2.1.1公共改动，保留O3/NO2/SO2封顶500ppb、CO不封顶，以及独立认证校准策略。
实验固件和既有INFO测试包不变。两个新BIN均只编译，未烧录、未擦除或实机验收。

## Firmware 2.1.2：上一版standard编译基线

正式源码：`firmware/standard/TSP`

- 构建输入：96；
- 源码指纹：`EDEFD29E492923514D5F07561D787C90F3767E7A84D9A5518DE838FFA7A0D0EC`；
- 应用BIN：688800字节；
- SHA-256：`84B9B2442CB6E6262DC756544BD2B2CFF97557497447E52A0629CCB5C34F3A47`；
- 构建目录：`firmware_workspace/build/standard-2.1.2`；
- 状态：`compiled-not-hardware-verified`，运行日志级别仍为DEBUG。

气体首次通信仍最多尝试3次；成功回复未就绪时，最多再进行2次状态/浓度组合读取，
每次只发1次通信请求。回复显示已就绪即停止（故障位仍按原策略判断），连续未就绪
仍标记无效，各次气体通信之间
保持至少500ms；采集阶段在70秒截止预算不足时跳过后续读取，不延长气泵时间或周期。

SD启动挂载失败后，在现有维护任务中约每60秒重试，独立于网络、RTC和LCD。
使用原有SD锁，成功后停止重试；失败按已取得的资源逐项清理，不格式化、不重启，
不重新初始化FFat。初始化I/O预算3秒，单命令超时参数上限1秒；这不是总耗时的
硬上限，SDK内部事件处理、调度和清理可能增加耗时。新增 `SD_MOUNT_RESULT` 日志
记录结果、耗时和堆余量，LCD沿用现有SD状态字段。该机制不支持运行中热拔卡，
也不补写未挂载期间的历史数据；pending底层首次写零问题未关闭。

## Firmware 2.1.2.1：上一版certified编译基线

正式源码：`firmware/certified/TSP`

- 构建输入：97；
- 源码指纹：`19B58E7ED62F4AB72B70E1BAA4AA3BF3A504C5D8C3A3C870FA3DCFC26BEAB382`；
- 应用BIN：689280字节；
- SHA-256：`6FE48C68F7DBBF5671BFBB1981B17712CA33BEF5E670DD518AAF18C34E7BE555`；
- 构建目录：`firmware_workspace/build/certified-2.1.2.1`；
- 状态：`compiled-not-hardware-verified`，运行日志级别仍为DEBUG。

同步2.1.2公共改动，认证专用O3/NO2/SO2封顶、CO策略和校准逻辑未改。
两版均通过气体第三次读取/截止预算、SD分阶段失败清理/重试的软件故障注入测试，
以及现有 `get_data` 兼容性和回复容量测试；软件模拟不替代硬件验收。
实验版与既有INFO包未修改。本次未烧录、未擦除、未操作设备FFat、未创建正式Release。

## Firmware 2.1.3／2.1.3.1：上一版编译基线

升级前2.1.3／2.1.3.1源码与2026-10-09下午的`atmospheric-pressure`构建记录一致；上午LED帧间隔构建保留供追溯：

| 项目 | standard 2.1.3 | certified 2.1.3.1 |
|---|---|---|
| 源码 | `firmware/standard/TSP` | `firmware/certified/TSP` |
| 构建输入 | 99 | 100 |
| 编译时间（UTC+8） | 2026-10-09 14:22:47 | 2026-10-09 14:22:51 |
| 应用BIN字节 | 692640 | 693120 |
| 应用BIN SHA-256 | `99E72FA5C7C6751609550AA88B0EC3D36CAF23C69D985B5F770558437262798A` | `7034A54705F8613688F8C31987BB8663C80C7EB2C3ADFDE75D53AF97934D7211` |
| 构建目录 | `firmware_workspace/build/standard-2.1.3-atmospheric-pressure-20261009` | `firmware_workspace/build/certified-2.1.3.1-atmospheric-pressure-20261009` |
| 全局静态RAM | 28024字节 | 28024字节 |
| 状态 | `compiled-not-hardware-verified` | `compiled-not-hardware-verified` |

2026-10-08首版构建基线保留如下，不包含此次帧间隔修复：

| 项目 | standard 2.1.3 | certified 2.1.3.1 |
|---|---|---|
| 源码 | `firmware/standard/TSP` | `firmware/certified/TSP` |
| 构建输入 | 99 | 100 |
| 应用BIN字节 | 692352 | 692832 |
| 应用BIN SHA-256 | `E579B89663DE11BBC51CB473169D822FA7BA811E0C31DC999D84A795B6193118` | `6414C5DD013A19C0D789324AA3F481A6EB02367B4051297C16E7539692470AC2` |
| 构建目录 | `firmware_workspace/build/standard-2.1.3` | `firmware_workspace/build/certified-2.1.3.1` |
| 源码指纹 | `B1EA9A8A58296A0E17CD8F04ACF736BCDE9193FA0FB10869713D3D8D1FC197B9` | `997319145AC0AB0549EDA96C6A02F724CC22A4E358DCFA6B30CB4F95D8F9EAE1` |
| 全局静态RAM | 27944字节 | 27944字节 |
| 状态 | `compiled-not-hardware-verified` | `compiled-not-hardware-verified` |

该历史基线的源码指纹以各自构建manifest为准。两次构建均使用正式`Build-Firmware.ps1`的
variant入口编译；本机CMD入口仍因Windows PowerShell的`Get-FileHash`解析失败，
因此使用PowerShell 7运行同一脚本，不手工切换源码或修改工具链。

本轮复用旧LCD／远程端JSON字段作为LED协议兼容层，MCU使用独立LED配置；
新配置默认开启两区域模式，旧文件的“保存数据／远程控制”值不自动迁移为LED选项。
LED每5秒连续紧凑分页，颗粒物、气象／噪声、气体依次显示；缺少或无效因子跳过，
无有效数据时不新增提示。关闭、四切二与尾页空位发送空格清除，实际效果待实机验证。
加热开关支持规范字段与旧别名，保存后关闭立即LOW，开启允许原温湿度控制；
SHT30和风扇独立继续运行。

2026-10-09针对“41区域换内容、其余空白且缓存14项”的反馈，修正同页区域帧无间隔
连续发送的问题；每次仅发送一帧，串口发送完成后至少50ms再发下一帧，清屏／模式
切换同样遵守。固定4条待发送文本保证一页不混批；失败目标保留并约5秒后重试。
新增`LED_TX`区域／发送起止时间诊断，`ok=1`仅表示串口写成功，不代表控制卡ACK。
控制卡接收能力是最符合现象的解释，仍须实机确认，不据此关闭硬件待验收问题。

两版静态RAM相对2.1.2系列增加424字节，其中本次修复比2026-10-08首版增加80字节
（27944→28024），LED管理器固定占用416字节。没有增加任务或队列。
温控任务改为始终存在，原来关闭后不创建任务的场景会多占用其原有约4KB任务栈
及RTOS任务开销；LED仍使用原有4KB栈、深度5队列，及时释放处理数据包。
内存长跑、LED控制卡接收／清屏、LCD／远程配置回读和加热输出待硬件验收。
详见[开关配置协议](docs/protocols/lcd/LCD_SWITCH_CONFIG_PROTOCOL.md)。
两版各17组控制离线回归通过，包括帧间隔、发送中切换／关闭、失败重试、批次更新、
计时回绕、2万次并发加热开关及满队列通知；原有气体／SD
故障注入和`get_data`兼容／容量回归也通过，均不替代硬件验收。

继承2.1.2系列可靠性修改，实验版、已发布Release及旧构建不变。
本构建未烧录、未擦除、未操作设备FFat，未生成新的正式Release。

## Firmware 2.1.4／2.1.4.1：上一版测试基线

逐版本记录：[standard 2.1.4](docs/changelog/CHANGELOG_2.1.4.md)、[certified 2.1.4.1](docs/changelog/CHANGELOG_2.1.4.1.md)。

| 项目 | standard 2.1.4 | certified 2.1.4.1 |
|---|---|---|
| 编译时间（UTC+8） | 2026-10-20T11:02:09+08:00 | 2026-10-20T11:03:45+08:00 |
| 构建输入 | 101 | 102 |
| 应用BIN字节 | 695920 | 696400 |
| 应用BIN SHA-256 | `00DF84B69BBC62BBEF07A611FE97AB3CE3CC5E58B885F8D08869CD68ED838D26` | `975DFD34137A0E1536C57C8C835AA57C5321174F7859CE5060BE5054A5A72455` |
| 构建目录 | `firmware_workspace/build/standard-2.1.4-hj-uart-20261020` | `firmware_workspace/build/certified-2.1.4.1-hj-uart-20261020` |
| 全局静态RAM | 28032字节 | 28032字节 |
| 状态 | `compiled-not-hardware-verified` | `compiled-not-hardware-verified` |

HJ212软件串口仅在同步写字节期间临时提高任务优先级，随后恢复原基础优先级；
中断仍开启，ACK等待、发送间隔与存储操作不在该保护窗口内。失败报文转交现有存储
任务，最多2项／总报文字节2048（含写入中的项）；失败后隔60秒重试一次，入队不等于
持久化。存储事件队列深度4，满时拒绝新项并正确释放引用，避免SD积压阻塞上传投递。
旧pending扫描／读取／删除仍有同步SD操作，底层SDK卡住不能由外层计时强行取消。

臭氧HJ212出口按用户指定的平台要求使用 `w34011`，兼容两个内部别名；
旧有效报文补传时仅在内存替换CP因子名并重算CRC。数值、单位、精度和认证气体策略不变。
保持Flag语义、9600波特率、120秒采集、70秒气泵、3000ms包间隔；多通道和LCD修改留待后续。

两版各18组控制回归、现有气体／SD／get_data回归及新增上传故障测试通过。
新增测试覆盖写串口优先级／恢复、ACK窗口、SD慢／失败／未挂载、容量／字节上限、
60秒重试及计时回绕、2017／2025实时和统计臭氧字段、旧报文CRC迁移。
使用两版CMD入口编译，进程内补足Windows PowerShell模块路径；未修改工具链。

本地应用BIN可用于后续明确授权的OTA测试；尚未生成正式Release、尚未实机验证。
先做Flag=4单地址两小时接收对照，再做跨午夜24小时验证，同时保存MCU日志和原始TCP。

## Firmware 2.1.5／2.1.5.1：上一版SD对照诊断基线

逐版本记录：[standard 2.1.5](docs/changelog/CHANGELOG_2.1.5.md)、[certified 2.1.5.1](docs/changelog/CHANGELOG_2.1.5.1.md)。

| 项目 | standard 2.1.5 | certified 2.1.5.1 |
|---|---|---|
| 编译时间（UTC+8） | 2026-10-20T16:18:35+08:00 | 2026-10-20T16:20:15+08:00 |
| 构建输入 | 104 | 105 |
| 应用BIN字节 | 699408 | 699888 |
| 应用BIN SHA-256 | `92131DC02B1768099DEA9CFA6AF9ED327A28A138951CCEF392E35C9D9FD86F25` | `186A7290BFC3EB65E52D6F3D7D3F1A9B91158D03529BFC53447DAA3890948607` |
| 构建目录 | `firmware_workspace/build/standard-2.1.5-sd-aba-20261020` | `firmware_workspace/build/certified-2.1.5.1-sd-aba-20261020` |
| 全局静态RAM | 28080字节 | 28080字节 |
| 状态 | `compiled-not-hardware-verified` | `compiled-not-hardware-verified` |

本版用于定位2.1.4.1仍存在的实时缺头问题，尚非正式修复。每次启动后自动执行一次：

| 日志阶段 | 行为／持续时间 |
|---|---|
| `on_a` | 正常SD业务30分钟 |
| `draining` | 禁止新SD业务，等待已有操作返回；不强制取消SDK调用 |
| `off_b` | 在途计数归零后，完整暂停SD业务60分钟 |
| `on_a2` | 恢复正常SD业务30分钟 |
| `complete` | 保持正常，不重复循环；下次启动重新执行 |

阶段切换不重启、不卸载／格式化SD、不改写FFat配置。暂停覆盖历史存盘和查询、
pending写入／重建／扫描／读取／删除及挂载重试。跳过的新历史事件即时释放引用；
待存RAM请求被拒收／清理并记录unprotected，既有卡内文件不因暂停被删除或重写。
历史查询走原有NG响应；暂停前已开始的范围查询在下次循环停止，不持续积压任务。
采集、统计、公网上传及LCD实时查询继续；恢复后新生成的统计记录仍可能跨越暂停边界。
被跳过的历史／pending项不作补写，不承诺暂停期间的本地持久化。

`SD_ABA_START/PHASE/STATUS/SKIPS`标明真实阶段时间和跳过计数。`SD_IO_BEGIN/END`
记录取得SD互斥锁后的逻辑操作窗口（目录／读写／校验／清理），并非每条SDK事务波形。
`HJ_UART_TX`测量真实优先级和发送两端的SD在途数；`HJ_UART_CONTEXT`给出阶段和
前8字节。诊断均复用现有任务、固定大小状态；没有新增RTOS任务／队列／大型缓存。

两版各19组控制回归、气体／SD挂载／get_data回归及实际源码上传测试通过。
新增用例覆盖30／60／30边界、慢I/O排空、计时回绕、一次执行、SD暂停时UART继续、
普通历史／原始SD／挂载拒收、待存队列清理及历史查询即时返回。构建前后指纹一致。
测试不替代现场证据；本版本未自动烧录、提交或发布。

后续测试保持Flag=4、9600、单地址，连续收集至少一个完整对照周期的MCU日志、
原始TCP和有效报文导出，按每个采集批次核对长度、CRC及缺头数量，分别统计各阶段。

## Firmware 2.1.6／2.1.6.1：上一版继承优先级修正诊断基线

逐版本记录：[standard 2.1.6](docs/changelog/CHANGELOG_2.1.6.md)、[certified 2.1.6.1](docs/changelog/CHANGELOG_2.1.6.1.md)。

| 项目 | standard 2.1.6 | certified 2.1.6.1 |
|---|---|---|
| 编译时间（UTC+8） | 2026-10-21T11:19:36+08:00 | 2026-10-21T11:21:13+08:00 |
| 构建输入 | 104 | 105 |
| 应用BIN字节 | 700256 | 700720 |
| 应用BIN SHA-256 | `63ED46B30810AF4EAE5B2140CED298324B71DB1447E0EABABBF5D491359F7DC3` | `1830831FED7F9EDFFFA54080FA6E0A549FD102CBF244CE1B4D7F2CCC59FF1615` |
| 构建目录 | `firmware_workspace/build/standard-2.1.6-hj-priority-20261021` | `firmware_workspace/build/certified-2.1.6.1-hj-priority-20261021` |
| 全局静态RAM | 28080字节 | 28080字节 |
| 状态 | `compiled-not-hardware-verified` | `compiled-not-hardware-verified` |

2.1.5.1现场对照暴露基础优先级8／实际继承9时，原保护只改变基础值，实际发送仍为9。
本版对已有继承先对齐基础／有效等级，再提升并测量实际等级；写完恢复进入前的有效
等级，事务释放串口锁后恢复原基础等级。保护状态只使用调用栈，不新增任务或堆缓存。
写前未达到至少11则不写GPIO并返回失败，写后检查不足也不记为发送成功。
既有重试／pending策略处理失败；ACK等待不保留临时11。SAVE写入失败能向调用方返回失败，
但不把DTU保存后重启无回复当作未写入。远程硬件UART及命令格式保持兼容。

新增`HJ_UART_CONTEXT scope_base/guard_ok`、`HJ_UART_BLOCK`和事务后的
`HJ_PRIORITY_RESTORE`。两版19组控制、气体／SD／get_data及当前源码上传回归通过；
修正测试模型后旧代码先出现实际优先级断言失败，修正代码再通过。新增场景涵盖日志＋
串口锁继承、并发变化时拒写、较高原等级、短写／重试、ACK超时／成功、忙／补传让路、
SET＋SAVE及CSQ／时间查询；不代替实际GPIO和服务器接收验收。

保留2.1.5的每次启动30／60／30分钟SD对照及历史空档约束。现场保持Flag=4、9600、
单地址，先记录完整对照周期，再在SD持续正常时跑24小时。检查实际发送等级至少11、
`guard_ok=1`、事务后基础等级恢复；最终按MCU批次与服务器长度／CRC有效报文逐条验收。
本版不宣称已解决全部缺失，尚未自动提交、发布或烧录；2.1.5基线`b730de2`已推送GitHub。

## Firmware 2.1.7／2.1.7.1：当前持续正常存卡版本

逐版本记录：[standard 2.1.7](docs/changelog/CHANGELOG_2.1.7.md)、[certified 2.1.7.1](docs/changelog/CHANGELOG_2.1.7.1.md)。

| 项目 | standard 2.1.7 | certified 2.1.7.1 |
|---|---|---|
| 编译时间（UTC+8） | 2026-10-23T13:47:46+08:00 | 2026-10-23T13:49:32+08:00 |
| 构建输入 | 104 | 105 |
| 应用BIN字节 | 699840 | 700320 |
| 应用BIN SHA-256 | `599916983160494B06D5F9F8A9C5B55DAD038D6FEDABAB4819336E6C3C6EA55A` | `01A2F182528ABE524F3806C1E0AA38218472F5266D6E8C83FB3EA9549CC01F98` |
| 构建目录 | `firmware_workspace/build/standard-2.1.7-normal-sd-20261023` | `firmware_workspace/build/certified-2.1.7.1-normal-sd-20261023` |
| 全局静态RAM | 28072字节 | 28072字节 |
| 状态 | `compiled-not-hardware-verified` | `compiled-not-hardware-verified` |

取消30／60／30分钟SD自动对照，历史和pending读写、查询、扫描及挂载重试持续正常
执行；启动记录`SD_MODE mode=normal comparison=off`。旧诊断阶段跳过的历史不补写。
保留2.1.6实际优先级保护和有界异步待存，原Flag配置、9600波特率、120秒采集、70秒
气泵及认证气体策略不变。

默认INFO，正常LED、气体查询、JSON回显、UART／SD详细过程留在DEBUG；采集、
MCU发送、存储和待传结果以及故障保留。SD操作返回时，耗时至少3秒记录慢操作告警，
不强制取消SDK。两版主机回归及编译通过；模拟48小时不是实机验收。
现场残帧和小时／日历史读取异常尚未修复，不能以日志减少宣称完整送达或落盘可靠。

识别副本为`TSP-standard-2.1.7-normal-SD.bin`和`TSP-certified-2.1.7.1-normal-SD.bin`，
与各自同目录的`TSP.ino.bin`一致，仅供明确授权后的应用OTA使用；旧构建和正式Release
均保留。本次未提交、推送、发布、烧录、擦除或操作设备FFat。

## 使用规则

1. 需要恢复已验证固件：使用2.0.4固定四段发布包。
2. 需要查看或重新编译2.0.4：打开独立2.0.4源码目录中的 `TSP.ino`。
3. 需要继续开发：standard只使用`firmware/standard/TSP`，certified只使用`firmware/certified/TSP`；公共优化同步，认证策略只存在于certified；禁止使用2.0.11构建。
4. 不在2.0.4独立源码目录中继续开发；需要改动时先复制或建立新的Git分支。
5. 构建目录和归档目录均不作为日常源码入口。
