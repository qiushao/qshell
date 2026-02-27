
set(SRC_HEADERS
        util/CharWidth.h
        util/CharacterColor.h
        util/Character.h
        util/ColorScheme.h
        util/Filter.h
        util/History.h
        util/HistorySearch.h
        util/KeyboardTranslator.h
        util/SearchBar.h
        util/TerminalCharacterDecoder.h
        Emulation.h
        Vt102Emulation.h
        Screen.h
        ScreenWindow.h
        TerminalDisplay.h
        qtermwidget.h
        qtermwidget_version.h)

set(SRC_FILES
        util/CharWidth.cpp
        util/ColorScheme.cpp
        util/Filter.cpp
        util/History.cpp
        util/HistorySearch.cpp
        util/KeyboardTranslator.cpp
        util/SearchBar.cpp
        util/TerminalCharacterDecoder.cpp
        Emulation.cpp
        Vt102Emulation.cpp
        Screen.cpp
        ScreenWindow.cpp
        TerminalDisplay.cpp
        qtermwidget.cpp
)

set(QRC_FILES qtermwidget.qrc)
set(QFORMS_FILES util/SearchBar.ui)

add_library(qtermwidget
        ${SRC_FILES}
        ${SRC_HEADERS}
        ${QRC_FILES}
        ${QFORMS_FILES}
)

target_link_libraries(qtermwidget PUBLIC
        Qt5::Core Qt5::Widgets Qt5::Network utf8proc
)

set(EXPORT_HEADERS util ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(qtermwidget PUBLIC ${EXPORT_HEADERS})
