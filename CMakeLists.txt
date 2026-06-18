cmake_minimum_required(VERSION 3.16)
project(SolarDrive VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- 查找依赖 ----
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQXX REQUIRED libpqxx)
pkg_check_modules(SPDLOG REQUIRED spdlog)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

# ---- 源文件 ----
file(GLOB_RECURSE SOLAR_SOURCES
    src/network/*.cpp
    src/http/*.cpp
    src/storage/*.cpp
    src/metadata/*.cpp
    src/api/*.cpp
    src/main.cpp
)

add_executable(solardrive ${SOLAR_SOURCES})

target_include_directories(solardrive PRIVATE src)

target_link_libraries(solardrive PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    ${LIBPQXX_LIBRARIES}
    ${SPDLOG_LIBRARIES}
    nlohmann_json::nlohmann_json
    Threads::Threads
    pq
)

target_include_directories(solardrive PRIVATE ${LIBPQXX_INCLUDE_DIRS} ${SPDLOG_INCLUDE_DIRS})
target_link_directories(solardrive PRIVATE ${LIBPQXX_LIBRARY_DIRS} ${SPDLOG_LIBRARY_DIRS})

# ---- 编译选项 ----
target_compile_options(solardrive PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-std=c++17>
)

# ---- 安装规则 ----
install(TARGETS solardrive DESTINATION bin)
