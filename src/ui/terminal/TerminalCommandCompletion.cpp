#include "TerminalCommandCompletion.h"

#include "Screen.h"
#include "ScreenWindow.h"
#include "TerminalDisplay.h"
#include "core/CommandHistory.h"
#include "ui/command/CommandCompletionPopup.h"
#include "util/TerminalCharacterDecoder.h"
#include <QKeyEvent>
#include <QTextBoundaryFinder>
#include <QTextStream>

namespace {
QString rowText(Screen *screen, int row, int start, int count) {
    QVector<Character> cells(screen->getColumns());
    screen->getImage(cells.data(), static_cast<int>(cells.size()), row, row);
    QString text;
    QTextStream stream(&text);
    PlainTextDecoder decoder;
    decoder.begin(&stream);
    decoder.decodeLine(cells.constData() + start, count, 0);
    decoder.end();
    return text;
}
}// namespace

TerminalCommandCompletion::TerminalCommandCompletion(TerminalDisplay *display)
    : QObject(display), display_(display), completion_(new CommandCompletionPopup(display)) {
    display_->installEventFilter(this);
    connect(screen(), &Screen::onNewLine, this, [this]() {
        if (submitted_) afterOutput();
    });
    connect(completion_, &CommandCompletionPopup::commandSelected, this, [this](const QString &command) {
        QString text = command;
        if (text.contains('\n') || text.contains('\r')) {
            if (!display_->bracketedPasteMode() || display_->bracketedPasteModeIsDisabled()) return;
            display_->bracketText(text);
        }
        const QString current = inputText();
        if (current.isEmpty() || submitted_) return;
        // Move to the end before erasing, so completion also works after cursor edits.
        QByteArray replacement("\x05");
        QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, current);
        while (boundaries.toNextBoundary() >= 0) replacement.append('\x7f');
        replacement.append(text.toUtf8());
        accepting_ = true;
        emit replaceInput(replacement);
    });
}

Screen *TerminalCommandCompletion::screen() const {
    return display_->screenWindow()->screen();
}

void TerminalCommandCompletion::reset() {
    startRow_ = -1;
    prefix_.clear();
    submitted_ = false;
    accepting_ = false;
    browsingHistory_ = false;
    completion_->hide();
}

QString TerminalCommandCompletion::inputText() const {
    auto *current = screen();
    if (startRow_ < 0 || !current->hasScroll() || display_->applicationCursorKeysMode() ||
        startRow_ >= current->getHistLines() + current->getLines() ||
        startColumn_ >= current->getColumns() ||
        rowText(current, startRow_, 0, startColumn_) != prefix_) return {};

    QString text;
    qsizetype cursorTextLength = 0;
    const int cursorRow = current->getHistLines() + current->getCursorY();
    int row = startRow_;
    int column = startColumn_;
    do {
        if (row == cursorRow && current->getCursorX() >= column) {
            cursorTextLength = text.size() + rowText(current, row, column, current->getCursorX() - column).size();
        }
        text += rowText(current, row, column, current->getColumns() - column);
        const bool wrapped = current->getLineProperties(row, row).first() & LINE_WRAPPED;
        if (!wrapped) break;
        ++row;
        column = 0;
    } while (row < current->getHistLines() + current->getLines());
    // Only trim screen padding beyond the cursor; spaces before it belong to the input.
    while (text.size() > cursorTextLength && text.endsWith(QLatin1Char(' '))) text.chop(1);
    return text;
}

void TerminalCommandCompletion::beforeSend(const QByteArray &data) {
    if (accepting_) return;
    // Vim can use application cursor keys without switching to the alternate screen.
    if (!screen()->hasScroll() || display_->applicationCursorKeysMode() || data.contains('\x03') || data.contains('\x04')) {
        reset();
        return;
    }
    if (submitted_) {
        afterOutput();
        if (submitted_) reset();
    }
    if (startRow_ < 0 && !data.isEmpty() && data != "\r" && data != "\n") {
        auto *current = screen();
        startRow_ = current->getHistLines() + current->getCursorY();
        startColumn_ = current->getCursorX();
        prefix_ = rowText(current, startRow_, 0, startColumn_);
    }
    if (data.contains('\r') || data.contains('\n')) {
        submitted_ = startRow_ >= 0;
        completion_->hide();
    }
}

void TerminalCommandCompletion::afterOutput() {
    auto *current = screen();
    if (!current->hasScroll() || display_->applicationCursorKeysMode()) {
        reset();
        return;
    }
    if (startRow_ < 0) return;
    if (submitted_) {
        int endRow = startRow_;
        const int lastRow = current->getHistLines() + current->getLines() - 1;
        while (endRow < lastRow && (current->getLineProperties(endRow, endRow).first() & LINE_WRAPPED)) ++endRow;
        if (current->getHistLines() + current->getCursorY() > endRow) {
            // Only save echoed input. Password prompts with echo disabled stay empty.
            CommandHistory::instance().add(inputText());
            reset();
        }
        return;
    }
    if (!accepting_ && !browsingHistory_) {
        const QRect cursor(display_->getCursorX() * display_->fontWidth() + display_->margin(),
                           display_->getCursorY() * display_->fontHeight() + display_->margin(),
                           display_->fontWidth(), display_->fontHeight());
        completion_->updateMatches(inputText(), cursor);
    }
}

bool TerminalCommandCompletion::eventFilter(QObject *watched, QEvent *event) {
    if (watched == display_) {
        if (event->type() == QEvent::KeyPress) {
            if (!screen()->hasScroll() || display_->applicationCursorKeysMode()) {
                reset();
                return false;
            }
            auto *key = dynamic_cast<QKeyEvent *>(event);
            if (completion_->handleKey(key)) return true;
            // Keep shell history navigation in the shell until the user edits the input.
            if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
                browsingHistory_ = true;
                completion_->hide();
            } else if (key->key() == Qt::Key_Backspace || key->key() == Qt::Key_Delete ||
                       (!key->text().isEmpty() && key->text().front().isPrint())) {
                browsingHistory_ = false;
            }
            accepting_ = false;
        } else if (event->type() == QEvent::Resize || event->type() == QEvent::Hide) {
            reset();
        }
    }
    return QObject::eventFilter(watched, event);
}
