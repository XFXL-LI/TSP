#include "TvocCollect.h"
#include "../../../../module/Serial/SerialManager.h"
#include "../../../../module/log/log_manager.h"

static CollectorRegistrar registrarTvoc(TvocPolicy::ID, &TvocCollect::getInstance());

TvocCollect& TvocCollect::getInstance() {
    static TvocCollect instance;
    return instance;
}
bool TvocCollect::begin() { return _port != nullptr && _id == TvocPolicy::ID; }
bool TvocCollect::modbusInit(Stream* port, String id, String portName,
                            float factor, String unit) {
    (void)portName; (void)factor; (void)unit;
    if (port == nullptr || id != TvocPolicy::ID) return false;
    _id = id;
    _port = port;
    _versionVerified = false;
    if (_modbus == nullptr) _modbus = new modbus_manager();
    _modbus->modbus_init(port);
    return true;
}
bool TvocCollect::gal(int increment, int ratio) {
    (void)increment; (void)ratio;
    return false; // This read-only adapter never reports a fictitious calibration success.
}
DataPacket* TvocCollect::collect() { return collect(ReadDeadline{}); }
DataPacket* TvocCollect::collect(ReadDeadline deadline) {
    DataPacket* packet = new DataPacket();
    strncpy(packet->sensor_id, _id.c_str(), sizeof(packet->sensor_id) - 1);
    if (!begin() || !deadline.fits(millis(), 1500)) return packet;
    auto mutex = SerialManager::getInstance().getMutex(SERIAL_485);
    if (mutex == nullptr || xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return packet;
    // readModbusRegs also uses a bounded 200 ms internal bus lock.
    auto read = [&](uint16_t address, uint16_t count, uint16_t* destination) {
        return deadline.fits(millis(), 700) &&
               _modbus->readModbusRegs(TvocPolicy::SLAVE, address, count,
                                     destination, 1, 500, 0) &&
               (!deadline.enabled || deadline.remainingMs(millis()) > 0);
    };
    if (!_versionVerified) {
        uint16_t version[3] = {};
        if (!read(TvocPolicy::VERSION_REGISTER, 3, version)) {
            xSemaphoreGive(mutex);
            return packet;
        }
        if (version[0] != 1 || version[1] != 2 || version[2] != 0) {
            packet->status = DataStatus::SENSOR_FAULT;
            LOG_WARNING("TVOC_PROTOCOL unsupported=%u.%u.%u expected=1.2.0",
                        version[0], version[1], version[2]);
            xSemaphoreGive(mutex);
            return packet;
        }
        _versionVerified = true;
    }
    const TvocPolicy::Sample sample = TvocPolicy::readSample(read);
    xSemaphoreGive(mutex);
    packet->raw_ppb = sample.ppb;
    packet->value = float(sample.ppb);
    packet->is_valid = sample.valid;
    packet->status = static_cast<DataStatus>(sample.status);
    packet->module_flags = sample.flags;
    LOG_DEBUG("TVOC_SAMPLE ppb=%lu valid=%d flag=%c flags=0x%04X cal_state=%u cal_error=%u",
              (unsigned long)sample.ppb, sample.valid ? 1 : 0, sample.status,
              sample.flags, sample.calibrationState, sample.calibrationError);
    return packet;
}
