#ifndef DTU_DRIVER_H
#define DTU_DRIVER_H

#include <Arduino.h>

#define GET_TIME_COMM           "config,get,nettime"
#define GET_CSQ_COMM            "config,get,csq"
#define GET_NETSTATUS_COMM      "config,get,netstatus,1"
#define CONFIG_SAVE_COM         "config,set,save"
#define GET_NETINFO_COM         "config,get,netchaninfo,1"
#define GET_UARTINFO_COM        "config,get,uart"
#define GET_TTLUARTINFO_COM     "config,get,ttluart"
#define GET_VBATT_COM           "config,get,vbatt"
#define SET_TCPIP_COM_ttlUart   "config,set,tcp,1,ttluart,1,0,00,60," // + 111.229.111.76,2088
#define SET_TCPIP_COM_Uart      "config,set,tcp,1,uart,1,0,00,60,"  // + 111.229.111.76,2088
#define SET_IP_END              ",0,0,0,0,0,0,0,0"


class DTUDriver {
private:
    Stream* _stream;
    String  _name;
    int     _id;

public:
    DTUDriver(String name, int id);
    ~DTUDriver();
    void setStream(Stream& stream) { _stream = &stream; };
    
    String sendCommand(const char* cmd, uint32_t timeout = 3000);
    String sendData(const String& data,
                    bool waitForResponse = true,
                    uint32_t responseTimeoutMs = 5000);
    int readCSQ();
    bool checkOnline();
    
    int getID() const { return _id; }
    String getName() const { return _name; }
};

#endif
