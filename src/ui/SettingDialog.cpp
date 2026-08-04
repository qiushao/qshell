#include "SettingDialog.h"
#include "core/ConfigManager.h"
#include "core/McpShellEnvironment.h"
#include "qtermwidget.h"

#include <QDir>
#include <QFileDialog>
#include <QIntValidator>
#include <QMessageBox>

SettingDialog::SettingDialog(QWidget *parent) : QDialog(parent) {
    initWidgets();
    fillColorScheme();
    const auto config = ConfigManager::instance();
    const GlobalSettings settings = config->globalSettings();
    fontFamilyEdit_->setCurrentText(settings.fontFamily);
    fontSizeEdit_->setText(QString::number(settings.fontSize));
    colorSchemeEdit_->setCurrentText(settings.colorScheme);
    copyOnSelectCheckBox_->setChecked(settings.copyOnSelect);
    debugCheckBox_->setChecked(settings.debug);
    logTimestampCheckBox_->setChecked(settings.logTimestamp);
    autoSaveLogDirectoryEdit_->setText(settings.autoSaveLogDirectory);
    autoSaveLogCheckBox_->setChecked(settings.autoSaveLog);
    mcpPortEdit_->setText(QString::number(settings.mcpPort));
    mcpBearerTokenEdit_->setText(settings.mcpBearerToken);
    mcpEnabledCheckBox_->setChecked(settings.mcpEnabled);
}

SettingDialog::~SettingDialog() = default;

void SettingDialog::initWidgets() {
    mainLayout_ = new QVBoxLayout(this);
    setLayout(mainLayout_);

    formLayout_ = new QFormLayout(this);
    mainLayout_->addLayout(formLayout_);

    fontFamilyEdit_ = new QFontComboBox(this);
    formLayout_->addRow(tr("Font Family:"), fontFamilyEdit_);

    fontSizeEdit_ = new QLineEdit(this);
    formLayout_->addRow(tr("Font Size:"), fontSizeEdit_);

    colorSchemeEdit_ = new QComboBox(this);
    formLayout_->addRow(tr("Color Scheme:"), colorSchemeEdit_);

    copyOnSelectCheckBox_ = new QCheckBox(this);
    copyOnSelectCheckBox_->setToolTip(tr("Automatically copy selected text to clipboard"));
    formLayout_->addRow(tr("Copy on Select:"), copyOnSelectCheckBox_);

    debugCheckBox_ = new QCheckBox(this);
    debugCheckBox_->setToolTip(tr("Enable debug log console"));
    formLayout_->addRow(tr("Enable debug"), debugCheckBox_);

    logTimestampCheckBox_ = new QCheckBox(this);
    logTimestampCheckBox_->setToolTip(tr("Add a system timestamp before each saved log line"));
    formLayout_->addRow(tr("Log Timestamp:"), logTimestampCheckBox_);

    autoSaveLogCheckBox_ = new QCheckBox(this);
    autoSaveLogCheckBox_->setToolTip(tr("Automatically save logs for newly opened sessions"));
    formLayout_->addRow(tr("Auto Save Log:"), autoSaveLogCheckBox_);

    autoSaveLogDirectoryEdit_ = new QLineEdit(this);
    autoSaveLogDirectoryEdit_->setReadOnly(true);
    selectAutoSaveLogDirectoryButton_ = new QPushButton(tr("Browse..."), this);
    autoSaveLogDirectoryLayout_ = new QHBoxLayout();
    autoSaveLogDirectoryLayout_->addWidget(autoSaveLogDirectoryEdit_);
    autoSaveLogDirectoryLayout_->addWidget(selectAutoSaveLogDirectoryButton_);
    formLayout_->addRow(tr("Log Directory:"), autoSaveLogDirectoryLayout_);

    mcpEnabledCheckBox_ = new QCheckBox(this);
    mcpEnabledCheckBox_->setToolTip(tr("Enable local MCP control endpoint on 127.0.0.1"));
    formLayout_->addRow(tr("Enable MCP:"), mcpEnabledCheckBox_);

    mcpPortEdit_ = new QLineEdit(this);
    mcpPortEdit_->setValidator(new QIntValidator(1, 65535, mcpPortEdit_));
    formLayout_->addRow(tr("MCP Port:"), mcpPortEdit_);

    mcpBearerTokenEdit_ = new QLineEdit(this);
    mcpBearerTokenEdit_->setReadOnly(true);
    mcpBearerTokenEdit_->setMinimumWidth(420);
    regenerateMcpTokenButton_ = new QPushButton(tr("Regenerate"), this);
    mcpTokenLayout_ = new QHBoxLayout();
    mcpTokenLayout_->addWidget(mcpBearerTokenEdit_);
    mcpTokenLayout_->addWidget(regenerateMcpTokenButton_);
    formLayout_->addRow(tr("MCP Token:"), mcpTokenLayout_);

    buttonLayout_ = new QHBoxLayout(this);
    mainLayout_->addLayout(buttonLayout_);

    okButton_ = new QPushButton(tr("OK"));
    cancelButton_ = new QPushButton(tr("Cancel"));
    buttonLayout_->addWidget(cancelButton_);
    buttonLayout_->addWidget(okButton_);

    QObject::connect(okButton_, &QPushButton::clicked, this, &SettingDialog::onOK);
    QObject::connect(cancelButton_, &QPushButton::clicked, this, &SettingDialog::onCancel);
    const auto selectAutoSaveLogDirectory = [this]() {
        const QString currentDirectory = autoSaveLogDirectoryEdit_->text().isEmpty()
                                                 ? QDir::homePath()
                                                 : autoSaveLogDirectoryEdit_->text();
        const QString directory = QFileDialog::getExistingDirectory(
                this, tr("Select Log Directory"), currentDirectory);
        if (!directory.isEmpty()) {
            autoSaveLogDirectoryEdit_->setText(directory);
        }
    };
    QObject::connect(selectAutoSaveLogDirectoryButton_, &QPushButton::clicked,
                     this, selectAutoSaveLogDirectory);
    QObject::connect(autoSaveLogCheckBox_, &QCheckBox::toggled, this,
                     [this, selectAutoSaveLogDirectory](bool enabled) {
                         autoSaveLogDirectoryEdit_->setEnabled(enabled);
                         selectAutoSaveLogDirectoryButton_->setEnabled(enabled);
                         if (enabled && autoSaveLogDirectoryEdit_->text().isEmpty()) {
                             selectAutoSaveLogDirectory();
                             if (autoSaveLogDirectoryEdit_->text().isEmpty()) {
                                 autoSaveLogCheckBox_->setChecked(false);
                             }
                         }
                     });
    autoSaveLogDirectoryEdit_->setEnabled(false);
    selectAutoSaveLogDirectoryButton_->setEnabled(false);
    QObject::connect(regenerateMcpTokenButton_, &QPushButton::clicked, this, [this]() {
        mcpBearerTokenEdit_->setText(ConfigManager::generateMcpBearerToken());
    });
    QObject::connect(mcpEnabledCheckBox_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled && mcpBearerTokenEdit_->text().isEmpty()) {
            mcpBearerTokenEdit_->setText(ConfigManager::generateMcpBearerToken());
        }
    });
}

void SettingDialog::fillColorScheme() {
    QStringList schemes = QTermWidget::availableColorSchemes();
    for (const QString &scheme: schemes) {
        colorSchemeEdit_->addItem(scheme, scheme);
    }
}

void SettingDialog::onOK() {
    GlobalSettings settings;
    settings.fontFamily = fontFamilyEdit_->currentText();
    settings.fontSize = fontSizeEdit_->text().toInt();
    settings.colorScheme = colorSchemeEdit_->currentText();
    settings.copyOnSelect = copyOnSelectCheckBox_->isChecked();
    settings.debug = debugCheckBox_->isChecked();
    settings.logTimestamp = logTimestampCheckBox_->isChecked();
    settings.autoSaveLog = autoSaveLogCheckBox_->isChecked();
    settings.autoSaveLogDirectory = autoSaveLogDirectoryEdit_->text();
    settings.mcpEnabled = mcpEnabledCheckBox_->isChecked();
    settings.mcpPort = mcpPortEdit_->text().toInt();
    if (settings.mcpPort < 1 || settings.mcpPort > 65535) {
        settings.mcpPort = 8765;
    }
    settings.mcpBearerToken = mcpBearerTokenEdit_->text().trimmed();
    if (settings.mcpEnabled && settings.mcpBearerToken.isEmpty()) {
        settings.mcpBearerToken = ConfigManager::generateMcpBearerToken();
    }
#if defined(Q_OS_UNIX)
    if (settings.mcpEnabled) {
        QString errorMessage;
        if (!McpShellEnvironment::configure(settings.mcpBearerToken, &errorMessage)) {
            QMessageBox::warning(this, tr("MCP Environment Setup"), errorMessage);
        }
    }
#endif
    ConfigManager::instance()->setGlobalSettings(settings);
    close();
}

void SettingDialog::onCancel() {
    close();
}
