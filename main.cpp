#include <QApplication>
#include <QDebug>
#include <QMetaType>
#include <QTimer>
#include <QDateTime>

#include "ConfigManager.h"
#include "MqttClient.h"
#include "MqttReceiver.h"
#include "FusionManager.h"
#include "FFmpegWidget.h"
#include "DataTypes.h"
#include "UIDataTypes.h"

int main(int argc, char* argv[])
{
    // 1. 初始化 Qt 应用程序
    QApplication a(argc, argv);

    // 2. 核心：注册跨线程传输的自定义元类型
    // 如果没有这些注册，Qt 的 QueuedConnection 将无法传递复杂的结构体
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<GpsData>("GpsData");
    qRegisterMetaType<GyroData>("GyroData");
    qRegisterMetaType<AisData>("AisData");
    qRegisterMetaType<ArpaData>("ArpaData");
    qRegisterMetaType<EngineStatusData>("EngineStatusData");
    qRegisterMetaType<FusionData>("FusionData");
    qRegisterMetaType<UIRenderSnapshot>("UIRenderSnapshot");
    qRegisterMetaType<std::vector<SingleObj_DetectData>>("std::vector<SingleObj_DetectData>");

    // 设置日志输出格式
    qSetMessagePattern("[%{time h:mm:ss.zzz} %{type}] %{message}");

    qDebug() << "=== 智能船舶多源数据融合系统 启动 ===";

    // 3. 加载配置文件 (单例初始化)
    ConfigManager& config = ConfigManager::getInstance();
    if (!config.loadConfig("../model/config.json")) {
        qCritical() << "致命错误: 无法加载 config.json，请检查文件路径！";
        return -1;
    }
    qDebug() << "配置文件加载成功，环境初始化中...";

    // 4. 实例化各解耦模块
    MqttClient* mqttClient = new MqttClient();         // 底层通信层
    MqttReceiver* mqttReceiver = new MqttReceiver();   // 协议解析层
    FFmpegWidget* mainWindow = new FFmpegWidget();     // UI 展现层 (含 AI 调度线程)
    FusionManager& fusionCenter = FusionManager::GetInstance(); // 融合计算层 (单例)

    // 5. 信号槽“逻辑装配” (The Wiring)

    // --- (A) 网络数据流向解析层 ---
    QObject::connect(mqttClient, &MqttClient::rawMessageReceived,
        mqttReceiver, &MqttReceiver::onRawMessageReceived);

    // --- (B) 解析层流向融合中心 (强类型数据) ---
    QObject::connect(mqttReceiver, &MqttReceiver::gpsUpdated, &fusionCenter, &FusionManager::updateGps);
    QObject::connect(mqttReceiver, &MqttReceiver::gyroUpdated, &fusionCenter, &FusionManager::updateGyro);
    QObject::connect(mqttReceiver, &MqttReceiver::aisUpdated, &fusionCenter, &FusionManager::updateAisData);
    QObject::connect(mqttReceiver, &MqttReceiver::arpaUpdated, &fusionCenter, &FusionManager::updateArpaData);
    QObject::connect(mqttReceiver, &MqttReceiver::engineStatusUpdated, &fusionCenter, &FusionManager::updateEngineStatus);

    // --- (C) 融合中心流向 UI 渲染层 (解耦后的渲染指令) ---
    QObject::connect(&fusionCenter, &FusionManager::fusionDataReady,mainWindow, &FFmpegWidget::processFusionData);

    // --- (D) MQTT 连接状态处理 (连接成功后自动订阅) ---
    QObject::connect(mqttClient, &MqttClient::connectionStateChanged, [mqttClient, &config](bool connected) {
        if (connected) {
            qDebug() << "[MQTT] 连接已建立，正在订阅主题:" << QString::fromStdString(config.subTopic);
            mqttClient->subscribe(QString::fromStdString(config.subTopic));
        }
        else {
            qWarning() << "[MQTT] 连接断开，尝试重连中...";
        }
        });
    // --- (E) 系统心脏起搏器：高频触发融合计算 (供 UI 丝滑渲染) ---
    QTimer* fusionTimer = new QTimer();
    QObject::connect(fusionTimer, &QTimer::timeout, [&fusionCenter](){
        fusionCenter.createFusionResult(); 
    });
    fusionTimer->start(100); // 100ms 触发一次 (10Hz)

    // --- (F) 融合数据发往 MQTT (带限流功能) ---
    QObject::connect(&fusionCenter, &FusionManager::fusionDataReady, mqttClient, [mqttClient, &config](const FusionData& data) {
        static qint64 lastPublishTime = 0;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        
        // 限制 MQTT 上报频率：每 10 秒发一次 (10000ms)
        if (now - lastPublishTime >= 1000) {
            std::string jsonStr = MessageProcessor::GetInstance().generateFusionData(data);
            // 调用 MqttClient 的 publish 方法发出去
            mqttClient->publish(QString::fromStdString(config.pubTopic), QString::fromStdString(jsonStr));
            lastPublishTime = now;
        }
    });
    // 6. 模块启动

    // 初始化并启动 MQTT 客户端
    mqttClient->init(
        QString::fromStdString(config.hostIP),
        config.hostPort,
        QString::fromStdString(config.userName),
        QString::fromStdString(config.password)
    );
    mqttClient->start();

    // 显示并启动视频流播放
    mainWindow->setWindowTitle("船舶多源数据融合监控终端");
    mainWindow->resize(1280, 720);
    mainWindow->show();

    // 延迟启动视频流，确保 UI 和融合中心已就绪
    QString videoPath = QString::fromStdString(config.sourcePath);
    mainWindow->startPlay(videoPath);

    // 7. 进入 Qt 事件循环
    int execRet = a.exec();

    // 8. 资源清理
    qDebug() << "系统正在关闭...";
    fusionTimer->stop();
    delete fusionTimer;
    delete mainWindow; 
    
    mqttClient->stop();
    delete mqttClient;
    delete mqttReceiver;

    return execRet;
}