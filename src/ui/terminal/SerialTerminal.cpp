#include "qtermwidget.h"
#include <QDebug>
#include <QtWidgets/QMessageBox>
#include "SerialTerminal.h"

SerialTerminal::SerialTerminal(const SessionData &session, QWidget *parent) : BaseTerminal(parent) {
    sessionData_ = session;

    serial_ = new QSerialPort(this);

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
    if (serial_->open(QIODevice::ReadWrite)) {
        qDebug() << "open serial " << sessionData_.name << " sucess";
    } else {
        QMessageBox::critical(this, tr("Error"), serial_->errorString());
    }

    connect_ = true;
}

void SerialTerminal::disconnect() {
    connect_ = false;
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
