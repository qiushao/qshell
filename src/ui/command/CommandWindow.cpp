#include "CommandWindow.h"
#include "CommandHistoryDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QKeyEvent>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QDebug>

CommandWindow::CommandWindow(QWidget *parent)
    : QWidget(parent) {

    // 设置历史文件路径
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configPath);
    historyFilePath_ = configPath + "/command_history.txt";

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

    // 加载历史记录
    loadHistory();
}

CommandWindow::~CommandWindow() {
    saveHistory();
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

void CommandWindow::loadHistory() {
    QFile file(historyFilePath_);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        in.setEncoding(QStringConverter::Utf8);
#else
        in.setCodec("UTF-8");
#endif
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (!line.isEmpty()) {
                history_.append(line);
            }
        }
        file.close();
    }

    // 限制历史记录大小
    while (history_.size() > MAX_HISTORY_SIZE) {
        history_.removeFirst();
    }

    historyIndex_ = history_.size();
    qDebug() << "Loaded" << history_.size() << "history entries from" << historyFilePath_;
}

void CommandWindow::saveHistory() {
    QFile file(historyFilePath_);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        out.setEncoding(QStringConverter::Utf8);
#else
        out.setCodec("UTF-8");
#endif
        for (const QString &cmd : history_) {
            out << cmd << "\n";
        }
        file.close();
        qDebug() << "Saved" << history_.size() << "history entries to" << historyFilePath_;
    } else {
        qWarning() << "Failed to save command history to" << historyFilePath_;
    }
}

void CommandWindow::addToHistory(const QString &command) {
    QString trimmedCmd = command.trimmed();
    if (trimmedCmd.isEmpty()) {
        return;
    }

    // 避免重复添加相同的命令（与上一条相同）
    if (!history_.isEmpty() && history_.last() == trimmedCmd) {
        historyIndex_ = history_.size();
        return;
    }

    history_.append(trimmedCmd);

    // 限制历史记录大小
    while (history_.size() > MAX_HISTORY_SIZE) {
        history_.removeFirst();
    }

    historyIndex_ = history_.size();
    currentInput_.clear();

    // 定期保存
    if (history_.size() % 10 == 0) {
        saveHistory();
    }
}

void CommandWindow::navigateHistory(int direction) {
    if (history_.isEmpty()) {
        return;
    }

    // 第一次按上键时，保存当前输入
    if (historyIndex_ == history_.size() && direction < 0) {
        currentInput_ = commandEditor_->toPlainText();
    }

    auto newIndex = historyIndex_ + direction;

    if (newIndex < 0) {
        newIndex = 0;
    } else if (newIndex > history_.size()) {
        newIndex = history_.size();
    }

    historyIndex_ = newIndex;

    if (historyIndex_ == history_.size()) {
        // 恢复用户原来的输入
        commandEditor_->setPlainText(currentInput_);
    } else {
        commandEditor_->setPlainText(history_[static_cast<int>(historyIndex_)]);
    }

    // 将光标移到末尾
    QTextCursor cursor = commandEditor_->textCursor();
    cursor.movePosition(QTextCursor::End);
    commandEditor_->setTextCursor(cursor);
}

void CommandWindow::showHistoryDialog() {
    CommandHistoryDialog dialog(history_, this);

    connect(&dialog, &CommandHistoryDialog::commandSelected,
            this, [this](const QString &command) {
        commandEditor_->setPlainText(command);
        QTextCursor cursor = commandEditor_->textCursor();
        cursor.movePosition(QTextCursor::End);
        commandEditor_->setTextCursor(cursor);
        commandEditor_->setFocus();
    });

    connect(&dialog, &CommandHistoryDialog::clearHistoryRequested,
            this, &CommandWindow::clearHistory);

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
        history_.clear();
        historyIndex_ = 0;
        currentInput_.clear();
        saveHistory();
        qDebug() << "Command history cleared";
    }
}

bool CommandWindow::eventFilter(QObject *obj, QEvent *e) {
    if (obj == commandEditor_ && e->type() == QEvent::KeyPress) {
        auto *event = dynamic_cast<QKeyEvent *>(e);

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
