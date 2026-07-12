#ifndef QSHELL_MCPTOOLREGISTRY_H
#define QSHELL_MCPTOOLREGISTRY_H

#include "core/datatype.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

class MainWindow;

class McpToolRegistry : public QObject {
    Q_OBJECT

public:
    struct ToolResponse {
        bool isError = false;
        QString text;
        QJsonObject structuredContent;
    };

    using ToolCallback = std::function<void(const ToolResponse &)>;

    explicit McpToolRegistry(MainWindow *mainWindow, QObject *parent = nullptr);

    static QJsonArray toolDefinitions();
    static bool hasTool(const QString &name);
    void callTool(const QString &name, const QJsonObject &arguments, const ToolCallback &callback);
    void setListenState(bool listening, int port);

private:
    using ToolFunction = std::function<ToolResponse()>;

    static QJsonObject makeInputSchema(const QJsonObject &properties, const QStringList &required = {});
    static QJsonObject makeStringProperty(const QString &description);
    static QJsonObject makeIntegerProperty(const QString &description, int minimum = 0);
    static QJsonObject makeBooleanProperty(const QString &description);
    static QJsonObject makeToolDefinition(const QString &name,
                                          const QString &title,
                                          const QString &description,
                                          const QJsonObject &inputSchema,
                                          bool readOnly,
                                          bool destructive = false,
                                          bool openWorld = false);
    static QString protocolToString(ProtocolType protocolType);
    static int timeoutFromArguments(const QJsonObject &arguments);

    Qt::ConnectionType mainConnectionType() const;
    void runUiTool(const ToolFunction &function, const ToolCallback &callback) const;
    static ToolResponse makeResponse(const QJsonObject &structuredContent,
                                     bool isError = false,
                                     const QString &text = QString());
    static ToolResponse makeErrorResponse(const QString &message);

    ToolResponse getStatus() const;
    static ToolResponse listSessions();
    ToolResponse openSessionById(const QJsonObject &arguments) const;
    ToolResponse openSessionByName(const QJsonObject &arguments) const;
    ToolResponse switchTab(const QJsonObject &arguments) const;
    ToolResponse nextTab() const;
    ToolResponse connectCurrent() const;
    ToolResponse disconnectCurrent() const;
    ToolResponse sendText(const QJsonObject &arguments) const;
    ToolResponse sendKey(const QJsonObject &arguments) const;
    ToolResponse getScreenText() const;
    ToolResponse getLastLine() const;
    ToolResponse clearScreen() const;

    void waitForString(const QJsonObject &arguments, const ToolCallback &callback);
    void waitForRegex(const QJsonObject &arguments, const ToolCallback &callback);
    void startWaitForString(const QString &text, int timeoutMs, const ToolCallback &callback);
    void startWaitForRegex(const QString &pattern, int timeoutMs, const ToolCallback &callback);

    MainWindow *mainWindow_ = nullptr;
    bool listening_ = false;
    int port_ = 0;
};

#endif// QSHELL_MCPTOOLREGISTRY_H
