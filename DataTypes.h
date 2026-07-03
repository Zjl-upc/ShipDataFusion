#pragma once
# ifndef DATATYPE_H
# define DATATYPE_H

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <QMetaType>
#include <opencv2/opencv.hpp>

struct GpsData {
    std::string DeviceType;
    int DeviceId;
    int Connect;
    int Mmsi = 0;
    std::string DateTime;

    struct Content {
        std::string lat = "0.0";
        std::string lon = "0.0";
        double sog = 0.0;
        double cog = 0.0;
        long long time = 0;
        int num = 0;
    }content;
};

struct GyroData {
    std::string DeviceType;
    int DeviceId;
    int Connect;
    int Mmsi = 0;
    std::string DateTime;
    struct Content {
        double head = 0.0;
        double rot = 0.0;
    }content;
};
struct EngineStatusData {
    std::string DeviceType;
    int Mmsi = 0;
    std::string DateTime;
    int rpmBackLift = 0;
    int rpmBackRight = 0;
    int lpower = 0;
    int rpower = 0;
};

struct AisTarget {
    int mmsi = 0;
    long long time;
    std::string lat = "0.0";
    std::string lon = "0.0";
    double distance = 0.0;
    int type = 0;
    double head = 0.0;
    double rot = 0.0;
    double sog = 0.0;
    double cog = 0.0;
    std::string name;
    int length = 0;
    int width = 0;
    int maxDraught = 0;
    double dcpa = 0.0;
    double tcpa = 0.0;
    double dangerLevel = 0.0;
};

struct AisData {
    std::string DataType;
    std::string DateTime;
    int Mmsi = 0;
    std::vector<AisTarget> content;
};

struct ArpaTarget {
    std::string id = "0.0";
    std::string name;
    std::string status;
    std::string lat = "0.0";
    std::string lon = "0.0";
    double speed = 0.0;
    double course = 0.0;
    double bearing = 0.0;
    double distance = 0.0;
    double dcpa = 0.0;
    double tcpa = 0.0;
    std::string acqType;
    long long utcTime;
    std::string bearingTR;
    std::string units;
    std::string courseTR;
};

struct ArpaData {
    std::string DataType;
    std::string DateTime;
    int Mmsi = 0;
    std::vector<ArpaTarget> content;
};


struct fusionTarget {
    std::string lifecycleKey; // 内部生命周期管理键，不参与现有 MQTT/UI 协议
    int posx = 0;
    int posy = 0;
    int width = 0;
    int height = 0;
    int mmsi = 0;
    long long time;
    std::string lat = "0.0";
    std::string lon = "0.0";
    double distance = 0.0;
    int type = 0;
    int fusionRet = 0;
    double sog = 0.0;
    double cog = 0.0;
    double dcpa = 0.0;
    double tcpa = 0.0;
    double dangerLevel = 0.0;

};
struct FusionData {
    std::string DataType;
    std::string DateTime;
    int Mmsi = 0;
    std::string lat = "0.0";
    std::string lon = "0.0";
    double sog = 0.0;
    double cog = 0.0;
    double hdg = 0.0;
    int lrpm = 0;
    int rrpm = 0;
    int lpower = 0;
    int rpower = 0;
    std::vector<fusionTarget> content;
};

struct SingleObj_DetectData {
    char ObjTypeNameID[32];
    int x, y, w, h;
    double confidence;
    int track_id = -1;
    bool is_tracked = false;
};

struct CameraConfig {
    int imageWidth = 1920;      // 替代 width
    int imageHeight = 1080;     // 替代 height
    double horizontalFov = 60.0; // 替代 fovH

    // 安装位置 (相对船体中心)
    double transX = 0.0;
    double transY = 0.0;
    double transZ = 15.0;

    // 安装角度 (Euler Angles)
    double yaw = 0.0;
    double pitch = 0.0;
    double roll = 0.0;

    cv::Mat distCoeffs; // 畸变系数
};

Q_DECLARE_METATYPE(fusionTarget)
Q_DECLARE_METATYPE(FusionData)
Q_DECLARE_METATYPE(std::vector<SingleObj_DetectData>)
#endif
