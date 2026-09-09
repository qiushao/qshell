#include "LanguageManager.h"

#include <QCoreApplication>
#include <QSettings>

// Referencing the resources explicitly also links them from the static library.
static void initTranslationResources() {
    Q_INIT_RESOURCE(qshell_translations);
    Q_INIT_RESOURCE(qshell_qt_translations);
}

LanguageManager *LanguageManager::instance() {
    static auto *manager = new LanguageManager(QCoreApplication::instance());
    return manager;
}

LanguageManager::LanguageManager(QObject *parent) : QObject(parent) {
    initTranslationResources();
    const QString saved = QSettings().value("language").toString();
    if (!setLanguage(saved)) {
        setLanguage(languageForLocale(QLocale::system()));
    }
}

QString LanguageManager::languageForLocale(const QLocale &locale) {
    if (locale.language() == QLocale::Chinese) {
        return locale.script() == QLocale::TraditionalHanScript ? "zh_TW" : "zh_CN";
    }
    return "en";
}

QString LanguageManager::language() const {
    return language_;
}

bool LanguageManager::setLanguage(const QString &language) {
    if (language != "zh_CN" && language != "zh_TW" && language != "en") {
        return false;
    }
    if (language == language_) {
        return true;
    }

    QCoreApplication::removeTranslator(&translator_);
    QCoreApplication::removeTranslator(&qtTranslator_);
    if (!translator_.load(":/i18n/qshell_" + language + ".qm")) {
        return false;
    }
    if (qtTranslator_.load(":/i18n/qtbase_" + language + ".qm")) {
        QCoreApplication::installTranslator(&qtTranslator_);
    }
    language_ = language;
    QLocale::setDefault(QLocale(language));
    QCoreApplication::installTranslator(&translator_);
    QSettings().setValue("language", language);
    return true;
}
