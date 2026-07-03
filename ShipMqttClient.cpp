#include "ShipMqttClient.h"
#include "MessageProcessor.h"
#include "FusionManager.h"
#include "ConfigManager.h"
#include <QDebug>

ShipMqttClient::ShipMqttClient(QObject* parent)
    : QObject(parent) {
    registerAllHandlers();
}

ShipMqttClient::~ShipMqttClient() {
    stop();
}

void ShipMqttClient::registerAllHandlers() {
    auto& processor = MessageProcessor::GetInstance();

    processor.registerHandler(MessageProcessor::GPS, [](const std::string& json, void*) {
        FusionManager::GetInstance().updateGps(MessageProcessor::GetInstance().parseGpsData(json));
        });
    processor.registerHandler(MessageProcessor::GYRO, [](const std::string& json, void*) {
        FusionManager::GetInstance().updateGyro(MessageProcessor::GetInstance().parseGyroData(json));
        });
    processor.registerHandler(MessageProcessor::AIS, [](const std::string& json, void*) {
        FusionManager::GetInstance().updateAisData(MessageProcessor::GetInstance().parseAisData(json));
        });
    processor.registerHandler(MessageProcessor::EngineStatusBack, [](const std::string& json, void*) {
        FusionManager::GetInstance().updateEngineStatus(MessageProcessor::GetInstance().parseEngineStatusData(json));
        });
    processor.registerHandler(MessageProcessor::ARPA, [](const std::string& json, void*) {
        FusionManager::GetInstance().updateArpaData(MessageProcessor::GetInstance().parseArpaData(json));
        });
}

void ShipMqttClient::init(const std::string& ip, int port, const std::string&, const std::string&) {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    ConfigManager& config = ConfigManager::getInstance();
    // 2. 从配置中赋值 Topic
    m_subTopic = config.subTopic;
    m_pubTopic = config.pubTopic;
    if (m_client) {
        MQTTClient_disconnect(m_client, 1000);
        MQTTClient_destroy(&m_client);
        m_client = nullptr;
    }

    std::string serverAddr = "tcp://" + ip + ":" + std::to_string(port);
    MQTTClient_create(&m_client, serverAddr.c_str(), "ShipFusionSystem", MQTTCLIENT_PERSISTENCE_NONE, nullptr);
}

bool ShipMqttClient::start() {
    if (m_running) return true;

    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        if (!m_client) {
            qCritical() << "MQTTClient 未初始化!";
            return false;
        }
        MQTTClient_setCallbacks(m_client, this, onConnectionLost, onMessageArrived, nullptr);
    }

    m_running = true;
    m_reportThread = std::thread(&ShipMqttClient::reportLoop, this);
    qDebug() << "MQTT 启动成功!";
    return true;
}

void ShipMqttClient::stop() {
    m_running = false;

    if (m_reportThread.joinable()) {
        m_reportThread.join();
    }

    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (m_client) {
        MQTTClient_disconnect(m_client, 1000);
        MQTTClient_destroy(&m_client);
        m_client = nullptr;
    }

    m_connected = false;
    m_connecting = false;
}

bool ShipMqttClient::connectOnce() {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (!m_client || m_connecting) return false;
    m_connecting = true;

    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    int rc = MQTTClient_connect(m_client, &conn_opts);
    if (rc == MQTTCLIENT_SUCCESS) {
        MQTTClient_subscribe(m_client, m_subTopic.c_str(), 1);
        m_connected = true;
        qDebug() << "MQTT 连接成功";
    }
    else {
        m_connected = false;
        qDebug() << "MQTT 连接失败, rc =" << rc;
    }

    m_connecting = false;
    return m_connected;
}

void ShipMqttClient::reportLoop() {
    while (m_running) {
        if (!m_connected) {
            qDebug() << "MQTT 未连接，尝试重连...";
            connectOnce();
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        FusionData fd = FusionManager::GetInstance().createFusionResult();
        std::string jsonStr = MessageProcessor::GetInstance().generateFusionData(fd);

        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        pubmsg.payload = const_cast<char*>(jsonStr.c_str());
        pubmsg.payloadlen = static_cast<int>(jsonStr.length());
        pubmsg.qos = 1;
        pubmsg.retained = 0;

        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            if (m_client) {
                int rc = MQTTClient_publishMessage(m_client, m_pubTopic.c_str(), &pubmsg, nullptr);
                if (rc != MQTTCLIENT_SUCCESS) {
                    qDebug() << "MQTT 发布失败, rc =" << rc;
                    m_connected = false;
                }
            }
        }
        //发送的时间
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

int ShipMqttClient::onMessageArrived(void* context, char* topicName, int topicLen, MQTTClient_message* message) {
    std::string payload(static_cast<char*>(message->payload), message->payloadlen);
    MessageProcessor::GetInstance().processMessage(payload);

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void ShipMqttClient::onConnectionLost(void* context, char* cause) {
    ShipMqttClient* self = static_cast<ShipMqttClient*>(context);
    qWarning() << "MQTT 连接丢失:" << cause;
    self->m_connected = false;
}
