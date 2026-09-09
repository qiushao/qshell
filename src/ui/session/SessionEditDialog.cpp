#include "SessionEditDialog.h"
#include "core/ConfigManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QGroupBox>
#include <QLabel>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QSerialPortInfo>

SessionEditDialog::SessionEditDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Session Properties"));
    setMinimumWidth(450);
    setupUI();
}

void SessionEditDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);

    // 通用设置区域
    mainLayout->addWidget(createGeneralSection());

    // 协议参数堆栈
    protocolStack_ = new QStackedWidget(this);

    // Local Shell 页面（空白或提示）
    auto* localShellPage = new QWidget();
    auto* localShellLayout = new QVBoxLayout(localShellPage);
    auto* localShellLabel = new QLabel(tr("No additional settings required for Local Shell."), localShellPage);
    localShellLabel->setAlignment(Qt::AlignCenter);
    localShellLabel->setStyleSheet("color: gray;");
    localShellLayout->addWidget(localShellLabel);
    localShellLayout->addStretch();
    protocolStack_->addWidget(localShellPage);

    // SSH 页面
    protocolStack_->addWidget(createSSHSection());

    // Serial 页面
    protocolStack_->addWidget(createSerialSection());

    mainLayout->addWidget(protocolStack_);

    // 弹性空间
    mainLayout->addStretch();

    // 按钮
    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (nameEdit_->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Validation Error"),
                               tr("Session name cannot be empty."));
            return;
        }
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    // 初始化协议切换
    onProtocolChanged(0);
}

QWidget* SessionEditDialog::createGeneralSection() {
    auto* groupBox = new QGroupBox(tr("General"), this);
    auto* layout = new QFormLayout(groupBox);

    nameEdit_ = new QLineEdit(groupBox);
    layout->addRow(tr("Name:"), nameEdit_);

    groupCombo_ = new QComboBox(groupBox);
    groupCombo_->addItem(tr("(No Group)"), QString());
    auto groups = ConfigManager::instance()->groups();
    for (const auto& group : groups) {
        groupCombo_->addItem(group.name, group.id);
    }
    layout->addRow(tr("Group:"), groupCombo_);

    protocolCombo_ = new QComboBox(groupBox);
    protocolCombo_->addItem(tr("Local Shell"), static_cast<int>(ProtocolType::LocalShell));
    protocolCombo_->addItem(tr("SSH"), static_cast<int>(ProtocolType::SSH));
    protocolCombo_->addItem(tr("Serial"), static_cast<int>(ProtocolType::Serial));
    connect(protocolCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SessionEditDialog::onProtocolChanged);
    layout->addRow(tr("Protocol:"), protocolCombo_);

    return groupBox;
}

QWidget* SessionEditDialog::createSSHSection() {
    auto* groupBox = new QGroupBox(tr("SSH Settings"), this);
    auto* layout = new QFormLayout(groupBox);

    hostEdit_ = new QLineEdit(groupBox);
    layout->addRow(tr("Host:"), hostEdit_);

    portSpin_ = new QSpinBox(groupBox);
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(22);
    layout->addRow(tr("Port:"), portSpin_);

    usernameEdit_ = new QLineEdit(groupBox);
    layout->addRow(tr("Username:"), usernameEdit_);

    authMethodCombo_ = new QComboBox(groupBox);
    authMethodCombo_->addItem(tr("Password"), static_cast<int>(SSHAuthMethod::Password));
    authMethodCombo_->addItem(tr("Public Key"), static_cast<int>(SSHAuthMethod::PublicKey));
    layout->addRow(tr("Auth Method:"), authMethodCombo_);

    authStack_ = new QStackedWidget(groupBox);

    // 密码认证页面
    auto* passwordPage = new QWidget();
    auto* passLayout = new QFormLayout(passwordPage);
    passLayout->setContentsMargins(0, 0, 0, 0);
    passwordEdit_ = new QLineEdit(passwordPage);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passLayout->addRow(tr("Password:"), passwordEdit_);
    authStack_->addWidget(passwordPage);

    // 密钥认证页面
    auto* keyPage = new QWidget();
    auto* keyLayout = new QFormLayout(keyPage);
    keyLayout->setContentsMargins(0, 0, 0, 0);

    auto* keyPathLayout = new QHBoxLayout();
    keyPathEdit_ = new QLineEdit(keyPage);
    auto* browseKeyBtn = new QPushButton(tr("Browse..."), keyPage);
    connect(browseKeyBtn, &QPushButton::clicked, this, &SessionEditDialog::onBrowseKeyPath);
    keyPathLayout->addWidget(keyPathEdit_);
    keyPathLayout->addWidget(browseKeyBtn);
    keyLayout->addRow(tr("Private Key:"), keyPathLayout);

    passphraseEdit_ = new QLineEdit(keyPage);
    passphraseEdit_->setEchoMode(QLineEdit::Password);
    keyLayout->addRow(tr("Passphrase:"), passphraseEdit_);
    authStack_->addWidget(keyPage);

    layout->addRow(authStack_);

    connect(authMethodCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            authStack_, &QStackedWidget::setCurrentIndex);

    return groupBox;
}

static QList<qint32> standardBaudRates() {
    return {
        1200, 2400, 4800, 9600, 19200, 38400,
        57600, 115200, 230400, 460800, 921600
    };
}

QWidget* SessionEditDialog::createSerialSection() {
    auto* groupBox = new QGroupBox(tr("Serial Settings"), this);
    auto* layout = new QFormLayout(groupBox);

    auto* portLayout = new QHBoxLayout();
    portNameCombo_ = new QComboBox(groupBox);
    portNameCombo_->setEditable(true);
    auto* refreshBtn = new QPushButton(tr("Refresh"), groupBox);
    connect(refreshBtn, &QPushButton::clicked, this, &SessionEditDialog::onRefreshPorts);
    portLayout->addWidget(portNameCombo_, 1);
    portLayout->addWidget(refreshBtn);
    layout->addRow(tr("Port:"), portLayout);

    baudRateCombo_ = new QComboBox(groupBox);
    baudRateCombo_->setEditable(true);
    for (auto rate : standardBaudRates()) {
        baudRateCombo_->addItem(QString::number(rate), rate);
    }
    baudRateCombo_->setCurrentText("115200");
    layout->addRow(tr("Baud Rate:"), baudRateCombo_);

    dataBitsCombo_ = new QComboBox(groupBox);
    dataBitsCombo_->addItem("5", 5);
    dataBitsCombo_->addItem("6", 6);
    dataBitsCombo_->addItem("7", 7);
    dataBitsCombo_->addItem("8", 8);
    dataBitsCombo_->setCurrentIndex(3);
    layout->addRow(tr("Data Bits:"), dataBitsCombo_);

    parityCombo_ = new QComboBox(groupBox);
    parityCombo_->addItem(tr("None"), static_cast<int>(Parity::None));
    parityCombo_->addItem(tr("Even"), static_cast<int>(Parity::Even));
    parityCombo_->addItem(tr("Odd"), static_cast<int>(Parity::Odd));
    parityCombo_->addItem(tr("Space"), static_cast<int>(Parity::Space));
    parityCombo_->addItem(tr("Mark"), static_cast<int>(Parity::Mark));
    layout->addRow(tr("Parity:"), parityCombo_);

    stopBitsCombo_ = new QComboBox(groupBox);
    stopBitsCombo_->addItem("1", static_cast<int>(StopBits::One));
    stopBitsCombo_->addItem("1.5", static_cast<int>(StopBits::OneAndHalf));
    stopBitsCombo_->addItem("2", static_cast<int>(StopBits::Two));
    layout->addRow(tr("Stop Bits:"), stopBitsCombo_);

    flowControlCombo_ = new QComboBox(groupBox);
    flowControlCombo_->addItem(tr("None"), static_cast<int>(FlowControl::None));
    flowControlCombo_->addItem(tr("Hardware (RTS/CTS)"), static_cast<int>(FlowControl::Hardware));
    flowControlCombo_->addItem(tr("Software (XON/XOFF)"), static_cast<int>(FlowControl::Software));
    layout->addRow(tr("Flow Control:"), flowControlCombo_);

    serialDataModeCombo_ = new QComboBox(groupBox);
    serialDataModeCombo_->setObjectName("serialDataMode");
    serialDataModeCombo_->addItem(tr("Text"), static_cast<int>(SerialDataMode::Text));
    serialDataModeCombo_->addItem(tr("Hexadecimal (bin)"), static_cast<int>(SerialDataMode::Bin));
    serialDataModeCombo_->setToolTip(tr("bin: display and enter hexadecimal bytes, e.g. 41 00 FF."));
    layout->addRow(tr("Data Mode:"), serialDataModeCombo_);

    // 初始化串口列表
    onRefreshPorts();

    return groupBox;
}

void SessionEditDialog::onProtocolChanged(int index) {
    auto type = static_cast<ProtocolType>(protocolCombo_->itemData(index).toInt());

    // 切换显示对应的协议设置面板
    switch (type) {
        case ProtocolType::LocalShell:
            protocolStack_->setCurrentIndex(0);
            break;
        case ProtocolType::SSH:
            protocolStack_->setCurrentIndex(1);
            break;
        case ProtocolType::Serial:
            protocolStack_->setCurrentIndex(2);
            break;
    }
}

void SessionEditDialog::onBrowseKeyPath() {
    QString path = QFileDialog::getOpenFileName(this, tr("Select Private Key"),
                                                QString(),
                                                tr("All Files (*)"));
    if (!path.isEmpty()) {
        keyPathEdit_->setText(path);
    }
}

void SessionEditDialog::onRefreshPorts() {
    portNameCombo_->clear();
    auto ports = QSerialPortInfo::availablePorts();
    for (const auto& port : ports) {
        portNameCombo_->addItem(port.portName(), port.portName());
    }
}

void SessionEditDialog::setSessionData(const SessionData& data) {
    sessionData_ = data;

    nameEdit_->setText(data.name);

    int groupIndex = groupCombo_->findData(data.groupId);
    groupCombo_->setCurrentIndex(groupIndex >= 0 ? groupIndex : 0);

    int protocolIndex = protocolCombo_->findData(static_cast<int>(data.protocolType));
    protocolCombo_->setCurrentIndex(protocolIndex >= 0 ? protocolIndex : 0);

    // SSH
    hostEdit_->setText(data.sshConfig.host);
    portSpin_->setValue(data.sshConfig.port);
    usernameEdit_->setText(data.sshConfig.username);
    authMethodCombo_->setCurrentIndex(static_cast<int>(data.sshConfig.authMethod));
    passwordEdit_->setText(data.sshConfig.password);
    keyPathEdit_->setText(data.sshConfig.privateKeyPath);
    passphraseEdit_->setText(data.sshConfig.passphrase);

    // Serial
    int portIndex = portNameCombo_->findData(data.serialConfig.portName);
    if (portIndex >= 0) {
        portNameCombo_->setCurrentIndex(portIndex);
    } else {
        portNameCombo_->setCurrentText(data.serialConfig.portName);
    }
    baudRateCombo_->setCurrentText(QString::number(data.serialConfig.baudRate));
    dataBitsCombo_->setCurrentIndex(dataBitsCombo_->findData(static_cast<int>(data.serialConfig.dataBits)));
    parityCombo_->setCurrentIndex(parityCombo_->findData(static_cast<int>(data.serialConfig.parity)));
    stopBitsCombo_->setCurrentIndex(stopBitsCombo_->findData(static_cast<int>(data.serialConfig.stopBits)));
    flowControlCombo_->setCurrentIndex(flowControlCombo_->findData(static_cast<int>(data.serialConfig.flowControl)));
    serialDataModeCombo_->setCurrentIndex(serialDataModeCombo_->findData(static_cast<int>(data.serialConfig.dataMode)));
}

SessionData SessionEditDialog::sessionData() const {
    SessionData data = sessionData_;

    data.name = nameEdit_->text().trimmed();
    data.groupId = groupCombo_->currentData().toString();
    data.protocolType = static_cast<ProtocolType>(protocolCombo_->currentData().toInt());

    // SSH
    data.sshConfig.host = hostEdit_->text().trimmed();
    data.sshConfig.port = portSpin_->value();
    data.sshConfig.username = usernameEdit_->text();
    data.sshConfig.authMethod = static_cast<SSHAuthMethod>(authMethodCombo_->currentData().toInt());
    data.sshConfig.password = passwordEdit_->text();
    data.sshConfig.privateKeyPath = keyPathEdit_->text();
    data.sshConfig.passphrase = passphraseEdit_->text();

    // Serial
    data.serialConfig.portName = portNameCombo_->currentData().toString();
    if (data.serialConfig.portName.isEmpty()) {
        data.serialConfig.portName = portNameCombo_->currentText();
    }
    data.serialConfig.baudRate = baudRateCombo_->currentText().toInt();
    data.serialConfig.dataBits = static_cast<DataBits>(dataBitsCombo_->currentData().toInt());
    data.serialConfig.parity = static_cast<Parity>(parityCombo_->currentData().toInt());
    data.serialConfig.stopBits = static_cast<StopBits>(stopBitsCombo_->currentData().toInt());
    data.serialConfig.flowControl = static_cast<FlowControl>(flowControlCombo_->currentData().toInt());
    data.serialConfig.dataMode = static_cast<SerialDataMode>(serialDataModeCombo_->currentData().toInt());

    return data;
}

void SessionEditDialog::setGroupId(const QString& groupId) {
    int index = groupCombo_->findData(groupId);
    if (index >= 0) {
        groupCombo_->setCurrentIndex(index);
    }
}
