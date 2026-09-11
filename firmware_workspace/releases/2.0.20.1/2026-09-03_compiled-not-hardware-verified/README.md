# Firmware 2.0.20.1 Certified

本升级包是基于Firmware 2.0.20通用原始数据版生成的特定认证版本。

- 状态：已编译和镜像校验，尚未进行硬件验证。
- O3、NO2、SO2业务数据最大500 ppb。
- CO不封顶。
- 认证标定目标：O3=250、NO2=250、CO=5000、SO2=250 ppb。
- 认证标定配置：`/gasCalibrationCertified.json`。
- DEBUG保持开启。

远程OTA只使用`firmware.bin`。四文件串口烧录地址见`flash_args.txt`。

当前未执行烧录、Flash擦除或设备FFat操作。
