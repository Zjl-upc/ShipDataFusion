#ifndef VIDEOGRANNER_H
#define VIDEOGRANNER_H

#include <QThread>
#include <QString>
#include <QMutex>
#include <QDebug>

// OpenCV 头文件
#include <opencv2/opencv.hpp>

/**
 * @brief 视频采集线程 (重构后仅保留拉流职责)
 * 职责：连接 RTSP/UDP 流，提取最新的一帧并用 Mutex 保护
 */
class VideoGrabber : public QThread
{
    Q_OBJECT
public:
    explicit VideoGrabber(QString url, QObject* parent = nullptr);
    ~VideoGrabber();

    void stop();

    // 供消费者(DetectionThread)调用的接口：获取最新一帧，返回 false 表示没有新数据
    bool getLatestFrame(cv::Mat& outFrame);

protected:
    void run() override;

private:
    QString m_url;
    volatile bool m_stop;
    cv::Mat m_latestFrame;     // 始终存储最新的一帧
    QMutex m_frameMutex;       // 保护 m_latestFrame
    bool m_hasNewFrame;        // 标记是否有新帧
};

#endif // FFMPEGPULL_H