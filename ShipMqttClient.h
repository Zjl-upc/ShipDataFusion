#pragma once
#include <QObject>
#include <thread>
#include <atomic>
#include <string>
#include <mutex>
#include "DataTypes.h"

extern "C" {
#include <MQTTClient.h> 
}

class ShipMqttClient : public QObject {
    Q_OBJECT
public:
    explicit ShipMqttClient(QObject* parent = nullptr);
    ~ShipMqttClient();

    // 初始化配置
    void init(const std::string& ip, int port, const std::string& user, const std::string& pass);

    // 启动与停止服务
    bool start();
    void stop();

private:
    // 初始化时注册所有消息处理逻辑
    void registerAllHandlers();

    // 定期上报融合数据的后台线程函数
    void reportLoop();
    bool connectOnce();

    // Paho MQTT 静态回调
    static int onMessageArrived(void* context, char* topicName, int topicLen, MQTTClient_message* message);
    static void onConnectionLost(void* context, char* cause);

    MQTTClient m_client = nullptr;
    // std::string m_subTopic = "/info/parseNavigate"; // 传感器原始数据主题
    std::string m_subTopic;
    std::string m_pubTopic;    // 融合结果上报主题

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_connected{ false };
    std::atomic<bool> m_connecting{ false };
    std::thread m_reportThread;
    std::mutex m_clientMutex;
};