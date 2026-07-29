#include "McpToolRegistry.h"

#include "core/ConfigManager.h"
#include "ui/MainWindow.h"
#include "ui/terminal/BaseTerminal.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <memory>

namespace {
constexpr int defaultWaitTimeoutMs = 30000;
constexpr int maxWaitTimeoutMs = 300000;
}// namespace

McpToolRegistry::McpToolRegistry(MainWindow *mainWindow, QObject *parent)
    : QObject(parent),
      mainWindow_(mainWindow) {}

void McpToolRegistry::setListenState(bool listening, int port) {
    listening_ = listening;
    port_ = port;
}

QJsonObject McpToolRegistry::makeInputSchema(const QJsonObject &properties, const QStringList &required) {
    QJsonObject schema;
    schema["type"] = "object";
    schema["properties"] = properties;
    if (!required.isEmpty()) {
        schema["required"] = QJsonArray::fromStringList(required);
    }
    return schema;
}

QJsonObject McpToolRegistry::makeStringProperty(const QString &description) {
    QJsonObject property;
    property["type"] = "string";
    property["description"] = description;
    return property;
}

QJsonObject McpToolRegistry::makeIntegerProperty(const QString &description, int minimum) {
    QJsonObject property;
    property["type"] = "integer";
    property["description"] = description;
    property["minimum"] = minimum;
    return property;
}

QJsonObject McpToolRegistry::makeBooleanProperty(const QString &description) {
    QJsonObject property;
    property["type"] = "boolean";
    property["description"] = description;
    return property;
}

QJsonObject McpToolRegistry::makeToolDefinition(const QString &name,
                                                const QString &title,
                                                const QString &description,
                                                const QJsonObject &inputSchema,
                                                bool readOnly,
                                                bool destructive,
                                                bool openWorld) {
    QJsonObject annotations;
    annotations["readOnlyHint"] = readOnly;
    annotations["destructiveHint"] = destructive;
    annotations["idempotentHint"] = readOnly;
    annotations["openWorldHint"] = openWorld;

    QJsonObject tool;
    tool["name"] = name;
    tool["title"] = title;
    tool["description"] = description;
    tool["inputSchema"] = inputSchema;
    tool["annotations"] = annotations;
    return tool;
}

QJsonArray McpToolRegistry::toolDefinitions() {
    QJsonArray tools;

    tools.append(makeToolDefinition("qshell_get_status",
                                    tr("Get qshell status"),
                                    tr("Return qshell version, open tab count, current tab, and MCP listener status."),
                                    makeInputSchema({}),
                                    true));
    tools.append(makeToolDefinition("qshell_list_sessions",
                                    tr("List configured sessions"),
                                    tr("Return configured session id, name, protocol, and group id without secrets."),
                                    makeInputSchema({}),
                                    true));

    QJsonObject openByIdProperties;
    openByIdProperties["sessionId"] = makeStringProperty(tr("Configured session id to open."));
    tools.append(makeToolDefinition("qshell_open_session_by_id",
                                    tr("Open session by id"),
                                    tr("Open a configured qshell session using its id."),
                                    makeInputSchema(openByIdProperties, {"sessionId"}),
                                    false,
                                    false,
                                    true));

    QJsonObject openByNameProperties;
    openByNameProperties["sessionName"] = makeStringProperty(tr("Configured session name to open."));
    tools.append(makeToolDefinition("qshell_open_session_by_name",
                                    tr("Open session by name"),
                                    tr("Open a configured qshell session using its name."),
                                    makeInputSchema(openByNameProperties, {"sessionName"}),
                                    false,
                                    false,
                                    true));

    QJsonObject switchTabProperties;
    switchTabProperties["index"] = makeIntegerProperty(tr("Zero-based tab index to switch to."), 0);
    switchTabProperties["name"] = makeStringProperty(tr("Tab title to switch to. Used when index is omitted."));
    tools.append(makeToolDefinition("qshell_switch_tab",
                                    tr("Switch tab"),
                                    tr("Switch the active terminal tab by zero-based index or tab title."),
                                    makeInputSchema(switchTabProperties),
                                    false));

    tools.append(makeToolDefinition("qshell_next_tab",
                                    tr("Next tab"),
                                    tr("Switch to the next terminal tab."),
                                    makeInputSchema({}),
                                    false));

    tools.append(makeToolDefinition("qshell_connect_current",
                                    tr("Connect current tab"),
                                    tr("Connect the current terminal tab if it is disconnected."),
                                    makeInputSchema({}),
                                    false,
                                    false,
                                    true));

    tools.append(makeToolDefinition("qshell_disconnect_current",
                                    tr("Disconnect current tab"),
                                    tr("Disconnect the current terminal tab if it is connected."),
                                    makeInputSchema({}),
                                    false,
                                    true,
                                    true));

    QJsonObject sendTextProperties;
    sendTextProperties["text"] = makeStringProperty(tr("Text to send to the current terminal."));
    sendTextProperties["interpretEscapes"] = makeBooleanProperty(tr(R"(Interpret \r, \n, and \t escape sequences before sending. Defaults to true.)"));
    tools.append(makeToolDefinition("qshell_send_text",
                                    tr("Send text"),
                                    tr("Send text to the current terminal."),
                                    makeInputSchema(sendTextProperties, {"text"}),
                                    false,
                                    true,
                                    true));

    QJsonObject sendKeyProperties;
    sendKeyProperties["key"] = makeStringProperty(tr("Key name such as Enter, Tab, Ctrl+C, F1, Up, or Down."));
    tools.append(makeToolDefinition("qshell_send_key",
                                    tr("Send key"),
                                    tr("Send a named key press to the current terminal."),
                                    makeInputSchema(sendKeyProperties, {"key"}),
                                    false,
                                    true,
                                    true));

    tools.append(makeToolDefinition("qshell_get_screen_text",
                                    tr("Get screen text"),
                                    tr("Return visible text from the current terminal screen."),
                                    makeInputSchema({}),
                                    true));

    tools.append(makeToolDefinition("qshell_get_last_line",
                                    tr("Get last line"),
                                    tr("Return the last visible line from the current terminal screen."),
                                    makeInputSchema({}),
                                    true));

    tools.append(makeToolDefinition("qshell_clear_screen",
                                    tr("Clear screen"),
                                    tr("Clear the current terminal screen."),
                                    makeInputSchema({}),
                                    false,
                                    true));

    QJsonObject uploadPathsProperty;
    uploadPathsProperty["type"] = "array";
    uploadPathsProperty["description"] =
            tr("Local file paths to upload when the remote side starts rz.");
    uploadPathsProperty["minItems"] = 1;
    uploadPathsProperty["items"] =
            QJsonObject{{"type", "string"}};
    QJsonObject zmodemUploadProperties;
    zmodemUploadProperties["filePaths"] =
            uploadPathsProperty;
    tools.append(makeToolDefinition(
            "qshell_zmodem_upload",
            tr("Prepare ZMODEM upload"),
            tr("Use local files for the next ZMODEM upload without opening a file chooser. Call this before sending rz to the remote terminal."),
            makeInputSchema(zmodemUploadProperties,
                            {"filePaths"}),
            false,
            false,
            true));

    QJsonObject zmodemDownloadProperties;
    zmodemDownloadProperties["directoryPath"] =
            makeStringProperty(
                    tr("Writable local directory for files received when the remote side starts sz."));
    tools.append(makeToolDefinition(
            "qshell_zmodem_download",
            tr("Prepare ZMODEM download"),
            tr("Use a local directory for the next ZMODEM download without opening a directory chooser. Call this before sending sz to the remote terminal."),
            makeInputSchema(zmodemDownloadProperties,
                            {"directoryPath"}),
            false,
            false,
            true));

    QJsonObject waitStringProperties;
    waitStringProperties["text"] = makeStringProperty(tr("Text to wait for in terminal output."));
    waitStringProperties["timeoutMs"] = makeIntegerProperty(tr("Timeout in milliseconds. Defaults to 30000."), 1);
    waitStringProperties["timeoutSeconds"] = makeIntegerProperty(tr("Timeout in seconds, used when timeoutMs is omitted."), 1);
    tools.append(makeToolDefinition("qshell_wait_for_string",
                                    tr("Wait for string"),
                                    tr("Wait until terminal output contains a string or the timeout expires."),
                                    makeInputSchema(waitStringProperties, {"text"}),
                                    true));

    QJsonObject waitRegexProperties;
    waitRegexProperties["pattern"] = makeStringProperty(tr("Regular expression to wait for in terminal output."));
    waitRegexProperties["timeoutMs"] = makeIntegerProperty(tr("Timeout in milliseconds. Defaults to 30000."), 1);
    waitRegexProperties["timeoutSeconds"] = makeIntegerProperty(tr("Timeout in seconds, used when timeoutMs is omitted."), 1);
    tools.append(makeToolDefinition("qshell_wait_for_regex",
                                    tr("Wait for regex"),
                                    tr("Wait until terminal output matches a regular expression or the timeout expires."),
                                    makeInputSchema(waitRegexProperties, {"pattern"}),
                                    true));

    return tools;
}

bool McpToolRegistry::hasTool(const QString &name) {
    return name == "qshell_get_status" || name == "qshell_list_sessions" || name == "qshell_open_session_by_id" || name == "qshell_open_session_by_name" || name == "qshell_switch_tab" || name == "qshell_next_tab" || name == "qshell_connect_current" || name == "qshell_disconnect_current" || name == "qshell_send_text" || name == "qshell_send_key" || name == "qshell_get_screen_text" || name == "qshell_get_last_line" || name == "qshell_clear_screen" || name == "qshell_zmodem_upload" || name == "qshell_zmodem_download" || name == "qshell_wait_for_string" || name == "qshell_wait_for_regex";
}

void McpToolRegistry::callTool(const QString &name, const QJsonObject &arguments, const ToolCallback &callback) {
    if (name == "qshell_get_status") {
        runUiTool([this]() { return getStatus(); }, callback);
    } else if (name == "qshell_list_sessions") {
        callback(listSessions());
    } else if (name == "qshell_open_session_by_id") {
        runUiTool([this, arguments]() { return openSessionById(arguments); }, callback);
    } else if (name == "qshell_open_session_by_name") {
        runUiTool([this, arguments]() { return openSessionByName(arguments); }, callback);
    } else if (name == "qshell_switch_tab") {
        runUiTool([this, arguments]() { return switchTab(arguments); }, callback);
    } else if (name == "qshell_next_tab") {
        runUiTool([this]() { return nextTab(); }, callback);
    } else if (name == "qshell_connect_current") {
        runUiTool([this]() { return connectCurrent(); }, callback);
    } else if (name == "qshell_disconnect_current") {
        runUiTool([this]() { return disconnectCurrent(); }, callback);
    } else if (name == "qshell_send_text") {
        runUiTool([this, arguments]() { return sendText(arguments); }, callback);
    } else if (name == "qshell_send_key") {
        runUiTool([this, arguments]() { return sendKey(arguments); }, callback);
    } else if (name == "qshell_get_screen_text") {
        runUiTool([this]() { return getScreenText(); }, callback);
    } else if (name == "qshell_get_last_line") {
        runUiTool([this]() { return getLastLine(); }, callback);
    } else if (name == "qshell_clear_screen") {
        runUiTool([this]() { return clearScreen(); }, callback);
    } else if (name == "qshell_zmodem_upload") {
        runUiTool(
                [this, arguments]() {
                    return prepareZmodemUpload(arguments);
                },
                callback);
    } else if (name == "qshell_zmodem_download") {
        runUiTool(
                [this, arguments]() {
                    return prepareZmodemDownload(arguments);
                },
                callback);
    } else if (name == "qshell_wait_for_string") {
        waitForString(arguments, callback);
    } else if (name == "qshell_wait_for_regex") {
        waitForRegex(arguments, callback);
    } else {
        callback(makeErrorResponse(tr("Unknown tool: %1").arg(name)));
    }
}

QString McpToolRegistry::protocolToString(ProtocolType protocolType) {
    switch (protocolType) {
        case ProtocolType::LocalShell:
            return "local";
        case ProtocolType::SSH:
            return "ssh";
        case ProtocolType::Serial:
            return "serial";
        case ProtocolType::UNKNOWN:
            return "unknown";
    }
    return "unknown";
}

int McpToolRegistry::timeoutFromArguments(const QJsonObject &arguments) {
    int timeoutMs = defaultWaitTimeoutMs;
    if (arguments.contains("timeoutMs")) {
        timeoutMs = arguments["timeoutMs"].toInt(defaultWaitTimeoutMs);
    } else if (arguments.contains("timeoutSeconds")) {
        timeoutMs = arguments["timeoutSeconds"].toInt(defaultWaitTimeoutMs / 1000) * 1000;
    }
    return qBound(1, timeoutMs, maxWaitTimeoutMs);
}

Qt::ConnectionType McpToolRegistry::mainConnectionType() const {
    if (mainWindow_ != nullptr && QThread::currentThread() != mainWindow_->thread()) {
        return Qt::BlockingQueuedConnection;
    }
    return Qt::DirectConnection;
}

void McpToolRegistry::runUiTool(const ToolFunction &function, const ToolCallback &callback) const {
    if (mainWindow_ == nullptr) {
        callback(makeErrorResponse(tr("Main window is not available.")));
        return;
    }

    ToolResponse response;
    QMetaObject::invokeMethod(mainWindow_, [&response, &function]() { response = function(); }, mainConnectionType());
    callback(response);
}

McpToolRegistry::ToolResponse McpToolRegistry::makeResponse(const QJsonObject &structuredContent,
                                                            bool isError,
                                                            const QString &text) {
    ToolResponse response;
    response.isError = isError;
    response.structuredContent = structuredContent;
    response.text = text.isNull() ? QString::fromUtf8(QJsonDocument(structuredContent).toJson(QJsonDocument::Compact)) : text;
    return response;
}

McpToolRegistry::ToolResponse McpToolRegistry::makeErrorResponse(const QString &message) {
    QJsonObject structuredContent;
    structuredContent["ok"] = false;
    structuredContent["error"] = message;
    return makeResponse(structuredContent, true, message);
}

McpToolRegistry::ToolResponse McpToolRegistry::getStatus() const {
    const GlobalSettings settings = ConfigManager::instance()->globalSettings();
    QJsonObject mcp;
    mcp["enabled"] = settings.mcpEnabled;
    mcp["listening"] = listening_;
    mcp["host"] = "127.0.0.1";
    mcp["port"] = port_;
    mcp["path"] = "/mcp";

    QJsonObject structuredContent;
    structuredContent["version"] = QCoreApplication::applicationVersion();
    structuredContent["tabCount"] = mainWindow_->tabCount();
    structuredContent["currentSessionName"] = mainWindow_->currentTabName();
    structuredContent["mcp"] = mcp;
    return makeResponse(structuredContent);
}

McpToolRegistry::ToolResponse McpToolRegistry::listSessions() {
    QJsonArray sessions;
    const QList<SessionData> configuredSessions = ConfigManager::instance()->sessions();
    for (const SessionData &session: configuredSessions) {
        QJsonObject sessionObject;
        sessionObject["id"] = session.id;
        sessionObject["name"] = session.name;
        sessionObject["protocol"] = protocolToString(session.protocolType);
        sessionObject["groupId"] = session.groupId;
        sessions.append(sessionObject);
    }

    QJsonObject structuredContent;
    structuredContent["sessions"] = sessions;
    structuredContent["count"] = sessions.size();
    return makeResponse(structuredContent);
}

McpToolRegistry::ToolResponse McpToolRegistry::openSessionById(const QJsonObject &arguments) const {
    const QString sessionId = arguments["sessionId"].toString().trimmed();
    if (sessionId.isEmpty()) {
        return makeErrorResponse(tr("sessionId is required."));
    }

    const bool opened = mainWindow_->openSessionById(sessionId);
    QJsonObject structuredContent;
    structuredContent["opened"] = opened;
    structuredContent["sessionId"] = sessionId;
    structuredContent["tabCount"] = mainWindow_->tabCount();
    structuredContent["currentSessionName"] = mainWindow_->currentTabName();
    return makeResponse(structuredContent, !opened, opened ? QString() : tr("Failed to open session id: %1").arg(sessionId));
}

McpToolRegistry::ToolResponse McpToolRegistry::openSessionByName(const QJsonObject &arguments) const {
    const QString sessionName = arguments["sessionName"].toString().trimmed();
    if (sessionName.isEmpty()) {
        return makeErrorResponse(tr("sessionName is required."));
    }

    const bool opened = mainWindow_->openSessionByName(sessionName);
    QJsonObject structuredContent;
    structuredContent["opened"] = opened;
    structuredContent["sessionName"] = sessionName;
    structuredContent["tabCount"] = mainWindow_->tabCount();
    structuredContent["currentSessionName"] = mainWindow_->currentTabName();
    return makeResponse(structuredContent, !opened, opened ? QString() : tr("Failed to open session name: %1").arg(sessionName));
}

McpToolRegistry::ToolResponse McpToolRegistry::switchTab(const QJsonObject &arguments) const {
    bool switched = false;
    QJsonObject structuredContent;
    if (arguments.contains("index")) {
        const int index = arguments["index"].toInt(-1);
        switched = mainWindow_->switchToTabIndex(index);
        structuredContent["index"] = index;
    } else {
        const QString name = arguments["name"].toString().trimmed();
        if (name.isEmpty()) {
            return makeErrorResponse(tr("Either index or name is required."));
        }
        switched = mainWindow_->switchToTab(name);
        structuredContent["name"] = name;
    }

    structuredContent["switched"] = switched;
    structuredContent["currentSessionName"] = mainWindow_->currentTabName();
    return makeResponse(structuredContent, !switched, switched ? QString() : tr("Failed to switch tab."));
}

McpToolRegistry::ToolResponse McpToolRegistry::nextTab() const {
    mainWindow_->nextTab();
    QJsonObject structuredContent;
    structuredContent["tabCount"] = mainWindow_->tabCount();
    structuredContent["currentSessionName"] = mainWindow_->currentTabName();
    return makeResponse(structuredContent);
}

McpToolRegistry::ToolResponse McpToolRegistry::connectCurrent() const {
    const bool connected = mainWindow_->connectCurrentSession();
    QJsonObject structuredContent;
    structuredContent["connected"] = connected;
    structuredContent["currentSessionName"] = mainWindow_->currentTabName();
    return makeResponse(structuredContent, !connected, connected ? QString() : tr("No current terminal could be connected."));
}

McpToolRegistry::ToolResponse McpToolRegistry::disconnectCurrent() const {
    const bool disconnected = mainWindow_->disconnectCurrentSession();
    QJsonObject structuredContent;
    structuredContent["disconnected"] = disconnected;
    structuredContent["currentSessionName"] = mainWindow_->currentTabName();
    return makeResponse(structuredContent, !disconnected, disconnected ? QString() : tr("No connected current terminal could be disconnected."));
}

McpToolRegistry::ToolResponse McpToolRegistry::sendText(const QJsonObject &arguments) const {
    QString text = arguments["text"].toString();
    if (!arguments.contains("text")) {
        return makeErrorResponse(tr("text is required."));
    }

    const bool interpretEscapes = arguments["interpretEscapes"].toBool(true);
    const bool sent = mainWindow_->sendTextToCurrent(text, interpretEscapes);
    QJsonObject structuredContent;
    structuredContent["sent"] = sent;
    structuredContent["length"] = text.size();
    structuredContent["currentSessionName"] = mainWindow_->currentTabName();
    return makeResponse(structuredContent, !sent, sent ? QString() : tr("No current terminal is available."));
}

McpToolRegistry::ToolResponse McpToolRegistry::sendKey(const QJsonObject &arguments) const {
    const QString key = arguments["key"].toString().trimmed();
    if (key.isEmpty()) {
        return makeErrorResponse(tr("key is required."));
    }

    const bool sent = mainWindow_->sendKeyToCurrent(key);
    QJsonObject structuredContent;
    structuredContent["sent"] = sent;
    structuredContent["key"] = key;
    structuredContent["currentSessionName"] = mainWindow_->currentTabName();
    return makeResponse(structuredContent, !sent, sent ? QString() : tr("No current terminal is available or key is unsupported."));
}

McpToolRegistry::ToolResponse McpToolRegistry::getScreenText() const {
    if (mainWindow_->getCurrentSession() == nullptr) {
        return makeErrorResponse(tr("No current terminal is available."));
    }

    const QString text = mainWindow_->getScreenText();
    QJsonObject structuredContent;
    structuredContent["text"] = text;
    structuredContent["length"] = text.size();
    structuredContent["currentSessionName"] = mainWindow_->currentTabName();
    return makeResponse(structuredContent, false, text);
}

McpToolRegistry::ToolResponse McpToolRegistry::getLastLine() const {
    if (mainWindow_->getCurrentSession() == nullptr) {
        return makeErrorResponse(tr("No current terminal is available."));
    }

    const QString text = mainWindow_->getLastLine();
    QJsonObject structuredContent;
    structuredContent["text"] = text;
    structuredContent["length"] = text.size();
    structuredContent["currentSessionName"] = mainWindow_->currentTabName();
    return makeResponse(structuredContent, false, text);
}

McpToolRegistry::ToolResponse McpToolRegistry::clearScreen() const {
    const bool cleared = mainWindow_->clearCurrentScreen();
    QJsonObject structuredContent;
    structuredContent["cleared"] = cleared;
    structuredContent["currentSessionName"] = mainWindow_->currentTabName();
    return makeResponse(structuredContent, !cleared, cleared ? QString() : tr("No current terminal is available."));
}

McpToolRegistry::ToolResponse
McpToolRegistry::prepareZmodemUpload(
        const QJsonObject &arguments) const {
    const QJsonValue filePathsValue =
            arguments.value("filePaths");
    if (!filePathsValue.isArray() || filePathsValue.toArray().isEmpty()) {
        return makeErrorResponse(
                tr("filePaths must be a non-empty array."));
    }

    QStringList filePaths;
    for (const QJsonValue pathValue:
         filePathsValue.toArray()) {
        if (!pathValue.isString() || pathValue.toString().isEmpty()) {
            return makeErrorResponse(
                    tr("Every file path must be a non-empty string."));
        }
        filePaths.append(pathValue.toString());
    }

    const bool prepared =
            mainWindow_->prepareZmodemUpload(filePaths);
    QJsonObject structuredContent;
    structuredContent["prepared"] = prepared;
    structuredContent["filePaths"] =
            QJsonArray::fromStringList(filePaths);
    return makeResponse(
            structuredContent,
            !prepared,
            prepared
                    ? QString()
                    : tr("Failed to prepare ZMODEM upload. Check the current connection, file paths, and transfer state."));
}

McpToolRegistry::ToolResponse
McpToolRegistry::prepareZmodemDownload(
        const QJsonObject &arguments) const {
    const QString directoryPath =
            arguments.value("directoryPath").toString();
    if (directoryPath.isEmpty()) {
        return makeErrorResponse(
                tr("directoryPath is required."));
    }

    const bool prepared =
            mainWindow_->prepareZmodemDownload(
                    directoryPath);
    QJsonObject structuredContent;
    structuredContent["prepared"] = prepared;
    structuredContent["directoryPath"] = directoryPath;
    return makeResponse(
            structuredContent,
            !prepared,
            prepared
                    ? QString()
                    : tr("Failed to prepare ZMODEM download. Check the current connection, directory path, and transfer state."));
}

void McpToolRegistry::waitForString(const QJsonObject &arguments, const ToolCallback &callback) {
    const QString text = arguments["text"].toString();
    if (!arguments.contains("text") || text.isEmpty()) {
        callback(makeErrorResponse(tr("text is required.")));
        return;
    }
    const int timeoutMs = timeoutFromArguments(arguments);

    if (mainWindow_ == nullptr) {
        callback(makeErrorResponse(tr("Main window is not available.")));
        return;
    }

    if (QThread::currentThread() == mainWindow_->thread()) {
        startWaitForString(text, timeoutMs, callback);
    } else {
        QMetaObject::invokeMethod(mainWindow_, [this, text, timeoutMs, callback]() { startWaitForString(text, timeoutMs, callback); }, Qt::QueuedConnection);
    }
}

void McpToolRegistry::waitForRegex(const QJsonObject &arguments, const ToolCallback &callback) {
    const QString pattern = arguments["pattern"].toString();
    if (!arguments.contains("pattern") || pattern.isEmpty()) {
        callback(makeErrorResponse(tr("pattern is required.")));
        return;
    }
    const int timeoutMs = timeoutFromArguments(arguments);

    if (mainWindow_ == nullptr) {
        callback(makeErrorResponse(tr("Main window is not available.")));
        return;
    }

    if (QThread::currentThread() == mainWindow_->thread()) {
        startWaitForRegex(pattern, timeoutMs, callback);
    } else {
        QMetaObject::invokeMethod(mainWindow_, [this, pattern, timeoutMs, callback]() { startWaitForRegex(pattern, timeoutMs, callback); }, Qt::QueuedConnection);
    }
}

void McpToolRegistry::startWaitForString(const QString &text, int timeoutMs, const ToolCallback &callback) {
    BaseTerminal *terminal = mainWindow_->getCurrentSession();
    if (terminal == nullptr) {
        callback(makeErrorResponse(tr("No current terminal is available.")));
        return;
    }

    QElapsedTimer elapsedTimer;
    elapsedTimer.start();
    const QString screenText = mainWindow_->getScreenText();
    if (screenText.contains(text)) {
        QJsonObject structuredContent;
        structuredContent["matched"] = true;
        structuredContent["timeout"] = false;
        structuredContent["elapsedMs"] = static_cast<int>(elapsedTimer.elapsed());
        structuredContent["text"] = text;
        structuredContent["source"] = "screen";
        callback(makeResponse(structuredContent));
        return;
    }

    auto context = new QObject(this);
    auto timer = new QTimer(context);
    timer->setSingleShot(true);
    auto connection = std::make_shared<QMetaObject::Connection>();
    auto finished = std::make_shared<bool>(false);
    const qint64 startTimeMs = QDateTime::currentMSecsSinceEpoch();

    auto complete = [this, callback, context, timer, connection, finished, startTimeMs, text](bool matched, const QString &line) {
        if (*finished) {
            return;
        }
        *finished = true;
        QObject::disconnect(*connection);
        timer->stop();

        QJsonObject structuredContent;
        structuredContent["matched"] = matched;
        structuredContent["timeout"] = !matched;
        structuredContent["elapsedMs"] = static_cast<int>(QDateTime::currentMSecsSinceEpoch() - startTimeMs);
        structuredContent["text"] = text;
        structuredContent["line"] = line;
        callback(makeResponse(structuredContent));
        context->deleteLater();
    };

    *connection = QObject::connect(terminal, &QTermWidget::onNewLine, context, [complete, text](const QString &line) {
        if (line.contains(text)) {
            complete(true, line);
        }
    });

    QObject::connect(timer, &QTimer::timeout, context, [complete]() {
        complete(false, QString());
    });
    timer->start(timeoutMs);
}

void McpToolRegistry::startWaitForRegex(const QString &pattern, int timeoutMs, const ToolCallback &callback) {
    BaseTerminal *terminal = mainWindow_->getCurrentSession();
    if (terminal == nullptr) {
        callback(makeErrorResponse(tr("No current terminal is available.")));
        return;
    }

    QRegularExpression regex(pattern);
    if (!regex.isValid()) {
        callback(makeErrorResponse(tr("Invalid regular expression: %1").arg(regex.errorString())));
        return;
    }

    QElapsedTimer elapsedTimer;
    elapsedTimer.start();
    const QString screenText = mainWindow_->getScreenText();
    QRegularExpressionMatch initialMatch = regex.match(screenText);
    if (initialMatch.hasMatch()) {
        QJsonObject structuredContent;
        structuredContent["matched"] = true;
        structuredContent["timeout"] = false;
        structuredContent["elapsedMs"] = static_cast<int>(elapsedTimer.elapsed());
        structuredContent["pattern"] = pattern;
        structuredContent["match"] = initialMatch.captured(0);
        structuredContent["source"] = "screen";
        callback(makeResponse(structuredContent));
        return;
    }

    auto context = new QObject(this);
    auto timer = new QTimer(context);
    timer->setSingleShot(true);
    auto connection = std::make_shared<QMetaObject::Connection>();
    auto finished = std::make_shared<bool>(false);
    const qint64 startTimeMs = QDateTime::currentMSecsSinceEpoch();

    auto complete = [this, callback, context, timer, connection, finished, startTimeMs, pattern](bool matched, const QString &line, const QString &match) {
        if (*finished) {
            return;
        }
        *finished = true;
        QObject::disconnect(*connection);
        timer->stop();

        QJsonObject structuredContent;
        structuredContent["matched"] = matched;
        structuredContent["timeout"] = !matched;
        structuredContent["elapsedMs"] = static_cast<int>(QDateTime::currentMSecsSinceEpoch() - startTimeMs);
        structuredContent["pattern"] = pattern;
        structuredContent["line"] = line;
        structuredContent["match"] = match;
        callback(makeResponse(structuredContent));
        context->deleteLater();
    };

    *connection = QObject::connect(terminal, &QTermWidget::onNewLine, context, [complete, regex](const QString &line) {
        const QRegularExpressionMatch match = regex.match(line);
        if (match.hasMatch()) {
            complete(true, line, match.captured(0));
        }
    });

    QObject::connect(timer, &QTimer::timeout, context, [complete]() {
        complete(false, QString(), QString());
    });
    timer->start(timeoutMs);
}
