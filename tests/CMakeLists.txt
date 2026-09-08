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

