#include "SessionTreeWidget.h"
#include "GroupEditDialog.h"
#include "SessionEditDialog.h"
#include "core/ConfigManager.h"
#include "core/datatype.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QVBoxLayout>

// 自定义数据角色
enum {
    ItemTypeRole = Qt::UserRole + 1,
    ItemIdRole = Qt::UserRole + 2
};

enum ItemType {
    GroupItem = 1,
    SessionItem = 2
};

SessionTreeWidget::SessionTreeWidget(QWidget *parent)
    : QTreeWidget(parent) {
    setupUI();
    loadData();

    // 连接配置管理器信号
    auto config = ConfigManager::instance();
    connect(config, &ConfigManager::sessionTreeUpdated, this, &SessionTreeWidget::refreshTree);
}

void SessionTreeWidget::setupUI() {
    setHeaderLabel(tr("Sessions"));
    setContextMenuPolicy(Qt::CustomContextMenu);
    setRootIsDecorated(true);
    setAnimated(true);
    header()->setVisible(false);

    // 启用拖拽排序
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);

    connect(this, &QTreeWidget::itemDoubleClicked,
            this, &SessionTreeWidget::onItemDoubleClicked);
    connect(this, &QTreeWidget::customContextMenuRequested,
            this, &SessionTreeWidget::onCustomContextMenu);
}

void SessionTreeWidget::loadData() {
    refreshTree();
}

void SessionTreeWidget::refreshTree() {
    qDebug() << "SessionTreeWidget::refreshTree";
    this->clear();

    auto config = ConfigManager::instance();

    // 添加分组
    QMap<QString, QTreeWidgetItem *> groupItems;
    for (const auto &group: config->groups()) {
        qDebug() << "add group:" << group.name;
        auto *item = new QTreeWidgetItem(this);
        item->setText(0, group.name);
        item->setData(0, ItemTypeRole, GroupItem);
        item->setData(0, ItemIdRole, group.id);
        item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        item->setExpanded(true);
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        groupItems[group.id] = item;
    }

    // 添加会话
    for (const auto &session: config->sessions()) {
        qDebug() << "add session:" << session.name;
        QTreeWidgetItem *parent = nullptr;
        if (!session.groupId.isEmpty() && groupItems.contains(session.groupId)) {
            parent = groupItems[session.groupId];
        }

        QTreeWidgetItem *item;
        if (parent) {
            item = new QTreeWidgetItem(parent);
        } else {
            item = new QTreeWidgetItem(this);
        }

        item->setText(0, session.name);
        item->setData(0, ItemTypeRole, SessionItem);
        item->setData(0, ItemIdRole, session.id);
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled);

        item->setIcon(0, style()->standardIcon(QStyle::SP_ComputerIcon));
    }
}

QTreeWidgetItem *SessionTreeWidget::findGroupItem(const QString &groupId) const {
    for (int i = 0; i < this->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = this->topLevelItem(i);
        if (item->data(0, ItemTypeRole).toInt() == GroupItem &&
            item->data(0, ItemIdRole).toString() == groupId) {
            return item;
        }
    }
    return nullptr;
}

QTreeWidgetItem *SessionTreeWidget::findSessionItem(const QString &sessionId) const {
    std::function<QTreeWidgetItem*(QTreeWidgetItem *)> findInItem = [&](QTreeWidgetItem *parent) -> QTreeWidgetItem * {
        for (int i = 0; i < (parent ? parent->childCount() : this->topLevelItemCount()); ++i) {
            QTreeWidgetItem *item = parent ? parent->child(i) : this->topLevelItem(i);
            if (item->data(0, ItemTypeRole).toInt() == SessionItem &&
                item->data(0, ItemIdRole).toString() == sessionId) {
                return item;
            }
            if (item->childCount() > 0) {
                if (auto found = findInItem(item)) {
                    return found;
                }
            }
        }
        return nullptr;
    };
    return findInItem(nullptr);
}

void SessionTreeWidget::onItemDoubleClicked(QTreeWidgetItem *item, int column) {
    Q_UNUSED(column)

    if (item->data(0, ItemTypeRole).toInt() == SessionItem) {
        QString sessionId = item->data(0, ItemIdRole).toString();
        emit openSession(sessionId);
    }
}

void SessionTreeWidget::onCustomContextMenu(const QPoint &pos) {
    QTreeWidgetItem *item = this->itemAt(pos);
    QMenu menu(this);

    if (!item) {
        // 空白区域右键
        menu.addAction(tr("New Group..."), this, &SessionTreeWidget::createNewGroup);
        menu.addAction(tr("New Session..."), this, &SessionTreeWidget::createNewSession);
    } else if (item->data(0, ItemTypeRole).toInt() == GroupItem) {
        // 分组右键
        QString groupId = item->data(0, ItemIdRole).toString();

        menu.addAction(tr("New Session in Group..."), this, [this, groupId]() {
            SessionEditDialog dialog(this);
            dialog.setGroupId(groupId);
            if (dialog.exec() == QDialog::Accepted) {
                ConfigManager::instance()->addSession(dialog.sessionData());
            }
        });

        menu.addSeparator();

        // 添加排序选项
        QAction *moveUpAction = menu.addAction(tr("Move Up"), this, [groupId]() {
            moveGroupUp(groupId);
        });
        QAction *moveDownAction = menu.addAction(tr("Move Down"), this, [groupId]() {
            moveGroupDown(groupId);
        });

        // 检查是否可以上移/下移
        int index = indexOfTopLevelItem(findGroupItem(groupId));
        int firstGroupIndex = -1;
        int lastGroupIndex = -1;
        for (int i = 0; i < topLevelItemCount(); ++i) {
            if (topLevelItem(i)->data(0, ItemTypeRole).toInt() == GroupItem) {
                if (firstGroupIndex < 0) firstGroupIndex = i;
                lastGroupIndex = i;
            }
        }
        moveUpAction->setEnabled(index > firstGroupIndex);
        moveDownAction->setEnabled(index < lastGroupIndex && index >= 0);

        menu.addSeparator();

        menu.addAction(tr("Edit Group..."), this, [this, groupId]() {
            editGroup(groupId);
        });

        menu.addAction(tr("Delete Group"), this, [this, groupId]() {
            deleteGroup(groupId);
        });
    } else {
        // 会话右键
        QString sessionId = item->data(0, ItemIdRole).toString();

        menu.addAction(tr("Connect"), this, [this, sessionId]() {
            emit openSession(sessionId);
        });

        menu.addSeparator();

        // 添加排序选项
        QAction *moveUpAction = menu.addAction(tr("Move Up"), this, [sessionId]() {
            moveSessionUp(sessionId);
        });
        QAction *moveDownAction = menu.addAction(tr("Move Down"), this, [sessionId]() {
            moveSessionDown(sessionId);
        });

        // 检查是否可以上移/下移
        auto config = ConfigManager::instance();
        SessionData session = config->sessionById(sessionId);
        QList<SessionData> sessions = config->sessions();

        // 找到同组的会话
        QList<SessionData> sameGroupSessions;
        for (const auto &s: sessions) {
            if (s.groupId == session.groupId) {
                sameGroupSessions.append(s);
            }
        }

        int sessionIndex = -1;
        for (int i = 0; i < sameGroupSessions.size(); ++i) {
            if (sameGroupSessions[i].id == sessionId) {
                sessionIndex = i;
                break;
            }
        }

        moveUpAction->setEnabled(sessionIndex > 0);
        moveDownAction->setEnabled(sessionIndex >= 0 && sessionIndex < sameGroupSessions.size() - 1);

        menu.addSeparator();

        menu.addAction(tr("Edit Session..."), this, [this, sessionId]() {
            editSession(sessionId);
        });

        menu.addAction(tr("Delete Session"), this, [this, sessionId]() {
            deleteSession(sessionId);
        });
    }

    menu.exec(this->viewport()->mapToGlobal(pos));
}

void SessionTreeWidget::createNewSession() {
    SessionEditDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        ConfigManager::instance()->addSession(dialog.sessionData());
    }
}

void SessionTreeWidget::createNewGroup() {
    GroupEditDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        ConfigManager::instance()->addGroup(dialog.groupData());
    }
}

void SessionTreeWidget::editSession(const QString &sessionId) {
    SessionData session = ConfigManager::instance()->sessionById(sessionId);
    if (session.id.isEmpty()) return;

    SessionEditDialog dialog(this);
    dialog.setSessionData(session);
    if (dialog.exec() == QDialog::Accepted) {
        ConfigManager::instance()->updateSession(dialog.sessionData());
    }
}

void SessionTreeWidget::deleteSession(const QString &sessionId) {
    SessionData session = ConfigManager::instance()->sessionById(sessionId);
    if (session.id.isEmpty()) return;

    QMessageBox::StandardButton result = QMessageBox::question(
        this,
        tr("Delete Session"),
        tr("Are you sure you want to delete session '%1'?").arg(session.name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (result == QMessageBox::Yes) {
        ConfigManager::instance()->removeSession(sessionId);
    }
}

void SessionTreeWidget::editGroup(const QString &groupId) {
    GroupData group = ConfigManager::instance()->group(groupId);
    if (group.id.isEmpty()) return;

    GroupEditDialog dialog(this);
    dialog.setGroupData(group);
    if (dialog.exec() == QDialog::Accepted) {
        ConfigManager::instance()->updateGroup(dialog.groupData());
    }
}

void SessionTreeWidget::deleteGroup(const QString &groupId) {
    if (!ConfigManager::instance()->isGroupEmpty(groupId)) {
        QMessageBox::warning(
            this,
            tr("Cannot Delete Group"),
            tr("The group is not empty. Please delete all sessions in the group first.")
        );
        return;
    }

    GroupData group = ConfigManager::instance()->group(groupId);
    if (group.id.isEmpty()) return;

    QMessageBox::StandardButton result = QMessageBox::question(
        this,
        tr("Delete Group"),
        tr("Are you sure you want to delete group '%1'?").arg(group.name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (result == QMessageBox::Yes) {
        ConfigManager::instance()->removeGroup(groupId);
    }
}

// 排序功能实现
void SessionTreeWidget::moveSessionUp(const QString &sessionId) {
    ConfigManager::instance()->moveSessionUp(sessionId);
}

void SessionTreeWidget::moveSessionDown(const QString &sessionId) {
    ConfigManager::instance()->moveSessionDown(sessionId);
}

void SessionTreeWidget::moveGroupUp(const QString &groupId) {
    ConfigManager::instance()->moveGroupUp(groupId);
}

void SessionTreeWidget::moveGroupDown(const QString &groupId) {
    ConfigManager::instance()->moveGroupDown(groupId);
}

// 拖拽功能实现
void SessionTreeWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (event->source() == this) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void SessionTreeWidget::dragMoveEvent(QDragMoveEvent *event) {
    QTreeWidgetItem *dragItem = currentItem();
    if (!dragItem) {
        event->ignore();
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QTreeWidgetItem *dropTarget = itemAt(event->position().toPoint());
#else
    QTreeWidgetItem *dropTarget = itemAt(event->pos());
#endif

    if (canDropOn(dragItem, dropTarget)) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

bool SessionTreeWidget::canDropOn(QTreeWidgetItem *dragItem, QTreeWidgetItem *dropTarget) {
    if (!dragItem) return false;

    int dragType = dragItem->data(0, ItemTypeRole).toInt();

    if (dragType == GroupItem) {
        // 分组只能拖到根级别（dropTarget为nullptr或者是顶级项之间）
        // 不能拖到其他分组或会话上
        if (dropTarget == nullptr) return true;
        // 允许拖到分组之间（但不是拖到分组内部）
        if (dropTarget->data(0, ItemTypeRole).toInt() == GroupItem) {
            return dropTarget != dragItem;
        }
        return false;
    } else if (dragType == SessionItem) {
        // 会话可以拖到：
        // 1. 根级别（移出分组）
        // 2. 分组上（移入分组）
        // 3. 其他会话旁边（排序）
        if (dropTarget == nullptr) return true;
        if (dropTarget == dragItem) return false;
        return true;
    }

    return false;
}

void SessionTreeWidget::dropEvent(QDropEvent *event) {
    QTreeWidgetItem *dragItem = currentItem();
    if (!dragItem) {
        event->ignore();
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QTreeWidgetItem *dropTarget = itemAt(event->position().toPoint());
#else
    QTreeWidgetItem *dropTarget = itemAt(event->pos());
#endif

    if (!canDropOn(dragItem, dropTarget)) {
        event->ignore();
        return;
    }

    int dragType = dragItem->data(0, ItemTypeRole).toInt();
    QString dragId = dragItem->data(0, ItemIdRole).toString();

    if (dragType == GroupItem) {
        // 处理分组拖拽
        int dropIndex = 0;
        if (dropTarget) {
            dropIndex = indexOfTopLevelItem(dropTarget);
            if (dropIndex < 0) {
                // dropTarget不是顶级项
                event->ignore();
                return;
            }
        } else {
            // 拖到最后
            dropIndex = topLevelItemCount();
        }

        handleGroupDrop(dragId, dropIndex);
    } else if (dragType == SessionItem) {
        // 处理会话拖拽
        int dropIndex = 0;
        if (dropTarget) {
            QTreeWidgetItem *targetParent = dropTarget->parent();
            if (targetParent) {
                dropIndex = targetParent->indexOfChild(dropTarget);
            } else {
                dropIndex = indexOfTopLevelItem(dropTarget);
            }
        }

        handleSessionDrop(dragId, dropTarget, dropIndex);
    }

    event->acceptProposedAction();
}

void SessionTreeWidget::handleSessionDrop(const QString &sessionId, QTreeWidgetItem *dropTarget, int dropIndex) {
    auto config = ConfigManager::instance();
    SessionData session = config->sessionById(sessionId);
    if (session.id.isEmpty()) return;

    QString newGroupId;

    if (dropTarget) {
        int targetType = dropTarget->data(0, ItemTypeRole).toInt();

        if (targetType == GroupItem) {
            // 拖到分组上，移入该分组
            newGroupId = dropTarget->data(0, ItemIdRole).toString();
        } else if (targetType == SessionItem) {
            // 拖到会话上，获取该会话的分组
            SessionData targetSession = config->sessionById(dropTarget->data(0, ItemIdRole).toString());
            newGroupId = targetSession.groupId;
        }
    }

    // 如果分组改变了，更新会话
    if (session.groupId != newGroupId) {
        session.groupId = newGroupId;
        config->updateSession(session);
    }

    // 计算新的排序位置
    QList<SessionData> sessions = config->sessions();
    QStringList sameGroupSessionIds;
    for (const auto &s: sessions) {
        if (s.groupId == newGroupId && s.id != sessionId) {
            sameGroupSessionIds.append(s.id);
        }
    }

    // 插入到正确位置
    if (dropIndex >= 0 && dropIndex <= sameGroupSessionIds.size()) {
        sameGroupSessionIds.insert(dropIndex, sessionId);
    } else {
        sameGroupSessionIds.append(sessionId);
    }

    // 重新排序（这里需要重新排序所有会话）
    QStringList allSessionIds;
    for (const auto &s: sessions) {
        if (s.groupId != newGroupId) {
            allSessionIds.append(s.id);
        }
    }
    // 将同组会话按新顺序添加
    for (const auto &id: sameGroupSessionIds) {
        allSessionIds.append(id);
    }

    config->reorderSessions(allSessionIds);
}

void SessionTreeWidget::handleGroupDrop(const QString &groupId, int dropIndex) {
    auto config = ConfigManager::instance();

    // 获取当前分组顺序
    QList<GroupData> groups = config->groups();
    QStringList groupIds;
    int currentIndex = -1;

    for (int i = 0; i < groups.size(); ++i) {
        if (groups[i].id == groupId) {
            currentIndex = i;
        } else {
            groupIds.append(groups[i].id);
        }
    }

    if (currentIndex < 0) return;

    // 调整目标索引
    if (dropIndex > currentIndex) {
        dropIndex--;
    }

    // 插入到新位置
    if (dropIndex >= 0 && dropIndex <= groupIds.size()) {
        groupIds.insert(dropIndex, groupId);
    } else {
        groupIds.append(groupId);
    }

    config->reorderGroups(groupIds);
}
