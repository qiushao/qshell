#ifndef SESSIONDATA_H
#define SESSIONDATA_H

#include <QString>
#include <QUuid>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>

// 协议类型
enum class ProtocolType {
    LocalShell,
    SSH,
    Serial,
    UNKNOWN
};

// SSH认证方式
enum class SSHAuthMethod {
    Password,
    PublicKey
};

// 串口校验位
enum class Parity {
    None,
    Even,
    Odd,
    Space,
    Mark
};

// 串口停止位
enum class StopBits {
    One,
    OneAndHalf,
    Two
};

// 串口数据位
enum class DataBits {
    Five = 5,
    Six = 6,
    Seven = 7,
    Eight = 8
};

// 串口流控
enum class FlowControl {
    None,
    Hardware,
    Software
};

// SSH配置
struct SSHConfig {
    QString host;
    int port = 22;
    QString username;
    SSHAuthMethod authMethod = SSHAuthMethod::Password;
    QString password;          // 加密存储
    QString privateKeyPath;
    QString passphrase;        // 加密存储

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["host"] = host;
        obj["port"] = port;
        obj["username"] = username;
        obj["authMethod"] = static_cast<int>(authMethod);
        obj["password"] = password;  // 已加密
        obj["privateKeyPath"] = privateKeyPath;
        obj["passphrase"] = passphrase;  // 已加密
        return obj;
    }

    static SSHConfig fromJson(const QJsonObject& obj) {
        SSHConfig config;
        config.host = obj["host"].toString();
        config.port = obj["port"].toInt(22);
        config.username = obj["username"].toString();
        config.authMethod = static_cast<SSHAuthMethod>(obj["authMethod"].toInt());
        config.password = obj["password"].toString();
        config.privateKeyPath = obj["privateKeyPath"].toString();
        config.passphrase = obj["passphrase"].toString();
        return config;
    }
};

// 串口配置
struct SerialConfig {
    QString portName;
    int baudRate = 115200;
    DataBits dataBits = DataBits::Eight;
    Parity parity = Parity::None;
    StopBits stopBits = StopBits::One;
    FlowControl flowControl = FlowControl::None;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["portName"] = portName;
        obj["baudRate"] = baudRate;
        obj["dataBits"] = static_cast<int>(dataBits);
        obj["parity"] = static_cast<int>(parity);
        obj["stopBits"] = static_cast<int>(stopBits);
        obj["flowControl"] = static_cast<int>(flowControl);
        return obj;
    }

    static SerialConfig fromJson(const QJsonObject& obj) {
        SerialConfig config;
        config.portName = obj["portName"].toString();
        config.baudRate = obj["baudRate"].toInt(115200);
        config.dataBits = static_cast<DataBits>(obj["dataBits"].toInt(8));
        config.parity = static_cast<Parity>(obj["parity"].toInt());
        config.stopBits = static_cast<StopBits>(obj["stopBits"].toInt());
        config.flowControl = static_cast<FlowControl>(obj["flowControl"].toInt());
        return config;
    }
};

// 会话数据
struct SessionData {
    QString id;
    QString name;
    QString groupId;  // 空表示在根级别
    ProtocolType protocolType = ProtocolType::UNKNOWN;
    SSHConfig sshConfig;
    SerialConfig serialConfig;
    int sortOrder = 0;  // 排序顺序

    SessionData() {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["groupId"] = groupId;
        obj["protocolType"] = static_cast<int>(protocolType);
        if (protocolType == ProtocolType::SSH) {
            obj["sshConfig"] = sshConfig.toJson();
        } else if (protocolType == ProtocolType::Serial) {
            obj["serialConfig"] = serialConfig.toJson();
        }
        obj["sortOrder"] = sortOrder;
        return obj;
    }

    static SessionData fromJson(const QJsonObject& obj) {
        SessionData data;
        data.id = obj["id"].toString();
        data.name = obj["name"].toString();
        data.groupId = obj["groupId"].toString();
        data.protocolType = static_cast<ProtocolType>(obj["protocolType"].toInt());
        if (data.protocolType == ProtocolType::SSH) {
            data.sshConfig = SSHConfig::fromJson(obj["sshConfig"].toObject());
        } else if (data.protocolType == ProtocolType::Serial) {
            data.serialConfig = SerialConfig::fromJson(obj["serialConfig"].toObject());
        }
        data.sortOrder = obj["sortOrder"].toInt(0);
        return data;
    }

    // 排序比较运算符
    bool operator<(const SessionData& other) const {
        return sortOrder < other.sortOrder;
    }
};

// 分组数据
struct GroupData {
    QString id;
    QString name;
    int sortOrder = 0;  // 排序顺序

    GroupData() {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["sortOrder"] = sortOrder;
        return obj;
    }

    static GroupData fromJson(const QJsonObject& obj) {
        GroupData data;
        data.id = obj["id"].toString();
        data.name = obj["name"].toString();
        data.sortOrder = obj["sortOrder"].toInt(0);
        return data;
    }

    // 排序比较运算符
    bool operator<(const GroupData& other) const {
        return sortOrder < other.sortOrder;
    }
};

// 快捷按钮
struct QuickButton {
    QString id;
    QString name;
    QString command;
    QString groupId;
    int sortOrder = 0;  // 排序顺序

    QuickButton() {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["command"] = command;
        obj["groupId"] = groupId;
        obj["sortOrder"] = sortOrder;
        return obj;
    }

    static QuickButton fromJson(const QJsonObject& obj) {
        QuickButton btn;
        btn.id = obj["id"].toString();
        btn.name = obj["name"].toString();
        btn.command = obj["command"].toString();
        btn.groupId = obj["groupId"].toString();
        btn.sortOrder = obj["sortOrder"].toInt(0);
        return btn;
    }

    // 排序比较运算符
    bool operator<(const QuickButton& other) const {
        return sortOrder < other.sortOrder;
    }
};

// 按钮分组
struct ButtonGroup {
    QString id;
    QString name;
    int sortOrder = 0;  // 排序顺序

    ButtonGroup() {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["sortOrder"] = sortOrder;
        return obj;
    }

    static ButtonGroup fromJson(const QJsonObject& obj) {
        ButtonGroup grp;
        grp.id = obj["id"].toString();
        grp.name = obj["name"].toString();
        grp.sortOrder = obj["sortOrder"].toInt(0);
        return grp;
    }

    // 排序比较运算符
    bool operator<(const ButtonGroup& other) const {
        return sortOrder < other.sortOrder;
    }
};

// 全局设置
struct GlobalSettings {
    QString fontFamily = "Consolas";
    int fontSize = 14;
    QString colorScheme = "Tango";
    bool copyOnSelect = false;
    bool debug = true;
    bool logTimestamp = true;
    bool autoSaveLog = false;
    QString autoSaveLogDirectory;
    bool mcpEnabled = false;
    int mcpPort = 8765;
    QString mcpBearerToken;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["fontFamily"] = fontFamily;
        obj["fontSize"] = fontSize;
        obj["colorScheme"] = colorScheme;
        obj["copyOnSelect"] = copyOnSelect;
        obj["debug"] = debug;
        obj["logTimestamp"] = logTimestamp;
        obj["autoSaveLog"] = autoSaveLog;
        obj["autoSaveLogDirectory"] = autoSaveLogDirectory;
        obj["mcpEnabled"] = mcpEnabled;
        obj["mcpPort"] = mcpPort;
        obj["mcpBearerToken"] = mcpBearerToken;
        return obj;
    }

    static GlobalSettings fromJson(const QJsonObject& obj) {
        GlobalSettings settings;
        settings.fontFamily = obj["fontFamily"].toString("Consolas");
        settings.fontSize = obj["fontSize"].toInt(12);
        settings.colorScheme = obj["colorScheme"].toString("Tango");
        settings.copyOnSelect = obj["copyOnSelect"].toBool();
        settings.debug = obj["debug"].toBool();
        settings.logTimestamp = obj["logTimestamp"].toBool(true);
        settings.autoSaveLog = obj["autoSaveLog"].toBool(false);
        settings.autoSaveLogDirectory = obj["autoSaveLogDirectory"].toString();
        settings.mcpEnabled = obj["mcpEnabled"].toBool(false);
        settings.mcpPort = obj["mcpPort"].toInt(8765);
        settings.mcpBearerToken = obj["mcpBearerToken"].toString();
        return settings;
    }
};

struct WindowLayout {
    bool showToolBar = true;
    bool showSessions = true;
    bool showCommandWindow = true;
    bool showCommandButton = true;
    bool expendSessionDock = true;
    int sessionDockWidth = 300;
    int commandWindowHeight = 30;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["showToolBar"] = showToolBar;
        obj["showSessions"] = showSessions;
        obj["showCommandWindow"] = showCommandWindow;
        obj["showCommandButton"] = showCommandButton;
        obj["expendSessionDock"] = expendSessionDock;
        obj["sessionDockWidth"] = sessionDockWidth;
        obj["commandWindowHeight"] = commandWindowHeight;
        return obj;
    }

    static WindowLayout fromJson(const QJsonObject& obj) {
        WindowLayout layout{};
        layout.showToolBar = obj["showToolBar"].toBool();
        layout.showSessions = obj["showSessions"].toBool();
        layout.showCommandWindow = obj["showCommandWindow"].toBool();
        layout.showCommandButton = obj["showCommandButton"].toBool();
        layout.expendSessionDock = obj["expendSessionDock"].toBool();
        layout.sessionDockWidth = obj["sessionDockWidth"].toInt();
        layout.commandWindowHeight = obj["commandWindowHeight"].toInt();
        return layout;
    }
};

#endif // SESSIONDATA_H
