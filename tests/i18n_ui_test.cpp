#include "core/ConfigManager.h"
#include "core/LanguageManager.h"
#include "ui/MainWindow.h"
#include "ui/SettingDialog.h"
#include "ui/command/CommandButtonBar.h"
#include "ui/command/CommandHistoryDialog.h"
#include "ui/command/CommandWindow.h"
#include "ui/session/GroupEditDialog.h"
#include "ui/session/SessionEditDialog.h"
#include "ui/terminal/BaseTerminal.h"
#include "ui/terminal/SerialTerminal.h"

#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QXmlStreamReader>
#include <cstdlib>
#include <functional>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        qCritical() << message;
        std::exit(1);
    }
}

QStringList placeholders(const QString &text) {
    QStringList result;
    auto matches = QRegularExpression("%[1-9][0-9]*|%n").globalMatch(text);
    while (matches.hasNext()) {
        result.append(matches.next().captured());
    }
    result.sort();
    return result;
}

void verifyCatalog(const QString &language) {
    QFile file(QStringLiteral(QSHELL_TRANSLATION_DIR) + "/qshell_" + language + ".ts");
    require(file.open(QIODevice::ReadOnly), "cannot read translation catalog");
    QXmlStreamReader xml(&file);
    QString context;
    int messages = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        if (xml.name() == u"name") context = xml.readElementText();
        if (xml.name() != u"message") continue;
        QString source;
        QString translation;
        while (xml.readNextStartElement()) {
            if (xml.name() == u"source") source = xml.readElementText();
            else if (xml.name() == u"translation") {
                require(!xml.attributes().hasAttribute("type"), "unfinished translation");
                translation = xml.readElementText();
            } else
                xml.skipCurrentElement();
        }
        require(!translation.isEmpty(), "empty translation");
        require(placeholders(source) == placeholders(translation), "translation changed placeholders");
        const QString actual = QCoreApplication::translate(context.toUtf8(), source.toUtf8());
        if (actual != translation) {
            qCritical() << language << context << source << actual << translation;
            require(false, "embedded translation does not match catalog");
        }
        ++messages;
    }
    require(!xml.hasError() && messages >= 342, "catalog is incomplete or invalid");
}

QStringList contextMenuTexts(QWidget *widget) {
    QStringList texts;
    QTimer::singleShot(0, widget, [&texts]() {
        auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        require(popup != nullptr, "context menu did not open");
        for (auto *action: popup->actions()) {
            texts.append(action->text());
        }
        popup->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(10, 10), widget->mapToGlobal(QPoint(10, 10)));
    QApplication::sendEvent(widget, &event);
    return texts;
}

bool hasLabel(const QWidget &widget, const QString &text) {
    for (const auto *label: widget.findChildren<QLabel *>()) {
        if (label->text() == text) return true;
    }
    return false;
}
}// namespace

int main(int argc, char *argv[]) {
    QTemporaryDir storage;
    require(storage.isValid(), "cannot create isolated storage");
    // Child processes inherit the same isolated settings to test restart behavior.
    if (argc == 1) {
        qputenv("HOME", storage.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", storage.path().toUtf8());
        qputenv("XDG_DATA_HOME", storage.path().toUtf8());
    }
    Q_INIT_RESOURCE(qtermwidget);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("qshell-i18n-test");
    QCoreApplication::setApplicationName("qshell-i18n-test");
    auto *languages = LanguageManager::instance();
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--check-language") {
        require(languages->language() == QString::fromLocal8Bit(argv[2]), "language was not restored on restart");
        return 0;
    }
    require(LanguageManager::languageForLocale(QLocale("zh_CN")) == "zh_CN", "simplified locale detection");
    require(LanguageManager::languageForLocale(QLocale("zh_SG")) == "zh_CN", "Singapore locale detection");
    require(LanguageManager::languageForLocale(QLocale("zh_TW")) == "zh_TW", "traditional locale detection");
    require(LanguageManager::languageForLocale(QLocale("zh_HK")) == "zh_TW", "Hong Kong locale detection");
    require(LanguageManager::languageForLocale(QLocale("de_DE")) == "en", "unsupported locale fallback");

    MainWindow window;
    window.show();
    auto *menu = window.findChild<QMenu *>("languageMenu");
    require(menu != nullptr && menu->actions().size() == 3, "missing top-level language menu");
    require(window.menuBar()->actions().contains(menu->menuAction()), "language menu is not in menu bar");
    auto *command = window.findChild<CommandWindow *>();
    auto *editor = qobject_cast<QTextEdit *>(command->widget());
    editor->setPlainText("echo keep this input 中文");
    SessionData session;
    session.id = "i18n-live-session";
    session.name = "用户会话 User Session";
    session.protocolType = ProtocolType::LocalShell;
    ConfigManager::instance()->addSession(session);
    require(window.openSessionById(session.id), "cannot open live tab");
    auto *terminal = window.getCurrentSession();
    require(terminal != nullptr && terminal->isConnect(), "live terminal was not connected");
    terminal->toggleShowSearchBar();

    SessionData binarySession;
    binarySession.protocolType = ProtocolType::Serial;
    binarySession.serialConfig.dataMode = SerialDataMode::Bin;
    SerialTerminal binaryTerminal(binarySession, nullptr);
    auto *binaryInput = binaryTerminal.findChild<QLineEdit *>();
    require(binaryInput != nullptr, "missing serial input");
    binaryInput->setText("41 00 FF");

    // Drive the real progress UI without starting a file transfer.
    auto *transfer = terminal->findChild<ZmodemTransfer *>();
    require(transfer != nullptr, "missing ZMODEM transfer object");
    transfer->fileStarted(ZmodemTransfer::Direction::Download, "file.dat", 1024, 1, 2);
    auto *progress = terminal->findChild<QProgressDialog *>();
    require(progress != nullptr, "missing transfer progress dialog");

    const QStringList codes = {"zh_CN", "zh_TW", "en", "zh_CN"};
    const QStringList fileNames = {"文件", "文件", "File", "文件"};
    const QStringList fontLabels = {"字体：", "字體：", "Font Family:", "字体："};
    const QStringList sessionTitles = {"会话属性", "會話屬性", "Session Properties", "会话属性"};
    const QStringList cancelLabels = {"取消", "取消", "Cancel", "取消"};
    for (int i = 0; i < codes.size(); ++i) {
        for (auto *action: menu->actions()) {
            if (action->data().toString() == codes[i]) action->trigger();
        }
        QApplication::processEvents();
        require(languages->language() == codes[i], "menu did not change language");
        int checked = 0;
        for (auto *action: menu->actions()) {
            if (action->isChecked()) {
                ++checked;
                require(action->data().toString() == codes[i], "wrong language checked");
            }
        }
        require(checked == 1, "language choices are not exclusive");
        require(window.menuBar()->actions().first()->text() == fileNames[i], "menu not retranslated");
        require(window.getCurrentSession() == terminal && terminal->isConnect() && window.tabCount() == 1,
                "language change replaced or disconnected live terminal");
        require(window.currentTabName() == session.name, "user session name was translated");
        require(editor->toPlainText() == "echo keep this input 中文", "command input lost on switch");
        require(editor->placeholderText() == CommandWindow::tr("Enter command here... (Press Enter to send, Ctrl+Enter for new line)"),
                "command placeholder not updated");
        require(binaryInput->text() == "41 00 FF" && binaryInput->placeholderText() ==
                                                             SerialTerminal::tr("Hex bytes, e.g. 41 00 FF (Enter to send)"),
                "serial input not retranslated in place");
        require(hasLabel(*terminal, QCoreApplication::translate("SearchBar", "Find:")), "terminal search bar not retranslated");
        auto *buttons = window.findChild<CommandButtonBar *>();
        require(buttons->findChild<QComboBox *>()->currentText() == CommandButtonBar::tr("(无分组)"),
                "command group placeholder not updated");
        require(progress->windowTitle() == BaseTerminal::tr("ZMODEM 文件传输") &&
                        progress->labelText().contains(BaseTerminal::tr("正在下载")) &&
                        progress->labelText().contains("file.dat"),
                "active transfer progress not retranslated");
        const auto terminalMenu = contextMenuTexts(terminal);
        require(terminalMenu.contains(BaseTerminal::tr("复制")) &&
                        terminalMenu.contains(BaseTerminal::tr("分屏")) &&
                        terminalMenu.contains(BaseTerminal::tr("保存日志")),
                "terminal context menu not translated");
        SettingDialog settings(&window);
        require(hasLabel(settings, fontLabels[i]), "settings labels not translated");
        require(settings.windowTitle() == SettingDialog::tr("Settings"), "settings title not translated");
        SessionEditDialog properties(&window);
        require(properties.windowTitle() == sessionTitles[i], "session properties not translated");
        auto *box = properties.findChild<QDialogButtonBox *>();
        require(box && box->button(QDialogButtonBox::Cancel)->text() == cancelLabels[i], "Qt standard buttons not translated");
        GroupEditDialog group(&window);
        require(group.windowTitle() == GroupEditDialog::tr("Group Properties"), "group dialog not translated");
        CommandHistoryDialog history(&window);
        require(history.windowTitle() == CommandHistoryDialog::tr("Command History"), "history dialog not translated");
        verifyCatalog(codes[i]);
        QSettings().sync();
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--check-language", codes[i]});
        require(child.waitForFinished(5000) && child.exitCode() == 0, "restart did not restore selected language");
        if (qEnvironmentVariableIsSet("QSHELL_I18N_SCREENSHOTS")) {
            settings.show();
            QApplication::processEvents();
            window.grab().save(qEnvironmentVariable("QSHELL_I18N_SCREENSHOTS") + "/main-" + codes[i] + ".png");
            settings.grab().save(qEnvironmentVariable("QSHELL_I18N_SCREENSHOTS") + "/settings-" + codes[i] + ".png");
        }
    }
    const QString previous = languages->language();
    require(!languages->setLanguage("unsupported") && languages->language() == previous,
            "invalid language changed state");
    terminal->disconnect();
    qInfo() << "All language catalogs, UI switching, live session preservation and restart checks passed";
    return 0;
}
