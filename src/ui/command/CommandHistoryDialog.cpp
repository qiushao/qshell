#include "CommandHistoryDialog.h"
#include "core/CommandHistory.h"
#include <QShortcut>

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

CommandHistoryDialog::CommandHistoryDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Command History"));
    setMinimumSize(500, 400);

    auto *layout = new QVBoxLayout(this);

    // 历史记录列表
    listWidget_ = new QListWidget(this);
    listWidget_->setObjectName(QStringLiteral("commandHistoryList"));
    listWidget_->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // 按时间倒序显示（最新的在最上面）
    reloadHistory();
    connect(&CommandHistory::instance(), &CommandHistory::changed,
            this, &CommandHistoryDialog::reloadHistory);

    layout->addWidget(new QLabel(tr("Double-click to select a command:")));
    layout->addWidget(listWidget_);

    // 按钮
    auto *buttonLayout = new QHBoxLayout();

    auto *deleteButton = new QPushButton(tr("Delete Selected"), this);
    deleteButton->setObjectName(QStringLiteral("deleteHistorySelection"));
    deleteButton->setEnabled(false);
    deleteButton->setAutoDefault(false);
    connect(deleteButton, &QPushButton::clicked, this, &CommandHistoryDialog::onDeleteSelected);
    connect(listWidget_, &QListWidget::itemSelectionChanged, this, [this, deleteButton]() {
        deleteButton->setEnabled(!listWidget_->selectedItems().isEmpty());
    });
    auto *deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), listWidget_);
    deleteShortcut->setContext(Qt::WidgetShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, &CommandHistoryDialog::onDeleteSelected);

    auto *clearButton = new QPushButton(tr("Clear History"), this);
    clearButton->setAutoDefault(false);
    clearButton->setIcon(QIcon::fromTheme("edit-clear"));
    connect(clearButton, &QPushButton::clicked, this, &CommandHistoryDialog::onClearHistory);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    buttonLayout->addWidget(deleteButton);
    buttonLayout->addWidget(clearButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(buttonBox);

    layout->addLayout(buttonLayout);

    connect(listWidget_, &QListWidget::itemDoubleClicked,
            this, &CommandHistoryDialog::onItemDoubleClicked);
}

void CommandHistoryDialog::reloadHistory() {
    listWidget_->clear();
    const auto &history = CommandHistory::instance().commands();
    for (auto it = history.crbegin(); it != history.crend(); ++it) {
        listWidget_->addItem(*it);
    }
}

void CommandHistoryDialog::onDeleteSelected() {
    QStringList commands;
    for (const auto *item : listWidget_->selectedItems()) {
        commands.append(item->text());
    }
    CommandHistory::instance().remove(commands);
}

void CommandHistoryDialog::onItemDoubleClicked(QListWidgetItem *item) {
    if (item) {
        emit commandSelected(item->text());
        accept();
    }
}

void CommandHistoryDialog::onClearHistory() {
    auto result = QMessageBox::question(
        this,
        tr("Clear History"),
        tr("Are you sure you want to clear all command history?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (result == QMessageBox::Yes) {
        CommandHistory::instance().clear();
    }
}
