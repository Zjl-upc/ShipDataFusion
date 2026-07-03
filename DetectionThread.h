#pragma once
#ifndef DETECTION_THREAD_H
#define DETECTION_THREAD_H

#include <QThread>
#include <QImage>
#include <QString>
#include <memory>
#include "VideoGrabber.h"
#include "YoloInferencer.h"
#include "BYTETracker.h"

class DetectionThread : public QThread {
    Q_OBJECT
public:
    explicit DetectionThread(const QString& videoPath, QObject* parent = nullptr);
    ~DetectionThread();

    void stop();

signals:
    // 解耦输出 1：发给 UI 渲染视频帧
    void frameReady(QImage img);

    // 解耦输出 2：发给 FusionManager 进行时空匹配
    void visionDetected(const std::vector<SingleObj_DetectData>& trackedResults);

protected:
    void run() override;

private:
    QString m_videoPath;
    volatile bool m_stop;

    std::unique_ptr<YoloInferencer> m_inferencer;
    BYTETracker* m_tracker; // ByteTrack实例
};

#endif // DETECTION_THREAD_H