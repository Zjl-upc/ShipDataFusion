#pragma once
# ifndef MESSAGEPROCESSOR_H
# define MESSAGEPROCESSOR_H

#include "DataTypes.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <mutex>
#include "json.hpp"

using json = nlohmann::json;

class MessageProcessor
{
public:
    enum MessageType
    {
        UNKNOWN, GPS, GYRO, AIS, ARPA, EngineStatusBack
    };
    using MessageCallback = std::function<void(const std::string&, void*)>;

    struct HandlerInfo
    {
        MessageCallback callback;
        void* userData;
    };

    static MessageProcessor& GetInstance();

    //注册处理函数
    void registerHandler(MessageType type, MessageCallback callback, void* userData = nullptr);
    //回调入口
    void processMessage(const std::string& jsonData);
    // 协议解析
    GpsData parseGpsData(const std::string& jsonStr);
    GyroData parseGyroData(const std::string& jsonStr);
    AisData parseAisData(const std::string& jsonStr);
    ArpaData parseArpaData(const std::string& jsonStr);
    EngineStatusData parseEngineStatusData(const std::string& jsonStr);
    // 协议生成
    std::string generateFusionData(const FusionData& data);
private:
    MessageProcessor() = default;
    ~MessageProcessor() = default;

    MessageType detectMessageType(const std::string& jsonData);
    MessageType detectType(const std::string& jsonStr);

    std::map<MessageType, HandlerInfo> m_handlers;
    std::mutex m_mutex;
    //std::string m_jsonData;

};

# endif
