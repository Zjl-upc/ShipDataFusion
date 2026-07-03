#pragma once
#ifndef YOLO_INFERENCER_H
#define YOLO_INFERENCER_H

#include <string>
#include <vector>
#include <memory>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include "DataTypes.h"
#include "ConfigManager.h"

class YoloInferencer {
public:
    YoloInferencer();
    ~YoloInferencer();

    // 初始化模型
    bool init(const std::string& modelPath, const std::string& classesPath,
        float confThresh, float nmsThresh, int overlap);

    // 核心推理接口：传入全景大图，输出原始检测框 (尚未经过ByteTrack)
    bool infer(const cv::Mat& frame, std::vector<SingleObj_DetectData>& outResults);

private:
    void applyGlobalNMS(std::vector<SingleObj_DetectData>& results);
    // 预处理
    cv::Mat letterbox(const cv::Mat& source, float& scale, float& pad_w, float& pad_h, int INP_WIDTH, int INP_HEIGHT);
    int getIntersectionArea(const cv::Rect& a, const cv::Rect& b);

private:
    Ort::Env m_env{ nullptr };
    std::unique_ptr<Ort::Session> m_session;
    std::string m_input_name;
    std::string m_output_name;

    std::vector<std::string> m_classes;

    // 推理参数
    float m_confThreshold;
    float m_nmsThreshold;
    int m_overlap;

    const int inpWidth = 1024;
    const int inpHeight = 1024;
    const int nnm_Tile = 7; // 切片数量

    bool m_firstFrame = true;
    int m_tile_Width = 0;
    int m_tile_Height = 0;
    int m_stride = 0;
};

#endif // YOLO_INFERENCER_H