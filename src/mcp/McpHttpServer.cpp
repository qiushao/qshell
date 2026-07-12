#include "McpHttpServer.h"

#include "ui/MainWindow.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

namespace {
constexpr int maxRequestBodyBytes = 1024 * 1024;
constexpr const char *mcpEndpointPath = "/mcp";
constexpr const char *mcpProtocolVersion = "2025-06-18";

QJsonValue responseId(const QJsonValue &id) {
    return id.isUndefined() ? QJsonValue(QJsonValue::Null) : id;
}
}// namespace

McpHttpServer::McpHttpServer(MainWindow *mainWindow, QObject *parent)
    : QObject(parent) {
    server_ = new QTcpServer(this);
    toolRegistry_ = new McpToolRegistry(mainWindow, this);
    connect(server_, &QTcpServer::newConnection, this, &McpHttpServer::onNewConnection);
}

McpHttpServer::~McpHttpServer() {
    stop();
}

bool McpHttpServer::start(int port, const QString &bearerToken, QString *errorMessage) {
    stop();

    if (port < 1 || port > 65535) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Invalid MCP port: %1").arg(port);
        }
        return false;
    }

    if (bearerToken.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("MCP bearer token is empty.");
        }
        return false;
    }

    bearerToken_ = bearerToken.trimmed();
    if (!server_->listen(QHostAddress::LocalHost, static_cast<quint16>(port))) {
        if (errorMessage != nullptr) {
            *errorMessage = server_->errorString();
        }
        bearerToken_.clear();
        toolRegistry_->setListenState(false, 0);
        return false;
    }

    toolRegistry_->setListenState(true, server_->serverPort());
    emit listeningChanged(true);
    return true;
}

void McpHttpServer::stop() {
    const bool wasListening = server_ != nullptr && server_->isListening();
    if (server_ != nullptr) {
        server_->close();
    }

    const QList<QTcpSocket *> sockets = requestBuffers_.keys();
    for (QTcpSocket *socket: sockets) {
        if (socket != nullptr) {
            socket->disconnectFromHost();
            socket->deleteLater();
        }
    }
    requestBuffers_.clear();
    bearerToken_.clear();
    if (toolRegistry_ != nullptr) {
        toolRegistry_->setListenState(false, 0);
    }
    if (wasListening) {
        emit listeningChanged(false);
    }
}

bool McpHttpServer::isListening() const {
    return server_ != nullptr && server_->isListening();
}

int McpHttpServer::port() const {
    if (!isListening()) {
        return 0;
    }
    return static_cast<int>(server_->serverPort());
}

QString McpHttpServer::endpointUrl() const {
    return QString("http://127.0.0.1:%1%2").arg(port()).arg(mcpEndpointPath);
}

void McpHttpServer::onNewConnection() {
    while (server_->hasPendingConnections()) {
        QTcpSocket *socket = server_->nextPendingConnection();
        if (socket == nullptr) {
            continue;
        }
        requestBuffers_.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            onReadyRead(socket);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            onSocketDisconnected(socket);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void McpHttpServer::onReadyRead(QTcpSocket *socket) {
    if (socket == nullptr || !requestBuffers_.contains(socket)) {
        return;
    }

    QByteArray &buffer = requestBuffers_[socket];
    buffer.append(socket->readAll());

    qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
    qsizetype separatorLength = 4;
    if (headerEnd < 0) {
        headerEnd = buffer.indexOf("\n\n");
        separatorLength = 2;
    }
    if (headerEnd < 0) {
        return;
    }

    const QByteArray headerBlock = buffer.left(headerEnd);
    const QList<QByteArray> headerLines = headerBlock.split('\n');
    if (headerLines.isEmpty()) {
        sendHttpResponse(socket, 400, statusText(400), "Bad Request", "text/plain; charset=utf-8");
        return;
    }

    const QList<QByteArray> requestLineParts = headerLines.first().trimmed().split(' ');
    if (requestLineParts.size() < 3) {
        sendHttpResponse(socket, 400, statusText(400), "Bad Request", "text/plain; charset=utf-8");
        return;
    }

    const QString method = QString::fromLatin1(requestLineParts.at(0)).toUpper();
    const QString path = normalizedPath(QString::fromUtf8(requestLineParts.at(1)));
    QMap<QString, QString> headers;
    for (int i = 1; i < headerLines.size(); ++i) {
        const QByteArray line = headerLines.at(i).trimmed();
        const qsizetype colonIndex = line.indexOf(':');
        if (colonIndex <= 0) {
            continue;
        }
        const QString key = QString::fromLatin1(line.left(colonIndex)).trimmed().toLower();
        const QString value = QString::fromUtf8(line.mid(colonIndex + 1)).trimmed();
        headers[key] = value;
    }

    const qsizetype contentLength = headers.value("content-length").toInt();
    if (contentLength < 0) {
        sendHttpResponse(socket, 400, statusText(400), "Bad Request", "text/plain; charset=utf-8");
        return;
    }
    if (contentLength > maxRequestBodyBytes) {
        sendHttpResponse(socket, 413, statusText(413), "Payload Too Large", "text/plain; charset=utf-8");
        return;
    }

    const qsizetype totalRequestSize = headerEnd + separatorLength + contentLength;
    if (buffer.size() < totalRequestSize) {
        return;
    }

    const QByteArray body = buffer.mid(headerEnd + separatorLength, contentLength);
    requestBuffers_.remove(socket);
    handleRequest(socket, method, path, headers, body);
}

void McpHttpServer::onSocketDisconnected(QTcpSocket *socket) {
    requestBuffers_.remove(socket);
}

void McpHttpServer::handleRequest(QTcpSocket *socket,
                                  const QString &method,
                                  const QString &path,
                                  const QMap<QString, QString> &headers,
                                  const QByteArray &body) {
    if (path != mcpEndpointPath) {
        sendHttpResponse(socket, 404, statusText(404), "Not Found", "text/plain; charset=utf-8");
        return;
    }

    if (!isOriginAllowed(headers)) {
        sendHttpResponse(socket, 403, statusText(403), "Forbidden", "text/plain; charset=utf-8");
        return;
    }

    if (!isAuthorized(headers)) {
        sendHttpResponse(socket,
                         401,
                         statusText(401),
                         "Unauthorized",
                         "text/plain; charset=utf-8",
                         {{QByteArray("WWW-Authenticate"), QByteArray("Bearer")}});
        return;
    }

    if (method == "GET") {
        sendHttpResponse(socket,
                         405,
                         statusText(405),
                         "This qshell MCP endpoint does not provide an SSE stream.",
                         "text/plain; charset=utf-8",
                         {{QByteArray("Allow"), QByteArray("POST")}});
        return;
    }

    if (method != "POST") {
        sendHttpResponse(socket,
                         405,
                         statusText(405),
                         "Method Not Allowed",
                         "text/plain; charset=utf-8",
                         {{QByteArray("Allow"), QByteArray("POST")}});
        return;
    }

    handleJsonRpc(socket, body);
}

void McpHttpServer::handleJsonRpc(QTcpSocket *socket, const QByteArray &body) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        sendJsonRpcError(socket, QJsonValue(QJsonValue::Null), -32700, tr("Parse error"));
        return;
    }

    const QJsonObject request = document.object();
    const QJsonValue id = request.value("id");
    const QString method = request.value("method").toString();

    if (request.value("jsonrpc").toString() != "2.0") {
        sendJsonRpcError(socket, id, -32600, tr("Invalid Request"));
        return;
    }

    if (method.isEmpty()) {
        sendHttpResponse(socket, 202, statusText(202));
        return;
    }

    if (!request.contains("id")) {
        sendHttpResponse(socket, 202, statusText(202));
        return;
    }

    handleJsonRpcRequest(socket, request, id, method);
}

void McpHttpServer::handleJsonRpcRequest(QTcpSocket *socket,
                                         const QJsonObject &request,
                                         const QJsonValue &id,
                                         const QString &method) {
    if (method == "initialize") {
        const QJsonObject params = request.value("params").toObject();
        const QString requestedVersion = params.value("protocolVersion").toString(mcpProtocolVersion);
        Q_UNUSED(requestedVersion)

        QJsonObject toolsCapability;
        toolsCapability["listChanged"] = false;

        QJsonObject capabilities;
        capabilities["tools"] = toolsCapability;

        QJsonObject serverInfo;
        serverInfo["name"] = "qshell";
        serverInfo["title"] = "QShell";
        serverInfo["version"] = QCoreApplication::applicationVersion();

        QJsonObject result;
        result["protocolVersion"] = mcpProtocolVersion;
        result["capabilities"] = capabilities;
        result["serverInfo"] = serverInfo;
        result["instructions"] = tr(
                "Control only the active qshell instance on this machine. Inspect status, sessions, and screen output "
                "before changing terminal state. send_text and send_key act on a live terminal and may execute commands; "
                "use them only when the user has requested that action. Use wait_for_string or wait_for_regex to verify "
                "expected terminal output.");
        sendJsonRpcResponse(socket, makeJsonRpcResult(id, result));
        return;
    }

    if (method == "ping") {
        sendJsonRpcResponse(socket, makeJsonRpcResult(id, {}));
        return;
    }

    if (method == "tools/list") {
        QJsonObject result;
        result["tools"] = McpToolRegistry::toolDefinitions();
        sendJsonRpcResponse(socket, makeJsonRpcResult(id, result));
        return;
    }

    if (method == "tools/call") {
        if (!request.value("params").isObject()) {
            sendJsonRpcError(socket, id, -32602, tr("Invalid params"));
            return;
        }

        const QJsonObject params = request.value("params").toObject();
        const QString toolName = params.value("name").toString();
        if (toolName.isEmpty()) {
            sendJsonRpcError(socket, id, -32602, tr("Tool name is required"));
            return;
        }

        if (!McpToolRegistry::hasTool(toolName)) {
            sendJsonRpcError(socket, id, -32602, tr("Unknown tool: %1").arg(toolName));
            return;
        }

        if (params.contains("arguments") && !params.value("arguments").isObject()) {
            sendJsonRpcError(socket, id, -32602, tr("Tool arguments must be an object"));
            return;
        }

        const QJsonObject arguments = params.value("arguments").toObject();
        const QPointer<QTcpSocket> socketPointer(socket);
        toolRegistry_->callTool(toolName, arguments, [this, socketPointer, id](const McpToolRegistry::ToolResponse &toolResponse) {
            if (socketPointer.isNull()) {
                return;
            }

            QJsonObject textContent;
            textContent["type"] = "text";
            textContent["text"] = toolResponse.text;

            QJsonArray content;
            content.append(textContent);

            QJsonObject result;
            result["content"] = content;
            result["structuredContent"] = toolResponse.structuredContent;
            result["isError"] = toolResponse.isError;
            sendJsonRpcResponse(socketPointer.data(), makeJsonRpcResult(id, result));
        });
        return;
    }

    sendJsonRpcError(socket, id, -32601, tr("Method not found"));
}

bool McpHttpServer::isOriginAllowed(const QMap<QString, QString> &headers) {
    const QString origin = headers.value("origin").trimmed();
    if (origin.isEmpty()) {
        return true;
    }

    const QUrl originUrl(origin);
    const QString host = originUrl.host().toLower();
    return host == "localhost" || host == "127.0.0.1" || host == "::1";
}

bool McpHttpServer::isAuthorized(const QMap<QString, QString> &headers) const {
    const QString authorization = headers.value("authorization").trimmed();
    const QString bearerPrefix = "Bearer ";
    if (!authorization.startsWith(bearerPrefix, Qt::CaseInsensitive)) {
        return false;
    }
    const QString token = authorization.mid(bearerPrefix.size()).trimmed();
    return !bearerToken_.isEmpty() && token == bearerToken_;
}

void McpHttpServer::sendHttpResponse(QTcpSocket *socket,
                                     int statusCode,
                                     const QByteArray &reasonPhrase,
                                     const QByteArray &body,
                                     const QByteArray &contentType,
                                     const QList<QPair<QByteArray, QByteArray>> &extraHeaders) {
    if (socket == nullptr) {
        return;
    }

    QByteArray response;
    response.append("HTTP/1.1 ");
    response.append(QByteArray::number(statusCode));
    response.append(' ');
    response.append(reasonPhrase.isEmpty() ? statusText(statusCode) : reasonPhrase);
    response.append("\r\n");
    response.append("Connection: close\r\n");
    response.append("Content-Length: ");
    response.append(QByteArray::number(body.size()));
    response.append("\r\n");
    if (!contentType.isEmpty()) {
        response.append("Content-Type: ");
        response.append(contentType);
        response.append("\r\n");
    }
    for (const auto &header: extraHeaders) {
        response.append(header.first);
        response.append(": ");
        response.append(header.second);
        response.append("\r\n");
    }
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void McpHttpServer::sendJsonRpcResponse(QTcpSocket *socket, const QJsonObject &response) {
    const QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
    sendHttpResponse(socket, 200, statusText(200), body, "application/json; charset=utf-8");
}

void McpHttpServer::sendJsonRpcError(QTcpSocket *socket,
                                     const QJsonValue &id,
                                     int code,
                                     const QString &message,
                                     const QJsonValue &data) {
    sendJsonRpcResponse(socket, makeJsonRpcErrorObject(id, code, message, data));
}

QJsonObject McpHttpServer::makeJsonRpcResult(const QJsonValue &id, const QJsonObject &result) {
    QJsonObject response;
    response["jsonrpc"] = "2.0";
    response["id"] = responseId(id);
    response["result"] = result;
    return response;
}

QJsonObject McpHttpServer::makeJsonRpcErrorObject(const QJsonValue &id,
                                                  int code,
                                                  const QString &message,
                                                  const QJsonValue &data) {
    QJsonObject error;
    error["code"] = code;
    error["message"] = message;
    if (!data.isUndefined()) {
        error["data"] = data;
    }

    QJsonObject response;
    response["jsonrpc"] = "2.0";
    response["id"] = responseId(id);
    response["error"] = error;
    return response;
}

QString McpHttpServer::normalizedPath(const QString &target) {
    QString path = target;
    if (target.startsWith("http://", Qt::CaseInsensitive) || target.startsWith("https://", Qt::CaseInsensitive)) {
        path = QUrl(target).path();
    }

    const qsizetype queryIndex = path.indexOf('?');
    if (queryIndex >= 0) {
        path = path.left(queryIndex);
    }
    return path;
}

QByteArray McpHttpServer::statusText(int statusCode) {
    switch (statusCode) {
        case 200:
            return "OK";
        case 202:
            return "Accepted";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 413:
            return "Payload Too Large";
        default:
            return "Error";
    }
}
