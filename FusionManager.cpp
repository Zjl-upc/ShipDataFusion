#include "FusionManager.h"
#include <QDateTime>
#include <QRect>
#include <cmath>
#include <QDebug>
#include <algorithm>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_set>
//
#include <QFile>
#include <QTextStream>
#include <QDateTime>

#include "ConfigManager.h"

namespace {

constexpr double kInvalidProjectedX = 7670.0;

struct AssociationEdge {
    int targetIndex = -1;
    int obsIndex = -1;
    double cost = 0.0;
};

struct CandidateBlock {
    std::vector<int> targetIndices;
    std::vector<int> obsIndices;
    std::vector<AssociationEdge> edges;
};

struct MatchResult {
    int targetIndex = -1;
    int obsIndex = -1;
    double cost = 0.0;
};

double safeToDouble(const std::string& value, double fallback = 0.0)
{
    try {
        if (value.empty()) return fallback;
        return std::stod(value);
    } catch (...) {
        return fallback;
    }
}

double normalizeAngleDeg(double angle)
{
    while (angle <= -180.0) angle += 360.0;
    while (angle > 180.0) angle -= 360.0;
    return angle;
}

double angleDiffDeg(double a, double b)
{
    return std::abs(normalizeAngleDeg(a - b));
}

std::vector<int> hungarianMinimize(const std::vector<std::vector<double>>& inputCost)
{
    if (inputCost.empty() || inputCost.front().empty()) return {};

    const int originalRows = static_cast<int>(inputCost.size());
    const int originalCols = static_cast<int>(inputCost.front().size());
    const bool transposed = originalRows > originalCols;
    const int n = transposed ? originalCols : originalRows;
    const int m = transposed ? originalRows : originalCols;
    const double inf = 1e12;

    std::vector<std::vector<double>> cost(n + 1, std::vector<double>(m + 1, inf));
    for (int i = 0; i < originalRows; ++i) {
        for (int j = 0; j < originalCols; ++j) {
            if (transposed) cost[j + 1][i + 1] = inputCost[i][j];
            else cost[i + 1][j + 1] = inputCost[i][j];
        }
    }

    std::vector<double> u(n + 1), v(m + 1);
    std::vector<int> p(m + 1), way(m + 1);

    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(m + 1, inf);
        std::vector<char> used(m + 1, false);

        do {
            used[j0] = true;
            int i0 = p[j0];
            double delta = inf;
            int j1 = 0;

            for (int j = 1; j <= m; ++j) {
                if (used[j]) continue;
                double cur = cost[i0][j] - u[i0] - v[j];
                if (cur < minv[j]) {
                    minv[j] = cur;
                    way[j] = j0;
                }
                if (minv[j] < delta) {
                    delta = minv[j];
                    j1 = j;
                }
            }

            for (int j = 0; j <= m; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);

        do {
            int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }

    std::vector<int> assignmentOriginal(originalRows, -1);
    for (int j = 1; j <= m; ++j) {
        if (p[j] == 0) continue;

        if (transposed) {
            int originalCol = p[j] - 1;
            int originalRow = j - 1;
            if (originalRow >= 0 && originalRow < originalRows &&
                originalCol >= 0 && originalCol < originalCols) {
                assignmentOriginal[originalRow] = originalCol;
            }
        } else {
            int originalRow = p[j] - 1;
            int originalCol = j - 1;
            if (originalRow >= 0 && originalRow < originalRows &&
                originalCol >= 0 && originalCol < originalCols) {
                assignmentOriginal[originalRow] = originalCol;
            }
        }
    }

    return assignmentOriginal;
}

std::vector<CandidateBlock> buildCandidateBlocks(int targetCount, int obsCount,
                                                 const std::vector<AssociationEdge>& edges)
{
    std::vector<std::vector<int>> adjacency(targetCount + obsCount);
    for (int e = 0; e < static_cast<int>(edges.size()); ++e) {
        const int tNode = edges[e].targetIndex;
        const int oNode = targetCount + edges[e].obsIndex;
        if (tNode < 0 || tNode >= targetCount || edges[e].obsIndex < 0 || edges[e].obsIndex >= obsCount) continue;
        adjacency[tNode].push_back(oNode);
        adjacency[oNode].push_back(tNode);
    }

    std::vector<char> visited(targetCount + obsCount, false);
    std::vector<CandidateBlock> blocks;

    for (int node = 0; node < targetCount + obsCount; ++node) {
        if (visited[node] || adjacency[node].empty()) continue;

        CandidateBlock block;
        std::queue<int> q;
        q.push(node);
        visited[node] = true;

        while (!q.empty()) {
            int cur = q.front();
            q.pop();

            if (cur < targetCount) block.targetIndices.push_back(cur);
            else block.obsIndices.push_back(cur - targetCount);

            for (int next : adjacency[cur]) {
                if (!visited[next]) {
                    visited[next] = true;
                    q.push(next);
                }
            }
        }

        for (const auto& edge : edges) {
            const bool targetInside = std::find(block.targetIndices.begin(), block.targetIndices.end(),
                                                edge.targetIndex) != block.targetIndices.end();
            const bool obsInside = std::find(block.obsIndices.begin(), block.obsIndices.end(),
                                             edge.obsIndex) != block.obsIndices.end();
            if (targetInside && obsInside) block.edges.push_back(edge);
        }

        blocks.push_back(block);
    }

    return blocks;
}

std::vector<MatchResult> solveBlocksWithHungarian(const std::vector<CandidateBlock>& blocks,
                                                  double acceptCost)
{
    std::vector<MatchResult> matches;
    constexpr double inf = 1e9;

    for (const auto& block : blocks) {
        if (block.targetIndices.empty() || block.obsIndices.empty()) continue;

        std::vector<std::vector<double>> matrix(
            block.targetIndices.size(),
            std::vector<double>(block.obsIndices.size(), inf));

        for (const auto& edge : block.edges) {
            auto tIt = std::find(block.targetIndices.begin(), block.targetIndices.end(), edge.targetIndex);
            auto oIt = std::find(block.obsIndices.begin(), block.obsIndices.end(), edge.obsIndex);
            if (tIt == block.targetIndices.end() || oIt == block.obsIndices.end()) continue;

            const int row = static_cast<int>(std::distance(block.targetIndices.begin(), tIt));
            const int col = static_cast<int>(std::distance(block.obsIndices.begin(), oIt));
            matrix[row][col] = edge.cost;
        }

        std::vector<int> assignment = hungarianMinimize(matrix);
        for (int row = 0; row < static_cast<int>(assignment.size()); ++row) {
            int col = assignment[row];
            if (col < 0 || col >= static_cast<int>(block.obsIndices.size())) continue;
            if (matrix[row][col] >= acceptCost) continue;

            matches.push_back({
                block.targetIndices[row],
                block.obsIndices[col],
                matrix[row][col]
            });
        }
    }

    return matches;
}

bool isVisionBoxUsable(const SingleObj_DetectData& box, int minWidth, int minHeight, double minConfidence)
{
    return box.w > minWidth && box.h > minHeight && box.confidence > minConfidence;
}

double visionCenterX(const SingleObj_DetectData& box)
{
    return box.x + box.w * 0.5;
}

double visionBottomY(const SingleObj_DetectData& box)
{
    return box.y + box.h;
}

int upgradeFusionRetWithVision(int fusionRet)
{
    if (fusionRet == 0) return 4;
    if (fusionRet == 1) return 5;
    if (fusionRet == 3) return 6;
    return fusionRet;
}

std::string lifecycleKeyForTarget(const fusionTarget& target)
{
    if (!target.lifecycleKey.empty()) return target.lifecycleKey;
    if (target.mmsi > 0) return "AIS:" + std::to_string(target.mmsi);
    if (target.type == 1) return "ARPA:" + target.lat + "," + target.lon;
    if (target.type == 2 && target.time > 0) return "VISION:" + std::to_string(target.time);
    return "UNKNOWN:" + std::to_string(target.time);
}

} // namespace

FusionManager& FusionManager::GetInstance() {
    static FusionManager inst;
    return inst;
}

FusionManager::FusionManager(QObject* parent) : QObject(parent) {
    
    ConfigManager& config = ConfigManager::getInstance();
    m_maxFusionDistance = config.maxFusionDistance;
    m_defultImgWidth = config.logicalWidth;
    m_fovH = config.fovH;
    m_pixelTolerance = config.pixelTolerance;
    m_centerPixelOffset = config.centerPixelOffset;
    m_cameraBiasDeg = config.cameraBiasDeg;
    m_aisGpsMaxTimeDiffMs = config.aisGpsMaxTimeDiffMs;
    m_arpaGateBaseMeters = config.arpaGateBaseMeters;
    m_arpaGateDistanceRatio = config.arpaGateDistanceRatio;
    m_arpaGateMinMeters = config.arpaGateMinMeters;
    m_arpaGateMaxMeters = config.arpaGateMaxMeters;
    m_arpaHeadingWeight = config.arpaHeadingWeight;
    m_arpaSpeedWeight = config.arpaSpeedWeight;
    m_arpaSpeedNormalizeKnots = config.arpaSpeedNormalizeKnots;
    m_arpaAssociationAcceptCost = config.arpaAssociationAcceptCost;
    m_visionMinBoxWidth = config.visionMinBoxWidth;
    m_visionMinBoxHeight = config.visionMinBoxHeight;
    m_visionMinConfidence = config.visionMinConfidence;
    m_visionToleranceBoxWidthRatio = config.visionToleranceBoxWidthRatio;
    m_visionDynamicToleranceMin = config.visionDynamicToleranceMin;
    m_visionDynamicToleranceMax = config.visionDynamicToleranceMax;
    m_visionNearDistanceMeters = config.visionNearDistanceMeters;
    m_visionExpandedBoxLeftRatio = config.visionExpandedBoxLeftRatio;
    m_visionExpandedBoxRightRatio = config.visionExpandedBoxRightRatio;
    m_visionMinBoxBottomY = config.visionMinBoxBottomY;
    m_visionNearCostBoxWidthRatio = config.visionNearCostBoxWidthRatio;
    m_visionConfidenceCostWeight = config.visionConfidenceCostWeight;
    m_visionAssociationAcceptCost = config.visionAssociationAcceptCost;
    m_aisStaleTimeoutMs = config.aisStaleTimeoutMs;
    m_arpaStaleTimeoutMs = config.arpaStaleTimeoutMs;
    m_visionStaleTimeoutMs = config.visionStaleTimeoutMs;
    m_strongIdentityDeadTtlMs = config.strongIdentityDeadTtlMs;
    m_weakIdentityDeadTtlMs = config.weakIdentityDeadTtlMs;
    //camera的配置
    //CameraConfig cameraConfig;

    //cameraConfig.imageWidth = cfg.camera_width;
    //cameraConfig.imageHeight = cfg.camera_height;
    //cameraConfig.horizontalFov = cfg.camera_fovH;
    //
    //cameraConfig.transX = cfg.camera_transX;
    //cameraConfig.transY = cfg.camera_transY;
    //cameraConfig.transZ = cfg.camera_transZ;
    //
    //cameraConfig.yaw = cfg.camera_yaw;

    //cameraConfig.pitch = 0.0; 
    //cameraConfig.roll = 0.0;
    //m_projector = std::make_unique<CameraProjector>(cameraConfig);
    //qDebug() << "[FusionManager] Camera Projector Initialized with Height:" << cameraConfig.transZ;
}

void FusionManager::updateGyro(const GyroData& data)
{
    std::unique_lock<std::shared_mutex> lock(m_rwMutex);
    m_gyro = data;
    m_gyroTs = QDateTime::fromString(QString::fromStdString(data.DateTime),"yyyy-MM-dd HH:mm:ss.zzz").toMSecsSinceEpoch();
}

void FusionManager::updateGps(const GpsData& data) {
    std::unique_lock<std::shared_mutex> lock(m_rwMutex);
    m_gps = data;
    m_gpsTs = QDateTime::fromString(QString::fromStdString(data.DateTime),"yyyy-MM-dd HH:mm:ss.zzz").toMSecsSinceEpoch();
}

void FusionManager::updateAisData(const AisData& data) {
    std::unique_lock<std::shared_mutex> lock(m_rwMutex);
    m_aisList = data.content;
    m_aisTs = QDateTime::fromString(QString::fromStdString(data.DateTime),"yyyy-MM-dd HH:mm:ss.zzz").toMSecsSinceEpoch();
}
void FusionManager::updateArpaData(const ArpaData& data) {
    std::unique_lock<std::shared_mutex> lock(m_rwMutex);
    m_arpaList = data.content;
    m_arpaTs = QDateTime::fromString(QString::fromStdString(data.DateTime),"yyyy-MM-dd HH:mm:ss.zzz").toMSecsSinceEpoch();
}

void FusionManager::updateVision(const std::vector<SingleObj_DetectData>& data) {
    std::unique_lock<std::shared_mutex> lock(m_rwMutex);
    m_visionList = data;
}
void FusionManager::updateEngineStatus(const EngineStatusData& data) {
    std::unique_lock<std::shared_mutex> lock(m_rwMutex);
    m_engineStatus = data;
    m_engineTs = QDateTime::fromString(QString::fromStdString(data.DateTime),"yyyy-MM-dd HH:mm:ss.zzz").toMSecsSinceEpoch();
}

FusionData FusionManager::createFusionResult()
{
    if (m_gps.content.lat.empty() || m_gps.content.lon.empty()) {
        qDebug() << "[Fusion] GPS未就绪，跳过";
        return FusionData();
    }
    // 1. 本船数据快照 (使用单例内部的成员变量)
    GpsData currentGps;
    GyroData currentGyro;
    EngineStatusData currentEngine;
    std::vector<AisTarget> aisList;
    std::vector<ArpaTarget> arpaList;
    std::vector<SingleObj_DetectData> visionList;

    {
        std::shared_lock<std::shared_mutex> lock(m_rwMutex);
        currentGps = m_gps;
        currentGyro = m_gyro;
        currentEngine = m_engineStatus;
        aisList = m_aisList;
        arpaList = m_arpaList;
        visionList = m_visionList;
    }

    FusionData data;
    // 2. 填充本船基本信息
    data.DataType = "FusionData";
    data.DateTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString();
    data.Mmsi = currentGps.Mmsi;
    data.lat = currentGps.content.lat.empty() ? "0.0" : currentGps.content.lat;
    data.lon = currentGps.content.lon.empty() ? "0.0" : currentGps.content.lon;
    data.sog = currentGps.content.sog;
    data.cog = currentGps.content.cog;
    data.hdg = currentGyro.content.head;
    data.lrpm = currentEngine.rpmBackLift;
    data.rrpm = currentEngine.rpmBackRight;
    data.lpower = currentEngine.lpower;
    data.rpower = currentEngine.rpower;
    // data.lpower = 50;
    // data.rpower = 60;
    double ownLat = std::stod(data.lat);
    double ownLon = std::stod(data.lon);
    double ownHeading = currentGyro.content.head;

    // 第一阶段：构造 AIS 全局目标候选。AIS 有 MMSI 强身份，一帧即可进入确认态。
    std::vector<fusionTarget> aisTargets;
    aisTargets.reserve(aisList.size());

    const bool aisTimeAligned = std::abs(m_aisTs - m_gpsTs) <= m_aisGpsMaxTimeDiffMs;
    if (!aisTimeAligned && !aisList.empty()) {
        qDebug() << "AIS时间:" << m_aisTs << " GPS时间:" << m_gpsTs;
        qDebug() << "[GPS-AIS时间不一致]";
    }

    for (const auto& ais : aisList) {
        const double aisLat = safeToDouble(ais.lat);
        const double aisLon = safeToDouble(ais.lon);
        const double distToOwn = getDistance(ownLat, ownLon, aisLat, aisLon);
        if (!aisTimeAligned || distToOwn > m_maxFusionDistance) continue;

        fusionTarget target;
        target.lifecycleKey = "AIS:" + std::to_string(ais.mmsi);
        target.mmsi = ais.mmsi;
        target.time = ais.time;
        target.lat = ais.lat;
        target.lon = ais.lon;
        target.distance = distToOwn;
        target.posx = calculatePosX(ownLat, ownLon, ownHeading, aisLat, aisLon,
                                    m_defultImgWidth, m_fovH, 0, m_centerPixelOffset,
                                    ais.mmsi, distToOwn);
        target.posy = 0;
        target.type = 0;      // AIS
        target.fusionRet = 0; // 仅 AIS
        target.sog = ais.sog;
        target.cog = ais.cog;
        target.dcpa = ais.dcpa;
        target.tcpa = ais.tcpa;
        target.dangerLevel = ais.dangerLevel;
        aisTargets.push_back(target);
    }

    // 第二阶段：AIS-ARPA 候选边构建。用距离、航向、速度做粗门控，再分块 Hungarian。
    std::vector<bool> arpaUsed(arpaList.size(), false);
    std::vector<AssociationEdge> arpaEdges;
    for (int ai = 0; ai < static_cast<int>(aisTargets.size()); ++ai) {
        const auto& aisTarget = aisTargets[ai];
        const double aisLat = safeToDouble(aisTarget.lat);
        const double aisLon = safeToDouble(aisTarget.lon);

        for (int ri = 0; ri < static_cast<int>(arpaList.size()); ++ri) {
            const auto& arpa = arpaList[ri];
            const double arpaLat = safeToDouble(arpa.lat);
            const double arpaLon = safeToDouble(arpa.lon);
            const double rangeGate = std::clamp(m_arpaGateBaseMeters + aisTarget.distance * m_arpaGateDistanceRatio,
                                                m_arpaGateMinMeters,
                                                m_arpaGateMaxMeters);
            const double dist = getDistance(aisLat, aisLon, arpaLat, arpaLon);
            if (dist > rangeGate) continue;

            const double headingPenalty = angleDiffDeg(aisTarget.cog, arpa.course) / 180.0;
            const double speedPenalty = std::abs(aisTarget.sog - arpa.speed) / std::max(1.0, m_arpaSpeedNormalizeKnots);
            const double cost = (dist / rangeGate) +
                                m_arpaHeadingWeight * headingPenalty +
                                m_arpaSpeedWeight * speedPenalty;
            arpaEdges.push_back({ai, ri, cost});
        }
    }

    const auto arpaBlocks = buildCandidateBlocks(static_cast<int>(aisTargets.size()),
                                                 static_cast<int>(arpaList.size()),
                                                 arpaEdges);
    const auto arpaMatches = solveBlocksWithHungarian(arpaBlocks, m_arpaAssociationAcceptCost);
    for (const auto& match : arpaMatches) {
        if (match.targetIndex < 0 || match.targetIndex >= static_cast<int>(aisTargets.size())) continue;
        if (match.obsIndex < 0 || match.obsIndex >= static_cast<int>(arpaList.size())) continue;

        arpaUsed[match.obsIndex] = true;
        aisTargets[match.targetIndex].fusionRet = 3; // AIS + ARPA
        qDebug() << "[AIS-ARPA融合] AIS:" << aisTargets[match.targetIndex].mmsi
                 << "<-> ARPA:" << arpaList[match.obsIndex].id.c_str()
                 << "cost:" << match.cost;
    }

    for (const auto& target : aisTargets) {
        data.content.push_back(target);
    }

    // 第三阶段：处理未匹配的 ARPA 目标，进入 ARPA-only 生命周期。
    for (size_t i = 0; i < arpaList.size(); ++i) {
        if (arpaUsed[i]) continue;
        fusionTarget target;
        target.lifecycleKey = "ARPA:" + arpaList[i].id;
        target.mmsi = 0;
        target.time = arpaList[i].utcTime;
        target.lat = arpaList[i].lat;
        target.lon = arpaList[i].lon;
        target.distance = getDistance(ownLat, ownLon, safeToDouble(target.lat), safeToDouble(target.lon));
        target.posx = calculatePosX(ownLat, ownLon, ownHeading, safeToDouble(target.lat), safeToDouble(target.lon),
                                    m_defultImgWidth, m_fovH, 0, m_centerPixelOffset,
                                    static_cast<int>(safeToDouble(arpaList[i].id)), target.distance);
        target.posy = 0;
        target.type = 1;      // ARPA
        target.fusionRet = 1; // 仅 ARPA
        target.sog = arpaList[i].speed;
        target.cog = arpaList[i].course;
        data.content.push_back(target);
    }

    // 第四阶段：将视觉目标关联到 GlobalTarget。先建候选边，再按连通块做最优匹配。
    for (auto& target : data.content) {
        if (!target.lat.empty() && !target.lon.empty()) {
            target.distance = getDistance(ownLat, ownLon, safeToDouble(target.lat), safeToDouble(target.lon));
        }
    }
    std::sort(data.content.begin(), data.content.end(), [](const fusionTarget& a, const fusionTarget& b) {
        return a.distance < b.distance;
    });

    const int CONFIRM_THRESHOLD = ConfigManager::getInstance().fsmConfirmThreshold;
    std::vector<bool> visionUsed(visionList.size(), false);
    qint64 current_time = QDateTime::currentMSecsSinceEpoch();
    std::vector<AssociationEdge> visionEdges;

    for (int ti = 0; ti < static_cast<int>(data.content.size()); ++ti) {
        const auto& target = data.content[ti];
        if (target.distance > m_maxFusionDistance) continue;
        if (target.posx == static_cast<int>(kInvalidProjectedX)) continue;

        for (int vi = 0; vi < static_cast<int>(visionList.size()); ++vi) {
            const auto& box = visionList[vi];
            if (!isVisionBoxUsable(box, m_visionMinBoxWidth, m_visionMinBoxHeight, m_visionMinConfidence)) continue;

            const int tid = box.track_id;
            double cost = std::numeric_limits<double>::infinity();

            if (tid != -1 && m_trackToMmsiMap.count(tid) > 0) {
                const auto& bindInfo = m_trackToMmsiMap[tid];
                if (bindInfo.state == BindState::LOCKED && bindInfo.mmsi == target.mmsi && target.mmsi > 0) {
                    cost = 0.0;
                }
            }

            if (!std::isfinite(cost)) {
                const double centerX = visionCenterX(box);
                const double pixelError = std::abs(centerX - target.posx);
                const double dynamicTolerance = std::clamp(m_pixelTolerance + box.w * m_visionToleranceBoxWidthRatio,
                                                           m_visionDynamicToleranceMin,
                                                           m_visionDynamicToleranceMax);
                const bool nearTarget = target.distance < m_visionNearDistanceMeters;
                const bool insideExpandedBox = target.posx >= box.x - box.w * m_visionExpandedBoxLeftRatio &&
                                               target.posx <= box.x + box.w * m_visionExpandedBoxRightRatio;
                const bool bottomLooksLikeShip = visionBottomY(box) > m_visionMinBoxBottomY;

                if (nearTarget) {
                    if (!insideExpandedBox || !bottomLooksLikeShip) continue;
                    cost = std::abs(target.posx - centerX) / std::max(1.0, box.w * m_visionNearCostBoxWidthRatio);
                } else {
                    if (pixelError > dynamicTolerance) continue;
                    cost = pixelError / dynamicTolerance;
                }

                cost += (1.0 - std::clamp(box.confidence, 0.0, 1.0)) * m_visionConfidenceCostWeight;
            }

            visionEdges.push_back({ti, vi, cost});
        }
    }

    const auto visionBlocks = buildCandidateBlocks(static_cast<int>(data.content.size()),
                                                   static_cast<int>(visionList.size()),
                                                   visionEdges);
    const auto visionMatches = solveBlocksWithHungarian(visionBlocks, m_visionAssociationAcceptCost);
    for (const auto& match : visionMatches) {
        if (match.targetIndex < 0 || match.targetIndex >= static_cast<int>(data.content.size())) continue;
        if (match.obsIndex < 0 || match.obsIndex >= static_cast<int>(visionList.size())) continue;

        auto& target = data.content[match.targetIndex];
        const auto& box = visionList[match.obsIndex];
        visionUsed[match.obsIndex] = true;

        target.posx = box.x;
        target.posy = box.y;
        target.width = box.w;
        target.height = box.h;
        target.fusionRet = upgradeFusionRetWithVision(target.fusionRet);

        const int tid = box.track_id;
        if (tid != -1 && target.mmsi > 0) {
            if (m_trackToMmsiMap.count(tid) == 0) {
                m_trackToMmsiMap[tid] = {target.mmsi, BindState::TENTATIVE, 1, current_time};
            } else {
                auto& bindInfo = m_trackToMmsiMap[tid];
                if (bindInfo.mmsi == target.mmsi) {
                    bindInfo.hitCount++;
                    bindInfo.lastSeenTime = current_time;
                    if (bindInfo.hitCount >= CONFIRM_THRESHOLD && bindInfo.state == BindState::TENTATIVE) {
                        bindInfo.state = BindState::LOCKED;
                        qDebug() << "Tracker ID:" << tid << " 已稳定锁定至 MMSI:" << target.mmsi;
                    }
                } else if (bindInfo.state != BindState::LOCKED) {
                    bindInfo.mmsi = target.mmsi;
                    bindInfo.state = BindState::TENTATIVE;
                    bindInfo.hitCount = 1;
                    bindInfo.lastSeenTime = current_time;
                }
            }
        }
    }

    // 第五阶段：处理独立视觉目标。它们进入短生命周期，不直接修正 AIS/ARPA 空间状态。
    for (size_t i = 0; i < visionList.size(); ++i) {
        if (visionUsed[i]) continue;
        if (!isVisionBoxUsable(visionList[i], m_visionMinBoxWidth, m_visionMinBoxHeight, m_visionMinConfidence)) continue;

        fusionTarget target;
        target.time = QDateTime::currentMSecsSinceEpoch();
        target.lifecycleKey = visionList[i].track_id >= 0
            ? "VISION:" + std::to_string(visionList[i].track_id)
            : "VISION:UNTRACKED:" + std::to_string(target.time);
        target.mmsi = 0;
        target.posx = visionList[i].x;
        target.posy = visionList[i].y;
        target.width = visionList[i].w;
        target.height = visionList[i].h;
        target.type = 2;      // 视觉目标
        target.fusionRet = 2; // 仅视觉
        data.content.push_back(target);
    }

    // 第六阶段：轻量 GlobalTarget 生命周期缓存。保持输出协议不变，但内部开始按真实目标管理状态。
    std::unordered_set<std::string> seenKeys;
    for (const auto& target : data.content) {
        const std::string key = lifecycleKeyForTarget(target);
        seenKeys.insert(key);

        auto& info = m_lifecycleMap[key];
        if (info.globalId < 0) {
            info.globalId = m_nextGlobalId++;
            info.createTime = current_time;
            info.lifecycle = LifecycleState::BORN;
        }

        info.lastSeenTime = current_time;
        info.mmsi = target.mmsi;
        info.aisStatus = (target.fusionRet == 0 || target.fusionRet == 3 ||
                          target.fusionRet == 4 || target.fusionRet == 6) ? SourceStatus::ACTIVE : SourceStatus::INACTIVE;
        info.arpaStatus = (target.fusionRet == 1 || target.fusionRet == 3 ||
                           target.fusionRet == 5 || target.fusionRet == 6) ? SourceStatus::ACTIVE : SourceStatus::INACTIVE;
        info.visionStatus = (target.fusionRet == 2 || target.fusionRet == 4 ||
                             target.fusionRet == 5 || target.fusionRet == 6) ? SourceStatus::ACTIVE : SourceStatus::INACTIVE;

        if (info.aisStatus == SourceStatus::ACTIVE) info.aisHitCount++;
        if (info.arpaStatus == SourceStatus::ACTIVE) info.arpaHitCount++;
        if (info.visionStatus == SourceStatus::ACTIVE) info.visionHitCount++;

        const int activeSources =
            (info.aisStatus == SourceStatus::ACTIVE ? 1 : 0) +
            (info.arpaStatus == SourceStatus::ACTIVE ? 1 : 0) +
            (info.visionStatus == SourceStatus::ACTIVE ? 1 : 0);

        if (target.mmsi > 0 || info.arpaHitCount >= 3 || info.visionHitCount >= CONFIRM_THRESHOLD) {
            info.lifecycle = activeSources > 0 ? LifecycleState::TRACKING : LifecycleState::CONFIRMED;
        } else if (info.lifecycle == LifecycleState::BORN) {
            info.lifecycle = LifecycleState::TENTATIVE;
        }

        if (activeSources >= 2) info.fusionState = FusionState::FUSED;
        else if (info.fusionState == FusionState::FUSED && activeSources == 1) info.fusionState = FusionState::PARTIAL_LOST;
        else info.fusionState = FusionState::SINGLE_SOURCE;
    }

    for (auto it = m_lifecycleMap.begin(); it != m_lifecycleMap.end(); ) {
        if (seenKeys.count(it->first) > 0) {
            ++it;
            continue;
        }

        const qint64 ageSinceSeen = current_time - it->second.lastSeenTime;
        if (it->second.aisStatus == SourceStatus::ACTIVE && ageSinceSeen > m_aisStaleTimeoutMs) it->second.aisStatus = SourceStatus::STALE;
        if (it->second.arpaStatus == SourceStatus::ACTIVE && ageSinceSeen > m_arpaStaleTimeoutMs) it->second.arpaStatus = SourceStatus::STALE;
        if (it->second.visionStatus == SourceStatus::ACTIVE && ageSinceSeen > m_visionStaleTimeoutMs) it->second.visionStatus = SourceStatus::STALE;

        const bool hasStrongIdentity = it->second.mmsi > 0;
        const qint64 deadTtl = hasStrongIdentity ? m_strongIdentityDeadTtlMs : m_weakIdentityDeadTtlMs;
        if (ageSinceSeen > deadTtl) {
            it = m_lifecycleMap.erase(it);
        } else {
            it->second.lifecycle = hasStrongIdentity ? LifecycleState::LOST : LifecycleState::COASTING;
            it->second.fusionState = FusionState::PARTIAL_LOST;
            ++it;
        }
    }

    // 发送信号通知 UI
    emit fusionDataReady(data);
    return data;
}

double FusionManager::toRad(double degree)
{
    return degree * PI / 180.0;;
}

double FusionManager::toDeg(double rad)
{
    return rad * 180.0 / PI;
}

double FusionManager::calculateBearing(double lat1, double lon1, double lat2, double lon2)
{
    double rLat1 = toRad(lat1);
    double rLat2 = toRad(lat2);
    double dLon = toRad(lon2 - lon1);

    double y = sin(dLon) * cos(rLat2);
    double x = cos(rLat1) * sin(rLat2) - sin(rLat1) * cos(rLat2) * cos(dLon);

    double bring = atan2(y, x);
    double result = fmod((toDeg(bring) + 360.0), 360.0);

    return result;
}



int FusionManager::calculatePosX(double shipLat, double shipLon, double shipHead, 
                                double targetLat, double targetLon, double imgWidth,
                                int cameraFovDeg, double cameraBiasDeg, int centerPixelOffset,int mmsi, double distance)
{
    double trueBearing = calculateBearing(shipLat, shipLon, targetLat, targetLon);

    double relativeBearing = trueBearing - shipHead;
    relativeBearing -= cameraBiasDeg;

    //double distance1 = distance;
    while (relativeBearing <= -180) relativeBearing += 360;
    while (relativeBearing > 180) relativeBearing -= 360;
    double halfFov = cameraFovDeg / 2.0;
    if (relativeBearing < -halfFov || relativeBearing > halfFov)
    {
        qDebug() << "目标不在视场内，丢弃";
        return 7670;  // ❗表示无效
    }
    // qDebug() <<"MMSi:"<< mmsi  <<"真方位:" << trueBearing << "船航向:" << shipHead << "相对方位:" << relativeBearing <<"相对距离:" << distance1;
    double opticalCenterX = (imgWidth / 2.0) + centerPixelOffset;
    // qDebug() << "中心像素位置:" << opticalCenterX;
    double pixelsPerDegree = imgWidth / cameraFovDeg;
    // qDebug() << "每度像素数:" << pixelsPerDegree;
    int pixelX = static_cast<int>(opticalCenterX + relativeBearing * pixelsPerDegree);
    // qDebug() << "像素位置:" << pixelX;
    pixelX = std::clamp(pixelX, 0, (int)imgWidth - 1);
    return pixelX;

}

double FusionManager::getDistance(double sLat, double sLon, double tLat, double tLon)
{
    const double R = 6371000; // 地球半径米
    auto toRad = [](double deg) { return deg * 3.1415926 / 180.0; };

    double dLat = toRad(tLat - sLat);
    double dLon = toRad(tLon - sLon);

    double a = sin(dLat / 2) * sin(dLat / 2) +
        cos(toRad(sLat)) * cos(toRad(tLat)) *
        sin(dLon / 2) * sin(dLon / 2);

    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return R * c;
}
