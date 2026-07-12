#include "ConfigManager.h"
#include "CryptoHelper.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <algorithm>

namespace {
bool loadConfigFromFile(const QString& filePath,
                        QMap<QString, GroupData>& groups,
                        QMap<QString, SessionData>& sessions,
                        QMap<QString, ButtonGroup>& buttonGroups,
                        QMap<QString, QuickButton>& quickButtons,
                        GlobalSettings& globalSettings,
                        WindowLayout& windowLayout,
                        QString* errorMessage) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Cannot open file: %1").arg(file.errorString());
        }
        return false;
    }

    const QByteArray jsonData = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Invalid JSON: %1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = doc.object();

    groups.clear();
    const QJsonArray groupsArray = root["groups"].toArray();
    for (const auto& val : groupsArray) {
        const GroupData group = GroupData::fromJson(val.toObject());
        groups[group.id] = group;
    }

    sessions.clear();
    const QJsonArray sessionsArray = root["sessions"].toArray();
    for (const auto& val : sessionsArray) {
        SessionData session = SessionData::fromJson(val.toObject());
        session.sshConfig.password = CryptoHelper::decrypt(session.sshConfig.password);
        session.sshConfig.passphrase = CryptoHelper::decrypt(session.sshConfig.passphrase);
        sessions[session.id] = session;
    }

    buttonGroups.clear();
    const QJsonArray btnGroupsArray = root["buttonGroups"].toArray();
    for (const auto& val : btnGroupsArray) {
        const ButtonGroup group = ButtonGroup::fromJson(val.toObject());
        buttonGroups[group.id] = group;
    }

    quickButtons.clear();
    const QJsonArray buttonsArray = root["quickButtons"].toArray();
    for (const auto& val : buttonsArray) {
        const QuickButton btn = QuickButton::fromJson(val.toObject());
        quickButtons[btn.id] = btn;
    }

    globalSettings = GlobalSettings::fromJson(root["globalSettings"].toObject());
    if (root.contains("windowLayout")) {
        windowLayout = WindowLayout::fromJson(root["windowLayout"].toObject());
    } else {
        windowLayout = WindowLayout{};
    }

    return true;
}

QJsonObject buildConfigJson(const QList<GroupData>& groups,
                            const QList<SessionData>& sessions,
                            const QList<ButtonGroup>& buttonGroups,
                            const QList<QuickButton>& quickButtons,
                            const GlobalSettings& globalSettings,
                            const WindowLayout& windowLayout) {
    QJsonObject root;

    QJsonArray groupsArray;
    for (const auto& group : groups) {
        groupsArray.append(group.toJson());
    }
    root["groups"] = groupsArray;

    QJsonArray sessionsArray;
    for (auto session : sessions) {
        session.sshConfig.password = CryptoHelper::encrypt(session.sshConfig.password);
        session.sshConfig.passphrase = CryptoHelper::encrypt(session.sshConfig.passphrase);
        sessionsArray.append(session.toJson());
    }
    root["sessions"] = sessionsArray;

    QJsonArray btnGroupsArray;
    for (const auto& group : buttonGroups) {
        btnGroupsArray.append(group.toJson());
    }
    root["buttonGroups"] = btnGroupsArray;

    QJsonArray buttonsArray;
    for (const auto& btn : quickButtons) {
        buttonsArray.append(btn.toJson());
    }
    root["quickButtons"] = buttonsArray;

    root["globalSettings"] = globalSettings.toJson();
    root["windowLayout"] = windowLayout.toJson();

    return root;
}
}

ConfigManager* ConfigManager::instance_ = nullptr;

ConfigManager* ConfigManager::instance() {
    if (!instance_) {
        instance_ = new ConfigManager();
    }
    return instance_;
}

QString ConfigManager::generateMcpBearerToken() {
    QByteArray randomBytes(32, Qt::Uninitialized);
    for (char& byte : randomBytes) {
        byte = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return QString::fromLatin1(randomBytes.toHex());
}

ConfigManager::ConfigManager(QObject* parent)
    : QObject(parent) {
    load();
}

QString ConfigManager::configFilePath() {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir dir(configDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return dir.filePath("config.json");
}

bool ConfigManager::load() {
    return loadConfigFromFile(configFilePath(),
                              groups_,
                              sessions_,
                              buttonGroups_,
                              quickButtons_,
                              globalSettings_,
                              windowLayout_,
                              nullptr);
}

bool ConfigManager::save() {
    return exportConfig(configFilePath(), nullptr);
}

bool ConfigManager::importConfig(const QString& filePath, QString* errorMessage) {
    QMap<QString, GroupData> groups;
    QMap<QString, SessionData> sessions;
    QMap<QString, ButtonGroup> buttonGroups;
    QMap<QString, QuickButton> quickButtons;
    GlobalSettings globalSettings;
    WindowLayout windowLayout;

    if (!loadConfigFromFile(filePath,
                            groups,
                            sessions,
                            buttonGroups,
                            quickButtons,
                            globalSettings,
                            windowLayout,
                            errorMessage)) {
        return false;
    }

    const auto oldGroups = groups_;
    const auto oldSessions = sessions_;
    const auto oldButtonGroups = buttonGroups_;
    const auto oldQuickButtons = quickButtons_;
    const auto oldGlobalSettings = globalSettings_;
    const auto oldWindowLayout = windowLayout_;

    groups_ = groups;
    sessions_ = sessions;
    buttonGroups_ = buttonGroups;
    quickButtons_ = quickButtons;
    globalSettings_ = globalSettings;
    windowLayout_ = windowLayout;

    if (!save()) {
        groups_ = oldGroups;
        sessions_ = oldSessions;
        buttonGroups_ = oldButtonGroups;
        quickButtons_ = oldQuickButtons;
        globalSettings_ = oldGlobalSettings;
        windowLayout_ = oldWindowLayout;
        if (errorMessage) {
            *errorMessage = QObject::tr("Failed to save imported configuration.");
        }
        return false;
    }

    emit sessionTreeUpdated();
    emit buttonGroupsChanged();
    emit quickButtonsChanged();
    emit globalSettingsChanged();
    return true;
}

bool ConfigManager::exportConfig(const QString& filePath, QString* errorMessage) {
    const QJsonObject root = buildConfigJson(groups(),
                                             sessions(),
                                             buttonGroups(),
                                             quickButtons(),
                                             globalSettings_,
                                             windowLayout_);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Cannot write file: %1").arg(file.errorString());
        }
        return false;
    }

    const QJsonDocument doc(root);
    if (file.write(doc.toJson(QJsonDocument::Indented)) == -1) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Write failed: %1").arg(file.errorString());
        }
        file.close();
        return false;
    }
#if defined(Q_OS_UNIX)
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Cannot restrict config file permissions: %1").arg(file.errorString());
        }
        file.close();
        return false;
    }
#endif
    file.close();
    return true;
}

// ==================== 会话管理 ====================

QList<SessionData> ConfigManager::sessions() const {
    QList<SessionData> result = sessions_.values();
    std::sort(result.begin(), result.end());
    return result;
}

SessionData ConfigManager::sessionById(const QString& id) const {
    return sessions_.value(id);
}

SessionData ConfigManager::sessionByName(const QString &name) const {
    QList<SessionData> result = sessions_.values();
    for (const SessionData &session : result) {
        if (session.name == name) {
            return session;
        }
    }

    SessionData session;
    session.id = "";
    return session;
}

void ConfigManager::addSession(const SessionData& session) {
    SessionData newSession = session;
    if (newSession.sortOrder == 0) {
        newSession.sortOrder = nextSessionSortOrder(newSession.groupId);
    }
    sessions_[newSession.id] = newSession;
    emit sessionTreeUpdated();
    save();
}

void ConfigManager::updateSession(const SessionData& session) {
    sessions_[session.id] = session;
    emit sessionTreeUpdated();
    save();
}

void ConfigManager::removeSession(const QString& id) {
    QString groupId = sessions_.value(id).groupId;
    sessions_.remove(id);
    normalizeSessionSortOrders(groupId);
    emit sessionTreeUpdated();
    save();
}

QList<SessionData> ConfigManager::sessionsByGroup(const QString& groupId) const {
    QList<SessionData> result;
    for (const auto& session : sessions_) {
        if (session.groupId == groupId) {
            result.append(session);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

int ConfigManager::nextSessionSortOrder(const QString& groupId) const {
    int maxOrder = -1;
    for (const auto& session : sessions_) {
        if (session.groupId == groupId && session.sortOrder > maxOrder) {
            maxOrder = session.sortOrder;
        }
    }
    return maxOrder + 1;
}

void ConfigManager::normalizeSessionSortOrders(const QString& groupId) {
    QList<SessionData> groupSessions = sessionsByGroup(groupId);
    for (int i = 0; i < groupSessions.size(); ++i) {
        if (sessions_.contains(groupSessions[i].id)) {
            sessions_[groupSessions[i].id].sortOrder = i;
        }
    }
}

void ConfigManager::moveSessionUp(const QString& id) {
    if (!sessions_.contains(id)) return;

    SessionData& session = sessions_[id];
    QList<SessionData> groupSessions = sessionsByGroup(session.groupId);

    int currentIndex = -1;
    for (int i = 0; i < groupSessions.size(); ++i) {
        if (groupSessions[i].id == id) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex > 0) {
        // 交换排序号
        QString prevId = groupSessions[currentIndex - 1].id;
        int tempOrder = sessions_[id].sortOrder;
        sessions_[id].sortOrder = sessions_[prevId].sortOrder;
        sessions_[prevId].sortOrder = tempOrder;

        emit sessionTreeUpdated();
        save();
    }
}

void ConfigManager::moveSessionDown(const QString& id) {
    if (!sessions_.contains(id)) return;

    SessionData& session = sessions_[id];
    QList<SessionData> groupSessions = sessionsByGroup(session.groupId);

    int currentIndex = -1;
    for (int i = 0; i < groupSessions.size(); ++i) {
        if (groupSessions[i].id == id) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex >= 0 && currentIndex < groupSessions.size() - 1) {
        // 交换排序号
        QString nextId = groupSessions[currentIndex + 1].id;
        int tempOrder = sessions_[id].sortOrder;
        sessions_[id].sortOrder = sessions_[nextId].sortOrder;
        sessions_[nextId].sortOrder = tempOrder;

        emit sessionTreeUpdated();
        save();
    }
}

void ConfigManager::moveSessionToIndex(const QString& id, int newIndex) {
    if (!sessions_.contains(id)) return;

    QString groupId = sessions_[id].groupId;
    QList<SessionData> groupSessions = sessionsByGroup(groupId);

    int currentIndex = -1;
    for (int i = 0; i < groupSessions.size(); ++i) {
        if (groupSessions[i].id == id) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex < 0 || currentIndex == newIndex) return;
    if (newIndex < 0 || newIndex >= groupSessions.size()) return;

    // 移动元素
    SessionData movedSession = groupSessions.takeAt(currentIndex);
    groupSessions.insert(newIndex, movedSession);

    // 更新所有排序号
    for (int i = 0; i < groupSessions.size(); ++i) {
        sessions_[groupSessions[i].id].sortOrder = i;
    }

    emit sessionTreeUpdated();
    save();
}

void ConfigManager::reorderSessions(const QStringList& sessionIds) {
    for (int i = 0; i < sessionIds.size(); ++i) {
        if (sessions_.contains(sessionIds[i])) {
            sessions_[sessionIds[i]].sortOrder = i;
        }
    }
    emit sessionTreeUpdated();
    save();
}

// ==================== 分组管理 ====================

QList<GroupData> ConfigManager::groups() const {
    QList<GroupData> result = groups_.values();
    std::sort(result.begin(), result.end());
    return result;
}

GroupData ConfigManager::group(const QString& id) const {
    return groups_.value(id);
}

void ConfigManager::addGroup(const GroupData& group) {
    GroupData newGroup = group;
    if (newGroup.sortOrder == 0 && !groups_.isEmpty()) {
        newGroup.sortOrder = nextGroupSortOrder();
    }
    groups_[newGroup.id] = newGroup;
    emit sessionTreeUpdated();
    save();
}

void ConfigManager::updateGroup(const GroupData& group) {
    groups_[group.id] = group;
    emit sessionTreeUpdated();
    save();
}

void ConfigManager::removeGroup(const QString& id) {
    groups_.remove(id);
    normalizeGroupSortOrders();
    emit sessionTreeUpdated();
    save();
}

bool ConfigManager::isGroupEmpty(const QString& groupId) const {
    for (const auto& session : sessions_) {
        if (session.groupId == groupId) {
            return false;
        }
    }
    return true;
}

int ConfigManager::nextGroupSortOrder() const {
    int maxOrder = -1;
    for (const auto& group : groups_) {
        if (group.sortOrder > maxOrder) {
            maxOrder = group.sortOrder;
        }
    }
    return maxOrder + 1;
}

void ConfigManager::normalizeGroupSortOrders() {
    QList<GroupData> sortedGroups = groups();
    for (int i = 0; i < sortedGroups.size(); ++i) {
        if (groups_.contains(sortedGroups[i].id)) {
            groups_[sortedGroups[i].id].sortOrder = i;
        }
    }
}

void ConfigManager::moveGroupUp(const QString& id) {
    if (!groups_.contains(id)) return;

    QList<GroupData> sortedGroups = groups();

    int currentIndex = -1;
    for (int i = 0; i < sortedGroups.size(); ++i) {
        if (sortedGroups[i].id == id) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex > 0) {
        QString prevId = sortedGroups[currentIndex - 1].id;
        int tempOrder = groups_[id].sortOrder;
        groups_[id].sortOrder = groups_[prevId].sortOrder;
        groups_[prevId].sortOrder = tempOrder;

        emit sessionTreeUpdated();
        save();
    }
}

void ConfigManager::moveGroupDown(const QString& id) {
    if (!groups_.contains(id)) return;

    QList<GroupData> sortedGroups = groups();

    int currentIndex = -1;
    for (int i = 0; i < sortedGroups.size(); ++i) {
        if (sortedGroups[i].id == id) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex >= 0 && currentIndex < sortedGroups.size() - 1) {
        QString nextId = sortedGroups[currentIndex + 1].id;
        int tempOrder = groups_[id].sortOrder;
        groups_[id].sortOrder = groups_[nextId].sortOrder;
        groups_[nextId].sortOrder = tempOrder;

        emit sessionTreeUpdated();
        save();
    }
}

void ConfigManager::moveGroupToIndex(const QString& id, int newIndex) {
    if (!groups_.contains(id)) return;

    QList<GroupData> sortedGroups = groups();

    int currentIndex = -1;
    for (int i = 0; i < sortedGroups.size(); ++i) {
        if (sortedGroups[i].id == id) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex < 0 || currentIndex == newIndex) return;
    if (newIndex < 0 || newIndex >= sortedGroups.size()) return;

    GroupData movedGroup = sortedGroups.takeAt(currentIndex);
    sortedGroups.insert(newIndex, movedGroup);

    for (int i = 0; i < sortedGroups.size(); ++i) {
        groups_[sortedGroups[i].id].sortOrder = i;
    }

    emit sessionTreeUpdated();
    save();
}

void ConfigManager::reorderGroups(const QStringList& groupIds) {
    for (int i = 0; i < groupIds.size(); ++i) {
        if (groups_.contains(groupIds[i])) {
            groups_[groupIds[i]].sortOrder = i;
        }
    }
    emit sessionTreeUpdated();
    save();
}

// ==================== 按钮分组管理 ====================

QList<ButtonGroup> ConfigManager::buttonGroups() const {
    QList<ButtonGroup> result = buttonGroups_.values();
    std::sort(result.begin(), result.end());
    return result;
}

ButtonGroup ConfigManager::buttonGroup(const QString& id) const {
    return buttonGroups_.value(id);
}

void ConfigManager::addButtonGroup(const ButtonGroup& group) {
    ButtonGroup newGroup = group;
    if (newGroup.sortOrder == 0 && !buttonGroups_.isEmpty()) {
        newGroup.sortOrder = nextButtonGroupSortOrder();
    }
    buttonGroups_[newGroup.id] = newGroup;
    emit buttonGroupsChanged();
    save();
}

void ConfigManager::updateButtonGroup(const ButtonGroup& group) {
    buttonGroups_[group.id] = group;
    emit buttonGroupsChanged();
    save();
}

void ConfigManager::removeButtonGroup(const QString& id) {
    buttonGroups_.remove(id);
    // 同时删除该分组下的所有按钮
    QStringList toRemove;
    for (const auto& btn : quickButtons_) {
        if (btn.groupId == id) {
            toRemove.append(btn.id);
        }
    }
    for (const auto& btnId : toRemove) {
        quickButtons_.remove(btnId);
    }
    normalizeButtonGroupSortOrders();
    emit buttonGroupsChanged();
    emit quickButtonsChanged();
    save();
}

int ConfigManager::nextButtonGroupSortOrder() const {
    int maxOrder = -1;
    for (const auto& group : buttonGroups_) {
        if (group.sortOrder > maxOrder) {
            maxOrder = group.sortOrder;
        }
    }
    return maxOrder + 1;
}

void ConfigManager::normalizeButtonGroupSortOrders() {
    QList<ButtonGroup> sorted = buttonGroups();
    for (int i = 0; i < sorted.size(); ++i) {
        if (buttonGroups_.contains(sorted[i].id)) {
            buttonGroups_[sorted[i].id].sortOrder = i;
        }
    }
}

void ConfigManager::moveButtonGroupUp(const QString& id) {
    if (!buttonGroups_.contains(id)) return;

    QList<ButtonGroup> sorted = buttonGroups();

    int currentIndex = -1;
    for (int i = 0; i < sorted.size(); ++i) {
        if (sorted[i].id == id) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex > 0) {
        QString prevId = sorted[currentIndex - 1].id;
        int tempOrder = buttonGroups_[id].sortOrder;
        buttonGroups_[id].sortOrder = buttonGroups_[prevId].sortOrder;
        buttonGroups_[prevId].sortOrder = tempOrder;

        emit buttonGroupsChanged();
        save();
    }
}

void ConfigManager::moveButtonGroupDown(const QString& id) {
    if (!buttonGroups_.contains(id)) return;

    QList<ButtonGroup> sorted = buttonGroups();

    int currentIndex = -1;
    for (int i = 0; i < sorted.size(); ++i) {
        if (sorted[i].id == id) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex >= 0 && currentIndex < sorted.size() - 1) {
        QString nextId = sorted[currentIndex + 1].id;
        int tempOrder = buttonGroups_[id].sortOrder;
        buttonGroups_[id].sortOrder = buttonGroups_[nextId].sortOrder;
        buttonGroups_[nextId].sortOrder = tempOrder;

        emit buttonGroupsChanged();
        save();
    }
}

void ConfigManager::moveButtonGroupToIndex(const QString& id, int newIndex) {
    if (!buttonGroups_.contains(id)) return;

    QList<ButtonGroup> sorted = buttonGroups();

    int currentIndex = -1;
    for (int i = 0; i < sorted.size(); ++i) {
        if (sorted[i].id == id) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex < 0 || currentIndex == newIndex) return;
    if (newIndex < 0 || newIndex >= sorted.size()) return;

    ButtonGroup moved = sorted.takeAt(currentIndex);
    sorted.insert(newIndex, moved);

    for (int i = 0; i < sorted.size(); ++i) {
        buttonGroups_[sorted[i].id].sortOrder = i;
    }

    emit buttonGroupsChanged();
    save();
}

void ConfigManager::reorderButtonGroups(const QStringList& groupIds) {
    for (int i = 0; i < groupIds.size(); ++i) {
        if (buttonGroups_.contains(groupIds[i])) {
            buttonGroups_[groupIds[i]].sortOrder = i;
        }
    }
    emit buttonGroupsChanged();
    save();
}

// ==================== 快捷按钮管理 ====================

QList<QuickButton> ConfigManager::quickButtons() const {
    QList<QuickButton> result = quickButtons_.values();
    std::sort(result.begin(), result.end());
    return result;
}

QList<QuickButton> ConfigManager::quickButtonsByGroup(const QString& groupId) const {
    QList<QuickButton> result;
    for (const auto& btn : quickButtons_) {
        if (btn.groupId == groupId) {
            result.append(btn);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

QuickButton ConfigManager::quickButton(const QString& id) const {
    return quickButtons_.value(id);
}

void ConfigManager::addQuickButton(const QuickButton& button) {
    QuickButton newButton = button;
    if (newButton.sortOrder == 0) {
        newButton.sortOrder = nextQuickButtonSortOrder(newButton.groupId);
    }
    quickButtons_[newButton.id] = newButton;
    emit quickButtonsChanged();
    save();
}

void ConfigManager::updateQuickButton(const QuickButton& button) {
    quickButtons_[button.id] = button;
    emit quickButtonsChanged();
    save();
}

void ConfigManager::removeQuickButton(const QString& id) {
    QString groupId = quickButtons_.value(id).groupId;
    quickButtons_.remove(id);
    normalizeQuickButtonSortOrders(groupId);
    emit quickButtonsChanged();
    save();
}

int ConfigManager::nextQuickButtonSortOrder(const QString& groupId) const {
    int maxOrder = -1;
    for (const auto& btn : quickButtons_) {
        if (btn.groupId == groupId && btn.sortOrder > maxOrder) {
            maxOrder = btn.sortOrder;
        }
    }
    return maxOrder + 1;
}

void ConfigManager::normalizeQuickButtonSortOrders(const QString& groupId) {
    QList<QuickButton> groupButtons = quickButtonsByGroup(groupId);
    for (int i = 0; i < groupButtons.size(); ++i) {
        if (quickButtons_.contains(groupButtons[i].id)) {
            quickButtons_[groupButtons[i].id].sortOrder = i;
        }
    }
}

void ConfigManager::moveQuickButtonUp(const QString& id) {
    if (!quickButtons_.contains(id)) return;

    QString groupId = quickButtons_[id].groupId;
    QList<QuickButton> groupButtons = quickButtonsByGroup(groupId);

    int currentIndex = -1;
    for (int i = 0; i < groupButtons.size(); ++i) {
        if (groupButtons[i].id == id) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex > 0) {
        QString prevId = groupButtons[currentIndex - 1].id;
        int tempOrder = quickButtons_[id].sortOrder;
        quickButtons_[id].sortOrder = quickButtons_[prevId].sortOrder;
        quickButtons_[prevId].sortOrder = tempOrder;

        emit quickButtonsChanged();
        save();
    }
}

void ConfigManager::moveQuickButtonDown(const QString& id) {
    if (!quickButtons_.contains(id)) return;

    QString groupId = quickButtons_[id].groupId;
    QList<QuickButton> groupButtons = quickButtonsByGroup(groupId);

    int currentIndex = -1;
    for (int i = 0; i < groupButtons.size(); ++i) {
        if (groupButtons[i].id == id) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex >= 0 && currentIndex < groupButtons.size() - 1) {
        QString nextId = groupButtons[currentIndex + 1].id;
        int tempOrder = quickButtons_[id].sortOrder;
        quickButtons_[id].sortOrder = quickButtons_[nextId].sortOrder;
        quickButtons_[nextId].sortOrder = tempOrder;

        emit quickButtonsChanged();
        save();
    }
}

void ConfigManager::moveQuickButtonToIndex(const QString& id, int newIndex) {
    if (!quickButtons_.contains(id)) return;

    QString groupId = quickButtons_[id].groupId;
    QList<QuickButton> groupButtons = quickButtonsByGroup(groupId);

    int currentIndex = -1;
    for (int i = 0; i < groupButtons.size(); ++i) {
        if (groupButtons[i].id == id) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex < 0 || currentIndex == newIndex) return;
    if (newIndex < 0 || newIndex >= groupButtons.size()) return;

    QuickButton moved = groupButtons.takeAt(currentIndex);
    groupButtons.insert(newIndex, moved);

    for (int i = 0; i < groupButtons.size(); ++i) {
        quickButtons_[groupButtons[i].id].sortOrder = i;
    }

    emit quickButtonsChanged();
    save();
}

void ConfigManager::reorderQuickButtons(const QStringList& buttonIds) {
    for (int i = 0; i < buttonIds.size(); ++i) {
        if (quickButtons_.contains(buttonIds[i])) {
            quickButtons_[buttonIds[i]].sortOrder = i;
        }
    }
    emit quickButtonsChanged();
    save();
}

// ==================== 全局设置 ====================

GlobalSettings ConfigManager::globalSettings() const {
    return globalSettings_;
}

void ConfigManager::setGlobalSettings(const GlobalSettings& settings) {
    globalSettings_ = settings;
    emit globalSettingsChanged();
    save();
}

// ==================== ui 布局 ====================
void ConfigManager::showToolBar(bool show) {
    windowLayout_.showToolBar = show;
    save();
}

void ConfigManager::showSessions(bool show) {
    windowLayout_.showSessions = show;
    save();
}

void ConfigManager::showCommandWindow(bool show) {
    windowLayout_.showCommandWindow = show;
    save();
}

void ConfigManager::showCommandButton(bool show) {
    windowLayout_.showCommandButton = show;
    save();
}

void ConfigManager::expendSessionDock(bool expend) {
    windowLayout_.expendSessionDock = expend;
    save();
}

void ConfigManager::setSessionDockWidth(int width) {
    windowLayout_.sessionDockWidth = width;
    save();
}

void ConfigManager::setCommandWindowHeight(int height) {
    windowLayout_.commandWindowHeight = height;
    save();
}

WindowLayout ConfigManager::getWindowLayout() const {
    return windowLayout_;
}
