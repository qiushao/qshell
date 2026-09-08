#ifndef QSHELL_COMMANDCOMPLETIONPOPUP_H
#define QSHELL_COMMANDCOMPLETIONPOPUP_H

#include <QObject>
#include <QRect>
#include <QStringList>

class QKeyEvent;
class QListWidget;
class QWidget;

class CommandCompletionPopup : public QObject {
    Q_OBJECT
public:
    explicit CommandCompletionPopup(QWidget *editor);
    void updateMatches(const QString &query, const QRect &cursorRect);
    bool handleKey(QKeyEvent *event);
    void hide();

signals:
    void commandSelected(const QString &command);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void acceptCurrent();
    QWidget *editor_;
    QListWidget *list_;
    QString query_;
    QRect cursorRect_;
};

#endif
