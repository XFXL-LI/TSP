#pragma once
#ifndef MODELJSON_H
#define MODELJSON_H

const char *MODEL_JSON = R"({
  "a34001": {"name": "TSP","alarmLimit": 200,"unit": "ug/m3"},
  "a34005": {"name": "PM1","alarmLimit": 200,"unit": "ug/m3"},
  "a34004": {"name": "PM2.5","alarmLimit": 200,"unit": "ug/m3"},
  "a34002": {"name": "PM10","alarmLimit": 200,"unit": "ug/m3"},
  "a01007": {"name": "WINDSPEED","alarmLimit": 200,"unit": "m/s"},
  "a01008": {"name": "WINDDIRECTION","alarmLimit": 200,"unit": "degree"},
  "w34011": {"name": "O3","alarmLimit": 200,"unit": "mg/m3"},
  "a21004": {"name": "NO2","alarmLimit": 200,"unit": "mg/m3"},
  "a21005": {"name": "CO","alarmLimit": 200,"unit": "mg/m3"},
  "a21026": {"name": "SO2","alarmLimit": 200,"unit": "mg/m3"},
  "a01001": {"name": "TEMP","alarmLimit": 200,"unit": "celsius"},
  "a01002": {"name": "HUMI","alarmLimit": 200,"unit": "%"},
  "a01006": {"name": "PRESSURE","alarmLimit": 200,"unit": "kPa"},
  "L90": {"name": "NOISE","alarmLimit": 200,"unit": "dB"}
})";

#endif