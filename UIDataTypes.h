#pragma once
#ifndef UIDATATYPES_H
#define UIDATATYPES_H

#include <QString>
#include <QColor>
#include <QRect>
#include <QMetaType>
#include <vector>

/**
 * @brief 纯净的 UI 渲染指令 (ViewModel)
 * 仅告诉渲染层：在哪个位置、画什么颜色的框和线、写什么字。
 */
struct UIRenderTarget {
    // 基础属性
    QColor color;       // 目标渲染颜色
    QString labelText;  // 已经拼装好的文字，如 "123456789-500.00"

    // 渲染模式控制
    bool hasVisionBox;  // true: 绘制矩形框(视觉目标) false: 绘制方位虚线(雷达/AIS)

    // 几何参数
    QRect boxRect;      // 视觉框模式下的矩形区域
    int lineX;          // 方位线模式下的屏幕 X 坐标
};

struct UIRenderSnapshot {
    std::vector<UIRenderTarget> targets;
};

// 必须声明为 Qt 元类型，才能在跨线程的信号槽 (QueuedConnection) 中传递
Q_DECLARE_METATYPE(UIRenderSnapshot)

#endif // UIDATATYPES_H