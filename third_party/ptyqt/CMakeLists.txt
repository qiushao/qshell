
set(SRC_HEADERS
        ptyqt.h)

set(SRC_FILES
        ptyqt.cpp
)

message(STATUS "Configuring for ${CMAKE_SYSTEM_NAME}")
if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(SRC_FILES ${SRC_FILES} unixptyprocess.cpp)
elseif (CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(SRC_FILES ${SRC_FILES} conptyprocess.cpp)
else ()
    message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} not supported")
endif ()

add_library(ptyqt
        ${SRC_FILES}
        ${SRC_HEADERS}
)

target_link_libraries(ptyqt PUBLIC
        Qt5::Core Qt5::Network utf8proc
)

set(EXPORT_HEADERS ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(ptyqt PUBLIC ${EXPORT_HEADERS})
