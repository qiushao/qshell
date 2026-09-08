#include "SerialTerminal.h"
#include "qtermwidget.h"
#include <QDebug>
#include <QEvent>
#include <QLineEdit>
#include <QRegularExpression>
#include <QToolTip>
#include <QVBoxLayout>
#include <QtWidgets/QMessageBox>

SerialTerminal::SerialTerminal(const SessionData &session, QWidget *parent) : BaseTerminal(parent) {
    sessionData_ = session;

    serial_ = new QSerialPort(this);

    if (isBinarySerial()) {
        binaryInput_ = new QLineEdit(this);
        binaryInput_->setPlaceholderText(tr("Hex bytes, e.g. 41 00 FF (Enter to send)"));
        binaryInput_->setToolTip(binaryInput_->placeholderText());
        layout()->addWidget(binaryInput_);
        setFocusProxy(binaryInput_);
        binaryInput_->installEventFilter(this);
        QObject::connect(binaryInput_, &QLineEdit::returnPressed, this, &SerialTerminal::sendBinaryInput);
    }

    // 把串口传过来的数据传给终端
    QObject::connect(serial_, &QSerialPort::readyRead, [this]() {
        const QByteArray data = serial_->readAll();
        receiveBackendData(data);
    });

    // 串口发生错误时的回调处理
    QObject::connect(serial_, &QSerialPort::errorOccurred, this, &SerialTerminal::handleError);
}

SerialTerminal::~SerialTerminal() {
    delete serial_;
}

void SerialTerminal::connect() {
    serial_->setPortName(sessionData_.serialConfig.portName);
    serial_->setBaudRate(sessionData_.serialConfig.baudRate);
    serial_->setDataBits(static_cast<QSerialPort::DataBits>(sessionData_.serialConfig.dataBits));
    serial_->setParity(static_cast<QSerialPort::Parity>(sessionData_.serialConfig.parity));
    serial_->setStopBits(static_cast<QSerialPort::StopBits>(sessionData_.serialConfig.stopBits));
    serial_->setFlowControl(static_cast<QSerialPort::FlowControl>(sessionData_.serialConfig.flowControl));
    connect_ = serial_->open(QIODevice::ReadWrite);
    if (connect_) {
        qDebug() << "open serial " << sessionData_.name << " sucess";
    } else {
        QMessageBox::critical(this, tr("Error"), serial_->errorString());
    }
}

void SerialTerminal::disconnect() {
    connect_ = false;
    if (binaryInput_ != nullptr) {
        binaryInput_->clear();
    }
    if (serial_->isOpen()) {
        serial_->close();
    }
}

void SerialTerminal::handleError(QSerialPort::SerialPortError error) {
    if (error != QSerialPort::SerialPortError::NoError) {
        emit onSessionError(this);
    }
}

void SerialTerminal::writeToBackend(const QByteArray &data) {
    if (serial_->isOpen()) {
        serial_->write(data);
    }
}

void SerialTerminal::sendUserData(const QByteArray &data) {
    if (binaryInput_ == nullptr) {
        BaseTerminal::sendUserData(data);
        return;
    }

    binaryInput_->setFocus();
    for (const char character : data) {
        if (character == '\r' || character == '\n') {
            sendBinaryInput();
        } else if (character == '\b' || character == '\x7f') {
            binaryInput_->backspace();
        } else if (character == '\x03') {
            binaryInput_->clear();
        } else {
            binaryInput_->insert(QString(QChar::fromLatin1(character)));
        }
    }
}

void SerialTerminal::sendBinaryInput() {
    const QString text = binaryInput_->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    static const QRegularExpression hexBytes(QStringLiteral("\\A(?:[0-9A-Fa-f]{2}\\s*)+\\z"));
    if (!hexBytes.match(text).hasMatch()) {
        QToolTip::showText(binaryInput_->mapToGlobal(QPoint(0, binaryInput_->height())),
                          tr("Enter complete hexadecimal bytes, e.g. 41 00 FF."), binaryInput_);
        return;
    }
    if (!isConnect()) {
        QToolTip::showText(binaryInput_->mapToGlobal(QPoint(0, binaryInput_->height())),
                          tr("Serial port is not connected."), binaryInput_);
        return;
    }
    if (sendBinaryData(QByteArray::fromHex(text.toLatin1()))) {
        binaryInput_->clear();
    }
}

bool SerialTerminal::eventFilter(QObject *watched, QEvent *event) {
    if (watched == binaryInput_ && event->type() == QEvent::FocusIn) {
        emit activated(this);
    }
    return BaseTerminal::eventFilter(watched, event);
}
