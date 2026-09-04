cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0076 NEW)

project(relaydx VERSION 0.1.0 LANGUAGES C)

option(RELAYDX_BUILD_EXECUTABLE "Build the Linux relaydx executable" ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(RELAYDX_BUILD_EXECUTABLE OFF CACHE BOOL
        "Build the Linux relaydx executable" FORCE)
    message(STATUS
        "relaydx is Linux-only; configuring source index without executable")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(RELAYDX_COMMON_SOURCES
    src/main.c
    src/core.c
    src/link.c
    src/dns.c
    src/libubox/uloop.c
)

set(RELAYDX_IPV4_SOURCES
    src/ipv4/relay.c
    src/ipv4/dhcp.c
    src/ipv4/route.c
)

set(RELAYDX_IPV6_SOURCES
    src/ipv6/ra.c
    src/ipv6/dhcp.c
    src/ipv6/ia.c
    src/ipv6/ndp.c
    src/ipv6/md5.c
)

set(RELAYDX_SOURCES
    ${RELAYDX_COMMON_SOURCES}
    ${RELAYDX_IPV4_SOURCES}
    ${RELAYDX_IPV6_SOURCES}
)

# Keep all source files attached to an IDE-visible target on every platform.
add_library(relaydx_sources INTERFACE)
target_sources(relaydx_sources INTERFACE ${RELAYDX_SOURCES})
target_include_directories(relaydx_sources INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(RELAYDX_BUILD_EXECUTABLE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR
            "RELAYDX_BUILD_EXECUTABLE requires a Linux CMake toolchain")
    endif()

    add_executable(relaydx ${RELAYDX_SOURCES})
    target_link_libraries(relaydx PRIVATE relaydx_sources)
    target_compile_definitions(relaydx PRIVATE _GNU_SOURCE)
    target_compile_options(relaydx PRIVATE
        -Wall -Wextra -Werror
        -Wno-sign-compare
        -Wno-unused-parameter
    )

    include(CTest)
    if(BUILD_TESTING)
        add_test(NAME relaydx_help COMMAND relaydx --help)
        set_tests_properties(relaydx_help PROPERTIES
            PASS_REGULAR_EXPRESSION "Usage:.*relaydx"
        )
    endif()

    install(TARGETS relaydx RUNTIME DESTINATION sbin)
else()
    message(STATUS
        "relaydx executable disabled; select a Linux toolchain to build it")
endif()
