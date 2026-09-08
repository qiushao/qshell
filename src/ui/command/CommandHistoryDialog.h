#ifndef QSHELL_COMMANDWINDOW_DIALOG_H
#define QSHELL_COMMANDWINDOW_DIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QStringList>
#include <QTextEdit>

class BaseTerminal;

class CommandHistoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit CommandHistoryDialog(QWidget *parent = nullptr);

signals:
    void commandSelected(const QString &command);

private:
    void onItemDoubleClicked(QListWidgetItem *item);
    void onClearHistory();
    void onDeleteSelected();
    void reloadHistory();

    QListWidget *listWidget_ = nullptr;
};

#endif// QSHELL_COMMANDWINDOW_DIALOG_H
