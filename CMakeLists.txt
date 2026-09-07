cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0076 NEW)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" RELAYDX_VERSION
    LIMIT_COUNT 1)
string(STRIP "${RELAYDX_VERSION}" RELAYDX_VERSION)
if(NOT RELAYDX_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Invalid version in version.txt: ${RELAYDX_VERSION}")
endif()
project(relaydx VERSION "${RELAYDX_VERSION}" LANGUAGES C)

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

set(RELAYDX_NETWORK_TESTING OFF CACHE BOOL
    "Enable privileged Linux network namespace tests")

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
        add_test(NAME relaydx_update_e2e
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/update-e2e.sh)
        set_tests_properties(relaydx_update_e2e PROPERTIES
            ENVIRONMENT "RELAYDX_BIN=$<TARGET_FILE:relaydx>"
        )
    endif()
    if(RELAYDX_NETWORK_TESTING)
        add_test(NAME relaydx_ipv6_ra_e2e
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/ipv6-ra-e2e.sh)
        add_test(NAME relaydx_dhcp6_iana_e2e
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/dhcp6-iana-e2e.sh)
        add_test(NAME relaydx_dhcp6_pd_e2e
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/dhcp6-pd-e2e.sh)
        set_tests_properties(relaydx_ipv6_ra_e2e relaydx_dhcp6_iana_e2e
            relaydx_dhcp6_pd_e2e PROPERTIES
            ENVIRONMENT "RELAYDX_BIN=$<TARGET_FILE:relaydx>"
        )
    endif()

    install(TARGETS relaydx RUNTIME DESTINATION sbin)
else()
    message(STATUS
        "relaydx executable disabled; select a Linux toolchain to build it")
endif()
