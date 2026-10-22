#ifndef QSHELL_COMMANDWINDOW_DIALOG_H
#define QSHELL_COMMANDWINDOW_DIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QStringList>
#include <QTextEdit>

class BaseTerminal;
class QLineEdit;
class QLabel;
class QPushButton;

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
    void showPage();

    QListWidget *listWidget_ = nullptr;
    QLineEdit *searchEdit_ = nullptr;
    QLabel *pageLabel_ = nullptr;
    QPushButton *previousButton_ = nullptr;
    QPushButton *nextButton_ = nullptr;
    QStringList filteredCommands_;
    int currentPage_ = 0;
    static constexpr int pageSize_ = 20;
};

#endif// QSHELL_COMMANDWINDOW_DIALOG_H
