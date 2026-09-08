#include "CommandHistory.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <utility>

CommandHistory::CommandHistory(QString filePath) : filePath_(std::move(filePath)) {
    QFile file(filePath_);
    if (file.open(QIODevice::ReadOnly)) {
        const auto array = QJsonDocument::fromJson(file.readAll()).array();
        for (const auto &value: array) {
            remember(value.toString());
        }
    } else if (!file.exists()) {
        QFile legacy(QFileInfo(filePath_).dir().filePath("command_history.txt"));
        if (legacy.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!legacy.atEnd()) {
                remember(QString::fromUtf8(legacy.readLine()));
            }
            save();
        }
    }
}

CommandHistory &CommandHistory::instance() {
    static CommandHistory history(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/command_history.json");
    return history;
}

const QStringList &CommandHistory::commands() const {
    return commands_;
}

void CommandHistory::remember(const QString &command) {
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    commands_.removeAll(trimmed);
    commands_.append(trimmed);
    while (commands_.size() > 1000) {
        commands_.removeFirst();
    }
}

void CommandHistory::add(const QString &command) {
    if (command.trimmed().isEmpty()) {
        return;
    }
    remember(command);
    save();
    emit changed();
}

void CommandHistory::remove(const QStringList &commands) {
    bool removed = false;
    for (const QString &command : commands) {
        removed = commands_.removeAll(command) > 0 || removed;
    }
    if (removed) {
        save();
        emit changed();
    }
}

void CommandHistory::clear() {
    commands_.clear();
    save();
    emit changed();
}

void CommandHistory::save() const {
    QDir().mkpath(QFileInfo(filePath_).absolutePath());
    QSaveFile file(filePath_);
    const QByteArray data = QJsonDocument(QJsonArray::fromStringList(commands_)).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        qWarning() << "Failed to save command history:" << file.errorString();
    }
}

QStringList CommandHistory::matches(const QString &query) const {
    QStringList result;
    if (query.trimmed().isEmpty()) {
        return result;
    }
    for (auto it = commands_.crbegin(); it != commands_.crend(); ++it) {
        if (it->startsWith(query, Qt::CaseInsensitive)) {
            result.append(*it);
            if (result.size() == 20) break;
        }
    }
    return result;
}
