# =============================================================================
# SolarDrive — CMake 构建配置
# 依赖：libpqxx、spdlog、hiredis、OpenSSL、yaml-cpp、nlohmann_json
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(SolarDrive VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- 查找依赖 ----
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQXX REQUIRED libpqxx)
pkg_check_modules(SPDLOG REQUIRED spdlog)
pkg_check_modules(HIREDIS REQUIRED hiredis)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(yaml-cpp QUIET)
find_package(Threads REQUIRED)

set(SOLAR_YAMLCPP_TARGET "")
if(yaml-cpp_FOUND)
    if(TARGET yaml-cpp::yaml-cpp)
        set(SOLAR_YAMLCPP_TARGET yaml-cpp::yaml-cpp)
    elseif(TARGET yaml-cpp)
        set(SOLAR_YAMLCPP_TARGET yaml-cpp)
    endif()
endif()

if(NOT SOLAR_YAMLCPP_TARGET)
    pkg_check_modules(YAMLCPP QUIET yaml-cpp)
    if(YAMLCPP_FOUND)
        add_library(solar_yamlcpp INTERFACE)
        target_include_directories(solar_yamlcpp INTERFACE ${YAMLCPP_INCLUDE_DIRS})
        target_link_libraries(solar_yamlcpp INTERFACE ${YAMLCPP_LIBRARIES})
        set(SOLAR_YAMLCPP_TARGET solar_yamlcpp)
    endif()
endif()

if(NOT SOLAR_YAMLCPP_TARGET)
    include(FetchContent)
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
    )
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
    set(SOLAR_YAMLCPP_TARGET yaml-cpp)
endif()

# ---- 源文件 ----
file(GLOB_RECURSE SOLAR_SOURCES
    src/network/*.cpp
    src/http/*.cpp
    src/storage/*.cpp
    src/metadata/*.cpp
    src/api/*.cpp
    src/auth/*.cpp
    src/cache/*.cpp
    src/config/*.cpp
    src/monitor/*.cpp
    src/ws/*.cpp
    src/main.cpp
)

add_executable(solardrive ${SOLAR_SOURCES})

target_include_directories(solardrive PRIVATE src)

target_link_libraries(solardrive PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    ${LIBPQXX_LIBRARIES}
    ${SPDLOG_LIBRARIES}
    ${HIREDIS_LIBRARIES}
    nlohmann_json::nlohmann_json
    ${SOLAR_YAMLCPP_TARGET}
    Threads::Threads
    pq
    hiredis
)

target_include_directories(solardrive PRIVATE ${LIBPQXX_INCLUDE_DIRS} ${SPDLOG_INCLUDE_DIRS} ${HIREDIS_INCLUDE_DIRS})
target_link_directories(solardrive PRIVATE ${LIBPQXX_LIBRARY_DIRS} ${SPDLOG_LIBRARY_DIRS})

# ---- 编译选项 ----
target_compile_options(solardrive PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-std=c++17>
)

# ---- 单元测试 ----
option(SOLAR_BUILD_TESTS "Build unit tests (ctest)" ON)
if(SOLAR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# ---- 安装规则 ----
install(TARGETS solardrive DESTINATION bin)
install(DIRECTORY web/ DESTINATION share/solardrive/web)
