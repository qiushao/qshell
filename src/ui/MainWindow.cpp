#include "MainWindow.h"
#include "SettingDialog.h"
#include "command/CommandButtonBar.h"
#include "core/ConfigManager.h"
#include "mcp/McpHttpServer.h"
#include "scriptengine/LuaScriptEngine.h"
#include "scriptengine/ScriptRunner.h"
#include "session/CollapsibleDockWidget.h"
#include "session/SessionTabWidget.h"
#include "session/SessionTreeWidget.h"
#include "ui/command/CommandWindow.h"
#include "ui/terminal/BaseTerminal.h"
#include "ui/terminal/LocalTerminal.h"
#include "ui/terminal/SSHTerminal.h"
#include "ui/terminal/SerialTerminal.h"

#include <QApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QThread>
#include <utility>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowState(Qt::WindowMaximized);
    setContextMenuPolicy(Qt::NoContextMenu);
    QIcon windowIcon(":/images/application.png");
    setWindowIcon(windowIcon);

    initLuaEngine();
    initIcons();
    initActions();
    initMenu();
    initToolbar();
    initTableWidget();
    initSessionTree();
    initCommandWindow();
    initButtonBar();
    restoreLayoutState();
    initMcpServer();

#if defined(Q_OS_WIN)
    qApp->installNativeEventFilter(this);
#endif
}

MainWindow::~MainWindow() {
    qDebug() << "~MainWindow";
#if defined(Q_OS_WIN)
    qApp->removeNativeEventFilter(this);
#endif
}

void MainWindow::initLuaEngine() {
    luaEngine_ = new LuaScriptEngine(this);
    QObject::connect(luaEngine_, &LuaScriptEngine::scriptFinished, this, [this]() {
        qDebug() << "Running script finished";
        stopScriptAction_->setEnabled(false);
        runLuaScriptAction_->setEnabled(true);
    });
    QObject::connect(luaEngine_, &LuaScriptEngine::scriptError, this, [this](const QString &error) {
        qDebug() << "Running script error";
        stopScriptAction_->setEnabled(false);
        runLuaScriptAction_->setEnabled(true);
        QMessageBox::warning(this, tr("脚本执行错误"), error);
    });
}

void MainWindow::initIcons() {
    connectIcon_ = new QIcon(":/images/connect.png");
    disconnectIcon_ = new QIcon(":/images/disconnect.png");
    connectStateIcon_ = new QIcon(":/images/connect_state.png");
    disconnectStateIcon_ = new QIcon(":/images/disconnect_state.png");

    settingsIcon_ = new QIcon(":/images/setting.png");
    exitIcon_ = new QIcon(":/images/exit.png");

    copySelectedIcon_ = new QIcon(":/images/copy.png");
    copyAllIcon_ = new QIcon(":/images/copy_all.png");
    pasteIcon_ = new QIcon(":/images/paste.png");
    findIcon_ = new QIcon(":/images/find.png");
    clearScreenIcon_ = new QIcon(":/images/clear.png");

    toggleOnIcon_ = new QIcon(":/images/toggle_on.png");
    toggleOffIcon_ = new QIcon(":/images/toggle_off.png");

    fullscreenIcon_ = new QIcon(":/images/fullscreen.png");
}

void MainWindow::initTableWidget() {
    tabWidget_ = new SessionTabWidget(this);
    tabWidget_->setTabsClosable(true);
    tabWidget_->setMovable(true);
    setCentralWidget(tabWidget_);

    connect(tabWidget_, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(tabWidget_, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    static bool firstShow = true;
    if (firstShow) {
        firstShow = false;
        QTimer::singleShot(100, this, &MainWindow::restoreLayoutState);
    }
}

bool MainWindow::runScriptAtStartup(const QString &scriptPath, const QStringList &scriptArgs) {
    if (scriptPath.isEmpty()) {
        qWarning() << "Startup script path is empty";
        return false;
    }

    if (!QFile::exists(scriptPath)) {
        qWarning() << "Startup script does not exist:" << scriptPath;
        return false;
    }

    runScript(scriptPath, scriptArgs);
    return true;
}

void MainWindow::onOpenSession(const QString &sessionId) {
    openSessionById(sessionId);
}

void MainWindow::onSessionError(BaseTerminal *terminal) const {
    if (terminal == currentTab_) {
        onDisconnectAction();
    } else {
        int index = tabWidget_->indexOf(terminal);
        terminal->disconnect();
        tabWidget_->setTabIcon(index, *disconnectStateIcon_);
    }
}

void MainWindow::initSessionTree() {
    sessionDock_ = new CollapsibleDockWidget(this);
    sessionTree_ = new SessionTreeWidget(this);
    addDockWidget(Qt::LeftDockWidgetArea, sessionDock_);
    sessionDock_->setContentWidget(sessionTree_);
    sessionDock_->setFeatures(QDockWidget::DockWidgetMovable);
    sessionDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    connect(sessionTree_, &SessionTreeWidget::openSession,
            this, &MainWindow::onOpenSession);
}

void MainWindow::initCommandWindow() {
    commandWindowDock_ = new QDockWidget(tr("Command Window"), this);
    commandWindow_ = new CommandWindow(this);
    commandWindowDock_->setWidget(commandWindow_);
    commandWindowDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::BottomDockWidgetArea, commandWindowDock_);
    resizeDocks({commandWindowDock_}, {80}, Qt::Vertical);

    // 隐藏标题栏
    QWidget *titleBar = commandWindowDock_->titleBarWidget();
    auto *emptyWidget = new QWidget();
    commandWindowDock_->setTitleBarWidget(emptyWidget);
    delete titleBar;

    // 连接命令发送信号
    connect(commandWindow_, &CommandWindow::commandSend,
            this, &MainWindow::onCommandSend);
}

void MainWindow::initButtonBar() {
    commandButtonBar_ = new CommandButtonBar(this);
    commandButtonBar_->setMovable(false);
    addToolBar(Qt::BottomToolBarArea, commandButtonBar_);

    // 连接命令触发信号
    connect(commandButtonBar_, &CommandButtonBar::commandTriggered,
            this, &MainWindow::onCommandSend);
}

void MainWindow::initMcpServer() {
    mcpServer_ = new McpHttpServer(this, this);
    connect(ConfigManager::instance(), &ConfigManager::globalSettingsChanged,
            this, &MainWindow::syncMcpServer);
    syncMcpServer();
}

void MainWindow::syncMcpServer() {
    if (mcpServer_ == nullptr) {
        return;
    }

    GlobalSettings settings = ConfigManager::instance()->globalSettings();
    if (!settings.mcpEnabled) {
        mcpServer_->stop();
        return;
    }

    if (settings.mcpBearerToken.trimmed().isEmpty()) {
        settings.mcpBearerToken = ConfigManager::generateMcpBearerToken();
        ConfigManager::instance()->setGlobalSettings(settings);
        return;
    }

    QString errorMessage;
    if (!mcpServer_->start(settings.mcpPort, settings.mcpBearerToken, &errorMessage)) {
        qWarning() << "Failed to start MCP server:" << errorMessage;
        return;
    }

    qDebug() << "MCP server listening at" << mcpServer_->endpointUrl();
}

void MainWindow::restoreLayoutState() {
    auto config = ConfigManager::instance();
    auto layout = config->getWindowLayout();
    if (!layout.showToolBar) {
        toolBar_->hide();
        toggleToolbarAction_->setIcon(*toggleOffIcon_);
    }

    if (!layout.showSessions) {
        sessionDock_->hide();
        toggleSessionManagerAction_->setIcon(*toggleOffIcon_);
    }

    if (!layout.showCommandWindow) {
        commandWindowDock_->hide();
        toggleCommandWindowAction_->setIcon(*toggleOffIcon_);
    }

    if (!layout.showCommandButton) {
        commandButtonBar_->hide();
        toggleCommandButtonAction_->setIcon(*toggleOffIcon_);
    }

    resizeDocks({sessionDock_}, {300}, Qt::Horizontal);
}

void MainWindow::onTabChanged(int index) {
    // qDebug() << "onTabChanged, index = " << index;

    if (index < 0) {
        currentTab_ = nullptr;
        return;
    }

    currentTab_ = dynamic_cast<BaseTerminal *>(tabWidget_->widget(index));
}

void MainWindow::onTabCloseRequested(int index) const {
    if (index < 0 || index >= tabWidget_->count()) {
        return;
    }

    QWidget *widget = tabWidget_->widget(index);
    auto *tab = dynamic_cast<BaseTerminal *>(widget);
    if (tab != nullptr) {
        tab->disconnect();
    }
    tabWidget_->removeTab(index);
    delete widget;
}

void MainWindow::onCommandSend(const QString &command) {
    sendTextToCurrent(command, true);
}

void MainWindow::initActions() {
    settingsAction_ = new QAction(*settingsIcon_, tr("Setting"), this);
    connect(settingsAction_, &QAction::triggered, this, &MainWindow::onSettingsAction);

    importConfigAction_ = new QAction(tr("Import Config..."), this);
    connect(importConfigAction_, &QAction::triggered, this, &MainWindow::onImportConfigAction);

    exportConfigAction_ = new QAction(tr("Export Config..."), this);
    connect(exportConfigAction_, &QAction::triggered, this, &MainWindow::onExportConfigAction);

    connectAction_ = new QAction(*connectIcon_, tr("Connect"), this);
    connectAction_->setEnabled(false);
    connect(connectAction_, &QAction::triggered, this, &MainWindow::onConnectAction);

    disConnectAction_ = new QAction(*disconnectIcon_, tr("Disconnect"), this);
    disConnectAction_->setEnabled(true);
    connect(disConnectAction_, &QAction::triggered, this, &MainWindow::onDisconnectAction);

    exitAction_ = new QAction(*exitIcon_, tr("Exit"), this);
    connect(exitAction_, &QAction::triggered, this, &MainWindow::onExitAction);


    copySelectedAction_ = new QAction(*copySelectedIcon_, tr("Copy Selected"), this);
    connect(copySelectedAction_, &QAction::triggered, this, &MainWindow::onCopySelectedAction);
    copySelectedAction_->setShortcut(QApplication::translate("MainWindow", "Ctrl+Shift+C", nullptr));

    copyAllAction_ = new QAction(*copyAllIcon_, tr("Copy All"), this);
    connect(copyAllAction_, &QAction::triggered, this, &MainWindow::onCopyAllAction);
    copyAllAction_->setShortcut(QApplication::translate("MainWindow", "Ctrl+Shift+A", nullptr));

    pasteAction_ = new QAction(*pasteIcon_, tr("Paste"), this);
    connect(pasteAction_, &QAction::triggered, this, &MainWindow::onPasteAction);
    pasteAction_->setShortcut(QApplication::translate("MainWindow", "Ctrl+Shift+V", nullptr));

    findAction_ = new QAction(*findIcon_, tr("Find"), this);
    connect(findAction_, &QAction::triggered, this, &MainWindow::onFindAction);
    findAction_->setShortcut(QApplication::translate("MainWindow", "Ctrl+F", nullptr));

    clearScreenAction_ = new QAction(*clearScreenIcon_, tr("Clear Screen"), this);
    connect(clearScreenAction_, &QAction::triggered, this, &MainWindow::onClearScreenAction);
    clearScreenAction_->setShortcut(QApplication::translate("MainWindow", "Ctrl+Shift+X", nullptr));

    toggleToolbarAction_ = new QAction(*toggleOnIcon_, tr("Toolbar"), this);
    connect(toggleToolbarAction_, &QAction::triggered, this, &MainWindow::onToggleToolbarAction);

    toggleSessionManagerAction_ = new QAction(*toggleOnIcon_, tr("Session Manager"), this);
    connect(toggleSessionManagerAction_, &QAction::triggered, this, &MainWindow::onToggleSessionManagerAction);

    toggleCommandWindowAction_ = new QAction(*toggleOnIcon_, tr("Command Window"), this);
    connect(toggleCommandWindowAction_, &QAction::triggered, this, &MainWindow::onToggleCommandWindowAction);

    toggleCommandButtonAction_ = new QAction(*toggleOnIcon_, tr("Command Button"), this);
    connect(toggleCommandButtonAction_, &QAction::triggered, this, &MainWindow::onToggleCommandButtonAction);

    fullscreenAction_ = new QAction(*fullscreenIcon_, tr("Fullscreen"), this);
    fullscreenAction_->setShortcut(QApplication::translate("MainWindow", "F11", nullptr));
    connect(fullscreenAction_, &QAction::triggered, this, &MainWindow::onFullscreenAction);

    // Script actions
    runLuaScriptAction_ = new QAction(tr("Run Lua Script..."), this);
    connect(runLuaScriptAction_, &QAction::triggered, this, &MainWindow::onRunLuaScriptAction);

    stopScriptAction_ = new QAction(tr("Stop Script"), this);
    stopScriptAction_->setEnabled(false);
    connect(stopScriptAction_, &QAction::triggered, this, &MainWindow::onStopScriptAction);

    // Help actions
    docAction_ = new QAction(tr("Documentation"), this);
    connect(docAction_, &QAction::triggered, this, &MainWindow::onDocAction);

    aboutAction_ = new QAction(tr("About"), this);
    connect(aboutAction_, &QAction::triggered, this, &MainWindow::onAboutAction);
}

void MainWindow::initMenu() {
    mainMenuBar_ = new QMenuBar(this);
    setMenuBar(mainMenuBar_);

    fileMenu_ = new QMenu(tr("File"), mainMenuBar_);
    mainMenuBar_->addAction(fileMenu_->menuAction());
    fileMenu_->addAction(settingsAction_);
    fileMenu_->addSeparator();
    fileMenu_->addAction(importConfigAction_);
    fileMenu_->addAction(exportConfigAction_);
    fileMenu_->addSeparator();
    fileMenu_->addAction(connectAction_);
    fileMenu_->addAction(disConnectAction_);
    fileTransferMenu_ = fileMenu_->addMenu(tr("文件传输"));
    fileMenu_->addSeparator();
    fileMenu_->addAction(exitAction_);
    connect(fileMenu_, &QMenu::aboutToShow,
            this, &MainWindow::updateFileTransferMenu);

    editMenu_ = new QMenu(tr("Edit"), mainMenuBar_);
    mainMenuBar_->addAction(editMenu_->menuAction());
    editMenu_->addAction(copySelectedAction_);
    editMenu_->addAction(copyAllAction_);
    editMenu_->addAction(pasteAction_);
    editMenu_->addAction(findAction_);
    editMenu_->addAction(clearScreenAction_);

    viewMenu_ = new QMenu(tr("View"), mainMenuBar_);
    mainMenuBar_->addAction(viewMenu_->menuAction());
    viewMenu_->addAction(toggleToolbarAction_);
    viewMenu_->addAction(toggleSessionManagerAction_);
    viewMenu_->addAction(toggleCommandWindowAction_);
    viewMenu_->addAction(toggleCommandButtonAction_);
    viewMenu_->addSeparator();
    viewMenu_->addAction(fullscreenAction_);

    // Script menu
    scriptMenu_ = new QMenu(tr("Script"), mainMenuBar_);
    mainMenuBar_->addAction(scriptMenu_->menuAction());
    scriptMenu_->addAction(runLuaScriptAction_);
    scriptMenu_->addAction(stopScriptAction_);
    scriptMenu_->addSeparator();
    recentScriptMenu_ = scriptMenu_->addMenu(tr("Recent Scripts"));
    loadRecentScripts();
    updateRecentScriptsMenu();

    helpMenu_ = new QMenu(tr("Help"), mainMenuBar_);
    mainMenuBar_->addAction(helpMenu_->menuAction());
    helpMenu_->addAction(docAction_);
    helpMenu_->addSeparator();
    helpMenu_->addAction(aboutAction_);
}

void MainWindow::updateFileTransferMenu() {
    fileTransferMenu_->clear();
    fileTransferMenu_->setEnabled(currentTab_ != nullptr);
    if (currentTab_ != nullptr) {
        currentTab_->populateFileTransferMenu(fileTransferMenu_);
    }
}

void MainWindow::initToolbar() {
    toolBar_ = new QToolBar(this);
    toolBar_->setMovable(false);
    addToolBar(Qt::TopToolBarArea, toolBar_);
    toolBar_->addAction(settingsAction_);
    toolBar_->addSeparator();
    toolBar_->addAction(connectAction_);
    toolBar_->addAction(disConnectAction_);
    toolBar_->addSeparator();
    toolBar_->addAction(copyAllAction_);
    toolBar_->addAction(copySelectedAction_);
    toolBar_->addAction(pasteAction_);
    toolBar_->addSeparator();
    toolBar_->addAction(findAction_);
    toolBar_->addAction(clearScreenAction_);
    // 添加弹性空白，将后面的控件推到右边
    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar_->addWidget(spacer);
    toolBar_->addAction(fullscreenAction_);
}

void MainWindow::onSettingsAction() {
    SettingDialog settingDialog(this);
    settingDialog.exec();
}

void MainWindow::onImportConfigAction() {
    if (QMessageBox::question(this,
                              tr("Import Config"),
                              tr("Importing will overwrite current configuration. Continue?"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(this,
                                                          tr("Import Config"),
                                                          QString(),
                                                          tr("JSON Files (*.json);;All Files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!ConfigManager::instance()->importConfig(filePath, &errorMessage)) {
        QMessageBox::critical(this,
                              tr("Import Failed"),
                              tr("Failed to import config.\n%1").arg(errorMessage));
        return;
    }

    const auto layout = ConfigManager::instance()->getWindowLayout();
    toolBar_->setVisible(layout.showToolBar);
    sessionDock_->setVisible(layout.showSessions);
    commandWindowDock_->setVisible(layout.showCommandWindow);
    commandButtonBar_->setVisible(layout.showCommandButton);
    toggleToolbarAction_->setIcon(layout.showToolBar ? *toggleOnIcon_ : *toggleOffIcon_);
    toggleSessionManagerAction_->setIcon(layout.showSessions ? *toggleOnIcon_ : *toggleOffIcon_);
    toggleCommandWindowAction_->setIcon(layout.showCommandWindow ? *toggleOnIcon_ : *toggleOffIcon_);
    toggleCommandButtonAction_->setIcon(layout.showCommandButton ? *toggleOnIcon_ : *toggleOffIcon_);
    resizeDocks({sessionDock_}, {layout.sessionDockWidth}, Qt::Horizontal);
    resizeDocks({commandWindowDock_}, {layout.commandWindowHeight}, Qt::Vertical);

    QMessageBox::information(this,
                             tr("Import Success"),
                             tr("Configuration imported successfully."));
}

void MainWindow::onExportConfigAction() {
    const QString filePath = QFileDialog::getSaveFileName(this,
                                                          tr("Export Config"),
                                                          "qshell-config.json",
                                                          tr("JSON Files (*.json);;All Files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!ConfigManager::instance()->exportConfig(filePath, &errorMessage)) {
        QMessageBox::critical(this,
                              tr("Export Failed"),
                              tr("Failed to export config.\n%1").arg(errorMessage));
        return;
    }

    QMessageBox::information(this,
                             tr("Export Success"),
                             tr("Configuration exported successfully."));
}

void MainWindow::onConnectAction() {
    if (currentTab_ == nullptr || currentTab_->isConnect()) {
        return;
    }

    currentTab_->connect();
    if (currentTab_->isConnect()) {
        connectAction_->setEnabled(false);
        disConnectAction_->setEnabled(true);
        tabWidget_->setTabIcon(tabWidget_->currentIndex(), *connectStateIcon_);
    } else {
        tabWidget_->setTabIcon(tabWidget_->currentIndex(), *disconnectStateIcon_);
    }
}

void MainWindow::onDisconnectAction() const {
    if (currentTab_ == nullptr || !currentTab_->isConnect()) {
        return;
    }

    currentTab_->disconnect();
    if (!currentTab_->isConnect()) {
        connectAction_->setEnabled(true);
        disConnectAction_->setEnabled(false);
    }
    tabWidget_->setTabIcon(tabWidget_->currentIndex(), *disconnectStateIcon_);
}

void MainWindow::onExitAction() {
    for (int i = 0; i < tabWidget_->count(); ++i) {
        auto *terminal = dynamic_cast<BaseTerminal *>(tabWidget_->widget(i));
        if (terminal != nullptr) {
            terminal->disconnect();
        }
    }
    QApplication::exit(0);
}

void MainWindow::onCopySelectedAction() {
    if (currentTab_ != nullptr) {
        currentTab_->copyClipboard();
    }
}

void MainWindow::onCopyAllAction() {
    if (currentTab_ == nullptr) {
        return;
    }
    currentTab_->setSelectionStart(0, 0);
    currentTab_->setSelectionEnd(currentTab_->screenLinesCount(), currentTab_->screenColumnsCount());
    currentTab_->copyClipboard();
}

void MainWindow::onPasteAction() {
    if (currentTab_ != nullptr) {
        currentTab_->pasteClipboard();
    }
}

void MainWindow::onFindAction() {
    if (currentTab_ != nullptr) {
        currentTab_->toggleShowSearchBar();
    }
}

void MainWindow::onClearScreenAction() {
    clearCurrentScreen();
}

void MainWindow::onToggleToolbarAction() {
    if (toolBar_->isHidden()) {
        toolBar_->show();
        toggleToolbarAction_->setIcon(*toggleOnIcon_);
    } else {
        toolBar_->hide();
        toggleToolbarAction_->setIcon(*toggleOffIcon_);
    }
    ConfigManager::instance()->showToolBar(!toolBar_->isHidden());
}

void MainWindow::onToggleSessionManagerAction() {
    if (sessionDock_->isHidden()) {
        sessionDock_->show();
        toggleSessionManagerAction_->setIcon(*toggleOnIcon_);
    } else {
        sessionDock_->hide();
        toggleSessionManagerAction_->setIcon(*toggleOffIcon_);
    }
    ConfigManager::instance()->showSessions(!sessionDock_->isHidden());
}

void MainWindow::onToggleCommandWindowAction() {
    if (commandWindowDock_->isHidden()) {
        commandWindowDock_->show();
        toggleCommandWindowAction_->setIcon(*toggleOnIcon_);
    } else {
        commandWindowDock_->hide();
        toggleCommandWindowAction_->setIcon(*toggleOffIcon_);
    }
    ConfigManager::instance()->showCommandWindow(!commandWindowDock_->isHidden());
}

void MainWindow::onToggleCommandButtonAction() {
    if (commandButtonBar_->isHidden()) {
        commandButtonBar_->show();
        toggleCommandButtonAction_->setIcon(*toggleOnIcon_);
    } else {
        commandButtonBar_->hide();
        toggleCommandButtonAction_->setIcon(*toggleOffIcon_);
    }
    ConfigManager::instance()->showCommandButton(!commandButtonBar_->isHidden());
}

BaseTerminal *MainWindow::terminalForWidget(QWidget *widget) {
    while (widget != nullptr) {
        if (auto *terminal = qobject_cast<BaseTerminal *>(widget)) {
            return terminal;
        }
        widget = widget->parentWidget();
    }
    return nullptr;
}

#if defined(Q_OS_WIN)
bool MainWindow::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(eventType)
    Q_UNUSED(result)

    const auto *nativeMessage = static_cast<MSG *>(message);
    if ((nativeMessage->message != WM_KEYDOWN && nativeMessage->message != WM_KEYUP) ||
        nativeMessage->wParam != 'C') {
        return false;
    }

    const bool keyDownSeen = copyShortcutKeyDownSeen_;
    if (nativeMessage->message == WM_KEYUP) {
        copyShortcutKeyDownSeen_ = false;
    }

    const bool controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool windowsPressed = (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
                                (GetKeyState(VK_RWIN) & 0x8000) != 0;
    if (!controlPressed || !shiftPressed || altPressed || windowsPressed) {
        return false;
    }

    if (nativeMessage->message == WM_KEYDOWN) {
        copyShortcutKeyDownSeen_ = true;
        return false;
    }

    // A registered global hotkey can consume WM_KEYDOWN while still allowing
    // WM_KEYUP through. Copy on release only when that missing-down pattern is
    // observed; otherwise the regular QAction shortcut already handled it.
    if (keyDownSeen) {
        return false;
    }

    BaseTerminal *terminal = terminalForWidget(QApplication::focusWidget());
    if (terminal == nullptr) {
        terminal = currentTab_;
    }
    if (terminal == nullptr) {
        return false;
    }

    terminal->copyClipboard();
    return true;
}
#endif

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (isFullscreen_ && watched == fullscreenWidget_ && event->type() == QEvent::KeyPress) {
        auto *keyEvent = dynamic_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            exitFullscreen();
            return true;
        }
        // F11 也可以退出全屏
        if (keyEvent->key() == Qt::Key_F11) {
            exitFullscreen();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onFullscreenAction() {
    if (currentTab_ == nullptr) {
        return;
    }

    if (!isFullscreen_) {
        isFullscreen_ = true;

        BaseTerminal *terminal = currentTab_;
        fullscreenWidget_ = terminal;

        int index = tabWidget_->indexOf(terminal);
        QString tabText = tabWidget_->tabText(index);
        QIcon tabIcon = tabWidget_->tabIcon(index);

        terminal->setProperty("tabIndex", index);
        terminal->setProperty("tabText", tabText);
        terminal->setProperty("tabIcon", tabIcon);

        disconnect(tabWidget_, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
        tabWidget_->removeTab(index);
        connect(tabWidget_, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

        terminal->setParent(nullptr);
        terminal->setWindowFlags(Qt::Window);
        terminal->showFullScreen();

        // 创建退出全屏按钮
        auto *exitBtn = new QPushButton(terminal);
        exitBtn->setObjectName("exitFullscreenBtn");
        exitBtn->setIcon(QIcon(":/images/fullscreen.png"));
        exitBtn->setIconSize(QSize(24, 24));
        exitBtn->setFixedSize(40, 40);
        exitBtn->setToolTip(tr("Exit Fullscreen (Esc)"));
        exitBtn->setCursor(Qt::PointingHandCursor);
        exitBtn->setStyleSheet(
                "QPushButton {"
                "  background-color: rgba(60, 60, 60, 220);"  // 更高的不透明度
                "  border: 2px solid rgba(255, 255, 255, 100);"  // 添加边框增加可见性
                "  border-radius: 20px;"
                "}"
                "QPushButton:hover {"
                "  background-color: rgba(100, 100, 100, 240);"
                "}");

        // 使用屏幕尺寸计算位置
        QScreen *screen = terminal->screen();
        if (screen) {
            QRect screenGeometry = screen->geometry();
            exitBtn->move(screenGeometry.width() - exitBtn->width() - 20, 20);
        }

        exitBtn->show();
        exitBtn->raise();
        connect(exitBtn, &QPushButton::clicked, this, &MainWindow::exitFullscreen);

        escShortcut_ = new QShortcut(QKeySequence(Qt::Key_Escape), terminal);
        escShortcut_->setContext(Qt::WindowShortcut);
        connect(escShortcut_, &QShortcut::activated, this, &MainWindow::exitFullscreen);

        terminal->setFocus();
        currentTab_ = nullptr;
    } else {
        exitFullscreen();
    }
}


void MainWindow::exitFullscreen() {
    if (!isFullscreen_ || fullscreenWidget_ == nullptr) {
        return;
    }

    auto *terminal = dynamic_cast<BaseTerminal *>(fullscreenWidget_);
    if (terminal == nullptr) {
        return;
    }

    int index = terminal->property("tabIndex").toInt();
    QString tabText = terminal->property("tabText").toString();
    auto tabIcon = terminal->property("tabIcon").value<QIcon>();

    // 删除退出按钮
    auto *exitBtn = terminal->findChild<QPushButton *>("exitFullscreenBtn");
    delete exitBtn;

    // 删除快捷键
    if (escShortcut_) {
        delete escShortcut_;
        escShortcut_ = nullptr;
    }

    terminal->setWindowFlags(Qt::Widget);
    terminal->showNormal();

    disconnect(tabWidget_, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    if (index > tabWidget_->count()) {
        index = tabWidget_->count();
    }

    tabWidget_->insertTab(index, terminal, tabIcon, tabText);
    tabWidget_->setCurrentIndex(index);

    connect(tabWidget_, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    currentTab_ = terminal;
    currentTab_->setFocus();

    isFullscreen_ = false;
    fullscreenWidget_ = nullptr;
}

void MainWindow::onRunLuaScriptAction() {
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Select Lua Script"),
        QString(),
        tr("Lua Scripts (*.lua);;All Files (*)")
    );

    if (!filePath.isEmpty()) {
        runScript(filePath);
    }
}

void MainWindow::onStopScriptAction() {
    LuaScriptEngine::stopScript();
}

void MainWindow::onRecentScriptTriggered() {
    auto *action = qobject_cast<QAction *>(sender());
    if (action) {
        QString scriptPath = action->data().toString();
        if (QFile::exists(scriptPath)) {
            runScript(scriptPath);
        } else {
            // 文件不存在，弹窗询问用户是否从历史列表中移除
            auto result = QMessageBox::question(
                this,
                tr("Script Not Found"),
                tr("The script file does not exist:\n%1\n\nDo you want to remove it from the recent list?")
                    .arg(scriptPath),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::Yes
            );

            if (result == QMessageBox::Yes) {
                recentScripts_.removeAll(scriptPath);
                saveRecentScripts();
                updateRecentScriptsMenu();
            }
        }
    }
}


void MainWindow::runScript(const QString &scriptPath, const QStringList &scriptArgs) {
    qDebug() << "Running script:" << scriptPath;
    auto runner = new ScriptRunner(luaEngine_, scriptPath, scriptArgs);
    connect(runner, &QThread::finished, runner, &QObject::deleteLater);
    runner->start();
    stopScriptAction_->setEnabled(true);
    runLuaScriptAction_->setEnabled(false);
    addRecentScript(scriptPath);
}

void MainWindow::addRecentScript(const QString &scriptPath) {
    // 移除已存在的相同路径
    recentScripts_.removeAll(scriptPath);

    // 添加到列表开头
    recentScripts_.prepend(scriptPath);

    // 限制最大数量
    while (recentScripts_.size() > MaxRecentScripts) {
        recentScripts_.removeLast();
    }

    saveRecentScripts();
    updateRecentScriptsMenu();
}

void MainWindow::loadRecentScripts() {
    QSettings settings;
    recentScripts_ = settings.value("recentScripts").toStringList();

    // 限制最大数量
    while (recentScripts_.size() > MaxRecentScripts) {
        recentScripts_.removeLast();
    }
}

void MainWindow::saveRecentScripts() {
    QSettings settings;
    settings.setValue("recentScripts", recentScripts_);
}

void MainWindow::updateRecentScriptsMenu() {
    recentScriptMenu_->clear();

    if (recentScripts_.isEmpty()) {
        QAction *emptyAction = recentScriptMenu_->addAction(tr("No Recent Scripts"));
        emptyAction->setEnabled(false);
        return;
    }

    for (int i = 0; i < recentScripts_.size(); ++i) {
        const QString &scriptPath = recentScripts_.at(i);
        QString displayName = QString("%1. %2").arg(i + 1).arg(scriptPath);

        QAction *action = recentScriptMenu_->addAction(displayName);
        action->setData(scriptPath);
        action->setToolTip(scriptPath);
        connect(action, &QAction::triggered, this, &MainWindow::onRecentScriptTriggered);
    }

    recentScriptMenu_->addSeparator();
    QAction *clearAction = recentScriptMenu_->addAction(tr("Clear Recent Scripts"));
    connect(clearAction, &QAction::triggered, this, [this]() {
        recentScripts_.clear();
        saveRecentScripts();
        updateRecentScriptsMenu();
    });
}


// 在文件末尾添加槽函数实现
void MainWindow::onDocAction() {
    QDesktopServices::openUrl(QUrl("https://github.com/qiushao/qshell"));  // 替换为实际文档地址
}

void MainWindow::onAboutAction() {
    QMessageBox::about(this, tr("关于"),
        tr("<h3>%1</h3>"
           "<p>版本: %2</p>"
           "<p>%3</p>")
        .arg(QCoreApplication::applicationName())
        .arg(QCoreApplication::applicationVersion())
        .arg("Copyright © 2026 qiushao"));
}

void MainWindow::onSendKey(const QString& keyName)
{
    sendKeyToCurrent(keyName);
}

bool MainWindow::sendKeyToCurrent(const QString& keyName) {
    // 按键名称到按键码的映射
    static const QMap<QString, int> keyMap = {
        {"Enter",     Qt::Key_Return},
        {"Return",    Qt::Key_Return},
        {"Tab",       Qt::Key_Tab},
        {"Escape",    Qt::Key_Escape},
        {"Esc",       Qt::Key_Escape},
        {"Backspace", Qt::Key_Backspace},
        {"Delete",    Qt::Key_Delete},
        {"Del",       Qt::Key_Delete},
        {"Up",        Qt::Key_Up},
        {"Down",      Qt::Key_Down},
        {"Left",      Qt::Key_Left},
        {"Right",     Qt::Key_Right},
        {"Home",      Qt::Key_Home},
        {"End",       Qt::Key_End},
        {"PageUp",    Qt::Key_PageUp},
        {"PageDown",  Qt::Key_PageDown},
        {"Insert",    Qt::Key_Insert},
        {"F1",        Qt::Key_F1},
        {"F2",        Qt::Key_F2},
        {"F3",        Qt::Key_F3},
        {"F4",        Qt::Key_F4},
        {"F5",        Qt::Key_F5},
        {"F6",        Qt::Key_F6},
        {"F7",        Qt::Key_F7},
        {"F8",        Qt::Key_F8},
        {"F9",        Qt::Key_F9},
        {"F10",       Qt::Key_F10},
        {"F11",       Qt::Key_F11},
        {"F12",       Qt::Key_F12},
    };

    // 处理组合键 (如 "Ctrl+C")
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    QString keyPart = keyName;

    if (keyName.contains("+")) {
        QStringList parts = keyName.split("+");
        keyPart = parts.last().trimmed();

        for (int i = 0; i < parts.size() - 1; ++i) {
            QString mod = parts[i].trimmed().toLower();
            if (mod == "ctrl")  modifiers |= Qt::ControlModifier;
            if (mod == "alt")   modifiers |= Qt::AltModifier;
            if (mod == "shift") modifiers |= Qt::ShiftModifier;
            if (mod == "meta")  modifiers |= Qt::MetaModifier;
        }
    }

    int key = keyMap.value(keyPart, 0);

    // 如果不在映射表中，尝试作为单个字符处理
    if (key == 0 && keyPart.length() == 1) {
        key = keyPart.toUpper().at(0).unicode();
    }

    if (key != 0) {
        // 发送按键事件到终端控件
        if (currentTab_) {
            QKeyEvent pressEvent(QEvent::KeyPress, key, modifiers);
            currentTab_->sendKeyEvent(&pressEvent);
            return true;
        }
    }
    return false;
}

QString MainWindow::getScreenText() const {
    if (currentTab_) {
        return currentTab_->getScreenText();
    }
    return "";
}

QString MainWindow::getLastLine() const {
    if (currentTab_) {
        return currentTab_->getLastLine();
    }
    return "";
}

bool MainWindow::openSessionById(const QString &sessionId) {
    auto session = ConfigManager::instance()->sessionById(sessionId);
    BaseTerminal *terminal = nullptr;

    if (session.protocolType == ProtocolType::Serial) {
        terminal = new SerialTerminal(session, this);
    } else if (session.protocolType == ProtocolType::LocalShell) {
        terminal = new LocalTerminal(this);
    } else if (session.protocolType == ProtocolType::SSH) {
        terminal = new SSHTerminal(session, this);
    } else {
        qDebug() << "unknown session type!!";
        return false;
    }

    QObject::connect(terminal, &BaseTerminal::onSessionError, this, &MainWindow::onSessionError);
    tabWidget_->addTab(terminal, *connectStateIcon_, session.name);
    tabWidget_->setCurrentWidget(terminal);
    terminal->setFocus();
    terminal->connect();
    qDebug() << "onOpenSession" << session.name;
    return true;
}

bool MainWindow::openSessionByName(const QString &sessionName) {
    auto session = ConfigManager::instance()->sessionByName(sessionName);
    if (session.id.isEmpty() || session.protocolType == ProtocolType::UNKNOWN) {
        return false;
    }
    return openSessionById(session.id);
}

int MainWindow::tabCount() const {
    return tabWidget_->count();
}

void MainWindow::nextTab() const {
    if (tabWidget_->count() < 2) {
        return;
    }

    const int currentIndex = tabWidget_->currentIndex();
    const int nextIndex = (currentIndex + 1) % tabWidget_->count();
    tabWidget_->setCurrentIndex(nextIndex);
}

bool MainWindow::switchToTab(const QString &tabName) const {
    auto tabCount = tabWidget_->count();
    if (tabCount == 0) {
        return false;
    }
    for (int i = 0; i < tabCount; i++) {
        auto title = tabWidget_->tabText(i);
        if (title == tabName) {
            tabWidget_->setCurrentIndex(i);
            return true;
        }
    }
    return false;
}

bool MainWindow::switchToTabIndex(int index) const {
    if (index < 0 || index >= tabWidget_->count()) {
        return false;
    }
    tabWidget_->setCurrentIndex(index);
    return true;
}

QString MainWindow::currentTabName() const {
    if (currentTab_ == nullptr) {
        return "";
    }

    const int index = tabWidget_->indexOf(currentTab_);
    if (index >= 0) {
        return tabWidget_->tabText(index);
    }
    return currentTab_->getSessionName();
}

bool MainWindow::connectCurrentSession() {
    if (currentTab_ == nullptr) {
        return false;
    }
    if (!currentTab_->isConnect()) {
        onConnectAction();
    }
    return currentTab_ != nullptr && currentTab_->isConnect();
}

bool MainWindow::disconnectCurrentSession() const {
    if (currentTab_ == nullptr) {
        return false;
    }
    if (currentTab_->isConnect()) {
        onDisconnectAction();
    }
    return currentTab_ != nullptr && !currentTab_->isConnect();
}

bool MainWindow::sendTextToCurrent(QString text, bool interpretEscapes) {
    if (currentTab_ == nullptr) {
        return false;
    }
    if (interpretEscapes) {
        text.replace(QString("\\r"), QString("\r"));
        text.replace(QString("\\n"), QString("\n"));
        text.replace(QString("\\t"), QString("\t"));
    }
    currentTab_->sendText(text);
    return true;
}

bool MainWindow::clearCurrentScreen() {
    if (currentTab_ == nullptr) {
        return false;
    }
    currentTab_->clear();
    return true;
}

BaseTerminal * MainWindow::getCurrentSession() const {
    return currentTab_;
}
