#ifndef CONFIG_JSON_H
#define CONFIG_JSON_H

#include <Arduino.h>
#include <cJSON.h>

class config_json {
private:
    cJSON* root;

    // 内部辅助
    cJSON* get_item(const char* key) const;

public:
    config_json();
    config_json(const char* json_str); // 支持直接构造
    ~config_json();

    bool setArray(const char* key);
    bool addToArray(const char* key, const char* value);
    bool addToArray(const char* key, double value);
    void buildResponse(const char* op, const char* code, const char* msg);

    // 核心接口：解析字符串
    bool parse(const char* json_str);
    
    // 核心接口：导出字符串 (需手动 free 结果)
    char* serialize(bool formatted = false) const;
    bool setString(const char* key, const char* value);
    bool setInt(const char* key, int value);
    cJSON* getJsonObject() const { return root; }
    
    // 类型安全的获取接口
    int     getInt(const char* key, int defaultValue = 0) const;
    float   getFloat(const char* key, float defaultValue = 0.0f) const;
    double  getDouble(const char* key, double defaultValue = 0.0) const;
    String  getString(const char* key, String defaultValue = "") const;
    bool    getBool(const char* key, bool defaultValue = false) const;
    
    // 结构获取接口
    cJSON* getArray(const char* key) const;
    cJSON* getObject(const char* key) const;

    // 状态检查
    bool    isValid() const { return root != nullptr; }
    bool    contains(const char* key) const;
    void    clear();
};

#endif