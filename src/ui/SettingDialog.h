#ifndef QSHELL_SETTINGDIALOG_H
#define QSHELL_SETTINGDIALOG_H

#include <QFormLayout>
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QFontComboBox>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QHBoxLayout>


class SettingDialog : public QDialog  {
    Q_OBJECT
    public:
    explicit SettingDialog(QWidget *parent);
    ~SettingDialog() override;


private:
    void initWidgets();
    void fillColorScheme();
    void onOK();
    void onCancel();

    QVBoxLayout *mainLayout_ = nullptr;
    QFormLayout *formLayout_ = nullptr;

    QFontComboBox *fontFamilyEdit_ = nullptr;
    QLineEdit *fontSizeEdit_ = nullptr;
    QComboBox *colorSchemeEdit_ = nullptr;
    QCheckBox *copyOnSelectCheckBox_ = nullptr;
    QCheckBox *debugCheckBox_ = nullptr;
    QCheckBox *logTimestampCheckBox_ = nullptr;
    QCheckBox *autoSaveLogCheckBox_ = nullptr;
    QLineEdit *autoSaveLogDirectoryEdit_ = nullptr;
    QPushButton *selectAutoSaveLogDirectoryButton_ = nullptr;
    QHBoxLayout *autoSaveLogDirectoryLayout_ = nullptr;
    QCheckBox *mcpEnabledCheckBox_ = nullptr;
    QLineEdit *mcpPortEdit_ = nullptr;
    QLineEdit *mcpBearerTokenEdit_ = nullptr;
    QPushButton *regenerateMcpTokenButton_ = nullptr;
    QHBoxLayout *mcpTokenLayout_ = nullptr;

    QHBoxLayout *buttonLayout_ = nullptr;
    QPushButton *okButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
};


#endif //QSHELL_SETTINGDIALOG_H
