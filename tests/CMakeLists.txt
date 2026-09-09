if (UNIX)
    add_executable(lua_serial_test
            lua_serial_test.cpp
            ../src/scriptengine/LuaSerialModule.cpp)
    target_include_directories(lua_serial_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(lua_serial_test Qt::Core Qt::SerialPort lua_static sol2)
    add_test(NAME lua_serial_test COMMAND lua_serial_test)
    set_tests_properties(lua_serial_test PROPERTIES TIMEOUT 15)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if (Python3_Interpreter_FOUND)
        add_test(NAME lua_serial_integration_test
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/lua_serial_integration_test.py
                $<TARGET_FILE:qshell>)
        set_tests_properties(lua_serial_integration_test PROPERTIES TIMEOUT 30)
        add_test(NAME lua_log_integration_test
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/lua_log_integration_test.py
                $<TARGET_FILE:qshell>)
        set_tests_properties(lua_log_integration_test PROPERTIES TIMEOUT 30)
    endif ()
endif ()

add_executable(qtermwidget_on_new_line_test
        qtermwidget_on_new_line_test.cpp)

target_link_libraries(qtermwidget_on_new_line_test
        Qt::Widgets
        qtermwidget)

add_test(NAME qtermwidget_on_new_line_test
        COMMAND qtermwidget_on_new_line_test)
set_tests_properties(qtermwidget_on_new_line_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(global_settings_test
        global_settings_test.cpp)

target_include_directories(global_settings_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(global_settings_test
        Qt::Core)

add_test(NAME global_settings_test
        COMMAND global_settings_test)

add_executable(terminal_timestamp_test terminal_timestamp_test.cpp)
target_link_libraries(terminal_timestamp_test Qt::Widgets qtermwidget)
add_test(NAME terminal_timestamp_test COMMAND terminal_timestamp_test)
set_tests_properties(terminal_timestamp_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(zmodem_transfer_test
        zmodem_transfer_test.cpp
        ../src/core/zmodem/ZmodemProtocol.cpp
        ../src/core/zmodem/ZmodemProtocol.h
        ../src/core/zmodem/ZmodemTransfer.cpp
        ../src/core/zmodem/ZmodemTransfer.h)

target_include_directories(zmodem_transfer_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(zmodem_transfer_test
        Qt::Core)

add_test(NAME zmodem_transfer_test
        COMMAND zmodem_transfer_test)
set_tests_properties(zmodem_transfer_test PROPERTIES
        TIMEOUT 45)

add_executable(xymodem_transfer_test
        xymodem_transfer_test.cpp
        ../src/core/xymodem/XyModemCommandDetector.cpp
        ../src/core/xymodem/XyModemCommandDetector.h
        ../src/core/xymodem/XyModemProtocol.cpp
        ../src/core/xymodem/XyModemProtocol.h
        ../src/core/xymodem/XyModemTransfer.cpp
        ../src/core/xymodem/XyModemTransfer.h)

target_include_directories(xymodem_transfer_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(xymodem_transfer_test
        Qt::Core)

add_test(NAME xymodem_transfer_test
        COMMAND xymodem_transfer_test)
set_tests_properties(xymodem_transfer_test PROPERTIES
        TIMEOUT 30)

add_executable(session_timestamp_test
        session_timestamp_test.cpp
        ../src/ui/terminal/BaseTerminal.cpp
        ../src/core/ConfigManager.cpp
        ../src/core/CryptoHelper.cpp
        ../src/core/xymodem/XyModemCommandDetector.cpp
        ../src/core/xymodem/XyModemProtocol.cpp
        ../src/core/xymodem/XyModemTransfer.cpp
        ../src/core/zmodem/ZmodemProtocol.cpp
        ../src/core/zmodem/ZmodemTransfer.cpp)

add_executable(serial_terminal_test
        serial_terminal_test.cpp
        ../src/ui/terminal/SerialTerminal.cpp
        ../src/ui/terminal/BaseTerminal.cpp
        ../src/ui/session/SessionEditDialog.cpp
        ../src/core/ConfigManager.cpp
        ../src/core/CryptoHelper.cpp
        ../src/core/xymodem/XyModemCommandDetector.cpp
        ../src/core/xymodem/XyModemProtocol.cpp
        ../src/core/xymodem/XyModemTransfer.cpp
        ../src/core/zmodem/ZmodemProtocol.cpp
        ../src/core/zmodem/ZmodemTransfer.cpp)

target_include_directories(session_timestamp_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
foreach(terminal_test session_timestamp_test serial_terminal_test)
    target_sources(${terminal_test} PRIVATE
            ../src/core/CommandHistory.cpp
            ../src/ui/command/CommandCompletionPopup.cpp
            ../src/ui/terminal/TerminalCommandCompletion.cpp)
endforeach()
target_link_libraries(session_timestamp_test Qt::Widgets qtermwidget ptyqt)
add_test(NAME session_timestamp_test COMMAND session_timestamp_test)
set_tests_properties(session_timestamp_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

target_include_directories(serial_terminal_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(serial_terminal_test
        Qt::Widgets
        Qt::SerialPort
        qtermwidget
        ptyqt)

add_test(NAME serial_terminal_test COMMAND serial_terminal_test)
set_tests_properties(serial_terminal_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 30)

add_executable(command_history_test
        command_history_test.cpp
        ../src/ui/terminal/BaseTerminal.cpp
        ../src/core/xymodem/XyModemCommandDetector.cpp
        ../src/core/xymodem/XyModemProtocol.cpp
        ../src/core/xymodem/XyModemTransfer.cpp
        ../src/core/zmodem/ZmodemProtocol.cpp
        ../src/core/zmodem/ZmodemTransfer.cpp
        ../src/core/ConfigManager.cpp
        ../src/core/CryptoHelper.cpp
        ../src/core/CommandHistory.cpp
        ../src/ui/command/CommandCompletionPopup.cpp
        ../src/ui/command/CommandWindow.cpp
        ../src/ui/command/CommandHistoryDialog.cpp
        ../src/ui/terminal/TerminalCommandCompletion.cpp)
target_include_directories(command_history_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(command_history_test Qt::Widgets qtermwidget ptyqt)
add_test(NAME command_history_test COMMAND command_history_test)
set_tests_properties(command_history_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 45)

# Exercise the actual application UI, including language changes with a live tab.
get_target_property(QSHELL_APP_SOURCES qshell SOURCES)
get_target_property(QSHELL_APP_SOURCE_DIR qshell SOURCE_DIR)
set(QSHELL_I18N_TEST_SOURCES)
foreach(source IN LISTS QSHELL_APP_SOURCES)
    if (NOT source STREQUAL "main.cpp" AND source MATCHES "\\.(cpp|h|qrc)$")
        get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${QSHELL_APP_SOURCE_DIR}")
        list(APPEND QSHELL_I18N_TEST_SOURCES "${source}")
    endif ()
endforeach()
list(REMOVE_DUPLICATES QSHELL_I18N_TEST_SOURCES)
add_executable(i18n_ui_test i18n_ui_test.cpp ${QSHELL_I18N_TEST_SOURCES})
get_target_property(QSHELL_APP_LIBRARIES qshell LINK_LIBRARIES)
target_link_libraries(i18n_ui_test ${QSHELL_APP_LIBRARIES})
target_compile_definitions(i18n_ui_test PRIVATE
        APP_VERSION="${APP_VERSION}"
        QSHELL_TRANSLATION_DIR="${CMAKE_SOURCE_DIR}/src/resources/i18n")
add_test(NAME i18n_ui_test COMMAND i18n_ui_test)
set_tests_properties(i18n_ui_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 30)

find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    add_test(NAME i18n_catalog_test
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/i18n_catalog_test.py
            $<TARGET_FILE:Qt6::lupdate>)
endif ()
