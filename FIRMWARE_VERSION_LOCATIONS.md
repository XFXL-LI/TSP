# TSP 固件版本与目录说明

最后更新：2026-09-10

本文件用于区分“正式实机验证固件”“对应的可编译源码”和“当前开发源码”。
不要只根据目录名或 `TSP.ino` 文件名判断版本。

## 当前版本总览

| Variant | 版本 | 当前正式源码 | 构建输入 | 源码指纹 |
|---|---:|---|---:|---|
| standard | 2.0.22 | `firmware/standard/TSP` | 88 | `84CD14958E077AE1FE4294C4C566F8CF39FE89952C5CB929301AF2CF4C553213` |
| certified | 2.0.22.1 | `firmware/certified/TSP` | 89 | `540EE286955E50B552882A8F7639E858DBC724DFECFC9B0571E0AC5D0A128779` |

当前构建入口为 `firmware_workspace/build-standard.cmd`、
`firmware_workspace/build-certified.cmd`和兼容standard的
`firmware_workspace/build-current.cmd`。状态入口为
`status-standard.cmd`、`status-certified.cmd`和默认standard的`status.cmd`。

两版正式源码均已得到以下迁移验证结论：

`MIGRATION BUILD VERIFIED - EXPECTED NONDETERMINISTIC METADATA ONLY`

该结论不是bit-identical；新旧BIN的差异已由编译时间及其派生镜像元数据完整解释。

2026-09-07正式Release分别为：

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

## Firmware 2.0.22：当前standard正式源码

当前正式源码：`firmware/standard/TSP`

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

## Firmware 2.0.22.1：当前certified正式源码

当前正式源码：`firmware/certified/TSP`

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

## 使用规则

1. 需要恢复已验证固件：使用2.0.4固定四段发布包。
2. 需要查看或重新编译2.0.4：打开独立2.0.4源码目录中的 `TSP.ino`。
3. 需要继续开发：standard只使用`firmware/standard/TSP`，certified只使用`firmware/certified/TSP`；公共优化同步，认证策略只存在于certified；禁止使用2.0.11构建。
4. 不在2.0.4独立源码目录中继续开发；需要改动时先复制或建立新的Git分支。
5. 构建目录和归档目录均不作为日常源码入口。
