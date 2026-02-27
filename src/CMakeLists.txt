
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SRC_FILES main.cpp
        resources/resources.qrc
        ui/MainWindow.cpp
        ui/SettingDialog.cpp
        core/CryptoHelper.cpp
        core/ConfigManager.cpp
        ui/session/CollapsibleDockWidget.cpp
        ui/session/SessionTabWidget.cpp
        ui/session/SessionTreeWidget.cpp
        ui/session/SessionEditDialog.cpp
        ui/session/GroupEditDialog.cpp
        ui/terminal/BaseTerminal.cpp
        ui/terminal/SerialTerminal.cpp
        ui/terminal/LocalTerminal.cpp
        ui/terminal/SSHTerminal.h
        ui/command/CommandHistoryDialog.cpp
        ui/command/CommandWindow.cpp
        ui/command/CommandButtonBar.cpp
        scriptengine/LuaScriptEngine.cpp
        scriptengine/ScriptRunner.cpp
)

if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(SRC_FILES ${SRC_FILES} ui/terminal/SSHTerminalLinux.cpp)
elseif (CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(SRC_FILES ${SRC_FILES} ui/terminal/SSHTerminalWindows.cpp)
else ()
    message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} not supported")
endif ()

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    add_executable(qshell WIN32
            ${SRC_FILES}
            resources/resources.qrc
    )
    target_link_libraries(qshell libssh2_static)
else ()
    add_executable(qshell
            ${SRC_FILES}
            resources/resources.qrc
    )
endif ()

target_link_libraries(qshell
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::SerialPort
        qtermwidget
        ptyqt
        lua_static
        sol2
)

target_compile_definitions(qshell PRIVATE APP_VERSION="${APP_VERSION}")

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "$ENV{Qt5_DIR}/bin")
    # 编译后执行 windeployqt
    add_custom_command(TARGET qshell POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --verbose 0
            --no-compiler-runtime
            --dir "$<TARGET_FILE_DIR:qshell>"
            "$<TARGET_FILE:qshell>"
            COMMENT "Running windeployqt to copy DLLs..."
    )
endif ()

if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
install(TARGETS qshell
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES resources/qshell.desktop
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications/)

install(FILES resources/images/qshell.png
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/128x128/apps)
endif ()


# ==================== CPack 打包配置 ====================
# 公共配置（Linux 和 macOS 共用）
if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
    # 基本信息
    set(CPACK_PACKAGE_NAME "qshell")
    set(CPACK_PACKAGE_VERSION "${APP_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "QShell Terminal Emulator")
    set(CPACK_PACKAGE_DESCRIPTION "A multi-protocol terminal emulator supporting SSH, Serial, and Local terminals.")
    set(CPACK_PACKAGE_VENDOR "qiushao")
    set(CPACK_PACKAGE_CONTACT "360325900@qq.com")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/qiushao/qshell")
endif()

# ==================== CPack DEB 打包配置 ====================
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    # 设置打包类型为 DEB
    set(CPACK_GENERATOR "DEB")

    # DEB 特定配置
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "qiushao <360325900@qq.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")

    # 自动检测 + 手动补充
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "libqt5core5a, libqt5gui5, libqt5widgets5, libqt5serialport5"
    )
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

    include(CPack)
endif()

# ==================== CPack macOS DMG 打包配置 ====================
if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    # 设置打包类型为 DragNDrop (DMG)
    set(CPACK_GENERATOR "DragNDrop")

    # macOS 特定配置
    set(CPACK_DMG_VOLUME_NAME "qshell")
    set(CPACK_DMG_FORMAT "UDZO")  # 压缩格式
    set(CPACK_DMG_BACKGROUND_IMAGE "")  # 可选：设置 DMG 背景图片
    set(CPACK_DMG_DS_STORE "")  # 可选：自定义 .DS_Store 布局

    # 应用程序图标 (需要将 PNG 转换为 ICNS 格式)
    # 注意：需要手动创建 qshell.icns 文件或使用脚本转换
    set(CPACK_BUNDLE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/images/qshell.icns")
    set(CPACK_BUNDLE_NAME "qshell")

    # 配置 Info.plist 模板，替换版本号
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        @ONLY
    )
    set(CPACK_BUNDLE_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")

    # 设置应用程序类型
    set(CPACK_PACKAGE_FILE_NAME "qshell-${APP_VERSION}-Darwin")

    include(CPack)
endif()
