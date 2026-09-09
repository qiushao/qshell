#include "CommandCompletionPopup.h"

#include "core/CommandHistory.h"
#include "core/ConfigManager.h"
#include <QApplication>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QScreen>
#include <algorithm>

CommandCompletionPopup::CommandCompletionPopup(QWidget *editor)
    : QObject(editor), editor_(editor), list_(new QListWidget(editor)) {
    list_->setObjectName(QStringLiteral("commandCompletionPopup"));
    list_->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    list_->setAttribute(Qt::WA_ShowWithoutActivating);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setTextElideMode(Qt::ElideRight);
    list_->setUniformItemSizes(true);
    connect(list_, &QListWidget::itemClicked, this, [this]() { acceptCurrent(); });
    connect(&CommandHistory::instance(), &CommandHistory::changed, this, [this]() {
        if (list_->isVisible()) updateMatches(query_, cursorRect_);
    });
    connect(ConfigManager::instance(), &ConfigManager::globalSettingsChanged, this, [this]() {
        if (!ConfigManager::instance()->globalSettings().commandHistoryCompletion) hide();
    });
    qApp->installEventFilter(this);
}

void CommandCompletionPopup::updateMatches(const QString &query, const QRect &cursorRect) {
    if (!ConfigManager::instance()->globalSettings().commandHistoryCompletion) {
        hide();
        return;
    }
    query_ = query;
    cursorRect_ = cursorRect;
    const QStringList matches = CommandHistory::instance().matches(query);
    if (!editor_->hasFocus() || matches.isEmpty()) {
        hide();
        return;
    }
    list_->clear();
    for (const QString &command: matches) {
        QString label = command;
        label.replace('\n', QStringLiteral(" ↵ "));
        auto *item = new QListWidgetItem(label, list_);
        item->setData(Qt::UserRole, command);
        item->setToolTip(command);
    }
    list_->setCurrentRow(-1);
    const QRect available = editor_->screen()->availableGeometry();
    const int width = std::min(available.width(), std::clamp(list_->sizeHintForColumn(0) + 32, 280, 720));
    const int height = std::min(available.height(), list_->sizeHintForRow(0) * std::min(list_->count(), 8) + 4);
    QPoint position = editor_->mapToGlobal(cursorRect.bottomLeft());
    if (position.y() + height > available.bottom() + 1) {
        position.setY(editor_->mapToGlobal(cursorRect.topLeft()).y() - height);
    }
    position.setX(std::clamp(position.x(), available.left(), available.right() - width + 1));
    position.setY(std::clamp(position.y(), available.top(), available.bottom() - height + 1));
    list_->setGeometry(QRect(position, QSize(width, height)));
    list_->show();
    list_->raise();
}

bool CommandCompletionPopup::handleKey(QKeyEvent *event) {
    if (!list_->isVisible() || event->modifiers() != Qt::NoModifier) {
        return false;
    }
    switch (event->key()) {
        case Qt::Key_Down:
            list_->setCurrentRow((list_->currentRow() + 1) % list_->count());
            return true;
        case Qt::Key_Up:
            list_->setCurrentRow(list_->currentRow() <= 0 ? list_->count() - 1 : list_->currentRow() - 1);
            return true;
        case Qt::Key_Tab:
            hide();
            return false;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (list_->currentRow() < 0) return false;
            acceptCurrent();
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        default:
            return false;
    }
}

void CommandCompletionPopup::acceptCurrent() {
    if (const auto *item = list_->currentItem()) {
        const QString command = item->data(Qt::UserRole).toString();
        hide();
        emit commandSelected(command);
    }
}

void CommandCompletionPopup::hide() {
    list_->hide();
}

bool CommandCompletionPopup::eventFilter(QObject *watched, QEvent *event) {
    if (list_->isVisible()) {
        if ((watched == editor_ && (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide)) ||
            (watched == editor_->window() && (event->type() == QEvent::Move || event->type() == QEvent::Resize || event->type() == QEvent::WindowDeactivate))) {
            hide();
        } else if (event->type() == QEvent::MouseButtonPress) {
            const auto *mouse = dynamic_cast<QMouseEvent *>(event);
            if (!list_->geometry().contains(mouse->globalPosition().toPoint())) hide();
        }
    }
    return QObject::eventFilter(watched, event);
}
