#ifndef QSHELL_TERMINALLINETIMESTAMP_H
#define QSHELL_TERMINALLINETIMESTAMP_H

#include <QByteArray>

class TerminalLineTimestamp {
public:
    QByteArray process(const QByteArray &data,
                       bool enabled,
                       const QByteArray &timestamp);

private:
    bool lineStart_ = true;
};

#endif// QSHELL_TERMINALLINETIMESTAMP_H
