add_executable(qtermwidget_on_new_line_test
        qtermwidget_on_new_line_test.cpp)

target_link_libraries(qtermwidget_on_new_line_test
        Qt::Widgets
        qtermwidget)

add_test(NAME qtermwidget_on_new_line_test
        COMMAND qtermwidget_on_new_line_test)
set_tests_properties(qtermwidget_on_new_line_test PROPERTIES
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
