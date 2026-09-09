#include "TerminalDisplay.h"
#include "core/CommandHistory.h"
#include "core/ConfigManager.h"
#include "ui/command/CommandHistoryDialog.h"
#include "ptyqt.h"
#include "qtermwidget.h"
#include "ui/command/CommandWindow.h"
#include "ui/terminal/BaseTerminal.h"
#include "ui/terminal/TerminalCommandCompletion.h"

#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QMessageBox>
#include <QTimer>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTextStream>
#include <QThread>
#include <cstdlib>
#include <functional>
#include <memory>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        qCritical() << message;
        std::exit(1);
    }
}

void key(QWidget *widget, int code, const QString &text = {}, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent event(QEvent::KeyPress, code, modifiers, text);
    QApplication::sendEvent(widget, &event);
    QApplication::processEvents();
}

void storageTest(const QString &directory) {
    const QString path = directory + "/command_history.json";
    QFile legacy(directory + "/command_history.txt");
    require(legacy.open(QIODevice::WriteOnly), "legacy file creation failed");
    legacy.write("old command\nother command\nold command\n");
    legacy.close();
    CommandHistory history(path);
    require(history.commands() == QStringList({"other command", "old command"}), "legacy history was not migrated and deduplicated");
    for (int i = 0; i < 1005; ++i) history.add(QString("command %1").arg(i));
    require(history.commands().size() == 1000 && history.commands().first() == "command 5", "history did not evict oldest commands at 1000");
    history.add("command 10");
    require(history.commands().size() == 1000 && history.commands().last() == "command 10", "duplicate was not moved to most recent");
    history.add("  \n  ");
    require(history.commands().size() == 1000, "empty command was recorded");
    history.add(QString::fromUtf8("echo 中文\necho second"));
    CommandHistory reloaded(path);
    require(reloaded.commands() == history.commands(), "history did not survive reload, including multiline Unicode");
    const QString multiline = QString::fromUtf8("echo 中文\necho second");
    history.remove({"command 10", multiline, "missing command"});
    require(!history.commands().contains("command 10") && !history.commands().contains(multiline), "selected history records were not removed");
    require(CommandHistory(path).commands() == history.commands(), "deleted records reappeared after reload");
    require(!history.matches("command 10").contains("command 10"), "deleted command still appeared in suggestions");
    require(history.matches("command").size() == 20, "suggestions were not capped at 20");
    history.clear();
    require(CommandHistory(path).commands().isEmpty(), "cleared history reloaded legacy commands");
    history.add("git status");
    history.add("git stash");
    history.add("git show");
    history.add("echo git status");
    history.add("git");
    require(history.matches("GIT").first() == "git", "case-insensitive prefix matching failed");
    require(history.matches("git st").first() == "git stash", "prefix matches should prefer recency");
    require(history.matches("gts").isEmpty(), "noncontiguous match should not be suggested");
    require(history.matches("status").isEmpty(), "substring match should not be suggested");
    require(history.matches("unmatched").isEmpty() && history.matches(" ").isEmpty(), "empty or unmatched query returned suggestions");
    history.clear();
    history.add("git aaa");
    history.add("git bbb");
    require(history.matches("git").first() == "git bbb", "equally ranked matches did not prefer recency");
    history.clear();
    history.add("cd sources/android-projects");
    history.add("cd sources/clion-projects/qshell/");
    require(history.matches("cd sources/").size() == 2, "shared prefix did not match both paths");
    require(history.matches("cd sources/android-projects") == QStringList({"cd sources/android-projects"}), "unrelated path survived complete prefix matching");
    require(history.matches("cd sources/android-projects ").isEmpty(), "prefix matching ignored a typed trailing space");
}

void editorTest() {
    auto &history = CommandHistory::instance();
    history.clear();
    history.add("git status");
    history.add("git stash");
    CommandWindow window;
    window.resize(640, 100);
    window.show();
    auto *editor = qobject_cast<QTextEdit *>(window.widget());
    editor->setFocus();
    QApplication::processEvents();
    QStringList sent;
    QObject::connect(&window, &CommandWindow::commandSend, [&](const QString &command) { sent.append(command); });
    auto *popup = editor->findChild<QListWidget *>("commandCompletionPopup");
    editor->setPlainText("git st");
    QApplication::processEvents();
    require(popup->isVisible() && popup->count() == 2, "editor prefix popup did not appear");
    auto *config = ConfigManager::instance();
    auto settings = config->globalSettings();
    settings.commandHistoryCompletion = false;
    config->setGlobalSettings(settings);
    require(!popup->isVisible(), "disabling completion did not hide the editor popup immediately");
    require(config->load() && !config->globalSettings().commandHistoryCompletion, "disabled completion setting did not survive reload");
    editor->setPlainText("git s");
    require(!popup->isVisible(), "editor showed suggestions while completion was disabled");
    settings.commandHistoryCompletion = true;
    config->setGlobalSettings(settings);
    editor->setPlainText("git st");
    require(popup->isVisible(), "re-enabling completion did not restore editor suggestions");
    const QString screenshot = qEnvironmentVariable("QSHELL_TEST_SCREENSHOT");
    if (!screenshot.isEmpty()) {
        const QRect windowRect(window.mapToGlobal(QPoint()), window.size());
        const QRect bounds = windowRect.united(popup->geometry());
        QImage image(bounds.size(), QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.drawPixmap(windowRect.topLeft() - bounds.topLeft(), window.grab());
        painter.drawPixmap(popup->geometry().topLeft() - bounds.topLeft(), popup->grab());
        painter.end();
        require(image.save(screenshot), "could not save popup screenshot");
    }
    key(editor, Qt::Key_Down);
    key(editor, Qt::Key_Return, "\r");
    require(editor->toPlainText() == "git stash" && sent.isEmpty() && !popup->isVisible(), "selected Enter should fill the best match without sending");
    key(editor, Qt::Key_Return, "\r");
    require(sent == QStringList({"git stash\r"}) && editor->toPlainText().isEmpty(), "Enter should submit filled command and clear editor");
    editor->setPlainText("git st");
    key(editor, Qt::Key_Down);
    key(editor, Qt::Key_Down);
    key(editor, Qt::Key_Return, "\r");
    require(editor->toPlainText() == "git status" && sent.size() == 1, "selected Enter should fill without sending");
    editor->setPlainText("git");
    key(editor, Qt::Key_Escape);
    require(!popup->isVisible() && editor->toPlainText() == "git", "Escape should dismiss without changing input");
    editor->setPlainText("echo original");
    key(editor, Qt::Key_Return, "\r");
    require(history.commands().last() == "echo original", "editor submission was not automatically recorded");
    editor->setPlainText("draft");
    key(editor, Qt::Key_Up, {}, Qt::ControlModifier);
    require(editor->toPlainText() == "echo original", "Ctrl+Up history navigation regressed");
    key(editor, Qt::Key_Down, {}, Qt::ControlModifier);
    require(editor->toPlainText() == "draft", "history navigation did not restore draft");
    history.add("echo from another session");
    key(editor, Qt::Key_Up, {}, Qt::ControlModifier);
    require(editor->toPlainText() == "echo from another session", "navigation missed another session's latest command");
    editor->setPlainText("git st");
    const QPoint position = popup->visualItemRect(popup->item(1)).center();
    const QPoint global = popup->viewport()->mapToGlobal(position);
    QMouseEvent press(QEvent::MouseButtonPress, position, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, position, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(popup->viewport(), &press);
    QApplication::sendEvent(popup->viewport(), &release);
    require(editor->toPlainText() == "git status" && !popup->isVisible(), "mouse selection did not fill command");
    history.clear();
    for (int i = 0; i < 25; ++i) history.add(QString("candidate %1").arg(i, 2, 10, QLatin1Char('0')));
    editor->setPlainText("candidate");
    require(popup->count() == 20 && popup->item(0)->text() == "candidate 24" && popup->item(19)->text() == "candidate 05",
            "popup did not retain the 20 best matches in recency order");
    key(editor, Qt::Key_Up);
    key(editor, Qt::Key_Return, "\r");
    require(editor->toPlainText() == "candidate 05", "last candidate was not keyboard accessible");
    history.clear();
    history.add("cd sources/clion-projects/qshell/");
    editor->setPlainText("cd sources/");
    require(popup->isVisible(), "shared path prefix did not show history");
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText("android-projects");
    require(!popup->isVisible(), "old path candidate remained after input stopped matching");
}

void managementTest() {
    auto &history = CommandHistory::instance();
    history.clear();
    history.add("git status");
    history.add("git sttaus");
    const QString multiline = QString::fromUtf8("echo 错误\necho second");
    history.add(multiline);

    CommandWindow window;
    window.show();
    auto *editor = qobject_cast<QTextEdit *>(window.widget());
    editor->setFocus();
    QApplication::processEvents();
    editor->setPlainText("git");
    auto *popup = editor->findChild<QListWidget *>("commandCompletionPopup");
    require(popup->isVisible() && popup->count() == 2, "could not show suggestions before deletion");
    history.remove({"git sttaus"});
    require(popup->isVisible() && popup->count() == 1 && popup->item(0)->text() == "git status", "visible popup retained a deleted command");
    history.remove({"git status"});
    require(!popup->isVisible(), "empty suggestions did not close after deletion");

    history.add("git status");
    history.add("git sttaus");
    CommandHistoryDialog dialog;
    dialog.show();
    dialog.activateWindow();
    auto *list = dialog.findChild<QListWidget *>("commandHistoryList");
    auto *deleteButton = dialog.findChild<QPushButton *>("deleteHistorySelection");
    list->setFocus();
    QApplication::processEvents();
    require(list->count() == 3 && !deleteButton->isEnabled(), "history manager did not load records or disable empty deletion");
    list->item(0)->setSelected(true);
    list->item(2)->setSelected(true);
    require(deleteButton->isEnabled(), "selected records could not be deleted");
    const QString screenshot = qEnvironmentVariable("QSHELL_HISTORY_MANAGER_SCREENSHOT");
    if (!screenshot.isEmpty()) require(dialog.grab().save(screenshot), "could not save manager screenshot");
    deleteButton->click();
    require(history.commands() == QStringList({"git status"}) && list->count() == 1 && !deleteButton->isEnabled(), "multi-selection deletion removed the wrong commands");
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/command_history.json";
    require(CommandHistory(path).commands() == history.commands(), "manager deletions were not persisted immediately");
    require(history.matches("git sttaus").isEmpty() && !history.commands().contains(multiline), "deleted incorrect or multiline commands were retained");
    list->setCurrentRow(0);
    list->setFocus();
    key(list, Qt::Key_Delete);
    require(history.commands().isEmpty() && list->count() == 0, "Delete shortcut did not delete the selected record");
    dialog.close();
    CommandHistoryDialog reopened;
    require(reopened.findChild<QListWidget *>("commandHistoryList")->count() == 0, "deleted records reappeared when manager was reopened");

    // The existing clear action should confirm once and clear the shared history.
    history.add("echo retained");
    int confirmations = 0;
    QTimer answer;
    QObject::connect(&answer, &QTimer::timeout, [&]() {
        if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            ++confirmations;
            message->button(QMessageBox::Yes)->click();
        }
    });
    answer.start(10);
    QTimer::singleShot(0, [&]() {
        auto *manager = qobject_cast<CommandHistoryDialog *>(QApplication::activeModalWidget());
        require(manager != nullptr, "history shortcut did not open the manager");
        for (auto *button : manager->findChildren<QPushButton *>()) {
            if (button->text() == "Clear History") button->click();
        }
        manager->accept();
    });
    window.showHistoryDialog();
    answer.stop();
    require(confirmations == 1 && history.commands().isEmpty(), "clearing history should require exactly one confirmation");
}

void terminalTest() {
    auto &history = CommandHistory::instance();
    history.clear();
    QTermWidget terminal;
    terminal.setHistorySize(1000);
    terminal.resize(640, 400);
    terminal.show();
    auto *display = terminal.findChild<TerminalDisplay *>();
    display->setFocus();
    QApplication::processEvents();
    TerminalCommandCompletion completion(display);
    auto output = [&](const QByteArray &data) {
        terminal.recvData(data.constData(), static_cast<int>(data.size()));
        completion.afterOutput();
        QApplication::processEvents();
    };
    output("user$ ");
    completion.beforeSend("echo hello");
    output("echo hello");
    completion.beforeSend("\r");
    output("\r\nhello\r\nuser$ ");
    require(history.commands() == QStringList({"echo hello"}), "terminal command was not automatically recorded");
    completion.beforeSend("echo delayed\r");
    output("echo delayed\r\ndelayed\r\nuser$ ");
    require(history.commands().last() == "echo delayed", "paste with Enter before echo was not recorded");
    completion.beforeSend("secret");
    completion.beforeSend("\r");
    output("\r\nuser$ ");
    require(!history.commands().contains("secret") && history.commands().size() == 2, "non-echoed password was recorded");
    completion.beforeSend("ech");
    output("ech");
    auto *popup = display->findChild<QListWidget *>("commandCompletionPopup");
    require(popup->isVisible() && popup->count() == 2, "terminal popup did not appear for echoed input");
    QByteArray replacement;
    QObject::connect(&completion, &TerminalCommandCompletion::replaceInput, [&](const QByteArray &data) { replacement = data; });
    QByteArray sent;
    QObject::connect(&terminal, &QTermWidget::sendData, [&](const char *data, int size) { sent.append(data, size); });
    key(display, Qt::Key_Down);
    key(display, Qt::Key_Tab, "\t");
    require(sent == "\t" && replacement.isEmpty() && !popup->isVisible(), "Tab must reach the shell without selecting a history candidate");
    completion.afterOutput();
    key(display, Qt::Key_Down);
    key(display, Qt::Key_Return, "\r");
    require(replacement == QByteArray("\x05\x7f\x7f\x7f") + "echo delayed", "terminal completion did not replace the current input without executing");
    completion.reset();
    output("\r\nuser$ ");
    completion.beforeSend("cancelled");
    output("cancelled");
    completion.beforeSend("\x03");
    output("^C\r\nuser$ ");
    require(!history.commands().contains("cancelled"), "cancelled command was recorded");
    output("\x1b[?1049h");
    completion.beforeSend("editor text\r");
    output("editor text\r\n");
    require(!history.commands().contains("editor text"), "alternate screen input was recorded");
    output("\x1b[?1049l");
    const QString wrapped = QStringLiteral("echo ") + QString(200, QLatin1Char('x')) + QString::fromUtf8(" 中文");
    completion.beforeSend(wrapped.toUtf8());
    output(wrapped.toUtf8());
    completion.beforeSend("\r");
    output("\r\nuser$ ");
    require(history.commands().last() == wrapped, "wrapped Unicode command was not recorded intact");
    completion.beforeSend("clear\r");
    output("clear\r\n\x1b[2J\x1b[H");
    require(history.commands().last() == "clear", "screen-clearing command was lost before history capture");
    completion.beforeSend("ech");
    output("ech");
    require(popup->isVisible(), "terminal suggestions missing before disabling completion");
    auto *config = ConfigManager::instance();
    auto settings = config->globalSettings();
    settings.commandHistoryCompletion = false;
    config->setGlobalSettings(settings);
    require(!popup->isVisible(), "disabling completion did not hide the terminal popup immediately");
    output("o disabled");
    require(!popup->isVisible(), "terminal showed suggestions while completion was disabled");
    completion.beforeSend("\r");
    output("\r\nuser$ ");
    require(history.commands().last() == "echo disabled", "disabling suggestions stopped terminal history recording");
    settings.commandHistoryCompletion = true;
    config->setGlobalSettings(settings);
    completion.beforeSend("ech");
    output("ech");
    require(popup->isVisible(), "re-enabling completion did not restore terminal suggestions");
    for (const QString &input : {QString::fromUtf8("cd 中文/ "), QStringLiteral("cd ") + QString(terminal.screenColumnsCount() - 9, QLatin1Char('x')) + "  "}) {
        completion.reset();
        output("\r\nuser$ ");
        history.clear();
        const QString command = input + "target/";
        history.add(command);
        completion.beforeSend(input.toUtf8());
        output(input.toUtf8());
        require(popup->isVisible(), "Unicode or wrapped input with trailing spaces did not show suggestions");
        replacement.clear();
        key(display, Qt::Key_Down);
        key(display, Qt::Key_Return, "\r");
        require(replacement == QByteArray("\x05") + QByteArray(input.size(), '\x7f') + command.toUtf8(),
                "Unicode or wrapped completion did not preserve trailing spaces when erasing input");
    }
    terminal.hide();
}

void terminalResponseTest() {
    class TestTerminal : public BaseTerminal {
    public:
        TestTerminal() : BaseTerminal(nullptr) {
            sessionData_.protocolType = ProtocolType::LocalShell;
            connect_ = true;
        }
        void connect() override {}
        void disconnect() override {}
        void writeToBackend(const QByteArray &data) override { sent += data; }
        using BaseTerminal::displayTerminalData;
        QByteArray sent;
    };

    auto &history = CommandHistory::instance();
    history.clear();
    const QString border = QString::fromUtf8("╭──────────────────────────────────────────────────╮");
    history.add(border);
    history.add("echo hello");
    TestTerminal terminal;
    terminal.resize(800, 400);
    terminal.show();
    auto *display = terminal.findChild<TerminalDisplay *>();
    display->setFocus();
    QApplication::processEvents();
    auto *popup = display->findChild<QListWidget *>("commandCompletionPopup");
    auto output = [&](const QByteArray &data) {
        terminal.displayTerminalData(data);
        QApplication::processEvents();
    };
    output("user$ ");
    terminal.sendText("codex\r");
    output("codex\r\n");
    require(history.commands().last() == "codex", "application launch command was not recorded");
    const QStringList saved = history.commands();
    for (const QByteArray &query : {QByteArray("\x1b[6n"), QByteArray("\x1b[5n"), QByteArray("\x1b[c"), QByteArray("\x1b[>c")}) {
        terminal.sent.clear();
        output(query);
        require(!terminal.sent.isEmpty(), "terminal query response did not reach the backend");
        output(border.toUtf8());
        require(!popup->isVisible(), "terminal query response opened completion for an application border");
        terminal.sendText("\r");
        output("\r\n");
        require(history.commands() == saved, "application border was recorded as a command after a terminal response");
    }

    output("user$ ");
    terminal.sendText("ech");
    output("ech");
    require(popup->isVisible(), "normal input did not show completion after terminal responses");
    output("\x1b[6n");
    require(popup->isVisible(), "terminal response interrupted completion for active input");
    terminal.sendText("o hello\r");
    output("o hello\r\nhello\r\nuser$ ");
    require(history.commands().last() == "echo hello", "terminal response interrupted command history capture");
    terminal.hide();
}

#ifdef Q_OS_LINUX
void shellTest(const QString &directory) {
    auto &history = CommandHistory::instance();
    history.clear();
    QTermWidget terminal;
    terminal.setHistorySize(1000);
    terminal.resize(640, 400);
    terminal.show();
    auto *display = terminal.findChild<TerminalDisplay *>();
    display->setFocus();
    QApplication::processEvents();
    TerminalCommandCompletion completion(display);
    std::unique_ptr<IPtyProcess> shell(PtyQt::createPtyProcess());
    require(shell->startProcess("/bin/bash", {"--noprofile", "--norc", "-i"}, directory,
                                {"PATH=/usr/bin:/bin", "TERM=xterm-256color", "PS1=test$ ", "HISTFILE=/dev/null", "INPUTRC=/dev/null"},
                                static_cast<qint16>(terminal.screenColumnsCount()), static_cast<qint16>(terminal.screenLinesCount())),
            "could not start bash PTY");
    QByteArray received;
    QObject::connect(shell->notifier(), &QIODevice::readyRead, &terminal, [&]() {
        const QByteArray data = shell->readAll();
        received += data;
        terminal.recvData(data.constData(), static_cast<int>(data.size()));
        completion.afterOutput();
    });
    QObject::connect(&terminal, &QTermWidget::sendData, &terminal, [&](const char *data, int size) {
        const QByteArray bytes(data, size);
        completion.beforeSend(bytes);
        shell->write(bytes);
    });
    QByteArray replacement;
    QObject::connect(&completion, &TerminalCommandCompletion::replaceInput, &terminal, [&](const QByteArray &data) {
        replacement = data;
        shell->write(data);
    });
    auto waitFor = [](const std::function<bool()> &predicate, const char *message) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < 3000) {
            QApplication::processEvents();
            QThread::msleep(5);
        }
        require(predicate(), message);
    };
    waitFor([&]() { return received.contains("test$ "); }, "bash prompt did not arrive");
    terminal.sendText("printf alpha\r");
    waitFor([&]() { return history.commands().contains("printf alpha") && received.endsWith("test$ "); }, "real shell command was not recorded");
    received.clear();
    terminal.sendText("printf a");
    auto *popup = display->findChild<QListWidget *>("commandCompletionPopup");
    waitFor([&]() { return popup->isVisible(); }, "real shell prefix popup did not appear");
    key(display, Qt::Key_Down);
    key(display, Qt::Key_Return, "\r");
    require(!replacement.contains('\r') && !replacement.contains('\n'), "selecting a command executed it");
    auto screenText = [&]() {
        QString text;
        QTextStream stream(&text);
        terminal.saveHistory(&stream, 0);
        return text;
    };
    waitFor([&]() { return screenText().trimmed().endsWith("test$ printf alpha"); }, "bash did not display the filled command");
    key(display, Qt::Key_Return, "\r");
    waitFor([&]() { return received.endsWith("test$ "); }, "filled command could not be executed");
    received.clear();
    terminal.sendText("printf betx");
    waitFor([&]() { return received.contains("printf betx"); }, "edited command echo missing");
    terminal.sendText("\x7f"
                      "a\r");
    waitFor([&]() { return history.commands().contains("printf beta") && received.endsWith("test$ "); }, "shell edit was not reflected in recorded history");
    key(display, Qt::Key_Up);
    waitFor([&]() { return screenText().trimmed().endsWith("test$ printf beta"); }, "Up did not recall the latest bash command");
    require(!popup->isVisible(), "bash history recall opened the completion popup");
    key(display, Qt::Key_Up);
    waitFor([&]() { return screenText().trimmed().endsWith("test$ printf alpha"); }, "repeated Up did not reach bash history");
    require(!popup->isVisible(), "repeated bash history recall opened the completion popup");
    key(display, Qt::Key_Down);
    waitFor([&]() { return screenText().trimmed().endsWith("test$ printf beta"); }, "Down did not reach bash history");
    require(!popup->isVisible(), "Down in bash history opened the completion popup");
    key(display, Qt::Key_Backspace, "\x7f");
    waitFor([&]() { return screenText().trimmed().endsWith("test$ printf bet") && popup->isVisible(); }, "editing recalled input did not restore completion");
    received.clear();
    terminal.sendText("\x03");
    waitFor([&]() { return received.endsWith("test$ "); }, "shell did not return to prompt after history navigation");
    QFile completionFile(directory + "/qshell-tab-target.txt");
    require(completionFile.open(QIODevice::WriteOnly), "could not create shell completion fixture");
    completionFile.close();
    history.add("cat qshell-tab-history.txt");
    terminal.sendText("cat qshell-tab-");
    waitFor([&]() { return popup->isVisible(); }, "history popup did not appear before native Tab completion");
    replacement.clear();
    key(display, Qt::Key_Down);
    key(display, Qt::Key_Tab, "\t");
    waitFor([&]() { return screenText().trimmed().endsWith("test$ cat qshell-tab-target.txt"); }, "Tab did not perform native shell filename completion");
    require(replacement.isEmpty(), "Tab selected history instead of shell completion");
    received.clear();
    terminal.sendText("\x03");
    waitFor([&]() { return received.endsWith("test$ "); }, "shell did not return to prompt after cancellation");
    history.clear();
    history.add("cd sources/android-projects");
    history.add("cd sources/clion-projects/qshell/");
    terminal.sendText("cd sources/");
    waitFor([&]() { return popup->isVisible() && popup->count() == 2; }, "shared path prefix did not show both shell candidates");
    terminal.sendText("android-projects");
    waitFor([&]() { return screenText().trimmed().endsWith("test$ cd sources/android-projects"); }, "complete path input was not echoed");
    require(popup->isVisible() && popup->count() == 1 && popup->item(0)->text() == "cd sources/android-projects", "shell popup retained a path that did not match the complete input");
    for (const QString &input : {QStringLiteral("cd"), QStringLiteral("cd "), QStringLiteral("cd  "), QStringLiteral("cd sources/")}) {
        received.clear();
        key(display, Qt::Key_C, "\x03", Qt::ControlModifier);
        waitFor([&]() { return received.endsWith("test$ "); }, "shell did not return to prompt before whitespace completion test");
        history.clear();
        const QString command = input == "cd  " ? "cd  sources/clion-projects/qshell/" : "cd sources/clion-projects/qshell/";
        history.add(command);
        received.clear();
        terminal.sendText(input);
        waitFor([&]() { return received.contains(input.toUtf8()) && popup->isVisible(); }, "completion input was not echoed");
        if (input == "cd sources/") {
            received.clear();
            terminal.sendText("\x02");
            waitFor([&]() { return !received.isEmpty(); }, "shell cursor did not move before completion");
        }
        replacement.clear();
        key(display, Qt::Key_Down);
        key(display, Qt::Key_Return, "\r");
        require(replacement.count('\x7f') == input.size(), "completion did not erase all input characters including trailing spaces");
        require(!replacement.contains('\r') && !replacement.contains('\n'), "whitespace completion executed the command");
        waitFor([&]() { return screenText().trimmed().endsWith("test$ " + command); }, "completion left characters from the previous input in bash");
    }
    terminal.hide();
    shell->kill();
}
#endif
}// namespace

int main(int argc, char *argv[]) {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory unavailable");
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    QApplication application(argc, argv);
    QApplication::setApplicationName("qshell-command-history-test");
    Q_INIT_RESOURCE(qtermwidget);
    storageTest(directory.path());
    editorTest();
    managementTest();
    terminalTest();
    terminalResponseTest();
#ifdef Q_OS_LINUX
    shellTest(directory.path());
#endif
    return 0;
}
