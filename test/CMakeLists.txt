find_package(GTest QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://gitee.com/mirrors/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

function(solar_add_gtest_suite target)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs SOURCES TEST_GLOB INCLUDE_DIRS LINK_LIBS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    file(GLOB test_sources CONFIGURE_DEPENDS ${ARG_TEST_GLOB})

    add_executable(${target} ${test_sources} ${ARG_SOURCES})

    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/network
        ${CMAKE_SOURCE_DIR}/src/http
        ${CMAKE_SOURCE_DIR}/src/auth
        ${CMAKE_SOURCE_DIR}/src/storage
        ${CMAKE_SOURCE_DIR}/src/ws
        ${SPDLOG_INCLUDE_DIRS}
        ${ARG_INCLUDE_DIRS}
    )

    target_link_libraries(${target} PRIVATE
        GTest::gtest_main
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        Threads::Threads
        ${SPDLOG_LIBRARIES}
        ${ARG_LINK_LIBS}
    )

    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
    )

    gtest_discover_tests(${target})
endfunction()

set(SOLAR_NET_SOURCES
    ${CMAKE_SOURCE_DIR}/src/network/acceptor.cpp
    ${CMAKE_SOURCE_DIR}/src/network/channel.cpp
    ${CMAKE_SOURCE_DIR}/src/network/epoll_poller.cpp
    ${CMAKE_SOURCE_DIR}/src/network/event_loop.cpp
    ${CMAKE_SOURCE_DIR}/src/network/event_loop_thread.cpp
    ${CMAKE_SOURCE_DIR}/src/network/event_loop_thread_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/network/log.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tcp_connection.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tcp_server.cpp
    ${CMAKE_SOURCE_DIR}/src/network/timer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/timer_queue.cpp
)

solar_add_gtest_suite(net_test
    SOURCES ${SOLAR_NET_SOURCES}
    TEST_GLOB ${CMAKE_CURRENT_SOURCE_DIR}/net_test/test_*.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/net_test
)

set(SOLAR_HTTP_SOURCES
    ${CMAKE_SOURCE_DIR}/src/http/http_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/http/http_request.cpp
    ${CMAKE_SOURCE_DIR}/src/http/http_response.cpp
    ${CMAKE_SOURCE_DIR}/src/http/http_router.cpp
    ${CMAKE_SOURCE_DIR}/src/http/static_handler.cpp
)

set(SOLAR_HTTP_NET_SOURCES
    ${CMAKE_SOURCE_DIR}/src/network/channel.cpp
    ${CMAKE_SOURCE_DIR}/src/network/epoll_poller.cpp
    ${CMAKE_SOURCE_DIR}/src/network/event_loop.cpp
    ${CMAKE_SOURCE_DIR}/src/network/log.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tcp_connection.cpp
    ${CMAKE_SOURCE_DIR}/src/network/timer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/timer_queue.cpp
)

solar_add_gtest_suite(http_test
    SOURCES ${SOLAR_HTTP_SOURCES} ${SOLAR_HTTP_NET_SOURCES}
    TEST_GLOB ${CMAKE_CURRENT_SOURCE_DIR}/http_test/test_*.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/http_test
)

set(SOLAR_AUTH_SOURCES
    ${CMAKE_SOURCE_DIR}/src/auth/jwt.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/auth_middleware.cpp
)

solar_add_gtest_suite(auth_test
    SOURCES ${SOLAR_AUTH_SOURCES}
    TEST_GLOB ${CMAKE_CURRENT_SOURCE_DIR}/auth_test/test_*.cpp
)

solar_add_gtest_suite(storage_test
    SOURCES ${CMAKE_SOURCE_DIR}/src/storage/object_store.cpp
    TEST_GLOB ${CMAKE_CURRENT_SOURCE_DIR}/storage_test/test_*.cpp
)

solar_add_gtest_suite(ws_test
    SOURCES ${CMAKE_SOURCE_DIR}/src/ws/websocket.cpp
    TEST_GLOB ${CMAKE_CURRENT_SOURCE_DIR}/ws_test/test_*.cpp
)

solar_add_gtest_suite(cache_test
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/cache/redis_client.cpp
        ${CMAKE_SOURCE_DIR}/src/cache/rate_limiter.cpp
    TEST_GLOB ${CMAKE_CURRENT_SOURCE_DIR}/cache_test/test_*.cpp
    LINK_LIBS ${HIREDIS_LIBRARIES}
    INCLUDE_DIRS ${HIREDIS_INCLUDE_DIRS}
)
