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

file(GLOB SOLAR_NET_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/net_test/test_*.cpp
)

add_executable(net_test ${SOLAR_NET_TEST_SOURCES} ${SOLAR_NET_SOURCES})

target_include_directories(net_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/network
    ${CMAKE_CURRENT_SOURCE_DIR}/net_test
    ${SPDLOG_INCLUDE_DIRS}
)

target_link_libraries(net_test PRIVATE
    GTest::gtest_main
    ${SPDLOG_LIBRARIES}
    Threads::Threads
)

target_compile_options(net_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
)

include(GoogleTest)
gtest_discover_tests(net_test)
