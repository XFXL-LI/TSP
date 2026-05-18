# TSP 空气微站
1. 通过一路485接口读取TSP传感器，通过一路485接口读取空气质量传感器，风速风向传感器及温湿度传感器，一路485接口对外提供modbus 地址
    * TSP.ino 入口函数
    * system.cpp 主函数

# 烧录方式
1. 通过Arduino 直接烧录
    * 配置信息：
2. 通过esp32 烧录软件进行烧录， build/release/烧录工具/flash_download_tool_3.9.7.exe
    * 烧录地址：
    * 烧录说明：
    1. TSP.ino.merged.bin 0Xe000
    2. TSP.ino.bootloader.bin 烧录地址：0X1000
    3. TSP.ino.partitions.bin 烧录地址：0X8000
    4. TSP.ino.bin 烧录地址： 0X10000
    5. 更新程序时不需要烧录底层程序，只需要烧录TSP.ino.bin即可

# 通过屏幕更改 TSP 配置
1. 初始化, TSP上电后自动启动
2. 配置PW ,MN 号
3. 配置flag以及 ST ，配置完成后不断电，发送的HJ212协议即可自动更改，采集数据版本烧录时确定

# 通过屏幕查看TSP数据
1. 通过屏幕查看各个采集因子实时数据， 其中 TSP 1分钟更新； 风速风向2分钟更新； 温湿度压强噪声2分钟更新； 其他气体传感器4分钟更新一个
2. 通过屏幕查看TSP发送到的IP地址及端口

# 通过屏幕进行TSP的校准以及气体传感器的校准
1. 通过屏幕校准功能，更改TSP的校准系数，校准方式为通已知浓度的PM气体，然后更改数值使采集到的数值与已知浓度相等
2. 通过屏幕校准气体采集，通普通空气， 点击气体校准，一分钟后气体校准自动成功

# v1.0.0 为通过modbus从站获取数据与更改配置
1. modbus地址位：
    * （可读可写读）
        * address  value
        * 0   从站id
        * 2   pm1数据校准
        * 3   pm1比例校准
        * 4   pm25数据校准
        * 5   pm25比例校准
        * 6   pm10数据校准
        * 7   pm10数据校准
        * 8   tsp数据校准
        * 9   tsp数据校准

        * 10  时间年
        * 11  月
        * 12  日
        * 13  时
        * 14  分
        * 15  秒

        * 127  信号质量地址位
        * 21  ST 设置地址位
        * 22  PW 设置地址位 2位地址，longABCD
        * 23  
        * 24  MN 设置地址位 2位地址，longABCD
        * 25
        * 26  FLAG 设置地址位

        * 30   TSP开关
        * 31   气象开关
        * 32
        * 33   气体开关
        * 34   总开关
        * 35   O3气体校准
        * 36   NO2气体校准
        * 37   CO气体校准
        * 38   SO2气体校准
        * 40
        * 41
        * 42
        * 43
        * 44   端口，改变端口写完


        * 48
        * 49
    * （只读）分钟数据
        * address  value
        * 50
        * 87

        * 128  TSP数据读取地址位
        * 129  PM1数据读取地址位
        * 130  PM25数据读取地址位
        * 131  PM10数据读取地址位
        * 132  风速数据读取地址位
        * 133  风向数据读取地址位
        * 134  温度数据读取地址位
        * 135  湿度数据读取地址位
        * 136  大气压数据读取地址位
        * 137  噪声数据读取地址位
        * 138  SO2数据读取地址位
        * 139  CO数据读取地址位
        * 140  NO2数据读取地址位
        * 141  O3数据读取地址位

# v2.0.0 为通过串口json自定义协议获取数据与更改配置
# 通过串口/485可以直接获取数据/更改配置
1. 串口数据读取：
    * 发送数据以 & 为结束标志
    * response data example:
        * response
        {
            "operation": "get_data",
            "ids": ["a34001", "a34002", "a34004", "a34005", "a01001", "a01002", "a01006", "a01007", "a01008", "a21008", "a21004", "a21005", "CSQ"],
        }&
    * request example:
        * OK request
        {
            "operation": "get_data",
            "code":"OK"
            "params": ["a34001", "a34002"],
            "values": [23, 33],
            "message": ""
        }&
        * NG request
        {
            "operation": "get_data",
            "code":"NG",
            "params":[],
            "values":[],
            "message": ""
        }&
2. 串口数据设置
    * request example:
        * response
        {
            "operation": "get_set_data",
            "id": ["PW", "MN", "IP", "ST", "FLAG", "TIME"],
        }&
        * OK request
        {
            "operation": "get_set_data",
            "code":"OK",
            "params":[],
            "values":["1", "1", "192.168.1.100:8002", "31", "9", "20260101101020"],
            "message": ""
        }&
        * NG request
        {
            "operation": "get_set_data",
            "code":"NG",
            "params":[],
            "values":[],
            "message": ""
        }&

    * response set example:
        * response
        {
            "operation": "set_data",
            "ids": [
                {
                    "id": "PW",
                    "values": "1"
                },
                {
                    "id": "MN",
                    "values": "1"
                },
                {
                    "id": "ST",
                    "values": 1
                },
                {
                    "id": "FLAG",
                    "values": 1
                }
            ]
        }&
        * request example:
        * OK request
        {
            "operation": "set_data",
            "code":"OK",
            "params":[],
            "values":[],
            "message": ""
        }&
        * NG request
        {
            "operation": "set_data",
            "code":"NG",
            "params":[],
            "values":[],
            "message": ""
        }&
        * 标定接口TSP带参数，TSP： DATA 加减 RATIO为倍率 SO2 的默认都为0
        {
            "operation": "gal_data",
            "ids": [
                {
                    "id": "TSP",
                    "params": ["DATA", "RATIO"],
                    "values": [1, 100]
                },
                {
                    "id": "SO2",
                    "params": ["DATA", "RATIO"],
                    "values": [0, 0]
                }
            ],
        }&
        {
            "operation": "gal_data",
            "code":"NG",
            "params":[],
            "values":[],
            "message": ""
        }&
        * 获取json配置
        {
            "operation": "get_config"
        }&
        {
            "operation":"get_config",
            "code":"OK",
            "params":"config.json",
            "value":"{
                "ip":"192.168.1.100",
                "pw":"123456",
                "mn":"123456789",
                "flag":"9",
                "st":"31"
                }",
            "message":"get config success"
        }&
        {
            "operation": "set_config",
            "config":{
                "ip": "192.168.1.100",
                "host": "0911",
                "mn": "123456789",
                "pw": "123456",
                "st": "31",
                "flag": "9"
                }
        }&
        {
            "operation":"set_config",
            "code":"OK",
            "params":"config.json",
            "value":"{
                "ip":"39.101.67.255",
                "pw":"123456",
                "mn":"qqqqqq12345678",
                "flag":"9",
                "st":"31",
                "collect_ids":[a21004,a21005,a21026,a21008,a34005,a34004,a34002,a34001,a01007,a01008,a01001,a01019,a01018,LA]
                }",
            "message":"set config success"
        }&
        {
            "operation": "collect_data",
            "ids": [
                "TSP",
                "",
                ""
            ]
        }&
        {
            "operation": "set_log",
            "config":{
                "level": "debug",
                "uart": "usb" / "dtu"
                }
        }&
        {"operation":"set_log","code":"OK","params":"NULL","value":"NULL{"level":"debug","uart":"dtu"}","message":"set log success"}&
        