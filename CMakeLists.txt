cmake_minimum_required(VERSION 3.10)
project(ShipFusionSystem)

# ===== 强制 Qt 路径（关键修复）=====
set(Qt5_DIR /home/hczk/Qt5.12.9/5.12.9/gcc_64/lib/cmake/Qt5)
set(CMAKE_PREFIX_PATH /home/hczk/Qt5.12.9/5.12.9/gcc_64)

# ===== 强制运行时 Qt 路径 =====
set(CMAKE_BUILD_RPATH "/home/hczk/Qt5.12.9/5.12.9/gcc_64/lib")
set(CMAKE_INSTALL_RPATH "/home/hczk/Qt5.12.9/5.12.9/gcc_64/lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
# ===== 1. 基础编译选项 =====
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
# 开启所有警告，有助于发现潜在的代码问题
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3")

# ===== 2. RPATH 设置 =====
# set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,/usr/lib/x86_64-linux-gnu")

# ===== 3. 查找依赖包 =====
# Qt5
find_package(Qt5 REQUIRED COMPONENTS Core Widgets Gui Concurrent Mqtt Network)
# OpenCV
find_package(OpenCV REQUIRED)
message(STATUS "OpenCV library status:")
message(STATUS "    version: ${OpenCV_VERSION}")
message(STATUS "    libraries: ${OpenCV_LIBS}")
message(STATUS "    include path: ${OpenCV_INCLUDE_DIRS}")

# 线程与加密库 (不再硬编码 pthread, ssl, crypto)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Paho MQTT (假设已通过 apt 或源码安装至系统)
find_package(PahoMqttCpp REQUIRED)

# ===== 4. OnnxRuntime 环境配置 =====
# 如果未设置，提供一个友好的默认回退路径（假设放在工程上一级目录）
set(ONNXRUNTIME_DIR $ENV{ONNXRUNTIME_ROOT})
if(NOT ONNXRUNTIME_DIR)
    message(WARNING "环境变量 ONNXRUNTIME_ROOT 未设置。正在尝试默认相对路径...")
    # 请根据实际存放位置修改下面的 fallback 路径
    set(ONNXRUNTIME_DIR "/home/hczk/lib/onnxruntime-linux-x64-gpu-1.19.2")
endif()

if(NOT EXISTS "${ONNXRUNTIME_DIR}/include")
    message(FATAL_ERROR "无法找到 ONNXRuntime 目录，请检查 ONNXRUNTIME_ROOT 环境变量或默认路径: ${ONNXRUNTIME_DIR}")
endif()

message(STATUS "Using ONNXRuntime from: ${ONNXRUNTIME_DIR}")

# ===== 5. 头文件与库目录包含 =====
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/video
    ${CMAKE_CURRENT_SOURCE_DIR}/data
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/tracker/bytetrack/include
    ${CMAKE_SOURCE_DIR}/tracker/deepsort/include
    ${OpenCV_INCLUDE_DIRS}
    ${ONNXRUNTIME_DIR}/include
    /usr/include/eigen3
)

link_directories(
    ${ONNXRUNTIME_DIR}/lib
)

# ===== 6. 收集源文件 =====
add_executable(${PROJECT_NAME}
    main.cpp
  
    # --- 视频与 AI 推理层 ---
    VideoGrabber.cpp    
    YoloInferencer.cpp 
    DetectionThread.cpp 

    # --- 网络与协议解析层 ---
    MqttClient.cpp      # 新增
    MqttReceiver.cpp    # 新增
    MessageProcessor.cpp

    # --- 融合与业务计算层 ---
    FusionManager.cpp
    # CameraProjector.cpp
    
    # --- UI 渲染层 ---
    FFmpegWidget.cpp    # 从 FFmpegPull 中分离出来的 UI

    # --- ByteTrack 跟踪器 ---
    tracker/bytetrack/src/BytekalmanFilter.cpp
    tracker/bytetrack/src/BYTETracker.cpp
    tracker/bytetrack/src/lapjv.cpp
    tracker/bytetrack/src/STrack.cpp
    tracker/bytetrack/src/utils.cpp

    # --- DeepSORT 跟踪器 (目前被注释, 保留编译) ---
    tracker/deepsort/src/FeatureTensor.cpp
    tracker/deepsort/src/model.cpp
    tracker/deepsort/src/kalmanfilter.cpp
    tracker/deepsort/src/linear_assignment.cpp
    tracker/deepsort/src/nn_matching.cpp
    tracker/deepsort/src/track.cpp
    tracker/deepsort/src/tracker.cpp
    tracker/deepsort/src/munkres.cpp
    tracker/deepsort/src/hungarianoper.cpp
)

# ===== 7. 链接目标库 =====
target_link_libraries(${PROJECT_NAME}
    Qt5::Core
    Qt5::Widgets
    Qt5::Gui
    Qt5::Concurrent
    Qt5::Mqtt
    Qt5::Network
    ${OpenCV_LIBS}
    onnxruntime
    paho-mqttpp3
    paho-mqtt3c
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)
