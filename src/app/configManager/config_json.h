#pragma once
#ifndef CONFIGJSON_H
#define CONFIGJSON_H

const char *CONFIG_JSON = R"({
    "collect_time": 60,
    "upload_interval": 60,
    "dtu_server": "39.101.67.255:1883"
})";

const char *TEMP_CONTROL_JSON = R"({
    "tempUpperLimit": 55,
    "tempLowerLimit": 15,
    "wetnUpperLimit": 80,
    "wetnLowerLimit": 40
})";

const char *HJ212_JSON = R"({
    "ip": "39.101.67.255:8002",
    "mn": "qqqqqq12345678",
    "pw": "123456",
    "st": "31",
    "flag": "9",
    "pv": "2017",
    "timeout": 5,
    "retry_times": 3
})";

const char *SWITCH_JSON = R"({
    "tempControlSwitch": true,
    "save_raw_data": true,
    "log_to_sd": true,
    "save_min_data": true,
    "save_hour_data": true,
    "save_day_data": true,
    "enable_hj212": true,
    "enable_remote_dtu": true,
    "mqtt_public": true
})";

const char *ALARM_JSON = R"({
    "alarm_sensor": "a34001",
    "alarm_upper_limit": 50.0,
    "alarm_lower_limit": 10.0,
    "alarm_switch": true
})";

#endif


