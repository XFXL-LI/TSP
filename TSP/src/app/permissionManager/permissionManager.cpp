#include "permissionManager.h"
#include "../../inc/sys_init.h"
#include "../../module/log/log_manager.h"
#include "../../module/file/file_storage.h"
#include "../../module/json/config_json.h"
#include "../../system/event/eventBus.h"

const char *USER_DB_PATH = "/users.db";

const PermissionSystem::CommandRoute PermissionSystem::ROUTE_TABLE[] = {
    {"get_data", EventID::DATA_QUERY_REQ, EventID::DATA_QUERY_RES, PermissionSystem::onDataQueryRes, 3000},
    {"get_config", EventID::CONFIG_QUERY_REQ, EventID::CONFIG_QUERY_RES, PermissionSystem::onConfigRes, 5000},
    {"set_config", EventID::CONFIG_SET_REQ, EventID::CONFIG_SET_RES, PermissionSystem::onSetConfigRes, 2000},
    {"get_records", EventID::RECORD_QUERY_REQ, EventID::RECORD_QUERY_RES, PermissionSystem::onGetRecordsRes, 15000},
    {"gal_data", EventID::GAL_REQ, EventID::GAL_RES, PermissionSystem::onGALRes, 60000},
    {"upload", EventID::UPLOAD_REQ, EventID::UPLOAD_RES, PermissionSystem::onUpload, 60000}
};
const size_t PermissionSystem::ROUTE_COUNT = sizeof(PermissionSystem::ROUTE_TABLE) / sizeof(PermissionSystem::CommandRoute);

PermissionSystem &PermissionSystem::getInstance()
{
    static PermissionSystem instance;
    return instance;
}

PermissionSystem::PermissionSystem()
{
}

void PermissionSystem::begin(const String &lcdPortName, const String &dtuPortName)
{
    _lcdSerialPort = lcdPortName;
    _dtuSerialPort = dtuPortName;
    _Serialmanager = &SerialManager::getInstance();

    _lcdStream = _Serialmanager->getStream(_lcdSerialPort);
    _dtuStream = _Serialmanager->getStream(_dtuSerialPort);
    loadUsersFromStorage();
    LOG_INFO("--- ESP32 Secure Console Ready ---");
}

void PermissionSystem::loadUsersFromStorage()
{
    file_storage &storage = file_storage::getInstance();
    if (!storage.isFFATReady())
    {
        storage.FFatInit();
    }
    String content;
    if (storage.readFFAT(USER_DB_PATH, content) == 0 && content.length() > 0)
    {
        parseUserConfig(content);
        LOG_DEBUG("[Auth] Users loaded from FFat.");
    }
    else
    {
        LOG_DEBUG("[Auth] No config found, creating default users...");
        _userDB["zhouwei"] = {"123456", PermissionLevel::ADMIN};
        _userDB["lizuolang"] = {"123456", PermissionLevel::ADMIN};
        _userDB["admin"] = {"jckj", PermissionLevel::ADMIN};
        _userDB["user"] = {"123456", PermissionLevel::USER};
        _userDB["guest"] = {"123", PermissionLevel::GUEST};
        saveUsersToStorage();
    }
}

void PermissionSystem::saveUsersToStorage()
{
    String data = "";
    for (auto const &[name, user] : _userDB)
    {
        data += name + "," + user.password + "," + String((int)user.level) + "\n";
    }
    file_storage::getInstance().writeFFAT(USER_DB_PATH, data);
}
void PermissionSystem::parseUserConfig(String content)
{
    _userDB.clear();
    int start = 0;
    int end = content.indexOf('\n');

    while (end != -1)
    {
        String line = content.substring(start, end);
        line.trim();
        if (line.length() > 0)
        {
            int firstComma = line.indexOf(',');
            int lastComma = line.lastIndexOf(',');

            if (firstComma != -1 && lastComma != -1 && firstComma != lastComma)
            {
                String name = line.substring(0, firstComma);
                String pass = line.substring(firstComma + 1, lastComma);
                int level = line.substring(lastComma + 1).toInt();
                _userDB[name] = {pass, static_cast<PermissionLevel>(level)};
            }
        }
        start = end + 1;
        end = content.indexOf('\n', start);
    }
}

void PermissionSystem::sendMsg(Stream *stream, const char *msg)
{
    LOG_DEBUG("Msg: %s", msg);
    stream->println(msg);
    LOG_DEBUG("send ok");
}
void PermissionSystem::poll()
{
    if (_isLoggedIn && (millis() - _lastActivity > TIMEOUT_MS))
    {
        handleLogout();
    }
}
void PermissionSystem::handleStreamInput(Stream *stream, String &buf)
{
    unsigned long now = millis();
    bool isReceiving = false;
    unsigned long lastByteTime = now;
    String test = "";
    int bracketLevel = 0;
    while (stream->available() > 0)
    {
        while(stream && stream->available() > 0)
        {
            char c = stream->read();
            lastByteTime = millis();

            if (!isReceiving) {
                if (c == '{') {
                    isReceiving = true;
                    test = "{";
                    bracketLevel = 1;
                }
                continue; 
            }
            test += c;
            if (c == '{') bracketLevel++;
            else if (c == '}') bracketLevel--;
            if (isReceiving && bracketLevel == 0) {
                LOG_DEBUG("Received TRUE Full JSON: %s", test.c_str());
                // 这里调用你的 processLine(test);
                isReceiving = false;
                test = "";
                break;
            }

            if (test.length() > 2000) {
                isReceiving = false; test = ""; bracketLevel = 0;
                break;
            }
        }
        if (isReceiving && (millis() - lastByteTime > 500)) {
            LOG_WARNING("JSON reception timeout, resetting...");
            isReceiving = false;
            test = "";
            bracketLevel = 0;
        }
    }
}

void PermissionSystem::processLine(String line, Stream *stream)
{
    LOG_DEBUG("Auth Receive: %s", line.c_str());

    // 使用你封装的 cJSON 工具类解析
    config_json json(line.c_str());

    // 1. 如果是合法的 JSON 格式
    if (json.isValid())
    {
        LOG_DEBUG("json is valid");
        String op = json.getString("operation", "");

        // --- 处理登录 ---
        if (op == "login")
        {
            LOG_DEBUG("Start login");
            // 支持 JSON 格式登录: {"operation":"login","user":"admin","pass":"123456"}
            String u = json.getString("user", "");
            String p = json.getString("pass", "");
            handleLogin(u, p, stream);
        }
        else if (op == "logout")
        {
            handleLogout();
            sendResponse(stream, "logout", "OK", "logout success");
        }
        else if (op == "get_data")
        {
            JSONCmdData *data = new JSONCmdData();
            data->command = "get_data";
            data->arguments = line;
            dispatchBusiness(data, stream);
        }
        else if (op == "restart")
        {
            sendResponse(stream, "restart", "OK", "Device restarting...");
            ESP.restart();
        }
        else if (op != "")
        {
            executeCommand(op, line, stream);
        }
    }
    else
    {
        sendResponse(stream, "ERROR", "NG", "Not parse json, json is error");
    }
}

void PermissionSystem::handleLogin(String user, String pass, Stream *stream)
{
    if (_userDB.count(user) && _userDB[user].password == pass)
    {
        _isLoggedIn = true;
        _currentUser = user;
        _currentLevel = _userDB[user].level;
        _lastActivity = millis();
        LOG_INFO("[Auth] %s logged", user.c_str());
        if ((int)_currentLevel == 0){
            sendResponse(stream, "login", "OK", "guest");
        } else if ((int)_currentLevel == 1) {
            sendResponse(stream, "login", "OK", "user");
        } else if ((int)_currentLevel == 2) {
            sendResponse(stream, "login", "OK", "admin");
        } else {
            sendResponse(stream, "login", "OK", "guest");
        }
    }
    else
    {
        LOG_ERROR("[Auth] Login failed for user: %s", user.c_str());
        vTaskDelay(pdMS_TO_TICKS(500));
        sendResponse(stream, "login", "NG", "login fail, user or pass error");
    }
}

void PermissionSystem::handleLogout()
{
    _isLoggedIn = false;
    _currentUser = "Guest";
    _currentLevel = PermissionLevel::GUEST;
}

bool PermissionSystem::hasAccess(PermissionLevel required)
{
    return static_cast<int>(_currentLevel) >= static_cast<int>(required);
}

void PermissionSystem::executeCommand(String cmd, String args, Stream *stream)
{
    if (!_isLoggedIn)
    {
        sendResponse(stream, cmd, "NG", "Account not logged in");
        return;
    }
    PermissionLevel requiredLevel = PermissionLevel::ADMIN;
    bool found = false;
    for (const auto &entry : PERMISSION_TABLE)
    {
        if (cmd == entry.cmd)
        {
            requiredLevel = entry.level;
            found = true;
            break;
        }
    }
    if (!hasAccess(requiredLevel))
    {
        String msg = "Permission denied: " + String(requiredLevel == PermissionLevel::ADMIN ? "ADMIN" : "USER") + " required";
        sendResponse(stream, cmd, "NG", msg);
        return;
    }
    JSONCmdData *data = new JSONCmdData();
    data->command = cmd;
    data->arguments = args;
    dispatchBusiness(data, stream);
}
void PermissionSystem::dispatchBusiness(JSONCmdData *data, Stream *stream)
{
    const CommandRoute *route = nullptr;
    for (int i = 0; i < ROUTE_COUNT; i++)
    {
        if (data->command == ROUTE_TABLE[i].cmd)
        {
            route = &ROUTE_TABLE[i];
            break;
        }
    }
    if (!route)
    {
        sendResponse(stream, data->command, "NG", "Unknown operation");
        data->release();
        return;
    }
    executeAsyncRoute(data, stream, route);
}
void PermissionSystem::executeAsyncRoute(JSONCmdData *data, Stream *stream, const CommandRoute *route)
{
    QueueHandle_t resQueue = EventBus::getInstance().createReceiverQueue(1);
    EventBus::getInstance().subscribe(route->resID, resQueue);
    sendEventData(data, route->reqID);
    EventMsg msg;
    if (EventBus::waitEvent(resQueue, msg, route->timeout))
    {
        if (msg.id == route->resID && route->handler != nullptr)
        {
            route->handler(msg.data, stream, data->command, data->arguments);
        }
    }
    else
    {
        sendResponse(stream, data->command, "NG", "Response timeout");
    }
    EventBus::getInstance().unsubscribe(route->resID, resQueue);
    vQueueDelete(resQueue);
    data->release();
}

void PermissionSystem::sendEventData(JSONCmdData *data, EventID id)
{
    int subCount = EventBus::getInstance().getSubscriberCount(id);
    for (int i = 0; i < subCount; i++)
    {
        data->retain();
    }
    EventBus::getInstance().publish(id, (void *)data);
}

void PermissionSystem::sendResponse(Stream *stream, String op, String code, String msg)
{
    config_json res;
    res.buildResponse(op.c_str(), code.c_str(), msg.c_str());

    char *resChar = res.serialize(false);
    if (resChar)
    {
        sendMsg(stream, resChar);
        free(resChar);
    }
}

// ***************************************    回调函数    ***************************************
void PermissionSystem::onDataQueryRes(void *eventData, Stream *stream, String cmd, String args)
{
    auto *packet = static_cast<AllProcessedDataPacket *>(eventData);
    if (packet)
    {
        getInstance().getData(packet, stream, cmd, args);
        packet->release();
    }
}

void PermissionSystem::onConfigRes(void *eventData, Stream *stream, String cmd, String args)
{
    LOG_DEBUG("Handling Config Response");
    auto *resData = static_cast<configData *>(eventData);
    if (resData)    {
        String jsonRes = "{";
        jsonRes += "\"operation\":\"" + resData->cmd + "\",";
        jsonRes += "\"code\":\"OK\",";
        jsonRes += "\"config\":\" " + resData->fileName + "\",";
        jsonRes += "\"content\":" + resData->content;
        jsonRes += "}";
        getInstance().sendMsg(stream, jsonRes.c_str());
        resData->release();
    }
}

void PermissionSystem::onSetConfigRes(void *eventData, Stream *stream, String cmd, String args)
{
    LOG_DEBUG("Handling SetConfig Response");
    auto *resData = static_cast<configData *>(eventData);
    if (resData)    {
        String jsonRes = "{";
        jsonRes += "\"operation\":\"" + resData->cmd + "\",";
        jsonRes += "\"code\":\"OK\",";
        jsonRes += "\"config\":\" " + resData->fileName + "\",";
        jsonRes += "\"content\":" + resData->content;
        jsonRes += "}";
        getInstance().sendMsg(stream, jsonRes.c_str());
        resData->release();
    }
}

void PermissionSystem::onGetRecordsRes(void *eventData, Stream *stream, String cmd, String args){
    LOG_DEBUG("TEST OK onGetRecordsRes callback");
}

void PermissionSystem::onGALRes(void *eventData, Stream *stream, String cmd, String args){
    LOG_DEBUG("TEST OK onGALRes callback");
    auto *resData = static_cast<galDataRes *>(eventData);
    if (!resData) {
        return;
    }
    bool isAllSuccess = !resData->results.empty(); 
    for (const auto& result : resData->results) {
        if (!result.galRes) {
            isAllSuccess = false;
            break;
        }
    }
    String codeStr = isAllSuccess ? "OK" : "NG";
    String jsonRes;
    jsonRes.reserve(256);
    jsonRes += "{";
    jsonRes += "\"operation\":\"" + resData->cmd + "\",";
    jsonRes += "\"code\":\"" + codeStr + "\",";
    jsonRes += "\"params\":\"gal_data\",";
    jsonRes += "\"values\":[";
    for (size_t i = 0; i < resData->results.size(); i++) {
        const auto& result = resData->results[i];
        jsonRes += "{";
        jsonRes += "\"id\":\"" + result.sensor_id + "\",";
        jsonRes += "\"galRes\":" + String(result.galRes ? "true" : "false");
        jsonRes += "}";
        if (i < resData->results.size() - 1) {
            jsonRes += ",";
        }
    }
    jsonRes += "]}";
    getInstance().sendMsg(stream, jsonRes.c_str());
    resData->release();
}
void PermissionSystem::onUpload(void *eventData, Stream *stream, String cmd, String args){

}

// ***************************************    回调函数    ***************************************

// ***************************************    打包处理    ***************************************
void PermissionSystem::getData(AllProcessedDataPacket *allData, Stream *stream, const String &cmd, const String &args)
{
    config_json request(args.c_str());
    cJSON *idsArray = request.getArray("ids");
    String jsonRes = "{";
    jsonRes += "\"operation\":\"" + cmd + "\",";
    jsonRes += "\"code\":\"OK\",";
    jsonRes += "\"message\":\"get data success\",";
    jsonRes += "\"params\":[";
    if (idsArray != nullptr)
    {
        int arraySize = cJSON_GetArraySize(idsArray);
        for (int i = 0; i < arraySize; i++)
        {
            cJSON *idItem = cJSON_GetArrayItem(idsArray, i);
            if (cJSON_IsString(idItem))
            {
                jsonRes += "\"" + String(idItem->valuestring) + "\"";
                if (i < arraySize - 1)
                    jsonRes += ",";
            }
        }
    }
    jsonRes += "],";
    jsonRes += "\"values\":[";
    if (idsArray != nullptr && allData != nullptr)
    {
        int arraySize = cJSON_GetArraySize(idsArray);
        for (int i = 0; i < arraySize; i++)
        {
            cJSON *idItem = cJSON_GetArrayItem(idsArray, i);
            if (cJSON_IsString(idItem))
            {
                const char *targetId = idItem->valuestring;
                if (strcmp(targetId, "csq") == 0)
                {
                    int csq = 99;
                    if (csqInfo.mutex == NULL)    {
                        LOG_ERROR("Failed to create mutex for CSQ info");
                    } else if (xSemaphoreTake(csqInfo.mutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
                        csq = csqInfo.csq;
                    }
                    jsonRes += String(csq);
                    if (i < arraySize - 1)
                        jsonRes += ",";
                } else {
                    float val = 0.00;

                    if (allData->processed_data_map.count(targetId))
                    {
                        auto &packet = allData->processed_data_map[targetId];
                        if (packet.is_valid)
                            val = packet.value;
                    }

                    // 使用 char 缓冲区强制格式化
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%.2f", val);
                    jsonRes += String(buf); // 直接拼接数字文本，不带双引号

                    if (i < arraySize - 1)
                        jsonRes += ",";
                }
            }
        }
    }
    jsonRes += "]}";
    sendMsg(stream, jsonRes.c_str());
}



// ***************************************    打包处理    ***************************************