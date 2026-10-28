#include "dtu_Driver.h"
#include "../../module/log/log_manager.h"
#include "Hj212TxGuard.h"
#include "../diagnostics/SdIoDiagnostics.h"

DTUDriver::DTUDriver(String name, int id) : _name(name), _id(id), _stream(nullptr) {
}

DTUDriver::~DTUDriver() {
}
size_t DTUDriver::writeWire(const uint8_t* data, size_t length, bool newline,
                            Hj212PriorityScope* priorityScope) {
    const bool protect = _name == "HJ212";
    size_t written = 0;
    bool protectionOk = true;
    uint32_t started = 0, finished = 0;
    UBaseType_t originalPriority = 0, priorityTx = 0, priorityEnd = 0;
    const UBaseType_t effectiveBefore = uxTaskPriorityGet(nullptr);
    SdIoDiagnostics::Snapshot sdBegin{}, sdEnd{};
    char prefix[17] = {};
    if (protect) {
        const size_t bytes = length < 8U ? length : 8U;
        static const char hex[] = "0123456789ABCDEF";
        for (size_t i = 0; i < bytes; ++i) {
            prefix[2U * i] = hex[data[i] >> 4U];
            prefix[2U * i + 1U] = hex[data[i] & 15U];
        }
    }
    {
        Hj212TxGuard guard(protect, priorityScope);
        originalPriority = guard.originalPriority();
        priorityTx = uxTaskPriorityGet(nullptr);
        sdBegin = SdIoDiagnostics::snapshot();
        started = millis();
        protectionOk = guard.ready();
        if (protectionOk) {
            written = _stream->write(data, length);
            if (newline) {
                static const uint8_t ending[] = {'\r', '\n'};
                written += _stream->write(ending, sizeof(ending));
            }
        }
        finished = millis();
        priorityEnd = uxTaskPriorityGet(nullptr);
        sdEnd = SdIoDiagnostics::snapshot();
    } // Restore the entry effective floor before logging, flush and ACK waits.
    if (protect && priorityEnd < Hj212TxGuard::PRIORITY) protectionOk = false;
    if (protect) {
        if (written != length + (newline ? 2U : 0U)) {
            LOG_ERROR("[DIAG] HJ_UART_SHORT expected=%u written=%u",
                      (unsigned)(length + (newline ? 2U : 0U)), (unsigned)written);
        }
        LOG_DEBUG("[DIAG] HJ_UART_TX bytes=%u written=%u start_ms=%u end_ms=%u elapsed_ms=%u priority_before=%u priority_tx=%u priority_end=%u sd_begin=%u sd_end=%u",
                 (unsigned)(length + (newline ? 2U : 0U)), (unsigned)written,
                 (unsigned)started, (unsigned)finished,
                 (unsigned)(finished - started), (unsigned)originalPriority,
                 (unsigned)priorityTx, (unsigned)priorityEnd,
                 (unsigned)sdBegin.active, (unsigned)sdEnd.active);
        LOG_DEBUG("[DIAG] HJ_UART_CONTEXT start_ms=%u phase=%s effective_before=%u effective_after=%u prefix=%s scope_base=%u guard_ok=%d",
                 (unsigned)started, SdIoDiagnostics::phaseName(sdBegin.phase),
                 (unsigned)effectiveBefore, (unsigned)uxTaskPriorityGet(nullptr), prefix,
                 (unsigned)(priorityScope ? priorityScope->originalPriority() : originalPriority),
                 protectionOk ? 1 : 0);
        if (!protectionOk) {
            LOG_ERROR("[DIAG] HJ_UART_BLOCK reason=priority_not_reached start_ms=%u target=%u actual_start=%u actual_end=%u written=%u",
                      (unsigned)started, (unsigned)Hj212TxGuard::PRIORITY,
                      (unsigned)priorityTx, (unsigned)priorityEnd, (unsigned)written);
        }
    }
    return protectionOk ? written : 0;
}

String DTUDriver::sendCommand(const char* cmd, uint32_t timeout,
                              Hj212PriorityScope* priorityScope, bool* commandWritten) {
    if (commandWritten) *commandWritten = false;
    if (!_stream) return "Err: No Stream";
    while (_stream->available()) { _stream->read(); }

    const size_t length = strlen(cmd);
    if (writeWire(reinterpret_cast<const uint8_t*>(cmd), length, true, priorityScope) != length + 2U)
        return "";
    if (commandWritten) *commandWritten = true;
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
                           uint32_t responseTimeoutMs, Hj212PriorityScope* priorityScope) {
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
    const size_t written = writeWire((const uint8_t *)buf, len, false, priorityScope);
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
