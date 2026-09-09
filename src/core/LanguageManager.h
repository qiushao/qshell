#ifndef QSHELL_LANGUAGEMANAGER_H
#define QSHELL_LANGUAGEMANAGER_H

#include <QLocale>
#include <QObject>
#include <QTranslator>

class LanguageManager : public QObject {
    Q_OBJECT

public:
    static LanguageManager *instance();
    static QString languageForLocale(const QLocale &locale);
    QString language() const;
    bool setLanguage(const QString &language);

private:
    explicit LanguageManager(QObject *parent);
    QString language_;
    QTranslator translator_;
    QTranslator qtTranslator_;
};

#endif// QSHELL_LANGUAGEMANAGER_H
