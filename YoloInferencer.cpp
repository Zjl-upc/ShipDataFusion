#include "YoloInferencer.h"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <QDebug>

YoloInferencer::YoloInferencer() {}

YoloInferencer::~YoloInferencer() {}

bool YoloInferencer::init(const std::string& modelPath, const std::string& classesPath,
    float confThresh, float nmsThresh, int overlap) {
    m_confThreshold = confThresh;
    m_nmsThreshold = nmsThresh;
    m_overlap = overlap;

    // 1. 加载类别
    std::ifstream classNamesFile(classesPath);
    if (classNamesFile.is_open()) {
        std::string className;
        while (std::getline(classNamesFile, className)) {
            m_classes.push_back(className);
        }
    }
    else {
        qWarning() << "[YoloInferencer] 无法加载类别文件:" << QString::fromStdString(classesPath);
        return false;
    }

    // 2. 初始化 ONNX Runtime
    try {
        m_env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "YOLO11_BATCH");
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(8);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        OrtCUDAProviderOptions cuda_options;
        cuda_options.device_id = 0;
        session_options.AppendExecutionProvider_CUDA(cuda_options);

        m_session = std::make_unique<Ort::Session>(m_env, modelPath.c_str(), session_options);
        Ort::AllocatorWithDefaultOptions allocator;
        m_input_name = m_session->GetInputNameAllocated(0, allocator).get();
        m_output_name = m_session->GetOutputNameAllocated(0, allocator).get();

        qDebug() << "[YoloInferencer] YOLO11 (ONNX) 模型加载成功 (使用CUDA进行推理)";
        return true;
    }
    catch (const Ort::Exception& e) {
        qCritical() << "[YoloInferencer] ONNX 模型加载失败:" << e.what();
        return false;
    }
}

bool YoloInferencer::infer(const cv::Mat& frame, std::vector<SingleObj_DetectData>& outResults) {
    if (!m_session || frame.empty()) return false;

    // 初始化分块参数 (仅首帧执行)
    if (m_firstFrame) {
        m_tile_Width = (frame.cols + (nnm_Tile - 1) * m_overlap) / nnm_Tile;
        m_tile_Height = frame.rows;
        m_stride = m_tile_Width - m_overlap;
        m_firstFrame = false;
    }

    int BATCH_SIZE = nnm_Tile;
    std::vector<float> input_tensor_values(BATCH_SIZE * 3 * inpHeight * inpWidth);
    std::vector<int> x1_offsets(BATCH_SIZE);
    float global_scale, global_pad_w, global_pad_h;

    // ================= 1. 预处理与切片 (并行) =================
    cv::parallel_for_(cv::Range(0, BATCH_SIZE), [&](const cv::Range& range) {
        for (int i = range.start; i < range.end; i++) {
            int x_offset = i * m_stride;
            if (i == BATCH_SIZE - 1) {
                x_offset = frame.cols - m_tile_Width;
            }
            x1_offsets[i] = x_offset;

            cv::Rect tile_roi(x_offset, 0, m_tile_Width, m_tile_Height);
            cv::Mat crop = frame(tile_roi);

            float local_scale, local_pad_w, local_pad_h;
            cv::Mat blob_img = letterbox(crop, local_scale, local_pad_w, local_pad_h, inpWidth, inpHeight);

            if (i == 0) {
                global_scale = local_scale;
                global_pad_w = local_pad_w;
                global_pad_h = local_pad_h;
            }

            int channel_stride = inpHeight * inpWidth;
            float* batch_ptr = input_tensor_values.data() + i * 3 * channel_stride;

            for (int row = 0; row < inpHeight; ++row) {
                const uchar* row_ptr = blob_img.ptr<uchar>(row);
                for (int col = 0; col < inpWidth; ++col) {
                    int pixel_idx = row * inpWidth + col;
                    batch_ptr[pixel_idx] = row_ptr[col * 3 + 2] / 255.0f;
                    batch_ptr[channel_stride + pixel_idx] = row_ptr[col * 3 + 1] / 255.0f;
                    batch_ptr[2 * channel_stride + pixel_idx] = row_ptr[col * 3 + 0] / 255.0f;
                }
            }
        }
        });

    // ================= 2. GPU 批处理推理阶段 =================
    std::vector<int64_t> input_shape = { BATCH_SIZE, 3, inpHeight, inpWidth };
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_tensor_values.data(), input_tensor_values.size(), input_shape.data(), input_shape.size());

    std::vector<const char*> input_node_names = { m_input_name.c_str() };
    std::vector<const char*> output_node_names = { m_output_name.c_str() };

    auto output_tensors = m_session->Run(Ort::RunOptions{ nullptr }, input_node_names.data(), &input_tensor, 1, output_node_names.data(), 1);

    // ================= 3. 后处理与映射阶段 =================
    float* output_data = output_tensors.front().GetTensorMutableData<float>();
    auto out_shape = output_tensors.front().GetTensorTypeAndShapeInfo().GetShape();

    int num_channels = out_shape.at(1);
    int num_anchors = out_shape.at(2);
    int NUM_CLASSES = m_classes.size();

    for (int b = 0; b < BATCH_SIZE; ++b) {
        float* batch_ptr = output_data + b * (num_channels * num_anchors);
        int current_x1 = x1_offsets[b];

        for (int a = 0; a < num_anchors; ++a) {
            float max_conf = 0.0f;
            int class_id = -1;

            for (int c = 0; c < NUM_CLASSES; ++c) {
                float conf = batch_ptr[(4 + c) * num_anchors + a];
                if (conf > max_conf) {
                    max_conf = conf;
                    class_id = c;
                }
            }

            if (max_conf > m_confThreshold) {
                float cx = batch_ptr[0 * num_anchors + a];
                float cy = batch_ptr[1 * num_anchors + a];
                float w = batch_ptr[2 * num_anchors + a];
                float h = batch_ptr[3 * num_anchors + a];

                // 还原回 Tile 尺度
                cx = (cx - global_pad_w) / global_scale;
                cy = (cy - global_pad_h) / global_scale;
                w = w / global_scale;
                h = h / global_scale;

                int left = std::round(cx - w / 2.0f);
                int top = std::round(cy - h / 2.0f);
                int actual_width = std::round(w);
                int actual_height = std::round(h);

                SingleObj_DetectData obj;
                obj.x = left + current_x1; // 加上当前 Tile 的全局偏移
                obj.y = top;
                obj.w = actual_width;
                obj.h = actual_height;
                obj.confidence = max_conf;
                strncpy(obj.ObjTypeNameID, m_classes[class_id].c_str(), 31);

                outResults.push_back(obj);
            }
        }
    }

    // ================= 4. 全局去重 NMS =================
    applyGlobalNMS(outResults);

    return true;
}

// ================= 辅助函数实现 =================

cv::Mat YoloInferencer::letterbox(const cv::Mat& source, float& scale, float& pad_w, float& pad_h, int INP_WIDTH, int INP_HEIGHT) {
    float w = source.cols;
    float h = source.rows;
    scale = std::min((float)INP_WIDTH / w, (float)INP_HEIGHT / h);

    int new_w = std::round(w * scale);
    int new_h = std::round(h * scale);

    pad_w = (INP_WIDTH - new_w) / 2.0f;
    pad_h = (INP_HEIGHT - new_h) / 2.0f;

    cv::Mat resized;
    cv::resize(source, resized, cv::Size(new_w, new_h));

    cv::Mat out;
    int top = std::round(pad_h - 0.1f);
    int bottom = std::round(pad_h + 0.1f);
    int left = std::round(pad_w - 0.1f);
    int right = std::round(pad_w + 0.1f);
    cv::copyMakeBorder(resized, out, top, bottom, left, right, cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
    return out;
}

int YoloInferencer::getIntersectionArea(const cv::Rect& a, const cv::Rect& b) {
    int x1 = std::max(a.x, b.x);
    int y1 = std::max(a.y, b.y);
    int x2 = std::min(a.x + a.width, b.x + b.width);
    int y2 = std::min(a.y + a.height, b.y + b.height);
    if (x1 >= x2 || y1 >= y2) return 0;
    return (x2 - x1) * (y2 - y1);
}

void YoloInferencer::applyGlobalNMS(std::vector<SingleObj_DetectData>& results) {
    if (results.empty()) return;

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;

    // 提取数据
    for (const auto& obj : results) {
        boxes.push_back(cv::Rect(obj.x, obj.y, obj.w, obj.h));
        scores.push_back((float)obj.confidence);
    }

    std::vector<int> indices;
    // 第一步：执行 OpenCV 自带的 NMS
    cv::dnn::NMSBoxes(boxes, scores, m_confThreshold, m_nmsThreshold, indices);

    std::vector<SingleObj_DetectData> nms_results;
    for (int idx : indices) {
        nms_results.push_back(results[idx]);
    }

    if (nms_results.size() < 2) {
        results = nms_results;
        return;
    }

    // 第二步：包含关系去重 (解决“大框包小框”或拼接缝隙处的碎片问题)
    std::sort(nms_results.begin(), nms_results.end(), [](const SingleObj_DetectData& a, const SingleObj_DetectData& b) {
        return a.confidence > b.confidence;
        });

    std::vector<bool> isSuppressed(nms_results.size(), false);
    std::vector<SingleObj_DetectData> final_results;
    const float CONTAIN_RATIO_THRES = 0.70f;

    for (size_t i = 0; i < nms_results.size(); ++i) {
        if (isSuppressed[i]) continue;

        const auto& boxA = nms_results[i];
        cv::Rect rectA(boxA.x, boxA.y, boxA.w, boxA.h);
        int areaA = rectA.area();

        for (size_t j = i + 1; j < nms_results.size(); ++j) {
            if (isSuppressed[j]) continue;

            const auto& boxB = nms_results[j];
            if (strncmp(boxA.ObjTypeNameID, boxB.ObjTypeNameID, 31) != 0) continue;

            cv::Rect rectB(boxB.x, boxB.y, boxB.w, boxB.h);
            int areaB = rectB.area();

            int interArea = getIntersectionArea(rectA, rectB);
            if (interArea <= 0) continue;

            float ratioB_in_A = (float)interArea / areaB;

            // 如果重叠部分占了 B 面积的 70% 以上，说明 B 是冗余碎片，丢弃
            if (ratioB_in_A > CONTAIN_RATIO_THRES) {
                isSuppressed[j] = true;
            }
        }
    }

    // 重构返回列表
    for (size_t i = 0; i < nms_results.size(); ++i) {
        if (!isSuppressed[i]) {
            final_results.push_back(nms_results[i]);
        }
    }
    results = final_results;
}