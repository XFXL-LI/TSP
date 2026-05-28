#include "dtu_Driver.h"
#include "../../module/log/log_manager.h"

DTUDriver::DTUDriver(String name, int id) : _name(name), _id(id), _stream(nullptr) {
}

DTUDriver::~DTUDriver() {
}
String DTUDriver::sendCommand(const char* cmd, uint32_t timeout) {
    if (!_stream) return "Err: No Stream";
    while (_stream->available()) { _stream->read(); }

    _stream->println(cmd);
    String currentLine = "";
    uint32_t start = millis();

    while (millis() - start < timeout) {
        while (_stream->available())
        {
            char c = _stream->read();
            if (c == '\r')
            {
                continue;
            }
            else if (c == '\n')
            {
                if (currentLine.length() > 0)
                {
                        return currentLine;
                }
            }
            else
            {
                currentLine += c;
            }
        }
        yield();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return "";
}
String DTUDriver::sendData(String data){
    const char *buf = data.c_str();
    size_t len = data.length();
    _stream->write((const uint8_t *)buf, len);
    _stream->flush();
    String res = "";
    uint64_t startTime = millis();
     while (millis() - startTime < 5000)
    {
        while (_stream->available() > 0)
        {
            char c = _stream->read();
            res += c;
            startTime = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return res;
}
int DTUDriver::readCSQ() {
    String res = sendCommand(GET_CSQ_COMM);
    int idx = res.indexOf(":");
    if (idx != -1) {
        String sub = res.substring(idx + 1);
        int commaIdx = sub.indexOf(",");
        return sub.substring(0, commaIdx).toInt();
    }
    return -1;
}

bool DTUDriver::checkOnline() {
    int csq = readCSQ();
    if (csq >= 0 && csq <= 31) {
        return true;
    } else {
        return false;
    }
}