#include "XyModemCommandDetector.h"

#include "XyModemProtocol.h"

#include <QString>
#include <QStringList>

#include <algorithm>

namespace {

constexpr qsizetype maximumCommandLength = 4096;
constexpr auto bracketedPasteStart = "\x1b[200~";
constexpr auto bracketedPasteEnd = "\x1b[201~";

bool isControlCharacter(const char value) {
    const auto byte =
            static_cast<unsigned char>(value);
    return byte < 0x20 || byte == 0x7f;
}

QString executableName(QString token) {
    if (token.size() >= 2 && ((token.startsWith('"') && token.endsWith('"')) || (token.startsWith('\'') && token.endsWith('\'')))) {
        token = token.mid(1, token.size() - 2);
    }
    token.replace('\\', '/');
    return token.section('/', -1);
}

XyModemCommand detectCommand(const QByteArray &data) {
    const QString line = QString::fromUtf8(data).simplified();
    const QStringList tokens =
            line.split(' ', Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        return XyModemCommand::None;
    }

    const QString executable = executableName(tokens.constFirst());
    if (executable == QStringLiteral("busybox")) {
        if (tokens.size() > 1 && tokens.at(1) == QStringLiteral("rx")) {
            return XyModemCommand::SendXmodem;
        }
        return XyModemCommand::None;
    }
    if (executable == QStringLiteral("rx")) {
        return XyModemCommand::SendXmodem;
    }
    if (executable == QStringLiteral("sx")) {
        return XyModemCommand::ReceiveXmodem;
    }
    if (executable == QStringLiteral("rb")) {
        return XyModemCommand::SendYmodem;
    }
    if (executable == QStringLiteral("sb")) {
        return XyModemCommand::ReceiveYmodem;
    }
    return XyModemCommand::None;
}

}// namespace

XyModemCommand XyModemCommandDetector::consume(
        const QByteArray &data) {
    QByteArray filteredData = data;
    filteredData.replace(bracketedPasteStart, "");
    filteredData.replace(bracketedPasteEnd, "");

    XyModemCommand detectedCommand = XyModemCommand::None;
    for (const char character: filteredData) {
        const auto byte =
                static_cast<unsigned char>(character);
        if (character == '\r' || character == '\n') {
            if (detectedCommand == XyModemCommand::None && simpleInput_) {
                detectedCommand = detectCommand(inputLine_);
            }
            inputLine_.clear();
            simpleInput_ = true;
        } else if (simpleInput_ && (character == '\b' || byte == 0x7f)) {
            if (!inputLine_.isEmpty()) {
                inputLine_.chop(1);
            }
        } else if (byte == 0x03 || byte == 0x15) {
            inputLine_.clear();
            simpleInput_ = true;
        } else if (simpleInput_ && (character == '\t' || byte >= 0x20)) {
            if (inputLine_.size() < maximumCommandLength) {
                inputLine_.append(character);
            } else {
                inputLine_.clear();
                simpleInput_ = false;
            }
        } else {
            inputLine_.clear();
            simpleInput_ = false;
        }
    }
    return detectedCommand;
}

bool isXyModemSendCommand(
        const XyModemCommand command) {
    return command == XyModemCommand::SendXmodem || command == XyModemCommand::SendYmodem;
}

qsizetype findXyModemReceiverHandshake(
        const XyModemCommand command,
        const QByteArray &data) {
    if (!isXyModemSendCommand(command)) {
        return -1;
    }

    for (qsizetype index = 0; index < data.size(); ++index) {
        const char value = data.at(index);
        if (command == XyModemCommand::SendXmodem && value == XyModem::nak) {
            return index;
        }
        if (value != XyModem::crcRequest) {
            continue;
        }

        const bool hasBoundaryBefore =
                index == 0 || data.at(index - 1) == '\r' || data.at(index - 1) == '\n' || data.at(index - 1) == XyModem::crcRequest;
        const bool hasBoundaryAfter =
                index + 1 == data.size() || isControlCharacter(data.at(index + 1)) || data.at(index + 1) == XyModem::crcRequest || data.at(index + 1) == XyModem::nak;
        if (hasBoundaryBefore && hasBoundaryAfter) {
            return index;
        }
    }
    return -1;
}

bool containsXyModemCommandFailure(
        const QByteArray &data) {
    const QByteArray lower = data.toLower();
    static const QList<QByteArray> failureMarkers{
            QByteArrayLiteral("not found"),
            QByteArrayLiteral("not recognized"),
            QByteArrayLiteral("unknown command"),
            QByteArrayLiteral("no such file"),
            QByteArrayLiteral("not an applet"),
            QByteArrayLiteral("permission denied"),
            QByteArrayLiteral("cannot execute"),
            QByteArrayLiteral("exec format error"),
            QByteArrayLiteral("can't open"),
            QByteArrayLiteral("cannot open"),
            QByteArrayLiteral("usage:")};
    return std::any_of(
            failureMarkers.cbegin(),
            failureMarkers.cend(),
            [&lower](const QByteArray &marker) {
                return lower.contains(marker);
            });
}
