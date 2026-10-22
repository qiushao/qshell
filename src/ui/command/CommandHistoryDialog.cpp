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
#include <QLineEdit>

CommandHistoryDialog::CommandHistoryDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Command History"));
    setMinimumSize(500, 400);

    auto *layout = new QVBoxLayout(this);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setObjectName(QStringLiteral("commandHistorySearch"));
    searchEdit_->setPlaceholderText(tr("Search commands..."));
    searchEdit_->setClearButtonEnabled(true);
    layout->addWidget(searchEdit_);
    connect(searchEdit_, &QLineEdit::textChanged, this, [this]() {
        currentPage_ = 0;
        reloadHistory();
    });

    // 历史记录列表
    listWidget_ = new QListWidget(this);
    listWidget_->setObjectName(QStringLiteral("commandHistoryList"));
    listWidget_->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // 按时间倒序显示（最新的在最上面）
    connect(&CommandHistory::instance(), &CommandHistory::changed,
            this, &CommandHistoryDialog::reloadHistory);

    layout->addWidget(new QLabel(tr("Double-click to select a command:")));
    layout->addWidget(listWidget_);

    auto *pageLayout = new QHBoxLayout();
    previousButton_ = new QPushButton(tr("Previous Page"), this);
    previousButton_->setObjectName(QStringLiteral("previousHistoryPage"));
    previousButton_->setAutoDefault(false);
    nextButton_ = new QPushButton(tr("Next Page"), this);
    nextButton_->setObjectName(QStringLiteral("nextHistoryPage"));
    nextButton_->setAutoDefault(false);
    pageLabel_ = new QLabel(this);
    pageLabel_->setObjectName(QStringLiteral("commandHistoryPage"));
    pageLabel_->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(previousButton_);
    pageLayout->addWidget(pageLabel_, 1);
    pageLayout->addWidget(nextButton_);
    layout->addLayout(pageLayout);
    connect(previousButton_, &QPushButton::clicked, this, [this]() {
        --currentPage_;
        showPage();
    });
    connect(nextButton_, &QPushButton::clicked, this, [this]() {
        ++currentPage_;
        showPage();
    });

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
    reloadHistory();
}

void CommandHistoryDialog::reloadHistory() {
    filteredCommands_.clear();
    const QString query = searchEdit_->text();
    const auto &history = CommandHistory::instance().commands();
    for (auto it = history.crbegin(); it != history.crend(); ++it) {
        if (it->contains(query, Qt::CaseInsensitive)) {
            filteredCommands_.append(*it);
        }
    }
    showPage();
}

void CommandHistoryDialog::showPage() {
    const int pageCount = static_cast<int>((filteredCommands_.size() + pageSize_ - 1) / pageSize_);
    currentPage_ = qBound(0, currentPage_, qMax(0, pageCount - 1));
    listWidget_->clear();
    listWidget_->addItems(filteredCommands_.mid(currentPage_ * pageSize_, pageSize_));
    previousButton_->setEnabled(currentPage_ > 0);
    nextButton_->setEnabled(currentPage_ + 1 < pageCount);
    pageLabel_->setText(tr("Page %1 / %2 (%3 commands)")
                               .arg(pageCount == 0 ? 0 : currentPage_ + 1)
                               .arg(pageCount)
                               .arg(filteredCommands_.size()));
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
