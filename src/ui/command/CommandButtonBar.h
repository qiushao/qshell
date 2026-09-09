#ifndef QSHELL_COMMANDBUTTONBAR_H
#define QSHELL_COMMANDBUTTONBAR_H

#include <QToolBar>
#include <QComboBox>
#include <QMap>

class QPushButton;
class QLayout;
class QWidget;

class CommandButtonBar : public QToolBar {
    Q_OBJECT
public:
    explicit CommandButtonBar(QWidget *parent = nullptr);
    ~CommandButtonBar() override = default;

    signals:
        void commandTriggered(const QString &command);

private slots:
    void onGroupChanged(int index);
    void onButtonClicked();
    void onGroupContextMenu(const QPoint &pos);
    void onAddButton();
    void onButtonContextMenu(const QPoint &pos);
    void refreshButtons();
    void refreshGroups();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

    void changeEvent(QEvent *event) override;

private:
    void setupUI();
    void clearButtons();
    void updateButtonBarHeight();

    QComboBox *groupComboBox_ = nullptr;
    QWidget *buttonContainer_ = nullptr;
    QLayout *buttonLayout_ = nullptr;
    QMap<QString, QPushButton*> buttons_;  // buttonId -> QPushButton
};

#endif // QSHELL_COMMANDBUTTONBAR_H
