#include "CommandButtonBar.h"
#include <QEvent>
#include "core/ConfigManager.h"

#include <QComboBox>
#include <QPushButton>
#include <QLayout>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QTextEdit>
#include <QDialogButtonBox>
#include <QLabel>
#include <QContextMenuEvent>
#include <QList>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTimer>

namespace {

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent, int spacing)
        : QLayout(parent)
        , spacing_(spacing) {
    }

    ~FlowLayout() override {
        QLayoutItem *item = nullptr;
        while ((item = takeAt(0)) != nullptr) {
            delete item;
        }
    }

    void addItem(QLayoutItem *item) override {
        items_.append(item);
    }

    [[nodiscard]] int count() const override {
        return static_cast<int>(items_.size());
    }

    [[nodiscard]] QLayoutItem *itemAt(int index) const override {
        if (index < 0 || index >= items_.size()) {
            return nullptr;
        }
        return items_.at(index);
    }

    QLayoutItem *takeAt(int index) override {
        if (index < 0 || index >= items_.size()) {
            return nullptr;
        }
        return items_.takeAt(index);
    }

    [[nodiscard]] Qt::Orientations expandingDirections() const override {
        return {};
    }

    [[nodiscard]] bool hasHeightForWidth() const override {
        return true;
    }

    [[nodiscard]] int heightForWidth(int width) const override {
        return doLayout(QRect(0, 0, width, 0), true);
    }

    [[nodiscard]] QSize minimumSize() const override {
        QSize size;
        for (const auto *item : items_) {
            size = size.expandedTo(item->minimumSize());
        }
        const QMargins margins = contentsMargins();
        size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
        return size;
    }

    [[nodiscard]] QSize sizeHint() const override {
        return minimumSize();
    }

    void setGeometry(const QRect &rect) override {
        QLayout::setGeometry(rect);
        const int height = doLayout(rect, false);
        Q_UNUSED(height)
    }

private:
    [[nodiscard]] int doLayout(const QRect &rect, bool testOnly) const {
        const QMargins margins = contentsMargins();
        const QRect effectiveRect = rect.adjusted(margins.left(),
                                                  margins.top(),
                                                  -margins.right(),
                                                  -margins.bottom());
        int x = effectiveRect.x();
        int y = effectiveRect.y();
        int lineHeight = 0;

        for (auto *item : items_) {
            const QSize itemSize = item->sizeHint();
            int nextX = x + itemSize.width() + spacing_;
            if (x > effectiveRect.x() && nextX - spacing_ > effectiveRect.right()) {
                x = effectiveRect.x();
                y += lineHeight + spacing_;
                nextX = x + itemSize.width() + spacing_;
                lineHeight = 0;
            }

            if (!testOnly) {
                item->setGeometry(QRect(QPoint(x, y), itemSize));
            }

            x = nextX;
            lineHeight = qMax(lineHeight, itemSize.height());
        }

        if (items_.isEmpty()) {
            return margins.top() + margins.bottom();
        }
        return y + lineHeight - rect.y() + margins.bottom();
    }

    QList<QLayoutItem*> items_;
    int spacing_ = 0;
};

} // namespace

CommandButtonBar::CommandButtonBar(QWidget *parent)
    : QToolBar(parent) {
    setupUI();

    // 连接配置管理器的信号
    connect(ConfigManager::instance(), &ConfigManager::buttonGroupsChanged,
            this, &CommandButtonBar::refreshGroups);
    connect(ConfigManager::instance(), &ConfigManager::quickButtonsChanged,
            this, &CommandButtonBar::refreshButtons);

    // 初始加载
    refreshGroups();
}

void CommandButtonBar::setupUI() {
    setMovable(false);
    setFloatable(false);

    // 分组选择下拉框
    groupComboBox_ = new QComboBox(this);
    groupComboBox_->setMinimumWidth(120);
    groupComboBox_->setMaximumWidth(200);
    groupComboBox_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(groupComboBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CommandButtonBar::onGroupChanged);
    connect(groupComboBox_, &QComboBox::customContextMenuRequested,
            this, &CommandButtonBar::onGroupContextMenu);
    addWidget(groupComboBox_);

    addSeparator();

    // 按钮容器
    buttonContainer_ = new QWidget(this);
    buttonContainer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    buttonLayout_ = new FlowLayout(buttonContainer_, 4);
    buttonLayout_->setContentsMargins(0, 0, 0, 0);
    addWidget(buttonContainer_);
}

void CommandButtonBar::refreshGroups() {
    QString currentGroupId;
    if (groupComboBox_->currentIndex() >= 0) {
        currentGroupId = groupComboBox_->currentData().toString();
    }

    groupComboBox_->blockSignals(true);
    groupComboBox_->clear();

    auto groups = ConfigManager::instance()->buttonGroups();

    if (groups.isEmpty()) {
        groupComboBox_->addItem(tr("(无分组)"), QString());
    } else {
        for (const auto &group : groups) {
            groupComboBox_->addItem(group.name, group.id);
        }
    }

    // 恢复之前选中的分组
    int index = -1;
    if (!currentGroupId.isEmpty()) {
        for (int i = 0; i < groupComboBox_->count(); ++i) {
            if (groupComboBox_->itemData(i).toString() == currentGroupId) {
                index = i;
                break;
            }
        }
    }
    groupComboBox_->setCurrentIndex(index >= 0 ? index : 0);
    groupComboBox_->blockSignals(false);

    refreshButtons();
}

void CommandButtonBar::refreshButtons() {
    clearButtons();

    QString groupId = groupComboBox_->currentData().toString();
    if (groupId.isEmpty() && groupComboBox_->currentIndex() == 0
        && groupComboBox_->itemText(0) == tr("(无分组)")) {
        return;
    }

    auto buttons = ConfigManager::instance()->quickButtonsByGroup(groupId);

    for (const auto &btn : buttons) {
        auto *pushBtn = new QPushButton(btn.name, buttonContainer_);
        pushBtn->setProperty("buttonId", btn.id);
        pushBtn->setProperty("command", btn.command);
        pushBtn->setToolTip(btn.command);
        pushBtn->setContextMenuPolicy(Qt::CustomContextMenu);

        connect(pushBtn, &QPushButton::clicked, this, &CommandButtonBar::onButtonClicked);
        connect(pushBtn, &QPushButton::customContextMenuRequested,
                this, &CommandButtonBar::onButtonContextMenu);

        buttonLayout_->addWidget(pushBtn);
        buttons_[btn.id] = pushBtn;
    }
    QTimer::singleShot(0, this, &CommandButtonBar::updateButtonBarHeight);
}

void CommandButtonBar::clearButtons() {
    QLayoutItem *item = nullptr;
    while ((item = buttonLayout_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    buttons_.clear();
    QTimer::singleShot(0, this, &CommandButtonBar::updateButtonBarHeight);
}

void CommandButtonBar::onGroupChanged(int index) {
    Q_UNUSED(index)
    refreshButtons();
}

void CommandButtonBar::onButtonClicked() {
    auto *btn = qobject_cast<QPushButton*>(sender());
    if (btn) {
        QString command = btn->property("command").toString();
        emit commandTriggered(command);
    }
}

void CommandButtonBar::onGroupContextMenu(const QPoint &pos) {
    QMenu menu(this);

    // 添加分组
    QAction *addGroupAction = menu.addAction(tr("添加分组"));
    connect(addGroupAction, &QAction::triggered, this, [this]() {
        bool ok;
        QString name = QInputDialog::getText(this, tr("添加分组"),
                                             tr("分组名称:"), QLineEdit::Normal,
                                             QString(), &ok);
        if (ok && !name.isEmpty()) {
            ButtonGroup group;
            group.name = name;
            ConfigManager::instance()->addButtonGroup(group);
        }
    });

    // 当前分组操作
    QString currentGroupId = groupComboBox_->currentData().toString();
    if (!currentGroupId.isEmpty()) {
        menu.addSeparator();

        // 重命名分组
        QAction *renameAction = menu.addAction(tr("重命名当前分组"));
        connect(renameAction, &QAction::triggered, this, [this, currentGroupId]() {
            auto group = ConfigManager::instance()->buttonGroup(currentGroupId);
            bool ok;
            QString name = QInputDialog::getText(this, tr("重命名分组"),
                                                 tr("分组名称:"), QLineEdit::Normal,
                                                 group.name, &ok);
            if (ok && !name.isEmpty()) {
                group.name = name;
                ConfigManager::instance()->updateButtonGroup(group);
            }
        });

        // 删除分组
        QAction *deleteAction = menu.addAction(tr("删除当前分组"));
        connect(deleteAction, &QAction::triggered, this, [this, currentGroupId]() {
            auto buttons = ConfigManager::instance()->quickButtonsByGroup(currentGroupId);
            QString msg = tr("确定要删除此分组吗？");
            if (!buttons.isEmpty()) {
                msg += tr("\n该分组下有 %1 个按钮，将一并删除。").arg(buttons.size());
            }

            if (QMessageBox::question(this, tr("确认删除"), msg) == QMessageBox::Yes) {
                ConfigManager::instance()->removeButtonGroup(currentGroupId);
            }
        });

        menu.addSeparator();

        // 上移/下移分组
        QAction *moveUpAction = menu.addAction(tr("上移分组"));
        connect(moveUpAction, &QAction::triggered, this, [currentGroupId]() {
            ConfigManager::instance()->moveButtonGroupUp(currentGroupId);
        });

        QAction *moveDownAction = menu.addAction(tr("下移分组"));
        connect(moveDownAction, &QAction::triggered, this, [currentGroupId]() {
            ConfigManager::instance()->moveButtonGroupDown(currentGroupId);
        });
    }

    menu.exec(groupComboBox_->mapToGlobal(pos));
}

void CommandButtonBar::contextMenuEvent(QContextMenuEvent *event) {
    // 检查点击位置是否在某个按钮上，如果是则不处理（由按钮自己的右键菜单处理）
    QWidget *child = childAt(event->pos());
    if (child) {
        // 检查是否点击在命令按钮上
        auto *btn = qobject_cast<QPushButton*>(child);
        if (btn && buttons_.values().contains(btn)) {
            // 点击在命令按钮上，让按钮自己处理
            return;
        }
        // 检查是否点击在分组下拉框上
        if (child == groupComboBox_ || groupComboBox_->isAncestorOf(child)) {
            return;
        }
    }

    // 在空白处显示添加按钮菜单
    QMenu menu(this);
    QAction *addAction = menu.addAction(tr("添加命令按钮"));
    connect(addAction, &QAction::triggered, this, &CommandButtonBar::onAddButton);
    menu.exec(event->globalPos());
}

void CommandButtonBar::resizeEvent(QResizeEvent *event) {
    QToolBar::resizeEvent(event);
    updateButtonBarHeight();
}

void CommandButtonBar::updateButtonBarHeight() {
    const int containerWidth = buttonContainer_->width();
    if (containerWidth <= 0) {
        return;
    }

    const int containerHeight = buttonLayout_->heightForWidth(containerWidth);
    if (buttonContainer_->minimumHeight() != containerHeight
        || buttonContainer_->maximumHeight() != containerHeight) {
        buttonContainer_->setMinimumHeight(containerHeight);
        buttonContainer_->setMaximumHeight(containerHeight);
        buttonContainer_->updateGeometry();
    }

    const int toolbarHeight = sizeHint().height();
    if (minimumHeight() != toolbarHeight || maximumHeight() != toolbarHeight) {
        setFixedHeight(toolbarHeight);
        updateGeometry();
    }
}

void CommandButtonBar::onAddButton() {
    QString groupId = groupComboBox_->currentData().toString();

    // 检查是否有分组
    if (groupId.isEmpty()) {
        auto groups = ConfigManager::instance()->buttonGroups();
        if (groups.isEmpty()) {
            QMessageBox::information(this, tr("提示"),
                                     tr("请先创建一个分组（在分组下拉框上右键点击）"));
            return;
        }
    }

    // 创建添加按钮对话框
    QDialog dialog(this);
    dialog.setWindowTitle(tr("添加命令按钮"));
    dialog.setMinimumWidth(400);

    auto *layout = new QFormLayout(&dialog);

    auto *nameEdit = new QLineEdit(&dialog);
    layout->addRow(tr("按钮名称:"), nameEdit);

    auto *commandEdit = new QTextEdit(&dialog);
    commandEdit->setMaximumHeight(100);
    layout->addRow(tr("命令:"), commandEdit);

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttonBox);

    if (dialog.exec() == QDialog::Accepted) {
        QString name = nameEdit->text().trimmed();
        QString command = commandEdit->toPlainText();

        if (name.isEmpty()) {
            QMessageBox::warning(this, tr("错误"), tr("按钮名称不能为空"));
            return;
        }

        QuickButton btn;
        btn.name = name;
        btn.command = command;
        btn.groupId = groupId;
        ConfigManager::instance()->addQuickButton(btn);
    }
}

void CommandButtonBar::onButtonContextMenu(const QPoint &pos) {
    auto *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    QString buttonId = btn->property("buttonId").toString();

    QMenu menu(this);

    // 编辑
    QAction *editAction = menu.addAction(tr("编辑"));
    connect(editAction, &QAction::triggered, this, [this, buttonId]() {
        auto button = ConfigManager::instance()->quickButton(buttonId);

        QDialog dialog(this);
        dialog.setWindowTitle(tr("编辑命令按钮"));
        dialog.setMinimumWidth(400);

        auto *layout = new QFormLayout(&dialog);

        auto *nameEdit = new QLineEdit(button.name, &dialog);
        layout->addRow(tr("按钮名称:"), nameEdit);

        auto *commandEdit = new QTextEdit(&dialog);
        commandEdit->setPlainText(button.command);
        commandEdit->setMaximumHeight(100);
        layout->addRow(tr("命令:"), commandEdit);

        auto *buttonBox = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addRow(buttonBox);

        if (dialog.exec() == QDialog::Accepted) {
            button.name = nameEdit->text().trimmed();
            button.command = commandEdit->toPlainText();

            if (!button.name.isEmpty()) {
                ConfigManager::instance()->updateQuickButton(button);
            }
        }
    });

    // 删除
    QAction *deleteAction = menu.addAction(tr("删除"));
    connect(deleteAction, &QAction::triggered, this, [this, buttonId, btn]() {
        if (QMessageBox::question(this, tr("确认删除"),
                                  tr("确定要删除按钮 \"%1\" 吗？").arg(btn->text()))
            == QMessageBox::Yes) {
            ConfigManager::instance()->removeQuickButton(buttonId);
        }
    });

    menu.addSeparator();

    // 移动到分组
    auto allGroups = ConfigManager::instance()->buttonGroups();
    auto button = ConfigManager::instance()->quickButton(buttonId);
    if (!allGroups.isEmpty() && allGroups.size() > 1) {
        QMenu *moveToGroupMenu = menu.addMenu(tr("移动到分组"));
        for (const auto &group : allGroups) {
            if (group.id == button.groupId) continue;
            QAction *moveAction = moveToGroupMenu->addAction(group.name);
            connect(moveAction, &QAction::triggered, this, [buttonId, groupId = group.id]() {
                auto qb = ConfigManager::instance()->quickButton(buttonId);
                qb.groupId = groupId;
                ConfigManager::instance()->updateQuickButton(qb);
            });
        }
        menu.addSeparator();
    }

    // 上移/下移
    QAction *moveUpAction = menu.addAction(tr("左移"));
    connect(moveUpAction, &QAction::triggered, this, [buttonId]() {
        ConfigManager::instance()->moveQuickButtonUp(buttonId);
    });

    QAction *moveDownAction = menu.addAction(tr("右移"));
    connect(moveDownAction, &QAction::triggered, this, [buttonId]() {
        ConfigManager::instance()->moveQuickButtonDown(buttonId);
    });

    menu.exec(btn->mapToGlobal(pos));
}

void CommandButtonBar::changeEvent(QEvent *event) {
    QToolBar::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        if (groupComboBox_->count() == 1 && groupComboBox_->itemData(0).toString().isEmpty()) {
            groupComboBox_->setItemText(0, tr("(无分组)"));
        }
    }
}
