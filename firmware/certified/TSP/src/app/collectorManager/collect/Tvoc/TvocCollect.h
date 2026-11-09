#pragma once
#include "../../../../module/modbus/modbus_manager.h"
#include "../../../../system/call/base_call.h"
#include "../../../../module/tvoc/TvocPolicy.h"

class TvocCollect : public BaseCollector {
private:
    TvocCollect() = default;
    String _id;
    Stream* _port = nullptr;
    modbus_manager* _modbus = nullptr;
    bool _versionVerified = false;
public:
    static TvocCollect& getInstance();
    bool begin() override;
    bool modbusInit(Stream* port, String id, String portName,
                    float factor, String unit) override;
    bool gal(int increment, int ratio) override;
    String getID() const override { return _id; }
    DataPacket* collect() override;
    DataPacket* collect(ReadDeadline deadline) override;
};
