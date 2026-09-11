# 固件发布包

每个可烧录目录必须同时具有：

- `firmware.bin`
- `bootloader.bin`
- `partitions.bin`
- `boot_app0.bin`
- `flash_args.txt`
- `SHA256SUMS.txt`
- `manifest.json`

`merged.bin` 不放入常规发布包，避免误以整片镜像方式写入并影响FFat配置区。
