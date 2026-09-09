#include "CommandWindow.h"
#include <QEvent>
#include "CommandHistoryDialog.h"
#include "CommandCompletionPopup.h"
#include "core/CommandHistory.h"
#include <QSignalBlocker>
#include <algorithm>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QLabel>
#include <QDialogButtonBox>

CommandWindow::CommandWindow(QWidget *parent)
    : QWidget(parent) {

    // 创建编辑器
    commandEditor_ = new QTextEdit(this);
    commandEditor_->setMinimumHeight(30);
    commandEditor_->setPlaceholderText(tr("Enter command here... (Press Enter to send, Ctrl+Enter for new line)"));
    commandEditor_->installEventFilter(this);

    // 布局
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(commandEditor_);

    // 设置右键菜单
    setupContextMenu();

    historyIndex_ = -1;
    connect(&CommandHistory::instance(), &CommandHistory::changed, this, [this]() {
        historyIndex_ = -1;
    });
    completion_ = new CommandCompletionPopup(commandEditor_);
    connect(commandEditor_, &QTextEdit::textChanged, this, [this]() {
        historyIndex_ = -1;
        completion_->updateMatches(commandEditor_->toPlainText(), commandEditor_->cursorRect());
    });
    connect(completion_, &CommandCompletionPopup::commandSelected, this, [this](const QString &command) {
        const QSignalBlocker blocker(commandEditor_);
        commandEditor_->setPlainText(command);
        commandEditor_->moveCursor(QTextCursor::End);
    });
}

void CommandWindow::setupContextMenu() {
    commandEditor_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(commandEditor_, &QTextEdit::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        QMenu *menu = commandEditor_->createStandardContextMenu();
        menu->addSeparator();

        QAction *historyAction = menu->addAction(QIcon::fromTheme("view-history"),
                                                  tr("View History..."));
        historyAction->setShortcut(QKeySequence("Ctrl+H"));
        connect(historyAction, &QAction::triggered, this, &CommandWindow::showHistoryDialog);

        const QAction *clearHistoryAction = menu->addAction(QIcon::fromTheme("edit-clear"),
                                                       tr("Clear History"));
        connect(clearHistoryAction, &QAction::triggered, this, &CommandWindow::clearHistory);

        menu->exec(commandEditor_->mapToGlobal(pos));
        delete menu;
    });
}

QWidget *CommandWindow::widget() const {
    return commandEditor_;
}

void CommandWindow::addToHistory(const QString &command) {
    CommandHistory::instance().add(command);
    historyIndex_ = -1;
    currentInput_.clear();
}

void CommandWindow::navigateHistory(int direction) {
    const auto &history = CommandHistory::instance().commands();
    historyIndex_ = historyIndex_ < 0 ? history.size() : std::min(historyIndex_, history.size());
    completion_->hide();
    const QSignalBlocker blocker(commandEditor_);
    if (history.isEmpty()) {
        return;
    }

    // 第一次按上键时，保存当前输入
    if (historyIndex_ == history.size() && direction < 0) {
        currentInput_ = commandEditor_->toPlainText();
    }

    auto newIndex = historyIndex_ + direction;

    if (newIndex < 0) {
        newIndex = 0;
    } else if (newIndex > history.size()) {
        newIndex = history.size();
    }

    historyIndex_ = newIndex;

    if (historyIndex_ == history.size()) {
        // 恢复用户原来的输入
        commandEditor_->setPlainText(currentInput_);
        historyIndex_ = -1;
    } else {
        commandEditor_->setPlainText(history[historyIndex_]);
    }

    // 将光标移到末尾
    QTextCursor cursor = commandEditor_->textCursor();
    cursor.movePosition(QTextCursor::End);
    commandEditor_->setTextCursor(cursor);
}

void CommandWindow::showHistoryDialog() {
    completion_->hide();
    CommandHistoryDialog dialog(this);

    connect(&dialog, &CommandHistoryDialog::commandSelected,
            this, [this](const QString &command) {
        const QSignalBlocker blocker(commandEditor_);
        commandEditor_->setPlainText(command);
        QTextCursor cursor = commandEditor_->textCursor();
        cursor.movePosition(QTextCursor::End);
        commandEditor_->setTextCursor(cursor);
        commandEditor_->setFocus();
    });

    dialog.exec();
}

void CommandWindow::clearHistory() {
    auto result = QMessageBox::question(
        this,
        tr("Clear History"),
        tr("Are you sure you want to clear all command history?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (result == QMessageBox::Yes) {
        CommandHistory::instance().clear();
        completion_->hide();
        historyIndex_ = -1;
        currentInput_.clear();
        qDebug() << "Command history cleared";
    }
}

bool CommandWindow::eventFilter(QObject *obj, QEvent *e) {
    if (obj == commandEditor_ && e->type() == QEvent::KeyPress) {
        auto *event = dynamic_cast<QKeyEvent *>(e);
        if (completion_->handleKey(event)) {
            return true;
        }

        // Ctrl+Up/Down 切换历史记录
        if (event->modifiers() & Qt::ControlModifier) {
            if (event->key() == Qt::Key_Up) {
                navigateHistory(-1);
                return true;
            } else if (event->key() == Qt::Key_Down) {
                navigateHistory(1);
                return true;
            } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
                // Ctrl+Enter 换行
                commandEditor_->append("");
                return true;
            } else if (event->key() == Qt::Key_H) {
                // Ctrl+H 显示历史
                showHistoryDialog();
                return true;
            }
        }

        // Enter 发送命令
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (!(event->modifiers() & Qt::ControlModifier)) {
                QString command = commandEditor_->toPlainText();

                addToHistory(command);
                emit commandSend(command + "\r");
                commandEditor_->clear();
                return true;
            }
        }
    }

    return QWidget::eventFilter(obj, e);
}

void CommandWindow::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        commandEditor_->setPlaceholderText(tr("Enter command here... (Press Enter to send, Ctrl+Enter for new line)"));
    }
}
