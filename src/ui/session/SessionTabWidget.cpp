#include "SessionTabWidget.h"

#include <QInputDialog>
#include <QMenu>
#include <QTabBar>

SessionTabWidget::SessionTabWidget(QWidget *parent)
    : QTabWidget(parent) {

#ifdef Q_OS_MACOS
    auto *macTabStyle = new LeftAlignedTabStyle();
    macTabStyle->setParent(this);
    setStyle(macTabStyle);
    tabBar()->setStyle(macTabStyle);
    tabBar()->setExpanding(false);
    tabBar()->setUsesScrollButtons(true);
    tabBar()->setElideMode(Qt::ElideNone);
#endif

    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(tabBar(), &QTabBar::customContextMenuRequested,
            this, &SessionTabWidget::showTabContextMenu);
}

void SessionTabWidget::tabInserted(int index) {
    QTabWidget::tabInserted(index);
#ifdef Q_OS_MACOS
    tabBar()->setExpanding(false);
#endif
}

void SessionTabWidget::tabRemoved(int index) {
    QTabWidget::tabRemoved(index);
#ifdef Q_OS_MACOS
    tabBar()->setExpanding(false);
#endif
}

void SessionTabWidget::showTabContextMenu(const QPoint &pos) {
    m_contextMenuTabIndex = tabBar()->tabAt(pos);

    if (m_contextMenuTabIndex < 0) {
        return;
    }

    QMenu menu(this);

    QAction *renameAction = menu.addAction(tr("Rename Tab"));
    connect(renameAction, &QAction::triggered, this, &SessionTabWidget::renameTab);

    menu.exec(tabBar()->mapToGlobal(pos));
}

void SessionTabWidget::renameTab() {
    if (m_contextMenuTabIndex < 0 || m_contextMenuTabIndex >= count()) {
        return;
    }

    QString currentName = tabText(m_contextMenuTabIndex);

    bool ok;
    QString newName = QInputDialog::getText(this,
                                            tr("Rename Tab"),
                                            tr("New name:"),
                                            QLineEdit::Normal,
                                            currentName,
                                            &ok);

    if (ok && !newName.isEmpty()) {
        setTabText(m_contextMenuTabIndex, newName);
    }
}
