#include "TimestampDisplay.h"

#include "Screen.h"
#include "ScreenWindow.h"
#include "TerminalDisplay.h"
#include <QEvent>
#include <QPainter>

TimestampDisplay::TimestampDisplay(TerminalDisplay *display, QWidget *parent)
    : QWidget(parent), display_(display) {
    setObjectName(QStringLiteral("terminalTimestampDisplay"));
    setFocusPolicy(Qt::NoFocus);
    display_->installEventFilter(this);
    connect(display_->screenWindow(), &ScreenWindow::outputChanged,
            this, qOverload<>(&QWidget::update));
    connect(display_->screenWindow(), &ScreenWindow::scrolled,
            this, qOverload<>(&QWidget::update));
    connect(display_, &TerminalDisplay::changedFontMetricSignal,
            this, [this]() { updateFont(); });
    updateFont();
}

void TimestampDisplay::updateFont() {
    setFont(display_->font());
    setFixedWidth(fontMetrics().horizontalAdvance(QStringLiteral("00-00 00:00:00")) + 12);
    update();
}

bool TimestampDisplay::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Paint || event->type() == QEvent::Resize)
        update();
    return QWidget::eventFilter(watched, event);
}

void TimestampDisplay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), display_->colorTable()[DEFAULT_BACK_COLOR].color);
    painter.setPen(display_->colorTable()[DEFAULT_FORE_COLOR].color);
    painter.setFont(font());
    const auto *window = display_->screenWindow();
    const int rowHeight = display_->fontHeight();
    const int baseline = display_->textTop() + fontMetrics().ascent() + display_->lineSpacing();
    for (int row = 0; row < window->windowLines(); ++row) {
        painter.drawText(6, baseline + row * rowHeight,
                         window->screen()->lineTimestamp(window->currentLine() + row));
    }
}
