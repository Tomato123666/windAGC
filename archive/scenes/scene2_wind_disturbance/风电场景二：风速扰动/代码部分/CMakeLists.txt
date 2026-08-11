cmake_minimum_required(VERSION 3.12)
project(WindAGC_Scene2 VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# RT_DB 库路径（相对于项目根目录）
set(RTDB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../储能协调控制器实时数据库与缓存模块详细设计/储能协调控制器实时数据库与缓存模块详细设计")
include_directories(${RTDB_ROOT}/include ${CMAKE_SOURCE_DIR})

# 可执行文件
add_executable(wind_agc_scene2
    main.cpp
    RtDbLogger.cpp
)

# 链接 rt_db 静态库
target_link_libraries(wind_agc_scene2
    "${RTDB_ROOT}/build/Release/rt_db.lib"
)

# 设置输出目录
set_target_properties(wind_agc_scene2 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
