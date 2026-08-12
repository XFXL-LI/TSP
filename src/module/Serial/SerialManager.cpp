#include <Arduino.h>
#include "../../inc/sys_init.h"
#include "SerialManager.h"
#include <SoftwareSerial.h>

static constexpr size_t REMOTE_DTU_RX_BUFFER_SIZE = 2048;

SerialManager& SerialManager::getInstance() {
    static SerialManager instance;
    return instance;
}

void SerialManager::begin(void){
    HardwarePortInit(SERIAL_TTL, &Serial1, TTL_BAUD, TTL_RXD, TTL_TXD);
    HardwarePortInit(SERIAL_DTU, &Serial2, DTU_BAUD, DTU_RXD, DTU_TXD);
    
    SoftwarePortInit(SERIAL_LCD, LCD_RXD, LCD_TXD, LCD_BAUD);
    SoftwarePortInit(SERIAL_485, RS485_RXD, RS485_TXD, RS485_BAUD);
    SoftwarePortInit(SERIAL_HJ212, HJ212_RXD, HJ212_TXD, HJ212_BAUD);
    SoftwarePortInit(SERIAL_LED, LED_RXD, LED_TXD, LED_BAUD);

    println(SERIAL_TTL, "[INFO]: System uart TTL init");
    println(SERIAL_485, "[INFO]: System uart 485 init");
    println(SERIAL_LCD, "[INFO]: System uart LCD init");
    println(SERIAL_HJ212, "[INFO]: System uart HJ212 init");
    println(SERIAL_DTU, "[INFO]: System uart DTU init");
    LOG_INFO("Serial init success!");
}

void SerialManager::HardwarePortInit(const String& name, HardwareSerial* serial, uint32_t baud, int rxPin, int txPin) {
   if (_serials.find(name) != _serials.end()) return;
    if (name == SERIAL_DTU) {
        size_t configuredSize = serial->setRxBufferSize(REMOTE_DTU_RX_BUFFER_SIZE);
        if (configuredSize < REMOTE_DTU_RX_BUFFER_SIZE) {
            LOG_ERROR("DTU UART RX buffer setup failed: requested=%u actual=%u",
                      (unsigned)REMOTE_DTU_RX_BUFFER_SIZE,
                      (unsigned)configuredSize);
        }
    }
    serial->begin(baud, SERIAL_8N1, rxPin, txPin);
    SerialPortWrapper* wrapper = new SerialPortWrapper();
    wrapper->stream = serial;
    wrapper->mutex = xSemaphoreCreateMutex();
    wrapper->isSoftware = false;
    wrapper->overflowCount = 0;
    _serials[name] = wrapper;
}

void SerialManager::SoftwarePortInit(const String& name, int rx, int tx, uint32_t baud) {
    if (_serials.find(name) != _serials.end()) return;

    SoftwareSerial* sw = new SoftwareSerial(rx, tx, false);
    sw->begin(baud, SWSERIAL_8N1, rx, tx, false, 1024);

    SerialPortWrapper* wrapper = new SerialPortWrapper();
    wrapper->stream = sw;
    wrapper->mutex = xSemaphoreCreateMutex();
    wrapper->isSoftware = true;
    wrapper->overflowCount = 0;
    _serials[name] = wrapper;
}

size_t SerialManager::write(const String& name, const uint8_t* buf, size_t len) {
    auto it = _serials.find(name);
    if (it == _serials.end() || buf == nullptr || len == 0) return 0;

    size_t written = 0;
    if (xSemaphoreTake(it->second->mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        
        written = it->second->stream->write(buf, len);
        
        it->second->stream->flush();
        
        xSemaphoreGive(it->second->mutex);
    }
    return written;
}

void SerialManager::println(const String& name, const char* msg) {
    if (msg == nullptr) return;

    write(name, (const uint8_t*)msg, strlen(msg));
    
    uint8_t newline[] = {"\r\n"};
    write(name, newline, 2);
    
}
Stream* SerialManager::getStream(const String& name) {
    auto it = _serials.find(name);
    if (it != _serials.end()) {
        return it->second->stream;
    }
    return nullptr;
}

SemaphoreHandle_t SerialManager::getMutex(const String& name) {
    auto it = _serials.find(name);
    if (it != _serials.end()) {
        return it->second->mutex;
    }
    return nullptr;
}

bool SerialManager::checkAndReportOverflow(const String& name) {
    auto it = _serials.find(name);
    if (it == _serials.end() || !it->second->isSoftware) {
        return false;
    }

    SoftwareSerial* sw = static_cast<SoftwareSerial*>(it->second->stream);
    if (!sw->overflow()) {
        return false;
    }

    ++it->second->overflowCount;
    LOG_ERROR("[DIAG] UART_OVERFLOW port=%s count=%u available=%d",
              name.c_str(),
              (unsigned)it->second->overflowCount,
              sw->available());
    return true;
}

SerialManager::~SerialManager() {
    for (auto& pair : _serials) {
        if (pair.second->isSoftware) {
            delete (SoftwareSerial*)pair.second->stream;
        }
        vSemaphoreDelete(pair.second->mutex);
        delete pair.second;
    }
}
