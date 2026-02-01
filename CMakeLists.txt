cmake_minimum_required(VERSION 3.14)
project(myqueue VERSION 1.0.0 LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 静态编译选项
option(BUILD_STATIC "Build static executable" OFF)
option(BUILD_FULLY_STATIC "Build fully static executable (requires musl or static glibc)" OFF)

# 编译警告
add_compile_options(-Wall -Wextra -Wpedantic)

# Debug模式下的额外选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0)
else()
    add_compile_options(-O2)
endif()

# 静态链接设置
if(BUILD_FULLY_STATIC)
    # 完全静态链接 - 需要静态版本的所有库
    # 注意：这在使用 glibc 时可能有 NSS 问题，建议使用 musl-libc
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(BUILD_SHARED_LIBS OFF)
    # 需要显式链接这些库
    set(STATIC_LIBS "-lpthread -ldl")
elseif(BUILD_STATIC)
    # 推荐方式：静态链接 C++ 运行时，动态链接 glibc
    # 这样可以在大多数 Linux 系统上运行
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    set(STATIC_LIBS "")
endif()

# 包含目录
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR})  # 用于 json.hpp

# nlohmann/json - 使用项目根目录下的 json.hpp
# 如果需要通过 FetchContent 获取，可以取消下面的注释
# include(FetchContent)
# FetchContent_Declare(
#     json
#     GIT_REPOSITORY https://github.com/nlohmann/json.git
#     GIT_TAG v3.11.3
# )
# FetchContent_MakeAvailable(json)

# 定义源文件
set(CORE_SOURCES
    src/core/task.cpp
    src/core/config.cpp
    src/core/gpu_monitor.cpp
    src/core/cpu_monitor.cpp
    src/core/resource_monitor.cpp
    src/core/task_queue.cpp
    src/core/executor.cpp
    src/core/scheduler.cpp
    src/core/server.cpp
)

set(IPC_SOURCES
    src/ipc/protocol.cpp
    src/ipc/ipc_server.cpp
    src/ipc/ipc_client.cpp
)

set(CLI_SOURCES
    # src/cli/cli.cpp
)

# 核心库
add_library(myqueue_core STATIC
    ${CORE_SOURCES}
    ${IPC_SOURCES}
)

target_include_directories(myqueue_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

# 主可执行文件
add_executable(myqueue
    src/cli/main.cpp
    ${CLI_SOURCES}
)

target_link_libraries(myqueue PRIVATE
    myqueue_core
    pthread
)

# 静态链接时的额外设置
if(BUILD_FULLY_STATIC)
    target_link_options(myqueue PRIVATE -static)
    target_link_libraries(myqueue PRIVATE -lpthread -ldl)
elseif(BUILD_STATIC)
    target_link_options(myqueue PRIVATE -static-libgcc -static-libstdc++)
    # 确保 pthread 正确链接
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(myqueue PRIVATE Threads::Threads)
endif()

# 安装规则
install(TARGETS myqueue DESTINATION bin)

# 测试配置
option(BUILD_TESTS "Build tests" ON)

if(BUILD_TESTS)
    enable_testing()
    
    # Google Test
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    # 防止 gtest 覆盖父项目的编译器/链接器设置
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    
    # RapidCheck (属性测试)
    FetchContent_Declare(
        rapidcheck
        GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
        GIT_TAG master
    )
    # 启用 RapidCheck 的 GTest 集成
    set(RC_ENABLE_GTEST ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(rapidcheck)
    
    # 测试可执行文件
    add_executable(myqueue_tests
        tests/test_main.cpp
        tests/test_task.cpp
        tests/test_config.cpp
        tests/test_protocol.cpp
        tests/test_ipc_server.cpp
        tests/test_ipc_client.cpp
        tests/test_gpu_monitor.cpp
        tests/test_cpu_monitor.cpp
        tests/test_resource_monitor.cpp
        tests/test_task_queue.cpp
        tests/test_executor.cpp
        tests/test_scheduler.cpp
    )
    
    target_link_libraries(myqueue_tests PRIVATE
        myqueue_core
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        rapidcheck
        rapidcheck_gtest
        pthread
    )
    
    # GCC 8 requires explicit linking of stdc++fs for std::filesystem
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
        target_link_libraries(myqueue_tests PRIVATE stdc++fs)
    endif()
    
    target_include_directories(myqueue_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    )
    
    include(GoogleTest)
    gtest_discover_tests(myqueue_tests)
endif()

# 打印配置信息
message(STATUS "")
message(STATUS "=== myqueue Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build static: ${BUILD_STATIC}")
message(STATUS "Build fully static: ${BUILD_FULLY_STATIC}")
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "=============================")
message(STATUS "")
