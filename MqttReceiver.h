#pragma once
#ifndef MQTTRECEIVER_H
#define MQTTRECEIVER_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include "DataTypes.h"
#include "MessageProcessor.h"

class MqttReceiver : public QObject {
    Q_OBJECT
public:
    explicit MqttReceiver(QObject* parent = nullptr);
    ~MqttReceiver() = default;

public slots:
    // 槽函数：接收来自 MqttClient 的底层数据
    void onRawMessageReceived(const QString& topic, const QByteArray& payload);

signals:
    // 强类型的业务信号，供 FusionManager 订阅
    void gpsUpdated(const GpsData& data);
    void gyroUpdated(const GyroData& data);
    void aisUpdated(const AisData& data);
    void arpaUpdated(const ArpaData& data);
    void engineStatusUpdated(const EngineStatusData& data);

private:
    // 辅助方法，用于预判 JSON 类型，避免过度解析
    MessageProcessor::MessageType detectTypeFast(const std::string& jsonStr);
};

#endif // MQTTRECEIVER_H