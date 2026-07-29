#ifndef QSHELL_MAINWINDOW_H
#define QSHELL_MAINWINDOW_H

#include <QMainWindow>
#include <QShortcut>
#include <QStringList>
#if defined(Q_OS_WIN)
#include <QAbstractNativeEventFilter>
#endif

class SessionTabWidget;
class SessionTreeWidget;
class CommandButtonBar;
class CommandWindow;
class BaseTerminal;
class CollapsibleDockWidget;
class LuaScriptEngine;
class McpHttpServer;

class MainWindow : public QMainWindow
#if defined(Q_OS_WIN)
    , public QAbstractNativeEventFilter
#endif
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void showEvent(QShowEvent *event) override;
    bool runScriptAtStartup(const QString &scriptPath, const QStringList &scriptArgs = {});
    Q_INVOKABLE QString getScreenText() const;
    Q_INVOKABLE QString getLastLine() const;
    Q_INVOKABLE bool openSessionById(const QString& sessionId);
    Q_INVOKABLE bool openSessionByName(const QString& sessionName);
    Q_INVOKABLE int tabCount() const;
    Q_INVOKABLE void nextTab() const;
    Q_INVOKABLE bool switchToTab(const QString& tabName) const;
    Q_INVOKABLE bool switchToTabIndex(int index) const;
    Q_INVOKABLE QString currentTabName() const;
    Q_INVOKABLE bool connectCurrentSession();
    Q_INVOKABLE bool disconnectCurrentSession() const;
    Q_INVOKABLE bool sendTextToCurrent(QString text, bool interpretEscapes = true);
    Q_INVOKABLE bool sendKeyToCurrent(const QString& keyName);
    Q_INVOKABLE bool clearCurrentScreen();
    BaseTerminal* getCurrentSession() const;

private slots:
    void onOpenSession(const QString& sessionId);
    void onSessionError(BaseTerminal *terminal) const;
    void onDisconnectAction() const;
    void onTabChanged(int index);
    void onTabCloseRequested(int index) const;
    void onCommandSend(const QString &command);
    void onSendKey(const QString& keyName);

    void onSettingsAction();
    void onImportConfigAction();
    void onExportConfigAction();
    void onConnectAction();
    void onExitAction();

    void onCopySelectedAction();
    void onCopyAllAction();
    void onPasteAction();
    void onFindAction();
    void onClearScreenAction();

    void onToggleToolbarAction();
    void onToggleSessionManagerAction();
    void onToggleCommandWindowAction();
    void onToggleCommandButtonAction();

    void onFullscreenAction();

    // Script menu slots
    void onRunLuaScriptAction();
    static void onStopScriptAction();
    void onRecentScriptTriggered();

    // Help menu slots
    static void onDocAction();
    void onAboutAction();
    void syncMcpServer();

private:
    void initLuaEngine();
    void initIcons();
    void initActions();
    void initMenu();
    void initToolbar();
    void initSessionTree();
    void initTableWidget();
    void initCommandWindow();
    void initButtonBar();
    void initMcpServer();
    void restoreLayoutState();
    void exitFullscreen();
    void updateFileTransferMenu();
    static BaseTerminal *terminalForWidget(QWidget *widget);

#if defined(Q_OS_WIN)
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
    bool copyShortcutKeyDownSeen_ = false;
#endif

    void runScript(const QString &scriptPath, const QStringList &scriptArgs = {});
    void addRecentScript(const QString &scriptPath);
    void loadRecentScripts();
    void saveRecentScripts();
    void updateRecentScriptsMenu();

    bool eventFilter(QObject *watched, QEvent *event) override;

    QIcon *connectIcon_ = nullptr;
    QIcon *disconnectIcon_ = nullptr;
    QIcon *connectStateIcon_ = nullptr;
    QIcon *disconnectStateIcon_ = nullptr;

    QMenuBar *mainMenuBar_ = nullptr;
    QMenu *fileMenu_ = nullptr;
    QMenu *fileTransferMenu_ = nullptr;
    QMenu *editMenu_ = nullptr;
    QMenu *viewMenu_ = nullptr;
    QMenu *scriptMenu_ = nullptr;
    QMenu *helpMenu_ = nullptr;

    QToolBar *toolBar_ = nullptr;
    QIcon *settingsIcon_ = nullptr;
    QIcon *exitIcon_ = nullptr;

    QIcon *copySelectedIcon_ = nullptr;
    QIcon *copyAllIcon_ = nullptr;
    QIcon *pasteIcon_ = nullptr;
    QIcon *findIcon_ = nullptr;
    QIcon *clearScreenIcon_ = nullptr;

    QIcon *toggleOnIcon_ = nullptr;
    QIcon *toggleOffIcon_ = nullptr;

    QIcon *fullscreenIcon_ = nullptr;

    QAction *settingsAction_ = nullptr;
    QAction *importConfigAction_ = nullptr;
    QAction *exportConfigAction_ = nullptr;
    QAction *connectAction_ = nullptr;
    QAction *disConnectAction_ = nullptr;
    QAction *exitAction_ = nullptr;

    QAction *copySelectedAction_ = nullptr;
    QAction *copyAllAction_ = nullptr;
    QAction *pasteAction_ = nullptr;
    QAction *findAction_ = nullptr;
    QAction *clearScreenAction_ = nullptr;

    QAction *toggleToolbarAction_ = nullptr;
    QAction *toggleSessionManagerAction_ = nullptr;
    QAction *toggleCommandWindowAction_ = nullptr;
    QAction *toggleCommandButtonAction_ = nullptr;

    QAction *fullscreenAction_ = nullptr;

    // Script actions
    QAction *runLuaScriptAction_ = nullptr;
    QAction *stopScriptAction_ = nullptr;
    QMenu *recentScriptMenu_ = nullptr;
    QStringList recentScripts_;
    static constexpr int MaxRecentScripts = 10;

    // Help actions
    QAction *docAction_ = nullptr;
    QAction *aboutAction_ = nullptr;

    // session table
    BaseTerminal *currentTab_ = nullptr;
    SessionTabWidget *tabWidget_ = nullptr;
    SessionTreeWidget *treeWidget_ = nullptr;

    // session manager
    CollapsibleDockWidget *sessionDock_ = nullptr;
    SessionTreeWidget *sessionTree_ = nullptr;

    // command window
    QDockWidget *commandWindowDock_ = nullptr;
    CommandWindow *commandWindow_ = nullptr;

    // command button bar
    CommandButtonBar *commandButtonBar_ = nullptr;

    // full screen
    bool isFullscreen_ = false;
    QWidget *fullscreenWidget_ = nullptr;
    QShortcut *escShortcut_ = nullptr;

    LuaScriptEngine *luaEngine_ = nullptr;
    McpHttpServer *mcpServer_ = nullptr;
};

#endif // QSHELL_MAINWINDOW_H
