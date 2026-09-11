#include "config_json.h"

config_json::config_json() : root(nullptr) {}

config_json::config_json(const char* json_str) : root(nullptr) {
    parse(json_str);
}

config_json::~config_json() { clear(); }

void config_json::clear() {
    if (root) {
        cJSON_Delete(root);
        root = nullptr;
    }
}
bool config_json::setArray(const char* key) {
    if (!root) root = cJSON_CreateObject();
    if (contains(key)) {
        cJSON_DeleteItemFromObject(root, key);
    }
    cJSON_AddItemToObject(root, key, cJSON_CreateArray());
    return true;
}

// 向数组中追加字符串
bool config_json::addToArray(const char* key, const char* value) {
    cJSON* arr = getArray(key);
    if (!arr || !cJSON_IsArray(arr)) return false;
    cJSON_AddItemToArray(arr, cJSON_CreateString(value));
    return true;
}

// 向数组中追加数字
bool config_json::addToArray(const char* key, double value) {
    cJSON* arr = getArray(key);
    if (!arr || !cJSON_IsArray(arr)) return false;
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(value));
    return true;
}

// 快速构建基础响应结构的辅助方法
void config_json::buildResponse(const char* op, const char* code, const char* msg) {
    clear();
    root = cJSON_CreateObject();
    setString("operation", op);
    setString("code", code);
    setString("message", msg);
}

// 传入 JSON 字符串进行解析
bool config_json::parse(const char* json_str) {
    clear();
    if (json_str == nullptr) return false;
    
    root = cJSON_Parse(json_str);
    if (root == nullptr) {
        // 可选：log_e("JSON Parse Error");
        return false;
    }
    return true;
}
// 在 config_json 类中补充这些方法
bool config_json::setString(const char* key, const char* value) {
    if (!root) root = cJSON_CreateObject();
    cJSON* item = cJSON_CreateString(value);
    if (contains(key)) {
        cJSON_ReplaceItemInObject(root, key, item);
    } else {
        cJSON_AddItemToObject(root, key, item);
    }
    return true;
}

bool config_json::setInt(const char* key, int value) {
    if (!root) root = cJSON_CreateObject();
    cJSON* item = cJSON_CreateNumber(value);
    if (contains(key)) {
        cJSON_ReplaceItemInObject(root, key, item);
    } else {
        cJSON_AddItemToObject(root, key, item);
    }
    return true;
}
// 将内存中的对象转回字符串
char* config_json::serialize(bool formatted) const {
    if (!root) return nullptr;
    return formatted ? cJSON_Print(root) : cJSON_PrintUnformatted(root);
}

cJSON* config_json::get_item(const char* key) const {
    if (!root || !key) return nullptr;
    return cJSON_GetObjectItemCaseSensitive(root, key);
}

bool config_json::contains(const char* key) const {
    return get_item(key) != nullptr;
}

int config_json::getInt(const char* key, int defaultValue) const {
    cJSON* item = get_item(key);
    return cJSON_IsNumber(item) ? item->valueint : defaultValue;
}

float config_json::getFloat(const char* key, float defaultValue) const {
    cJSON* item = get_item(key);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : defaultValue;
}

double config_json::getDouble(const char* key, double defaultValue) const {
    cJSON* item = get_item(key);
    return cJSON_IsNumber(item) ? item->valuedouble : defaultValue;
}

String config_json::getString(const char* key, String defaultValue) const {
    cJSON* item = get_item(key);
    return cJSON_IsString(item) ? String(item->valuestring) : defaultValue;
}

bool config_json::getBool(const char* key, bool defaultValue) const {
    cJSON* item = get_item(key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return defaultValue;
}

cJSON* config_json::getArray(const char* key) const {
    cJSON* item = get_item(key);
    return cJSON_IsArray(item) ? item : nullptr;
}

cJSON* config_json::getObject(const char* key) const {
    cJSON* item = get_item(key);
    return cJSON_IsObject(item) ? item : nullptr;
}