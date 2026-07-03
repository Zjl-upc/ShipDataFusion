#pragma once
#include <QObject>
#include <mutex>
#include "DataTypes.h"
//#include "CameraProjector.h"
#include <shared_mutex>
#include <unordered_map>
#include "UIDataTypes.h"

// 绑定状态机枚举
enum class BindState {
    UNBOUND = 0,    // 未绑定 (通常不在字典里就是 UNBOUND)
    TENTATIVE = 1,  // 观察态 (刚匹配上，还在考察期)
    LOCKED = 2      // 锁定态 (稳定匹配，已确立强绑定)
};

// 升级绑定信息结构体
struct TrackBindInfo {
    int mmsi;
    BindState state;
    int hitCount;          // 连续匹配成功的次数 (信任度)
    qint64 lastSeenTime;   // 最后一次更新的时间戳 (毫秒)
};

enum class LifecycleState {
    BORN,
    TENTATIVE,
    CONFIRMED,
    TRACKING,
    COASTING,
    LOST,
    DEAD
};

enum class SourceStatus {
    INACTIVE,
    ACTIVE,
    STALE,
    LOST,
    CONFLICTED
};

enum class FusionState {
    SINGLE_SOURCE,
    FUSION_CANDIDATE,
    FUSED,
    PARTIAL_LOST,
    CONFLICTED
};

struct TargetLifecycleInfo {
    int globalId = -1;
    int mmsi = 0;
    std::string arpaId;
    int visionTrackId = -1;
    LifecycleState lifecycle = LifecycleState::BORN;
    FusionState fusionState = FusionState::SINGLE_SOURCE;
    SourceStatus aisStatus = SourceStatus::INACTIVE;
    SourceStatus arpaStatus = SourceStatus::INACTIVE;
    SourceStatus visionStatus = SourceStatus::INACTIVE;
    int aisHitCount = 0;
    int arpaHitCount = 0;
    int visionHitCount = 0;
    qint64 createTime = 0;
    qint64 lastSeenTime = 0;
};

class FusionManager : public QObject {
    Q_OBJECT
public:
    static FusionManager& GetInstance();

    // 数据异步注入接口,MQTT 回调
    void updateGyro(const GyroData& data);
    void updateGps(const GpsData& data);
    void updateArpaData(const ArpaData& data);
    void updateAisData(const AisData& data);
    void updateEngineStatus(const EngineStatusData& data);

    void updateVision(const std::vector<SingleObj_DetectData>& data);

    // 融合计算上传
    FusionData createFusionResult();

signals:
    void fusionDataReady(const FusionData& data);
    void uiRenderDataReady(const UIRenderSnapshot& snapshot);

private:

    explicit FusionManager(QObject* parent = nullptr);
    const double PI = 3.14159265358979323846;
    //经纬度转角度转像素坐标
    double  toRad(double degree);
    double  toDeg(double rad);
    double calculateBearing(double sLat, double sLon, double tLat, double tLon);
    int calculatePosX(double sLat, double sLon, double sHead, double tLat, double tLon, 
        double imgWidth, int cameraFovDeg, double cameraBiasDeg, int centerPixelOffset ,
        int mmsi,double distance); //centerPixelOffset:中心像素便宜
    double getDistance(double sLat, double sLon, double tLat, double tLon);

    std::unordered_map<int, TrackBindInfo> m_trackToMmsiMap;
    std::unordered_map<std::string, TargetLifecycleInfo> m_lifecycleMap;
    int m_nextGlobalId = 1;

    GpsData m_gps;
    GyroData m_gyro;
    EngineStatusData m_engineStatus;
    std::vector<AisTarget> m_aisList;
    std::vector<ArpaTarget> m_arpaList;
    std::vector<SingleObj_DetectData> m_visionList;

    int64_t m_gpsTs{0};
    int64_t m_aisTs{0};
    int64_t m_gyroTs{0};
    int64_t m_arpaTs{0};
    int64_t m_engineTs{0};

    // std::unordered_map<int, int> m_trackToMmsiMap;
    //std::unique_ptr<CameraProjector> m_projector;
    double m_maxFusionDistance;
    double m_defultImgWidth;
    double m_fovH;
    double m_cameraBiasDeg;
    int m_pixelTolerance;
    int m_centerPixelOffset;

    int m_aisGpsMaxTimeDiffMs;

    double m_arpaGateBaseMeters;
    double m_arpaGateDistanceRatio;
    double m_arpaGateMinMeters;
    double m_arpaGateMaxMeters;
    double m_arpaHeadingWeight;
    double m_arpaSpeedWeight;
    double m_arpaSpeedNormalizeKnots;
    double m_arpaAssociationAcceptCost;

    int m_visionMinBoxWidth;
    int m_visionMinBoxHeight;
    double m_visionMinConfidence;
    double m_visionToleranceBoxWidthRatio;
    double m_visionDynamicToleranceMin;
    double m_visionDynamicToleranceMax;
    double m_visionNearDistanceMeters;
    double m_visionExpandedBoxLeftRatio;
    double m_visionExpandedBoxRightRatio;
    double m_visionMinBoxBottomY;
    double m_visionNearCostBoxWidthRatio;
    double m_visionConfidenceCostWeight;
    double m_visionAssociationAcceptCost;

    int m_aisStaleTimeoutMs;
    int m_arpaStaleTimeoutMs;
    int m_visionStaleTimeoutMs;
    int m_strongIdentityDeadTtlMs;
    int m_weakIdentityDeadTtlMs;

    mutable std::shared_mutex m_rwMutex;
};
