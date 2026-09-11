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
String DTUDriver::sendData(const String& data, bool waitForResponse,
                           uint32_t responseTimeoutMs) {
    // Firmware 2.0.1 (2026-07-30): M100M-B2 sends one transparent frame at
    // a time. Treat 600 ms without another byte as the end of its response.
    static constexpr uint32_t M100M_B2_RESPONSE_IDLE_MS = 600;

    if (_stream == nullptr || data.length() == 0) {
        return "";
    }

    // Do not allow a delayed response from an earlier transmission to be
    // mistaken for the response to this packet.
    while (_stream->available()) {
        _stream->read();
    }

    const char *buf = data.c_str();
    const size_t len = data.length();
    const size_t written = _stream->write((const uint8_t *)buf, len);
    _stream->flush();

    if (written != len) {
        LOG_ERROR("DTU write incomplete: expected=%u actual=%u",
                  (unsigned)len, (unsigned)written);
        return "";
    }

    // Flag without the ACK bit means that a complete UART transmission is
    // the protocol-level success condition; the server need not return 9014.
    if (!waitForResponse) {
        return "TX_OK";
    }

    String res = "";
    uint32_t startTime = millis();
    uint32_t lastByteTime = startTime;
    bool receivedAny = false;
    while (millis() - startTime < responseTimeoutMs)
    {
        while (_stream->available() > 0)
        {
            char c = _stream->read();
            res += c;
            receivedAny = true;
            lastByteTime = millis();
        }
        // Keep the configured overall timeout, but do not hold SERIAL_HJ212
        // for the full timeout after a complete ACK has already arrived.
        if (receivedAny && millis() - lastByteTime >= M100M_B2_RESPONSE_IDLE_MS) {
            break;
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
