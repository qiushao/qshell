#ifndef QSHELL_TERMINALCOMMANDCOMPLETION_H
#define QSHELL_TERMINALCOMMANDCOMPLETION_H

#include <QByteArray>
#include <QObject>
#include <QString>

class CommandCompletionPopup;
class TerminalDisplay;
class Screen;

class TerminalCommandCompletion : public QObject {
    Q_OBJECT
public:
    explicit TerminalCommandCompletion(TerminalDisplay *display);
    void beforeSend(const QByteArray &data);
    void afterOutput();
    void reset();

signals:
    void replaceInput(const QByteArray &data);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString inputText() const;
    Screen *screen() const;
    TerminalDisplay *display_;
    CommandCompletionPopup *completion_;
    int startRow_ = -1;
    int startColumn_ = 0;
    QString prefix_;
    bool submitted_ = false;
    bool accepting_ = false;
    bool browsingHistory_ = false;
};

#endif
