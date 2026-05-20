#ifndef BASE_CALL_H
#define BASE_CALL_H

#include <Arduino.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <map>
#include "../../inc/sys_init.h"

class BaseCollector {
public:
    virtual ~BaseCollector() {}
    virtual bool modbusInit(Stream* new_port, String id, String port_name, float factor, String unit) = 0;
    virtual bool begin() = 0;
    virtual DataPacket* collect() = 0;
    virtual bool gal(int increment, int ratio) = 0;
    virtual String getID() const = 0;

    static std::map<String, BaseCollector*>& getRegistry() {
        static std::map<String, BaseCollector*> _registry;
        return _registry;
    }
    static void registerInstance(const String& id, BaseCollector* instance) {
        if (instance) {
            getRegistry()[id] = instance;
        }
    }
};
struct CollectorRegistrar {
    CollectorRegistrar(const String& id, BaseCollector* instance) {
        BaseCollector::registerInstance(id, instance);
    }
};
#endif