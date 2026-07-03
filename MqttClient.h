#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H

#include <QObject>
#include <QString>
// #include <QMqttClient>
#include <QtMqtt/qmqttclient.h>
#include <QTimer>

class MqttClient : public QObject
{
    Q_OBJECT
public:
    explicit MqttClient(QObject *parent = nullptr);
    ~MqttClient();

    // 依然保持和以前一样的对外接口
    void init(const QString& host, int port, const QString& user = "", const QString& pass = "");
    void start();
    void stop();
    void subscribe(const QString& topic);
    void publish(const QString& topic, const QString& message);

signals:
    // 依然对外发送相同的信号，main.cpp 不需要任何修改
    void connectionStateChanged(bool connected);
    void rawMessageReceived(const QString& topic, const QByteArray& message);

private slots:
    // QMqttClient 内部状态响应槽函数
    void onConnected();
    void onDisconnected();
    void onMessageReceived(const QByteArray &message, const QMqttTopicName &topic);
    void tryReconnect(); // 替代以前的死循环重连

private:
    QMqttClient *m_client;
    QTimer *m_reconnectTimer;  // 使用定时器代替原来的后台 while(true) 线程
    
    // 记录连接信息以便断线重连
    QString m_host;
    int m_port;
    QString m_user;
    QString m_pass;
};

#endif // MQTTCLIENT_H