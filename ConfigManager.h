#pragma once
#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H
#include <iostream>
#include <fstream>
#include <string>
#include "json.hpp"

using json = nlohmann::json;

class ConfigManager {
public:
    static ConfigManager& getInstance() {
        static ConfigManager instance;
        return instance;
    }

    bool loadConfig(const std::string& filename) {
        std::ifstream f(filename);
        if (!f.is_open()) return false;

        try {
            json data = json::parse(f);

            //MQTT配置
            hostIP = data["mqtt"]["host_ip"];
            hostPort = data["mqtt"]["host_port"];
            userName = data["mqtt"]["username"];
            password = data["mqtt"]["password"];
            subTopic = data["mqtt"]["sub_topic"];
            pubTopic = data["mqtt"]["pub_topic"];

            // 视频配置
            sourcePath = data["video"]["source_path"];
            logicalWidth = data["video"]["logical_width"];
            logicalHeight = data["video"]["logical_height"];

            // 检测配置
            sliceWidth = data["detection"]["slice_width"];
            sliceHeight = data["detection"]["slice_height"];
            overlap_size = data["detection"]["overlap_size"];
            confThreshold = data["detection"]["conf_threshold"];
            nmsThreshold = data["detection"]["nms_threshold"];
            objThreshold = data["detection"]["obj_threshold"];

            //摄像头配置
            camera_width = data["camera"]["width"];
            camera_height = data["camera"]["height"];
            camera_fovH = data["camera"]["fovH"]; 
            camera_yaw = data["camera"]["yaw"];
            camera_transX = data["camera"]["transX"];
            camera_transY = data["camera"]["transY"];
            camera_transZ = data["camera"]["transZ"];

            // 模型配置
            modelPath = data["model"]["model_path"];
            modelName = data["model"]["model_name"];

            //融合配置
            maxFusionDistance = data["fusion"].value("max_fusion_distance", 1000.0);
            fovH = data["fusion"].value("fovH", 270.0);
            centerPixelOffset = data["fusion"].value("center_PixelOffset", 350);
            pixelTolerance = data["fusion"].value("pixel_tolerance", 180);
            fsmConfirmThreshold = data["fusion"].value("fsm_confirm_threshold", 5);
            cameraBiasDeg = data["fusion"].value("camera_bias_deg", 0.0);

            // 融合调参：时间同步
            aisGpsMaxTimeDiffMs = data["fusion"].value("ais_gps_max_time_diff_ms", 8000);

            // 融合调参：AIS-ARPA 候选门控与代价
            arpaGateBaseMeters = data["fusion"].value("arpa_gate_base_meters", 120.0);
            arpaGateDistanceRatio = data["fusion"].value("arpa_gate_distance_ratio", 0.08);
            arpaGateMinMeters = data["fusion"].value("arpa_gate_min_meters", 150.0);
            arpaGateMaxMeters = data["fusion"].value("arpa_gate_max_meters", 600.0);
            arpaHeadingWeight = data["fusion"].value("arpa_heading_weight", 0.25);
            arpaSpeedWeight = data["fusion"].value("arpa_speed_weight", 0.15);
            arpaSpeedNormalizeKnots = data["fusion"].value("arpa_speed_normalize_knots", 20.0);
            arpaAssociationAcceptCost = data["fusion"].value("arpa_association_accept_cost", 1.35);

            // 融合调参：视觉候选门控与代价
            visionMinBoxWidth = data["fusion"].value("vision_min_box_width", 4);
            visionMinBoxHeight = data["fusion"].value("vision_min_box_height", 4);
            visionMinConfidence = data["fusion"].value("vision_min_confidence", 0.01);
            visionToleranceBoxWidthRatio = data["fusion"].value("vision_tolerance_box_width_ratio", 0.20);
            visionDynamicToleranceMin = data["fusion"].value("vision_dynamic_tolerance_min", 60.0);
            visionDynamicToleranceMax = data["fusion"].value("vision_dynamic_tolerance_max", 360.0);
            visionNearDistanceMeters = data["fusion"].value("vision_near_distance_meters", 500.0);
            visionExpandedBoxLeftRatio = data["fusion"].value("vision_expanded_box_left_ratio", 0.25);
            visionExpandedBoxRightRatio = data["fusion"].value("vision_expanded_box_right_ratio", 1.25);
            visionMinBoxBottomY = data["fusion"].value("vision_min_box_bottom_y", 150.0);
            visionNearCostBoxWidthRatio = data["fusion"].value("vision_near_cost_box_width_ratio", 0.75);
            visionConfidenceCostWeight = data["fusion"].value("vision_confidence_cost_weight", 0.15);
            visionAssociationAcceptCost = data["fusion"].value("vision_association_accept_cost", 1.25);

            // 融合调参：生命周期超时
            aisStaleTimeoutMs = data["fusion"].value("ais_stale_timeout_ms", 60000);
            arpaStaleTimeoutMs = data["fusion"].value("arpa_stale_timeout_ms", 3000);
            visionStaleTimeoutMs = data["fusion"].value("vision_stale_timeout_ms", 1000);
            strongIdentityDeadTtlMs = data["fusion"].value("strong_identity_dead_ttl_ms", 300000);
            weakIdentityDeadTtlMs = data["fusion"].value("weak_identity_dead_ttl_ms", 15000);



            return true;
        }
        catch (json::parse_error& e) {
            std::cerr << "JSON Parse Error: " << e.what() << std::endl;
            return false;
        }
    }

    // 成员变量
    std::string sourcePath;
    std::string modelPath, modelName;
    int logicalWidth, logicalHeight;
    int sliceWidth, sliceHeight, overlap_size;
    float confThreshold,nmsThreshold,objThreshold;
    int camera_width,camera_height,camera_fovH;
    double camera_yaw,camera_transX,camera_transY,camera_transZ;

    std::string hostIP;
    int hostPort;
    std::string userName;
    std::string password;
    std::string subTopic, pubTopic;

    double maxFusionDistance = 1000.0;
    double fovH = 270.0;
    int centerPixelOffset = 350;
    int pixelTolerance = 180;
    int fsmConfirmThreshold = 5;
    double cameraBiasDeg = 12.31;

    int aisGpsMaxTimeDiffMs = 8000;

    double arpaGateBaseMeters = 120.0;
    double arpaGateDistanceRatio = 0.08;
    double arpaGateMinMeters = 150.0;
    double arpaGateMaxMeters = 600.0;
    double arpaHeadingWeight = 0.25;
    double arpaSpeedWeight = 0.15;
    double arpaSpeedNormalizeKnots = 20.0;
    double arpaAssociationAcceptCost = 1.35;

    int visionMinBoxWidth = 4;
    int visionMinBoxHeight = 4;
    double visionMinConfidence = 0.01;
    double visionToleranceBoxWidthRatio = 0.20;
    double visionDynamicToleranceMin = 60.0;
    double visionDynamicToleranceMax = 360.0;
    double visionNearDistanceMeters = 500.0;
    double visionExpandedBoxLeftRatio = 0.25;
    double visionExpandedBoxRightRatio = 1.25;
    double visionMinBoxBottomY = 150.0;
    double visionNearCostBoxWidthRatio = 0.75;
    double visionConfidenceCostWeight = 0.15;
    double visionAssociationAcceptCost = 1.25;

    int aisStaleTimeoutMs = 60000;
    int arpaStaleTimeoutMs = 3000;
    int visionStaleTimeoutMs = 1000;
    int strongIdentityDeadTtlMs = 300000;
    int weakIdentityDeadTtlMs = 15000;



private:
    ConfigManager() {}
};
#endif //CONFIGMANAGER_H
