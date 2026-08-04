#include "core/datatype.h"

#include <QDebug>

int main() {
    const GlobalSettings defaults;
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

    const GlobalSettings restored =
            GlobalSettings::fromJson(configured.toJson());
    if (!restored.autoSaveLog || !restored.terminalTimestamp || restored.logTimestamp ||
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

    return 0;
}
