#include "TerminalLineTimestamp.h"

QByteArray TerminalLineTimestamp::process(
        const QByteArray &data,
        const bool enabled,
        const QByteArray &timestamp) {
    if (!enabled) {
        for (const char character: data) {
            lineStart_ = character == '\n';
        }
        return data;
    }

    QByteArray output;
    for (const char character: data) {
        if (lineStart_) {
            output.append(timestamp);
            lineStart_ = false;
        }
        output.append(character);
        if (character == '\n') {
            lineStart_ = true;
        }
    }
    return output;
}
