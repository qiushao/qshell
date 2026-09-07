#ifndef TIMESTAMPDISPLAY_H
#define TIMESTAMPDISPLAY_H

#include <QWidget>

class TerminalDisplay;

// A separate gutter: timestamps never become terminal characters.
class TimestampDisplay : public QWidget {
public:
    explicit TimestampDisplay(TerminalDisplay *display, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateFont();
    TerminalDisplay *display_;
};

#endif // TIMESTAMPDISPLAY_H
