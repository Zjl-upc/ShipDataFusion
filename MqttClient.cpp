#include "MqttClient.h"
#include <QDebug>

MqttClient::MqttClient(QObject *parent) 
    : QObject(parent), m_client(new QMqttClient(this)), m_reconnectTimer(new QTimer(this))
{
    // 1. 绑定底层 QMqttClient 的信号
    connect(m_client, &QMqttClient::connected, this, &MqttClient::onConnected);
    connect(m_client, &QMqttClient::disconnected, this, &MqttClient::onDisconnected);
    connect(m_client, &QMqttClient::messageReceived, this, &MqttClient::onMessageReceived);

    // 2. 绑定错误输出 (可选，方便调试)
    connect(m_client, &QMqttClient::errorChanged, this, [this](QMqttClient::ClientError error){
        if (error != QMqttClient::NoError) {
            qDebug() << "[QMqttClient] 发生错误，错误码:" << error;
        }
    });

    // 3. 设置断线自动重连定时器 (替代原来的 while 循环)
    m_reconnectTimer->setInterval(2000); // 每2秒尝试重连一次
    connect(m_reconnectTimer, &QTimer::timeout, this, &MqttClient::tryReconnect);
}

MqttClient::~MqttClient()
{
    // 退出时再也不会死锁了，Qt 会自动处理异步销毁
    stop();
}

void MqttClient::init(const QString& host, int port, const QString& user, const QString& pass)
{
    m_host = host;
    m_port = port;
    m_user = user;
    m_pass = pass;

    m_client->setHostname(m_host);
    m_client->setPort(m_port);
    if (!m_user.isEmpty()) {
        m_client->setUsername(m_user);
        m_client->setPassword(m_pass);
    }
    
    // 设置 KeepAlive 保持连接
    m_client->setKeepAlive(20);
}

void MqttClient::start()
{
    qDebug() << "[QMqttClient] 正在连接服务器:" << m_host << ":" << m_port;
    m_client->connectToHost();
}

void MqttClient::stop()
{
    m_reconnectTimer->stop(); // 停止重连尝试
    
    if (m_client->state() == QMqttClient::Connected || m_client->state() == QMqttClient::Connecting) {
        qDebug() << "[QMqttClient] 正在断开连接...";
        m_client->disconnectFromHost();
    }
}

void MqttClient::subscribe(const QString& topic)
{
    if (m_client->state() == QMqttClient::Connected) {
        auto subscription = m_client->subscribe(QMqttTopicFilter(topic), 1);
        if (!subscription) {
            qWarning() << "[QMqttClient] 订阅失败:" << topic;
        } else {
            qDebug() << "[QMqttClient] 订阅成功:" << topic;
        }
    } else {
        qWarning() << "[QMqttClient] 尚未连接，无法订阅:" << topic;
    }
}

void MqttClient::publish(const QString& topic, const QString& message)
{
    if (m_client->state() == QMqttClient::Connected) {
        // QoS 设为 1
        m_client->publish(QMqttTopicName(topic), message.toUtf8(), 1);
    }
}

// ==================== 私有槽函数响应 ====================

void MqttClient::onConnected()
{
    qDebug() << "[QMqttClient] 连接服务器成功！";
    m_reconnectTimer->stop(); // 连接成功，停止重连定时器
    emit connectionStateChanged(true); // 通知外部 (例如 main.cpp 去执行订阅)
}

void MqttClient::onDisconnected()
{
    qWarning() << "[QMqttClient] 与服务器断开连接！准备自动重连...";
    emit connectionStateChanged(false);
    
    // 启动定时器，2秒后尝试重新连接
    if (!m_reconnectTimer->isActive()) {
        m_reconnectTimer->start();
    }
}

void MqttClient::tryReconnect()
{
    if (m_client->state() == QMqttClient::Disconnected) {
        qDebug() << "[QMqttClient] 正在尝试重新连接...";
        m_client->connectToHost();
    }
}

void MqttClient::onMessageReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    // 将收到的字节流和 Topic 转换为 QString 发送给解析层 (MqttReceiver)
    emit rawMessageReceived(topic.name(), message);
}