#ifndef QSHELL_SESSIONTREEWIDGET_H
#define QSHELL_SESSIONTREEWIDGET_H

#include <QItemDelegate>
#include <QTreeWidget>

#include "core/datatype.h"

class SessionTreeWidget : public QTreeWidget {
    Q_OBJECT

public:
    explicit SessionTreeWidget(QWidget *parent = nullptr);

    ~SessionTreeWidget() override = default;

public slots:
    void createNewSession();

    void createNewGroup();

signals:
    void openSession(const QString &sessionId);

protected:
    // 拖拽支持
    void dragEnterEvent(QDragEnterEvent *event) override;

    void dragMoveEvent(QDragMoveEvent *event) override;

    void dropEvent(QDropEvent *event) override;

private slots:
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);

    void onCustomContextMenu(const QPoint &pos);

protected:
    void changeEvent(QEvent *event) override;

private:
    void setupUI();

    void loadData();

    void refreshTree();

    QTreeWidgetItem *findGroupItem(const QString &groupId) const;

    QTreeWidgetItem *findSessionItem(const QString &sessionId) const;

    void editSession(const QString &sessionId);

    void deleteSession(const QString &sessionId);

    void editGroup(const QString &groupId);

    void deleteGroup(const QString &groupId);

    // 排序功能
    static void moveSessionUp(const QString &sessionId);

    static void moveSessionDown(const QString &sessionId);

    static void moveGroupUp(const QString &groupId);

    static void moveGroupDown(const QString &groupId);

    // 拖拽辅助
    static bool canDropOn(QTreeWidgetItem *dragItem, QTreeWidgetItem *dropTarget);

    static void handleSessionDrop(const QString &sessionId, QTreeWidgetItem *dropTarget, int dropIndex);

    static void handleGroupDrop(const QString &groupId, int dropIndex);
};

#endif//QSHELL_SESSIONTREEWIDGET_H
