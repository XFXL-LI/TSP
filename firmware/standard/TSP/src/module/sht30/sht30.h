#pragma once
#ifndef SHT30_H
#define SHT30_H

#include <Arduino.h>
#include <Wire.h>

class SHT30 {
public:
    SHT30(uint8_t address = 0x44);
    ~SHT30() = default;
    bool begin(int sdaPin = 8, int sclPin = 18, uint32_t frequency = 100000);
    bool readTemperatureHumidity(float &temperature, float &humidity);

private:
    uint8_t _address;
    bool _isInitialized;
};

#endif