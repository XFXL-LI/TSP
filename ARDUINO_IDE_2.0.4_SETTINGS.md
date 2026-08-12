# Firmware 2.0.4 Arduino IDE 编译与烧录设置

最后更新：2026-08-12

本文记录2026-08-10 Firmware 2.0.4正式构建使用的板卡、Flash、分区和库版本。
新安装Arduino IDE、换电脑或重新选择开发板后，应逐项核对。

## 1. 打开的工程

Arduino IDE打开：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\sources\Firmware_2.0.4_hw-verified\TSP\TSP.ino`

不要打开当前2.0.6开发目录中的 `TSP.ino`，否则编译出来的不是2.0.4。

## 2. 开发板平台和依赖库

开发板管理器：

- 平台名称：`esp32 by Espressif Systems`；
- 必须使用版本：`3.3.7`；
- 不要直接使用新安装时可能提示的3.3.11或其他版本，除非重新完成编译和实机验证。

库管理器/用户库版本：

- `ClosedCube SHT31D`：1.5.1；
- `Ds1302`：1.1.0；
- `EspSoftwareSerial`：8.1.0；
- `modbus-esp8266`：4.1.0。

归档自动构建使用的 Arduino CLI 为1.5.2-rc.1；在Arduino IDE中最关键的是保持
ESP32 Core和上述依赖库版本一致。

## 3. Arduino IDE“工具”菜单完整设置

先选择：`工具 -> 开发板 -> esp32 -> ESP32S3 Dev Module`。

然后逐项选择：

| Arduino IDE菜单 | 必须选择的值 |
| --- | --- |
| Upload Speed | `921600` |
| USB Mode | `Hardware CDC and JTAG` |
| USB CDC On Boot | `Disabled` |
| USB Firmware MSC On Boot | `Disabled` |
| USB DFU On Boot | `Disabled` |
| Upload Mode | `UART0 / Hardware CDC` |
| CPU Frequency | `240MHz (WiFi)` |
| Flash Mode | `QIO 80MHz` |
| Flash Size | `16MB (128Mb)` |
| Partition Scheme | `16M Flash (3MB APP/9.9MB FATFS)` |
| Core Debug Level | `None` |
| PSRAM | `Disabled` |
| Arduino Runs On | `Core 1` |
| Events Run On | `Core 1` |
| Erase All Flash Before Sketch Upload | `Disabled` |
| JTAG Adapter | `Disabled` |
| Zigbee Mode | `Disabled` |

端口选择当前设备实际出现的COM口。2026-08-10验证设备当时是COM9，但Windows重插、
换USB口或换电脑后端口号可能变化，不能固定照抄COM9。

说明：`Core Debug Level = None`只是关闭ESP32 Core自身的调试级别，不会关闭固件源码
中的 `#define DEBUG`；Firmware 2.0.4源码中的DEBUG仍保持开启。

## 4. 可用于核对的完整FQBN

正式构建记录中的完整参数为：

```text
esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default
```

如果Arduino IDE菜单值与此参数不一致，应先修正再编译。

## 5. 分区布局

`16M Flash (3MB APP/9.9MB FATFS)`对应：

| 分区 | 起始地址 | 大小 |
| --- | ---: | ---: |
| NVS | `0x9000` | `0x5000` |
| OTA Data | `0xE000` | `0x2000` |
| APP0 | `0x10000` | `0x300000` |
| APP1 | `0x310000` | `0x300000` |
| FFat | `0x610000` | `0x9E0000` |
| Core Dump | `0xFF0000` | `0x10000` |

Flash Size或Partition Scheme选错会改变APP和FFat布局，可能导致OTA空间不正确、配置区
无法读取，甚至覆盖原有FFat数据。

## 6. 编译和烧录的区别

- 点击“验证/编译”只生成新BIN，不会烧录MCU。
- 点击“上传”会把本次重新编译的BIN写入设备；这个新BIN不能自动继承8月10日的实机
  验证状态。
- 需要恢复正式2.0.4时，优先使用固定四段验证包，不要临时重新编译代替正式包。

正式四段包：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\releases\2.0.4\2026-08-10_hw-verified`

四段地址：

| 地址 | 文件 |
| ---: | --- |
| `0x0000` | `bootloader.bin` |
| `0x8000` | `partitions.bin` |
| `0xE000` | `boot_app0.bin` |
| `0x10000` | `firmware.bin` |

不得选择全片擦除，否则可能清除从 `0x610000` 开始的FFat配置区。正式包内部的
`flash_args.txt`虽然显示底层esptool参数 `--flash-mode dio`，但Arduino IDE菜单仍应选择
`QIO 80MHz`；这是ESP32 Core 3.3.7对该菜单项的正常内部映射，不要手动改成DIO。

## 7. 编译结果参考

从恢复的2.0.4独立源码、使用上述设置编译得到：

- Sketch：623296字节；
- 全局变量：25936字节；
- 应用BIN：623440字节。

不同时间重新编译时SHA-256会因编译时间和构建摘要变化，不能只用新编译BIN哈希判断
代码是否一致。真正已实机验证的固定 `firmware.bin` SHA-256为：

`6CE8B44EA4BF509815A7CE57470B1CC79CA84F85355BF6E4F2D1CB6EA13A4773`
