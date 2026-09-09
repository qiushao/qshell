#ifndef QSHELL_SERIAL_TERMINAL_H
#define QSHELL_SERIAL_TERMINAL_H

#include "BaseTerminal.h"
#include "core/datatype.h"
#include <QtSerialPort/QSerialPort>

class QLineEdit;

class SerialTerminal : public BaseTerminal {
    Q_OBJECT
public:
    explicit SerialTerminal(const SessionData &session, QWidget *parent);
    ~SerialTerminal() override;
    void connect() override;
    void disconnect() override;

protected:
    void sendUserData(const QByteArray &data) override;
    void writeToBackend(const QByteArray &data) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    void changeEvent(QEvent *event) override;

private:
    void handleError(QSerialPort::SerialPortError error);
    void sendBinaryInput();
    QSerialPort *serial_ = nullptr;
    QLineEdit *binaryInput_ = nullptr;
};


#endif //QSHELL_SERIAL_TERMINAL_H
