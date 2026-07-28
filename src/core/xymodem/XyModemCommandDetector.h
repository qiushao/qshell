#ifndef QSHELL_XYMODEM_COMMAND_DETECTOR_H
#define QSHELL_XYMODEM_COMMAND_DETECTOR_H

#include <QByteArray>

enum class XyModemCommand {
    None,
    SendXmodem,
    ReceiveXmodem,
    SendYmodem,
    ReceiveYmodem
};

class XyModemCommandDetector {
public:
    XyModemCommand consume(const QByteArray &data);

private:
    QByteArray inputLine_;
    bool simpleInput_ = true;
};

bool isXyModemSendCommand(XyModemCommand command);
qsizetype findXyModemReceiverHandshake(
        XyModemCommand command,
        const QByteArray &data);
bool containsXyModemCommandFailure(const QByteArray &data);

#endif// QSHELL_XYMODEM_COMMAND_DETECTOR_H
