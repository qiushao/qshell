cmake_minimum_required(VERSION 3.12)
project(qshell LANGUAGES CXX C)

message("CMAKE_SYSTEM_NAME = ${CMAKE_SYSTEM_NAME}")
message("CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    message(STATUS "Qt6_DIR=$ENV{Qt6_DIR}")
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{Qt6_DIR}")
    message(STATUS "CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}")
endif ()

if(NOT DEFINED APP_VERSION)
    set(APP_VERSION "1.0.0")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build vendored dependencies as static libraries" FORCE)

find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        Network
        SerialPort
        REQUIRED)

# clang-tidy
option(QSLOG_CLANG_TIDY_ENABLE "run clang-tidy" ON)
if(QSLOG_CLANG_TIDY_ENABLE AND ${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(CMAKE_CXX_CLANG_TIDY "clang-tidy")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    message(STATUS "enabled clang-tidy")
endif()

add_subdirectory(third_party)
add_subdirectory(src)
