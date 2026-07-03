#include "MqttReceiver.h"
#include "json.hpp" // 引入 nlohmann::json
#include <QDebug>
#include <iostream>

using json = nlohmann::json;

MqttReceiver::MqttReceiver(QObject* parent) : QObject(parent) {
}

void MqttReceiver::onRawMessageReceived(const QString& topic, const QByteArray& payload) {
    std::string jsonStr = payload.toStdString();

    // 1. 快速探测消息类型
    MessageProcessor::MessageType type = detectTypeFast(jsonStr);

    // 2. 调用单例 MessageProcessor 进行解析，并通过信号发射
    // (注意：后续我们也可以把 MessageProcessor 改造成非单例的工具类，但目前先保持兼容)
    auto& parser = MessageProcessor::GetInstance();

    switch (type) {
    case MessageProcessor::GPS:
        emit gpsUpdated(parser.parseGpsData(jsonStr));
        break;
    case MessageProcessor::GYRO:
        emit gyroUpdated(parser.parseGyroData(jsonStr));
        break;
    case MessageProcessor::AIS:
        emit aisUpdated(parser.parseAisData(jsonStr));
        break;
    case MessageProcessor::ARPA:
        emit arpaUpdated(parser.parseArpaData(jsonStr));
        break;
    case MessageProcessor::EngineStatusBack:
        emit engineStatusUpdated(parser.parseEngineStatusData(jsonStr));
        break;
    default:
        // 未知类型或无需处理的消息，直接忽略
        break;
    }
}

MessageProcessor::MessageType MqttReceiver::detectTypeFast(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        if (j.contains("DeviceType")) {
            std::string devicetype = j.value("DeviceType", "");
            if (devicetype.find("GPS") == 0) return MessageProcessor::GPS;
            if (devicetype.find("GYRO") == 0) return MessageProcessor::GYRO;
        }
        if (j.contains("DataType")) {
            std::string datatype = j.value("DataType", "");
            if (datatype == "AisData") return MessageProcessor::AIS;
            if (datatype == "ArpaData") return MessageProcessor::ARPA;
            if (datatype == "EngineStatusBack") return MessageProcessor::EngineStatusBack;
        }
    }
    catch (const std::exception& e) {
        qWarning() << "[MqttReceiver] JSON解析探测失败:" << e.what();
    }
    return MessageProcessor::UNKNOWN;
}