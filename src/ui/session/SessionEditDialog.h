#ifndef SESSIONDIALOG_H
#define SESSIONDIALOG_H

#include "core/datatype.h"
#include <QDialog>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QStackedWidget;
class QGroupBox;

class SessionEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit SessionEditDialog(QWidget* parent = nullptr);

    void setSessionData(const SessionData& data);
    SessionData sessionData() const;
    void setGroupId(const QString& groupId);

private slots:
    void onProtocolChanged(int index);
    void onBrowseKeyPath();
    void onRefreshPorts();

private:
    void setupUI();
    QWidget* createGeneralSection();
    QWidget* createSSHSection();
    QWidget* createSerialSection();

    SessionData sessionData_;

    // 通用
    QLineEdit* nameEdit_ = nullptr;
    QComboBox* groupCombo_ = nullptr;
    QComboBox* protocolCombo_ = nullptr;

    // 协议参数区域
    QStackedWidget* protocolStack_ = nullptr;

    // SSH
    QLineEdit* hostEdit_ = nullptr;
    QSpinBox* portSpin_ = nullptr;
    QLineEdit* usernameEdit_ = nullptr;
    QComboBox* authMethodCombo_ = nullptr;
    QStackedWidget* authStack_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QLineEdit* keyPathEdit_ = nullptr;
    QLineEdit* passphraseEdit_ = nullptr;

    // 串口
    QComboBox* portNameCombo_ = nullptr;
    QComboBox* baudRateCombo_ = nullptr;
    QComboBox* dataBitsCombo_ = nullptr;
    QComboBox* parityCombo_ = nullptr;
    QComboBox* stopBitsCombo_ = nullptr;
    QComboBox* flowControlCombo_ = nullptr;
    QComboBox* serialDataModeCombo_ = nullptr;
};

#endif // SESSIONDIALOG_H
