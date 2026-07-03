#include "DetectionThread.h"
#include <chrono>
#include <QDebug>

DetectionThread::DetectionThread(const QString& videoPath, QObject* parent)
    : QThread(parent), m_videoPath(videoPath), m_stop(false)
{
    // 1. 获取配置并初始化推理器
    ConfigManager& config = ConfigManager::getInstance();
    m_inferencer = std::make_unique<YoloInferencer>();
    m_inferencer->init(config.modelPath, config.modelName,
        config.confThreshold, config.nmsThreshold, config.overlap_size);

    // 2. 初始化跟踪器
    m_tracker = new BYTETracker(20, 40);
}

DetectionThread::~DetectionThread() {
    stop();
    wait();
    if (m_tracker) {
        delete m_tracker;
    }
}

void DetectionThread::stop() {
    m_stop = true;
}

void DetectionThread::run() {
    VideoGrabber grabber(m_videoPath);
    grabber.start();

    cv::Mat frame;
    using clock = std::chrono::steady_clock;

    while (!m_stop) {
        auto frameStart = clock::now();

        // 1. 获取视频帧
        if (!grabber.getLatestFrame(frame)) {
            QThread::msleep(1);
            continue;
        }
        if (frame.empty()) continue;

        // 2. AI 推理 (直接获取未跟踪的框)
        std::vector<SingleObj_DetectData> rawDetections;
        if (!m_inferencer->infer(frame, rawDetections)) {
            continue;
        }

        // 3. 目标跟踪 (ByteTrack 格式转换与处理)
        std::vector<Object> byteObjects;
        for (size_t i = 0; i < rawDetections.size(); ++i) {
            Object obj;
            obj.box = cv::Rect_<float>(rawDetections[i].x, rawDetections[i].y,
                rawDetections[i].w, rawDetections[i].h);
            obj.confidence = rawDetections[i].confidence;
            obj.classId = i;
            byteObjects.push_back(obj);
        }

        std::vector<STrack> output_stracks = m_tracker->update(byteObjects);
        std::vector<SingleObj_DetectData> trackedResults;

        for (size_t i = 0; i < output_stracks.size(); i++) {
            SingleObj_DetectData data;
            data.x = output_stracks[i].tlwh[0];
            data.y = output_stracks[i].tlwh[1];
            data.w = output_stracks[i].tlwh[2];
            data.h = output_stracks[i].tlwh[3];
            data.confidence = output_stracks[i].score;
            data.track_id = output_stracks[i].track_id;

            int original_idx = output_stracks[i].classId;
            if (original_idx >= 0 && original_idx < rawDetections.size()) {
                strncpy(data.ObjTypeNameID, rawDetections[original_idx].ObjTypeNameID, 31);
            }
            else {
                strncpy(data.ObjTypeNameID, "Ship", 31);
            }
            trackedResults.push_back(data);
        }

        // ==========================================
        // 核心解耦点：不再调用 FusionManager::GetInstance()
        // ==========================================
        emit visionDetected(trackedResults);

        // 4. 转换格式给 UI 显示 (不再画 OpenCV 框，画框留给 UI Overlay 解决)
        // 保持原样 BGR888 传递，让 Qt 自己去画
        cv::Mat rgb;
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);

        QImage img(
            rgb.data,
            rgb.cols,
            rgb.rows,
            static_cast<int>(rgb.step),
            QImage::Format_RGB888
        );
        // QImage img((const uchar*)frame.data, frame.cols, frame.rows, frame.step, QImage::Format_BGR888);
        emit frameReady(img.copy());

        // 帧率控制
        auto frameEnd = clock::now();
        double frameMs = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart).count();
        if (frameMs < 33.0) { // 限制最高 ~30 FPS，防止吃满 CPU
            QThread::msleep(static_cast<unsigned long>(33.0 - frameMs));
        }
    }

    grabber.stop();
    grabber.wait();
}