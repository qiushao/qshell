// CollapsibleDockWidget.cpp
#include "CollapsibleDockWidget.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QStyle>
#include <QTimer>
#include <QMainWindow>

CollapsibleDockWidget::CollapsibleDockWidget(QWidget *parent)
    : QDockWidget(parent)
    , contentWidget_(nullptr)
    , expandedWidth_(200)
    , collapsed_(false)
{
    titleBar_ = new QWidget(this);
    auto *layout = new QHBoxLayout(titleBar_);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);

    toggleButton_ = new QPushButton(this);
    toggleButton_->setFixedSize(20, 20);
    toggleButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));
    toggleButton_->setFlat(true);
    toggleButton_->setToolTip(tr("Collapse/Expand"));

    layout->addWidget(toggleButton_);
    layout->addStretch();

    setTitleBarWidget(titleBar_);
    setMinimumWidth(MIN_WIDTH);
    setMaximumWidth(MAX_WIDTH);

    connect(toggleButton_, &QPushButton::clicked, this, &CollapsibleDockWidget::toggleCollapse);
}

void CollapsibleDockWidget::setContentWidget(QWidget *widget)
{
    contentWidget_ = widget;
    setWidget(widget);
}

void CollapsibleDockWidget::toggleCollapse()
{
    if (!collapsed_) {
        // 折叠
        expandedWidth_ = width();
        collapsed_ = true;

        if (contentWidget_) {
            contentWidget_->hide();
        }

        setMinimumWidth(COLLAPSED_WIDTH);
        setMaximumWidth(COLLAPSED_WIDTH);
        toggleButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));

    } else {
        // 展开
        collapsed_ = false;
        toggleButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));

        // 1. 先完全解除宽度限制
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);

        // 2. 显示内容
        if (contentWidget_) {
            contentWidget_->show();
        }

        // 3. 通知主窗口重新布局
        if (auto *mainWin = qobject_cast<QMainWindow*>(parentWidget())) {
            mainWin->resizeDocks({this}, {expandedWidth_}, Qt::Horizontal);
        }

        // 4. 恢复宽度限制
        QTimer::singleShot(50, this, [this]() {
            setMinimumWidth(MIN_WIDTH);
            setMaximumWidth(MAX_WIDTH);
        });
    }
}

void CollapsibleDockWidget::changeEvent(QEvent *event) {
    QDockWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        toggleButton_->setToolTip(tr("Collapse/Expand"));
    }
}
