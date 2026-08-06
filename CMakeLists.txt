cmake_minimum_required(VERSION 3.21)

project(
    Manage
    VERSION 0.8.0
    DESCRIPTION "Windows quotation management system for local or LAN deployment"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MANAGE_ENABLE_QT "Configure the Qt desktop, server, and data module boundaries" ON)

option(BUILD_TESTING "Build the automated test targets" ON)
if(BUILD_TESTING)
    enable_testing()
endif()

function(manage_apply_project_options target)
    target_compile_features(${target} PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_subdirectory(src/domain)

if(MANAGE_ENABLE_QT)
    find_package(
        Qt6 6.5
        REQUIRED
        COMPONENTS Core Gui Widgets Network Sql HttpServer
    )

    qt_standard_project_setup()
    add_subdirectory(third_party/QXlsx EXCLUDE_FROM_ALL)
    add_subdirectory(src/auth)
    add_subdirectory(src/data)
    add_subdirectory(src/excel)
    add_subdirectory(src/server)
    add_subdirectory(src/desktop)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
