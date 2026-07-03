#include "MessageProcessor.h"
#include <iostream>
#include <QDebug>



MessageProcessor& MessageProcessor::GetInstance()
{
    static MessageProcessor GetInstance;
    return GetInstance;

}

void MessageProcessor::registerHandler(MessageType type, MessageCallback callback, void* userData)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handlers[type] = { callback, userData };
}

void MessageProcessor::processMessage(const std::string& jsonData)
{
    MessageType type = detectMessageType(jsonData);
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_handlers.find(type);
    if (it != m_handlers.end())
    {
        it->second.callback(jsonData, it->second.userData);
    }
    else
    {
        //std::cout << "没有找到处理该消息类型的回调函数" << std::endl;
    }
}

MessageProcessor::MessageType MessageProcessor::detectMessageType(const std::string& jsonData)
{
    try {
        json j = json::parse(jsonData);
        if (j.contains("DeviceType"))
        {
            std::string devicetype = j.value("DeviceType", "");
            if (devicetype.find("GPS") == 0) return MessageType::GPS;
            if (devicetype.find("GYRO") == 0) return MessageType::GYRO;
        }
        if (j.contains("DataType")) {
            std::string datatype = j["DataType"].get<std::string>();
            if (datatype == "AisData") return AIS;
            if (datatype == "ArpaData") return ARPA;
            if (datatype == "EngineStatusBack") return EngineStatusBack;
        }
    }
    catch
        (const std::exception& e) {
        std::cout << "Error parsing json: " << e.what() << std::endl;
    }
    return UNKNOWN;
}


GpsData MessageProcessor::parseGpsData(const std::string& jsonStr)
{
    GpsData gpsData;
    try {
        json j = json::parse(jsonStr);
        gpsData.DeviceType = j.value("DeviceType", "");
        gpsData.DeviceId = j.value("DeviceId", 0);
        gpsData.Connect = j.value("Connect", 0);
        gpsData.Mmsi = j.value("Mmsi", 0);
        gpsData.DateTime = j.value("DateTime", "");
        if (j.contains("Content") && j["Content"].is_object()) {
            json content = j["Content"];
            gpsData.content.lat = content.value("lat", "");
            gpsData.content.lon = content.value("lon", "");
            gpsData.content.sog = content.value("sog", 0.0);
            gpsData.content.cog = content.value("cog", 0.0);
            gpsData.content.time = content.value("time", 0LL);
            gpsData.content.num = content.value("num", 0);
            // qDebug() << "[解析 GPS] MMSI:" << gpsData.Mmsi << "纬度:" << gpsData.content.lat.c_str() << "经度:" << gpsData.content.lon.c_str() 
                    //  << "航速：" << gpsData.content.sog << "cog:" << gpsData.content.cog;
        }
    }
    catch (const std::exception& e) {
        std::cout << "Error parsing JSON: " << e.what() << std::endl;
    }
    return gpsData;
}

GyroData MessageProcessor::parseGyroData(const std::string& jsonStr) {
    GyroData gyroData;
    try {
        json j = json::parse(jsonStr);
        gyroData.DeviceType = j.value("DeviceType", "");
        gyroData.DeviceId = j.value("DeviceId", 0);
        gyroData.Connect = j.value("Connect", 0);
        gyroData.Mmsi = j.value("Mmsi", 0);
        gyroData.DateTime = j.value("DateTime", "");
        if (j.contains("Content") && j["Content"].is_object()) {
            json content = j["Content"];
            gyroData.content.head = content.value("head", 0.0);
            gyroData.content.rot = content.value("rot", 0.0);
            // qDebug() << "[解析 GYRO] MMSI:" << gyroData.Mmsi << "head:" << gyroData.content.head << "rot:" << gyroData.content.rot;
        }

    }
    catch (const std::exception& e) {
        std::cout << "Error parsing GYROJSON: " << e.what() << std::endl;
    }
    return gyroData;
}

EngineStatusData MessageProcessor::parseEngineStatusData(const std::string& jsonStr)
{
    EngineStatusData engineData;
    try {
        json j = json::parse(jsonStr);
        engineData.DeviceType = j.value("DeviceType", "");
        engineData.Mmsi = j.value("Mmsi", 0);
        engineData.DateTime = j.value("DateTime", "");
        if (j.contains("Content") && j["Content"].is_array()) {
            json content = j["Content"];
            if (content.size() > 0) {
                engineData.rpmBackLift = content[0].value("rpmBack_left", 0);
                engineData.lpower = content[0].value("KW_left", 0);
            }
            if (content.size() > 1) {
                engineData.rpmBackRight = content[1].value("rpmBack_right", 0);
                engineData.rpower = content[1].value("KW_right", 0);
            }
        qDebug() << "[解析 EngineStatus] MMSI:" << engineData.Mmsi << "rpmBack_left:" << engineData.rpmBackLift << "rpmBack_right:" << engineData.rpmBackRight;
        }

    }
    catch (const std::exception& e) {
        std::cout << "Error parsing EngineJSON: " << e.what() << std::endl;
    }
    return engineData;

}

AisData MessageProcessor::parseAisData(const std::string& jsonStr) {
    AisData aisdata;
    try {
        json j = json::parse(jsonStr);
        aisdata.DataType = j.value("DataType", "");;
        aisdata.Mmsi = j.value("Mmsi", 0);
        aisdata.DateTime = j.value("DateTime", "");
        if (j.contains("Content") && j["Content"].is_array()) {
            //json contentArray = j["Content"];

            for (const auto& item : j["Content"]) {
                AisTarget target;

                target.mmsi = item.value("mmsi", 0);
                target.time = item.value("time", 0LL);
                target.lat = item.value("lat", "");
                target.lon = item.value("lon", "");
                target.type = item.value("type", 0);
                target.head = item.value("head", 0.0);
                target.rot = item.value("rot", 0.0);
                target.sog = item.value("sog", 0.0);
                target.cog = item.value("cog", 0.0);
                target.name = item.value("name", "");
                target.length = item.value("length", 0);
                target.width = item.value("width", 0);
                target.maxDraught = item.value("maxDraught", 0);
                target.dcpa = item.value("dcpa", 0.0);
                target.tcpa = item.value("tcpa", 0.0);
                target.dangerLevel = item.value("dangerLevel", 0.0);

                aisdata.content.push_back(target);
                // qDebug() << "[解析 AIS] MMSI:" << aisdata.Mmsi << "mmsi:" << target.mmsi << "time:" << target.time << "lat:" << target.lat.c_str() << "lon:" << target.lon.c_str()
                //          << "type:" << target.type << "head:" << target.head << "rot:" << target.rot << "sog:" << target.sog << "cog:" << target.cog << "name:" << target.name.c_str()
                //          << "length:" << target.length << "width:" << target.width << "maxDraught:" << target.maxDraught << "dcpa:" << target.dcpa << "tcpa:" << target.tcpa
                //          << "dangerLevel:" << target.dangerLevel;
            }
        }

    }
    catch (const std::exception& e) {
        std::cout << "Error parsing AISJSON: " << e.what() << std::endl;
    }
    return aisdata;
}

ArpaData MessageProcessor::parseArpaData(const std::string& jsonStr) {
    ArpaData arpadata;
    try {
        json j = json::parse(jsonStr);
        arpadata.DataType = j.value("DataType", "");;
        arpadata.Mmsi = j.value("Mmsi", 0);
        arpadata.DateTime = j.value("DateTime", "");
        if (j.contains("Content") && j["Content"].is_array()) {
            json contentArray = j["Content"];

            for (const auto& item : contentArray) {
                ArpaTarget target;
                target.id = item.value("id", "");
                target.name = item.value("name", "");
                target.status = item.value("status", "");
                target.lat = item.value("lat", "");
                target.lon = item.value("lon", "");
                target.speed = item.value("speed", 0.0);
                target.course = item.value("course", 0.0);
                target.bearing = item.value("bearing", 0.0);
                target.distance = item.value("distance", 0.0);
                target.dcpa = item.value("dcpa", 0.0);
                target.tcpa = item.value("tcpa", 0.0);
                target.acqType = item.value("acqType", "");
                target.utcTime = item.value("utcTime", 0LL);
                target.bearingTR = item.value("bearingTR", "");
                target.units = item.value("units", "");
                target.courseTR = item.value("courseTR", "");
                arpadata.content.push_back(target);
                // qDebug() << "[解析 ARPA] MMSI:" << arpadata.Mmsi << "id:" << target.id.c_str() << "name:" << target.name.c_str() << "status:" << target.status.c_str() << "lat:" << target.lat.c_str() << "lon:" << target.lon.c_str()
                        //  << "speed:" << target.speed << "course:" << target.course << "bearing:" << target.bearing << "distance:" << target.distance << "dcpa:" << target.dcpa << "tcpa:" << target.tcpa
                        //  << "acqType:" << target.acqType.c_str() << "utcTime:" << target.utcTime << "bearingTR:" << target.bearingTR.c_str() << "units:" << target.units.c_str() << "courseTR:" << target.courseTR.c_str();
            }
        }

    }
    catch (const std::exception& e) {
        std::cout << "Error parsing ARPAJSON: " << e.what() << std::endl;
    }
    return arpadata;
}

std::string MessageProcessor::generateFusionData(const FusionData& data)
{
    json fusiondata;
    fusiondata["DataType"] = data.DataType;
    fusiondata["DateTime"] = data.DateTime;
    fusiondata["Mmsi"] = data.Mmsi;

    fusiondata["lat"] = data.lat;
    fusiondata["lon"] = data.lon;
    fusiondata["sog"] = data.sog;
    fusiondata["cog"] = data.cog;
    fusiondata["hdg"] = data.hdg;
    fusiondata["lrpm"] = data.lrpm;
    fusiondata["rrpm"] = data.rrpm;
    fusiondata["lpower"] = data.lpower;
    fusiondata["rpower"] = data.rpower;

    json contentArray = json::array();
    for (const auto& target : data.content) {
        json targetObj;

        targetObj["posx"] = target.posx;
        targetObj["posy"] = target.posy;
        targetObj["width"] = target.width;
        targetObj["height"] = target.height;
        targetObj["mmsi"] = target.mmsi;
        targetObj["time"] = target.time;
        targetObj["lat"] = target.lat;
        targetObj["lon"] = target.lon;
        targetObj["type"] = target.type;
        targetObj["fusionRet"] = target.fusionRet;
        targetObj["sog"] = target.sog;
        targetObj["cog"] = target.cog;
        targetObj["dcpa"] = target.dcpa;
        targetObj["tcpa"] = target.tcpa;
        targetObj["dangerLevel"] = target.dangerLevel;

        contentArray.push_back(targetObj);
        // qDebug() << "[生成融合数据] MMSI:" << data.Mmsi << "posx:" << target.posx << "posy:" << target.posy << "width:" << target.width << "height:" << target.height << "mmsi:" << target.mmsi << "time:" << target.time 
        //          << "lat:" << target.lat.c_str() << "lon:" << target.lon.c_str() << "type:" << target.type << "fusionRet:" << target.fusionRet << "sog:" << target.sog << "cog:" << target.cog << "dcpa:" << target.dcpa 
        //          << "tcpa:" << target.tcpa << "dangerLevel:" << target.dangerLevel;
    }
    fusiondata["Content"] = contentArray;

    return fusiondata.dump();
}

