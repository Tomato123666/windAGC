cmake_minimum_required(VERSION 3.20)
project(WindFarmAGC VERSION 1.0.0 LANGUAGES CXX C)

# ============================================================================
# Global settings
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    add_compile_options(/utf-8 /W3)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Common include
set(COMMON_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/common")

# ============================================================================
# RT_DB library
# ============================================================================
set(RT_DB_BASE "${CMAKE_SOURCE_DIR}/储能协调控制器实时数据库与缓存模块详细设计/储能协调控制器实时数据库与缓存模块详细设计")

add_library(rt_db STATIC
    "${RT_DB_BASE}/src/rt_db/rt_db_api.c"
)
target_include_directories(rt_db PUBLIC
    "${RT_DB_BASE}/include"
    "${RT_DB_BASE}/src/rt_db"
)
target_compile_options(rt_db PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/W3>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall>
)

# RT_DB init daemon
add_executable(rt_db_init
    "${RT_DB_BASE}/src/tools/init_rt_db.c"
)
target_include_directories(rt_db_init PRIVATE "${RT_DB_BASE}/src/rt_db")
target_link_libraries(rt_db_init rt_db)

# RT_DB tests
add_executable(test_rt_db
    "${RT_DB_BASE}/test/test_rt_db.c"
)
target_include_directories(test_rt_db PRIVATE "${RT_DB_BASE}/include")
target_link_libraries(test_rt_db rt_db)

add_executable(capacity_test
    "${RT_DB_BASE}/test/capacity_test.c"
)
target_include_directories(capacity_test PRIVATE "${RT_DB_BASE}/include")
target_link_libraries(capacity_test rt_db)

# ============================================================================
# Scene 1: AGC Economic Dispatch (Wang Hanming)
# ============================================================================
set(SCENE1_DIR "${CMAKE_SOURCE_DIR}/archive/scenes/scene1_baseline/wind electricity application_1")

add_executable(scene1_agc_economic
    "${SCENE1_DIR}/main.cpp"
    "${SCENE1_DIR}/AGCController.cpp"
    "${SCENE1_DIR}/EconomicDispatcher.cpp"
    "${SCENE1_DIR}/PredictionFusion.cpp"
    "${SCENE1_DIR}/Turbine.cpp"
    "${SCENE1_DIR}/Utils.cpp"
    "${SCENE1_DIR}/RtDbLogger.cpp"
)
target_include_directories(scene1_agc_economic PRIVATE
    "${COMMON_INCLUDE_DIR}"
    "${SCENE1_DIR}"
)
target_link_libraries(scene1_agc_economic rt_db)

# ============================================================================
# Scene 2: Wind Disturbance (Zhuo Shijie)
# ============================================================================
set(SCENE2_DIR "${CMAKE_SOURCE_DIR}/archive/scenes/scene2_wind_disturbance/风电场景二：风速扰动/代码部分")

add_executable(scene2_wind_disturbance
    "${SCENE2_DIR}/main.cpp"
    "${SCENE2_DIR}/RtDbLogger.cpp"
)
target_include_directories(scene2_wind_disturbance PRIVATE
    "${COMMON_INCLUDE_DIR}"
    "${SCENE2_DIR}"
)
target_link_libraries(scene2_wind_disturbance rt_db)

# ============================================================================
# Scene 3: Primary Frequency Regulation (Zheng Huaiyong)
# ============================================================================
set(SCENE3_DIR "${CMAKE_SOURCE_DIR}/archive/scenes/scene3_freq_regulation/WindFarmFRController")

add_executable(scene3_freq_regulation
    "${SCENE3_DIR}/main.cpp"
    "${SCENE3_DIR}/RtDbLogger.cpp"
)
target_include_directories(scene3_freq_regulation PRIVATE
    "${COMMON_INCLUDE_DIR}"
    "${SCENE3_DIR}"
)
target_link_libraries(scene3_freq_regulation rt_db)

# ============================================================================
# Scene 4: Ramp Tracking (Li Longjiu) ★ Core scene
# ============================================================================
set(SCENE4_DIR "${CMAKE_SOURCE_DIR}/archive/scenes/scene4_ramp_tracking")

add_executable(scene4_ramp_tracking
    "${SCENE4_DIR}/main.cpp"
    "${SCENE4_DIR}/CommandPreprocessor指令预处理器.cpp"
    "${SCENE4_DIR}/DynamicCoordinator动态协调器.cpp"
    "${SCENE4_DIR}/FeedbackCompensator反馈补偿器..cpp"
    "${SCENE4_DIR}/ProtectionManager保护管理器.cpp"
    "${SCENE4_DIR}/RampTrackingController爬坡跟踪控制器.cpp"
    "${SCENE4_DIR}/RtDbLogger.cpp"
)
target_include_directories(scene4_ramp_tracking PRIVATE
    "${COMMON_INCLUDE_DIR}"
    "${SCENE4_DIR}"
)
target_link_libraries(scene4_ramp_tracking rt_db)

# ============================================================================
# Scene 5: Curtailment (Lai Jiawen)
# ============================================================================
set(SCENE5_DIR "${CMAKE_SOURCE_DIR}/archive/scenes/scene5_curtailment/风电AGC限电-赖嘉雯/windAGC/windAGC")

file(GLOB SCENE5_SRCS "${SCENE5_DIR}/*.cpp")
add_executable(scene5_curtailment ${SCENE5_SRCS})
target_include_directories(scene5_curtailment PRIVATE
    "${COMMON_INCLUDE_DIR}"
    "${SCENE5_DIR}"
)
target_link_libraries(scene5_curtailment rt_db)

# ============================================================================
# Scene 6: Safety Management (Zhang Xuanqi + Li Junxi)
# ============================================================================
set(SCENE6_DIR "${CMAKE_SOURCE_DIR}/archive/scenes/scene6_safety")

add_executable(scene6_safety_management
    "${SCENE6_DIR}/main.cpp"
    "${SCENE6_DIR}/RtDbLogger.cpp"
)
target_include_directories(scene6_safety_management PRIVATE
    "${COMMON_INCLUDE_DIR}"
    "${SCENE6_DIR}"
)
target_link_libraries(scene6_safety_management rt_db)

# ============================================================================
# Unified AGC System: 6 scenarios integrated with auto mode switching
# ============================================================================
set(UNIFIED_DIR "${CMAKE_SOURCE_DIR}/unified_agc")

add_executable(unified_agc
    "${UNIFIED_DIR}/main.cpp"
    "${UNIFIED_DIR}/UnifiedAGC.cpp"
    "${UNIFIED_DIR}/Strategy_Normal.cpp"
    "${UNIFIED_DIR}/Strategy_Disturbance.cpp"
    "${UNIFIED_DIR}/Strategy_FrequencyReg.cpp"
    "${UNIFIED_DIR}/Strategy_RampTracking.cpp"
    "${UNIFIED_DIR}/Strategy_Curtailment.cpp"
    "${UNIFIED_DIR}/Strategy_Safety.cpp"
)
target_include_directories(unified_agc PRIVATE
    "${COMMON_INCLUDE_DIR}"
    "${UNIFIED_DIR}"
    "${RT_DB_BASE}/include"
    "${RT_DB_BASE}/src/rt_db"
)
target_link_libraries(unified_agc rt_db)

# ============================================================================
message(STATUS "Wind Farm AGC - Unified Build configured")
message(STATUS "  Targets: rt_db_init, test_rt_db, capacity_test")
message(STATUS "  Scenes:  scene1 ~ scene6")
message(STATUS "  Unified:  unified_agc (auto mode-switching)")
message(STATUS "  Output:  ${CMAKE_BINARY_DIR}/bin/")
