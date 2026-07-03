#include "FFmpegWidget.h"
#include "FusionManager.h"
#include <QDebug>

FFmpegWidget::FFmpegWidget(QWidget* parent)
    : QWidget(parent), m_worker(nullptr)
{
    // 设置背景色为黑色，防止闪烁
    setAttribute(Qt::WA_OpaquePaintEvent);
    qRegisterMetaType<UIRenderSnapshot>("UIRenderSnapshot");

    // 初始化默认字体
    QFont font = this->font();
    font.setFamily("Microsoft YaHei");
    font.setPointSize(9);
    font.setBold(true);
    setFont(font);
}

FFmpegWidget::~FFmpegWidget()
{
    if (m_worker) {
        m_worker->stop();
        m_worker->wait();
        delete m_worker;
    }
}

void FFmpegWidget::startPlay(QString path)
{
    if (m_worker) {
        m_worker->stop();
        m_worker->wait();
        delete m_worker;
    }

    // 创建调度线程
    m_worker = new DetectionThread(path, this);

    // 连接 1: 视频帧推送给 UI
    connect(m_worker, &DetectionThread::frameReady, this, &FFmpegWidget::updateImage);

    // 连接 2: 视觉结果推送给融合中心 (跨线程)
    // 注意：这里是由 UI 负责将两个解耦的模块通过信号槽“粘合”起来
    connect(m_worker, &DetectionThread::visionDetected,
        &FusionManager::GetInstance(), &FusionManager::updateVision, Qt::QueuedConnection);

    m_worker->start();
}

void FFmpegWidget::updateImage(const QImage& img)
{
    m_currentFrame = img;
    update(); // 触发 paintEvent
}

void FFmpegWidget::updateRenderSnapshot(const UIRenderSnapshot& snapshot)
{
    m_lastSnapshot = snapshot;
    update();
}

void FFmpegWidget::processFusionData(const FusionData& data)
{
    UIRenderSnapshot snapshot;

    for (const auto& target : data.content) {
        UIRenderTarget uiTarget;
        uiTarget.hasVisionBox = false;

        // 1. 颜色与标签映射
        switch (target.fusionRet) {
        case 0: uiTarget.color = Qt::cyan;          uiTarget.labelText = "AIS";       break;
        case 1: uiTarget.color = QColor(255, 165, 0); uiTarget.labelText = "ARPA";      break;
        case 2: uiTarget.color = Qt::magenta;       uiTarget.labelText = "Ship";      uiTarget.hasVisionBox = true; break;
        case 3: uiTarget.color = Qt::yellow;        uiTarget.labelText = "AIS+ARPA";  break;
        case 4: uiTarget.color = Qt::green;         uiTarget.labelText = "AIS+VIS";   uiTarget.hasVisionBox = true; break;
        case 5: uiTarget.color = QColor(160, 32, 240); uiTarget.labelText = "ARPA+VIS";  uiTarget.hasVisionBox = true; break;
        case 6: uiTarget.color = Qt::red;           uiTarget.labelText = "ALL FUSION"; uiTarget.hasVisionBox = true; break;
        default: uiTarget.color = Qt::white;        uiTarget.labelText = "Unknown";   break;
        }

        QString mmsiStr = (target.mmsi > 0) ? QString::number(target.mmsi) : "";
        QString distStr = QString::number(target.distance, 'f', 2);
        // uiTarget.labelText = QString("%1-%2").arg(mmsiStr).arg(distStr);
        uiTarget.labelText = QString("%1").arg(distStr);

        if (uiTarget.hasVisionBox) {
            uiTarget.boxRect = QRect(target.posx, target.posy, target.width, target.height);
        }
        else {
            uiTarget.lineX = target.posx;
        }

        snapshot.targets.push_back(uiTarget);
    }

    // 存入缓存，并触发重绘
    m_lastSnapshot = snapshot;
    update();
}

void FFmpegWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::black);

    // 1. 计算视频缩放与居中偏移 (保持比例)
    int imgX = 0, imgY = 0, imgW = 0, imgH = 0;

    if (!m_currentFrame.isNull()) {
        // 缓存缩放图提高性能
        if (m_scaledImageCache.isNull() ||
            m_scaledImageCache.size() != size() ||
            m_scaledImageCache.cacheKey() != m_currentFrame.cacheKey()) {

            m_scaledImageCache = m_currentFrame.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        imgW = m_scaledImageCache.width();
        imgH = m_scaledImageCache.height();
        imgX = (width() - imgW) / 2;
        imgY = (height() - imgH) / 2;

        p.drawImage(imgX, imgY, m_scaledImageCache);
    }
    else {
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, "等待视频流...");
        return;
    }

    // 2. 绘制 Overlay 指令层
    // 计算从逻辑坐标 (7680x1080) 到当前显示区域的比例
    double scaleX = static_cast<double>(imgW) / LOGIC_WIDTH;
    double scaleY = static_cast<double>(imgH) / LOGIC_HEIGHT;

    for (const auto& target : m_lastSnapshot.targets) {
        p.setPen(QPen(target.color, 2, Qt::SolidLine));

        if (target.hasVisionBox) {
            // --- 模式 A: 绘制视觉目标框 ---
            int drawX = imgX + static_cast<int>(target.boxRect.x() * scaleX);
            int drawY = imgY + static_cast<int>(target.boxRect.y() * scaleY);
            int drawW = static_cast<int>(target.boxRect.width() * scaleX);
            int drawH = static_cast<int>(target.boxRect.height() * scaleY);

            p.drawRect(drawX, drawY, drawW, drawH);

            // 绘制顶部文字标签及其背景
            int centerX = drawX + drawW / 2;
            int textWidth = qMax(60, drawW);
            int textX = centerX - textWidth / 2;
            int labelY = imgY + 15; // 标签统一固定在视频顶部区域

            QRect textRect(textX, labelY, textWidth, 22);
            p.fillRect(textRect, QColor(0, 0, 0, 160)); // 半透明背景
            p.setPen(Qt::white);
            p.drawText(textRect, Qt::AlignCenter, target.labelText);

            // 绘制标签边框
            p.setPen(QPen(target.color, 1));
            p.drawRect(textRect);

            // 绘制虚线连接标签和检测框
            p.setPen(QPen(target.color, 1, Qt::DashLine));
            p.drawLine(centerX, labelY + 22, centerX, drawY);

        }
        else {
            // --- 模式 B: 绘制纯数据方位指示线 ---
            int drawX = imgX + static_cast<int>(target.lineX * scaleX);

            // 确保不画出视频边界
            if (drawX >= imgX && drawX <= (imgX + imgW)) {
                int labelY = imgY + 15;
                int baseY = imgY + static_cast<int>(imgH * 0.7); // 虚线延伸至视频下部

                // 绘制小标签
                QRect textRect(drawX - 30, labelY, 60, 22);
                p.fillRect(textRect, QColor(0, 0, 0, 160));
                p.setPen(QPen(target.color, 2));
                p.drawText(textRect, Qt::AlignCenter, target.labelText);

                // 绘制方位虚线
                p.setPen(QPen(target.color, 1, Qt::DashLine));
                p.drawLine(drawX, labelY + 22, drawX, baseY);

                // 绘制底部标记圆点
                p.setPen(Qt::NoPen);
                p.setBrush(target.color);
                p.drawEllipse(QPoint(drawX, baseY), 4, 4);
                p.setBrush(Qt::NoBrush);
            }
        }
    }
}