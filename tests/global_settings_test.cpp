#include "core/datatype.h"

#include <QDebug>

int main() {
    const GlobalSettings defaults;
    if (defaults.commandHistorySize != 10000 ||
        GlobalSettings::fromJson(QJsonObject()).commandHistorySize != 10000) {
        qCritical() << "command history size must default to 10000 for new and legacy settings";
        return 1;
    }
    for (const auto &value : {QJsonValue(0), QJsonValue(-1), QJsonValue("invalid")}) {
        if (GlobalSettings::fromJson(QJsonObject{{"commandHistorySize", value}}).commandHistorySize != 10000) {
            qCritical() << "invalid history size must fall back to the default";
            return 1;
        }
    }
    if (!defaults.commandHistoryCompletion ||
        !GlobalSettings::fromJson(QJsonObject()).commandHistoryCompletion) {
        qCritical() << "command history completion must be enabled for new and legacy settings";
        return 1;
    }
    if (defaults.autoSaveLog) {
        qCritical() << "automatic log saving must be disabled by default";
        return 1;
    }
    if (defaults.terminalTimestamp) {
        qCritical() << "terminal timestamps must be disabled by default";
        return 1;
    }

    GlobalSettings configured;
    configured.autoSaveLog = true;
    configured.autoSaveLogDirectory = QStringLiteral("/tmp/qshell-logs");
    configured.terminalTimestamp = true;
    configured.commandHistoryCompletion = false;
    configured.commandHistorySize = 12345;

    const GlobalSettings restored =
            GlobalSettings::fromJson(configured.toJson());
    if (!restored.autoSaveLog || !restored.terminalTimestamp || restored.commandHistoryCompletion ||
        restored.commandHistorySize != configured.commandHistorySize ||
        restored.autoSaveLogDirectory != configured.autoSaveLogDirectory) {
        qCritical() << "global settings did not survive JSON round trip";
        return 1;
    }

    const GlobalSettings legacy =
            GlobalSettings::fromJson(QJsonObject());
    if (legacy.autoSaveLog || legacy.terminalTimestamp ||
        !legacy.autoSaveLogDirectory.isEmpty()) {
        qCritical() << "missing global settings must use disabled defaults";
        return 1;
    }

    QJsonObject oldSettings;
    oldSettings["logTimestamp"] = true;
    oldSettings["terminalTimestamp"] = false;
    const auto migrated = GlobalSettings::fromJson(oldSettings);
    if (migrated.terminalTimestamp || migrated.toJson().contains("logTimestamp")) {
        qCritical() << "obsolete log timestamp setting was retained";
        return 1;
    }
    return 0;
}
