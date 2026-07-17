add_executable(qtermwidget_on_new_line_test
        qtermwidget_on_new_line_test.cpp)

target_link_libraries(qtermwidget_on_new_line_test
        Qt::Widgets
        qtermwidget)

add_test(NAME qtermwidget_on_new_line_test
        COMMAND qtermwidget_on_new_line_test)
set_tests_properties(qtermwidget_on_new_line_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
