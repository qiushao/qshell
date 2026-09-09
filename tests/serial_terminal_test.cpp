#include "core/datatype.h"
#include "ui/session/SessionEditDialog.h"
#include "ui/terminal/SerialTerminal.h"

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStandardPaths>

class TestSerialTerminal : public SerialTerminal {
public:
    explicit TestSerialTerminal(const SessionData &session) : SerialTerminal(session, nullptr) {
        connect_ = true;
    }

    ~TestSerialTerminal() override {
        hide();
    }

    using BaseTerminal::receiveBackendData;
    QByteArray sent;

protected:
    void writeToBackend(const QByteArray &data) override {
        sent.append(data);
    }
};

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QApplication::setApplicationName(QStringLiteral("qshell-serial-test"));
    auto check = [](bool condition, const char *message) {
        if (!condition) {
            qCritical() << message;
        }
        return condition;
    };

    SessionData session;
    session.name = QStringLiteral("Serial test");
    session.protocolType = ProtocolType::Serial;
    session.serialConfig.portName = QStringLiteral("test-port");
    if (!check(session.serialConfig.dataMode == SerialDataMode::Text &&
                       SerialConfig::fromJson(QJsonObject()).dataMode == SerialDataMode::Text,
               "new and legacy serial configurations must default to text")) {
        return 1;
    }
    for (const auto mode : {SerialDataMode::Text, SerialDataMode::Bin}) {
        session.serialConfig.dataMode = mode;
        const auto restored = SessionData::fromJson(session.toJson());
        if (!check(restored.serialConfig.dataMode == mode &&
                           restored.serialConfig.portName == session.serialConfig.portName,
                   "serial configuration must survive JSON round trip")) {
            return 1;
        }
    }

    SessionEditDialog dialog(nullptr);
    if (!check(dialog.sessionData().serialConfig.dataMode == SerialDataMode::Text,
               "session editor must default to text")) {
        return 1;
    }
    dialog.setSessionData(session);
    auto *modeCombo = dialog.findChild<QComboBox *>("serialDataMode");
    if (!check(modeCombo != nullptr && modeCombo->currentData().toInt() == static_cast<int>(SerialDataMode::Bin) &&
                       dialog.sessionData().serialConfig.dataMode == SerialDataMode::Bin,
               "session editor must restore bin mode")) {
        return 1;
    }
    modeCombo->setCurrentIndex(modeCombo->findData(static_cast<int>(SerialDataMode::Text)));
    if (!check(dialog.sessionData().serialConfig.dataMode == SerialDataMode::Text,
               "session editor must save the selected mode")) {
        return 1;
    }

    QStringList lines;
    TestSerialTerminal terminal(session);
    QObject::connect(&terminal, &QTermWidget::onNewLine, [&lines](const QString &line) {
        lines.append(line);
    });
    terminal.resize(800, 400);
    terminal.show();
    QApplication::processEvents();
    auto *input = terminal.findChild<QLineEdit *>(QString(), Qt::FindDirectChildrenOnly);
    if (!check(input != nullptr && input->isVisible(), "bin mode must provide a visible hex input")) {
        return 1;
    }

    terminal.sendText(QStringLiteral("41 00 fF"));
    if (!check(terminal.sent.isEmpty() && lines.isEmpty(),
               "unsubmitted input must not send or echo")) {
        return 1;
    }
    terminal.sendText(QStringLiteral("\r"));
    if (!check(terminal.sent == QByteArray::fromHex("4100ff") && input->text().isEmpty(),
               "hex input must send raw bytes without an appended carriage return")) {
        return 1;
    }
    if (!check(lines == QStringList{QStringLiteral("41 00 FF")},
               "sent bytes must echo in the terminal as uppercase hex")) {
        return 1;
    }
    lines.clear();
    terminal.sent.clear();
    for (const char character : QByteArray("1b0800\r\n")) {
        terminal.proxySendData(QByteArray(1, character));
    }
    if (!check(terminal.sent == QByteArray::fromHex("1b0800") &&
                       lines == QStringList{QStringLiteral("1B 08 00")},
               "incremental compact hex input must send and echo only once for CRLF")) {
        return 1;
    }
    lines.clear();
    terminal.sent.clear();
    for (const QString &invalid : {QStringLiteral("4"), QStringLiteral("41 GG"),
                                   QStringLiteral("0x41"), QStringLiteral("4 1"),
                                   QStringLiteral("41 F"), QStringLiteral("41中文")}) {
        input->setText(invalid);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(input, &enter);
        if (!check(terminal.sent.isEmpty() && input->text() == invalid && lines.isEmpty(),
                   "invalid input must be retained without sending or echoing")) {
            return 1;
        }
    }
    input->setText(QStringLiteral("42"));
    QKeyEvent backspace(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QApplication::sendEvent(input, &backspace);
    QKeyEvent digit(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier, QStringLiteral("1"));
    QApplication::sendEvent(input, &digit);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(input, &enter);
    if (!check(terminal.sent == QByteArray("A") && lines == QStringList{QStringLiteral("41")},
               "native input editing and Enter must send and echo corrected bytes")) {
        return 1;
    }

    lines.clear();
    terminal.receiveBackendData(QByteArray::fromHex("001b5b324a0d0aff"));
    terminal.receiveBackendData(QByteArray("**\x18" "B00"));
    if (!check(lines == QStringList{QStringLiteral("00 1B 5B 32 4A 0D 0A FF"),
                                   QStringLiteral("2A 2A 18 42 30 30")},
               "control bytes and transfer handshake bytes must display literally as hex")) {
        return 1;
    }
    for (int value = 0; value < 256; ++value) {
        terminal.receiveBackendData(QByteArray(1, static_cast<char>(value)));
        if (!check(lines.last() == QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper(),
                   "all byte values must display without loss")) {
            return 1;
        }
    }
    terminal.sent.clear();
    lines.clear();
    input->setText(QStringLiteral("4"));
    const QByteArray rawBytes = QByteArray::fromHex("00ff0d0a5c72");
    if (!check(terminal.sendBinaryData(rawBytes) && terminal.sent == rawBytes &&
                       input->text() == QStringLiteral("4") &&
                       lines == QStringList{QStringLiteral("00 FF 0D 0A 5C 72")},
               "binary API must send raw bytes and echo without using or changing the input buffer")) {
        return 1;
    }
    terminal.sent.clear();
    lines.clear();
    if (!check(terminal.sendBinaryData(QByteArray()) && terminal.sent.isEmpty() && lines.isEmpty(),
               "empty binary sends must not write or echo")) {
        return 1;
    }
    terminal.disconnect();
    if (!check(!terminal.sendBinaryData(rawBytes) && terminal.sent.isEmpty() && lines.isEmpty(),
               "disconnected binary sends must fail without writing or echoing")) {
        return 1;
    }
    terminal.sendText(QStringLiteral("41\r"));
    if (!check(terminal.sent.isEmpty() && input->text() == QStringLiteral("41") && lines.isEmpty(),
               "disconnected input must be retained without sending or echoing")) {
        return 1;
    }

    session.serialConfig.dataMode = SerialDataMode::Text;
    TestSerialTerminal textTerminal(session);
    QByteArray allBytes;
    for (int value = 0; value < 256; ++value) {
        allBytes.append(static_cast<char>(value));
    }
    if (!check(textTerminal.sendBinaryData(allBytes) && textTerminal.sent == allBytes,
               "binary API must preserve every byte in text sessions too")) {
        return 1;
    }
    textTerminal.sent.clear();
    textTerminal.proxySendData(QByteArray::fromHex("41001bff0d"));
    if (!check(textTerminal.sent == QByteArray::fromHex("41001bff0d") &&
                       textTerminal.findChild<QLineEdit *>(QString(), Qt::FindDirectChildrenOnly) == nullptr,
               "text mode must preserve existing raw sending and terminal input")) {
        return 1;
    }
    return 0;
}
