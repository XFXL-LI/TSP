#ifndef SERIAL_MANAGER_H
#define SERIAL_MANAGER_H

#include <Arduino.h>
#include <map>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define SERIAL_485      "485"
#define RS485_TXD       5
#define RS485_RXD       4
#define RS485_BAUD      9600

#define SERIAL_LCD      "LCD"
#define LCD_TXD         16
#define LCD_RXD         15
#define LCD_BAUD        9600

#define SERIAL_DTU      "DTU"
#define DTU_TXD         37
#define DTU_RXD         38
#define DTU_BAUD        115200

#define SERIAL_LED      "LED"
#define LED_TXD         0
#define LED_RXD         -1
#define LED_BAUD        9600

#define SERIAL_TTL      "TTL"
#define TTL_TXD         10
#define TTL_RXD         9
#define TTL_BAUD        9600

#define SERIAL_HJ212    "HJ212"
#define HJ212_TXD       13
#define HJ212_RXD       12
#define HJ212_BAUD      9600

struct SerialPortWrapper {
    Stream* stream;
    SemaphoreHandle_t mutex;
    bool isSoftware;
    uint32_t overflowCount;
};

class SerialManager {
public:
    static SerialManager& getInstance();
    void begin(void);

    void HardwarePortInit(const String& name, HardwareSerial* serial, uint32_t baud, int rxPin, int txPin);
    void SoftwarePortInit(const String& name, int rx, int tx, uint32_t baud);

    size_t write(const String& name, const uint8_t* buf, size_t len);
    size_t write(const String& name, const String& s) {
        return write(name, (const uint8_t*)s.c_str(), s.length());
    }
    size_t write(const String& name, const char* s) {
        if (s == nullptr) return 0;
        return write(name, (const uint8_t*)s, strlen(s));
    }
    template <size_t N>
    size_t write(const String& name, const uint8_t (&arr)[N]) {
        return write(name, arr, N);
    }
    size_t write(const String& name, const std::vector<uint8_t>& v) {
        return write(name, v.data(), v.size());
    }

    void println(const String& name, const char* msg);
    
    Stream* getStream(const String& name);
    SemaphoreHandle_t getMutex(const String& name);
    bool checkAndReportOverflow(const String& name);

private:
    SerialManager() {}
    ~SerialManager();

    SerialManager(const SerialManager&) = delete;
    SerialManager& operator=(const SerialManager&) = delete;

    std::map<String, SerialPortWrapper*> _serials;
};

#endif
