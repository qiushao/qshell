#ifndef QSHELL_COMMANDHISTORY_H
#define QSHELL_COMMANDHISTORY_H

#include <QStringList>
#include <QObject>

class CommandHistory : public QObject {
    Q_OBJECT
public:
    explicit CommandHistory(QString filePath);
    static CommandHistory &instance();
    const QStringList &commands() const;
    void add(const QString &command);
    void remove(const QStringList &commands);
    void clear();
    QStringList matches(const QString &query) const;

signals:
    void changed();

private:
    void remember(const QString &command);
    void save() const;

    QString filePath_;
    QStringList commands_;
};

#endif
