#ifndef FFMPEGWIDGET_H
#define FFMPEGWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QTimer>
#include <QMutex>

#include "DataTypes.h"
#include "DetectionThread.h"
#include "UIDataTypes.h" 

class FFmpegWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FFmpegWidget(QWidget* parent = nullptr);
    ~FFmpegWidget();

    // 启动视频流处理
    void startPlay(QString path);

public slots:
    // 接收视频帧
    void updateImage(const QImage& img);
    // 接收融合后的渲染快照
    void updateRenderSnapshot(const UIRenderSnapshot& snapshot);
    void processFusionData(const FusionData& data);
protected:
    void paintEvent(QPaintEvent* event) override;

private:
    DetectionThread* m_worker;

    QImage m_currentFrame;         // 当前原始视频帧
    QImage m_scaledImageCache;    // 缩放后的视频缓存
    UIRenderSnapshot m_lastSnapshot; // 最新的渲染指令快照

    // 逻辑分辨率常量
    const double LOGIC_WIDTH = 7680.0;
    const double LOGIC_HEIGHT = 603.0;
};

#endif // FFMPEGWIDGET_H