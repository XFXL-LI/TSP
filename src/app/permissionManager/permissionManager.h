#ifndef PERMISSION_SYSTEM_H
#define PERMISSION_SYSTEM_H

#include <Arduino.h>
#include <map>
#include "../../inc/sys_init.h"
#include "../../system/event/eventBus.h"
#include "../../module/Serial/SerialManager.h"

class PermissionSystem;

enum class PermissionLevel
{
    GUEST = 0, // 访客
    USER = 1,  // 操作员
    ADMIN = 2  // 管理员
};
struct CommandPermission
{
    const char *cmd;
    PermissionLevel level;
};

const CommandPermission PERMISSION_TABLE[] = {
    {"gal_data", PermissionLevel::USER},
    {"get_config", PermissionLevel::USER},
    {"restart", PermissionLevel::USER},
    {"set_log", PermissionLevel::USER},
    {"upload", PermissionLevel::USER},
    {"get_records", PermissionLevel::USER},
    {"set_config", PermissionLevel::ADMIN},
    {"update_profile", PermissionLevel::ADMIN}};

class PermissionSystem
{
public:
    typedef void (*ResponseHandler)(void *eventData, Stream *stream, String cmd, String args);
    struct CommandRoute
    {
        const char *cmd;
        EventID reqID;
        EventID resID;
        ResponseHandler handler;
        uint32_t timeout;
    };
    static const CommandRoute ROUTE_TABLE[];
    static const size_t ROUTE_COUNT;

    static PermissionSystem &getInstance();

    void begin(const String &lcdPort, const String &dtuPort);
    void poll();
    void processLine(String line, Stream *stream);

    bool hasAccess(PermissionLevel required);
    String getCurrentUser() { return _currentUser; }

private:
    PermissionSystem();

    void loadUsersFromStorage();
    void saveUsersToStorage();
    void parseUserConfig(String);
    void handleLogin(String user, String pass, Stream *stream);
    void handleLogout();
    void executeCommand(String cmd, String args, Stream *stream);
    void dispatchBusiness(JSONCmdData *data, Stream *stream);
    void sendResponse(Stream *stream, String op, String code, String msg);
    void sendEventData(JSONCmdData *data, EventID id);
    void executeAsyncRoute(JSONCmdData *data, Stream *stream, const CommandRoute *route);
    bool handleGenericRequest(JSONCmdData *data, Stream *stream, EventID reqID, EventID resID);

    void sendMsg(Stream *stream, const char *msg);

    // 回调
    static void onDataQueryRes(void *eventData, Stream *stream, String cmd, String args);
    void getData(AllProcessedDataPacket *allData, Stream *stream, const String &cmd, const String &args);
    void getRecordsData(AllProcessedDataPacket *allData, Stream *stream, const String &cmd, const String &args);
    static void onConfigRes(void *eventData, Stream *stream, String cmd, String args);
    static void onSetConfigRes(void *eventData, Stream *stream, String cmd, String args);
    static void onGetRecordsRes(void *eventData, Stream *stream, String cmd, String args);
    static void onGALRes(void *eventData, Stream *stream, String cmd, String args);
    static void onUpload(void *eventData, Stream *stream, String cmd, String args);
    static void onDTUCommand(void *eventData, Stream *stream, String cmd, String args);
    struct User
    {
        String password;
        PermissionLevel level;
    };

    std::map<String, User> _userDB;
    String _lcdSerialPort;
    String _dtuSerialPort;

    SerialManager *_Serialmanager = nullptr;
    Stream *_lcdStream = nullptr;
    Stream *_dtuStream = nullptr;

    String _lcdBuf = "";
    String _dtuBuf = "";

    void handleStreamInput(Stream *stream, String &buf);

    bool _isLoggedIn = false;
    String _currentUser = "Guest";
    PermissionLevel _currentLevel = PermissionLevel::GUEST;
    unsigned long _lastActivity = 0;
    const unsigned long TIMEOUT_MS = 900000;
};

#endif