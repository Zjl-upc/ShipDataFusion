#include "VideoGrabber.h"

// ================= VideoGrabber 实现(视频读取) ================
VideoGrabber::VideoGrabber(QString url, QObject* parent)
    : QThread(parent), m_url(url), m_stop(false), m_hasNewFrame(false)
{
}

VideoGrabber::~VideoGrabber()
{
    stop();
    wait();
}

void VideoGrabber::stop()
{
    m_stop = true;
}

bool VideoGrabber::getLatestFrame(cv::Mat& outFrame)
{
    QMutexLocker locker(&m_frameMutex);
    if (!m_hasNewFrame || m_latestFrame.empty()) {
        return false;
    }
    // 浅拷贝传递，性能更高
    cv::swap(m_latestFrame, outFrame);
    m_hasNewFrame = false;   // 避免重复处理同一帧
    return true;
}

void VideoGrabber::run()
{
    cv::VideoCapture cap;

    // 针对网络流的特殊优化设置
    if (m_url.startsWith("rtsp") || m_url.startsWith("udp")) {
        // 设置环境变量，强制使用 TCP，减少丢包花屏
        qputenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp|buffer_size;1024000|max_delay;500000");
        cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    }

    if (!cap.open(m_url.toStdString(), cv::CAP_FFMPEG)) {
        qDebug() << "[VideoGrabber] 无法打开流: " << m_url;
        return;
    }

    qDebug() << "[VideoGrabber] 流打开成功，开始采集...";

    cv::Mat tempFrame;
    while (!m_stop) {
        if (!cap.read(tempFrame)) {
            qDebug() << "[VideoGrabber] 读取失败或流结束，尝试重连...";
            QThread::sleep(1);
            cap.release();
            if (!cap.open(m_url.toStdString(), cv::CAP_FFMPEG)) {
                QThread::sleep(2);
            }
            continue;
        }

        if (tempFrame.empty()) continue;

        // 拿到新帧，立即存入互斥区
        {
            QMutexLocker locker(&m_frameMutex);
            // 旧的帧直接被丢弃（Drop），只保留最新的，保证实时性
            m_latestFrame = tempFrame;
            m_hasNewFrame = true;
        }
    }
    cap.release();
    qDebug() << "[VideoGrabber] 视频流采集线程已安全退出。";
}