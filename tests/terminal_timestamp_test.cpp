#include "qtermwidget.h"
#include "Screen.h"
#include "ScreenWindow.h"
#include "TerminalDisplay.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <cstdlib>
#include <QRegularExpression>
#include <QThread>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        qCritical() << message;
        std::exit(1);
    }
}

void feed(QTermWidget &terminal, const QByteArray &data) {
    terminal.recvData(data.constData(), static_cast<int>(data.size()));
}

QString exported(QTermWidget &terminal, bool timestamps, int first, int last) {
    QString text;
    QTextStream stream(&text);
    terminal.saveHistory(&stream, 0, first, last, timestamps);
    stream.flush();
    return text;
}
}

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    Q_INIT_RESOURCE(qtermwidget);
    QTermWidget terminal;
    terminal.setTerminalSizeHint(false);
    terminal.setColorScheme(QStringLiteral("Tango"));
    terminal.resize(900, 420);
    terminal.setTerminalFont(QFont(QStringLiteral("monospace"), 12));
    terminal.setHistorySize(4);
    terminal.setTerminalTimestampEnabled(true);
    terminal.show();
    QApplication::processEvents();
    auto *display = terminal.findChild<TerminalDisplay *>();
    auto *gutter = terminal.findChild<QWidget *>(QStringLiteral("terminalTimestampDisplay"));
    auto *window = display->screenWindow();
    auto *screen = window->screen();
    require(gutter && gutter->isVisible(), "timestamp gutter is missing");
    require(gutter->geometry().right() < display->geometry().left(), "gutter overlaps terminal cells");

    QStringList lines;
    QStringList timestamps;
    QObject::connect(&terminal, &QTermWidget::onNewLineWithTimestamp,
                     [&](const QString &line, const QString &timestamp) {
                         lines.append(line);
                         timestamps.append(timestamp);
                     });
    feed(terminal, "\033[31mfirst");
    const QString firstTimestamp = screen->lineTimestamp(0);
    require(QRegularExpression(QStringLiteral("^\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}$"))
                    .match(firstTimestamp).hasMatch(), "incorrect timestamp format");
    QThread::msleep(1100);
    feed(terminal, " part\033[0m\r\n\r\n");
    require(lines == QStringList({QStringLiteral("first part"), QString()}),
            "timestamps or ANSI escapes contaminated terminal output");
    require(timestamps[0] == firstTimestamp && !timestamps[1].isEmpty(),
            "log timestamp did not preserve first output time or blank line time");
    require(exported(terminal, true, 0, 0).trimmed() == firstTimestamp + " first part",
            "buffered log does not match displayed timestamp");
    terminal.setSelectionStart(0, 0);
    terminal.setSelectionEnd(0, 9);
    require(terminal.selectedText() == QStringLiteral("first part"),
            "timestamps changed terminal selection");

    // Cursor addressing and CR overwrite operate solely on terminal characters.
    feed(terminal, "\033[1;1HX\rY");
    require(exported(terminal, false, 0, 0).trimmed() == QStringLiteral("Yirst part"),
            "timestamp gutter interfered with cursor addressing");
    require(screen->lineTimestamp(0) == firstTimestamp, "overwrite changed row timestamp");
    feed(terminal, "\033[1;1H\033[L");
    require(screen->lineTimestamp(0).isEmpty() && screen->lineTimestamp(1) == firstTimestamp,
            "inserted row did not move timestamps with text");
    feed(terminal, "\033[M");
    require(screen->lineTimestamp(0) == firstTimestamp,
            "deleted row did not move timestamps with text");
    feed(terminal, "\033[2K");
    require(screen->lineTimestamp(0).isEmpty(), "erased row retained its timestamp");

    // Wrapping must use exactly the reported terminal column count.
    feed(terminal, "\033[2J\033[H");
    terminal.clearScrollback();
    const int columns = terminal.screenColumnsCount();
    feed(terminal, QByteArray(columns, 'a') + "b");
    require(screen->getCursorX() == 1 && screen->getCursorY() == 1,
            "timestamps occupied terminal columns");
    require(!screen->lineTimestamp(0).isEmpty() && !screen->lineTimestamp(1).isEmpty(),
            "wrapped rows lost timestamps");

    // History capacity rollover, scrollback, and viewport resize keep row metadata.
    feed(terminal, "\033[2J\033[H");
    terminal.clearScrollback();
    const int count = terminal.screenLinesCount() + 8;
    for (int i = 0; i < count; ++i)
        feed(terminal, "row-" + QByteArray::number(i) + "\r\n");
    require(terminal.historyLinesCount() == 4, "history did not roll over");
    const QString historyTimestamp = screen->lineTimestamp(0);
    const QString historyText = exported(terminal, false, 0, 0).trimmed();
    require(!historyTimestamp.isEmpty(), "history dropped timestamp metadata");
    require(exported(terminal, true, 0, 0).trimmed() == historyTimestamp + ' ' + historyText,
            "history timestamp and text diverged");
    window->setTrackOutput(false);
    window->scrollTo(0);
    QApplication::processEvents();
    require(window->currentLine() == 0 && screen->lineTimestamp(window->currentLine()) == historyTimestamp,
            "scrollback lost timestamp alignment");
    terminal.resize(1000, 460);
    QApplication::processEvents();
    require(screen->lineTimestamp(0) == historyTimestamp, "resize changed retained history timestamps");

    // Distinct times verify exact text/time pairing through ring-buffer rollover.
    HistoryScrollBuffer history(4);
    for (int i = 0; i < 10; ++i) {
        const Character character('a' + i);
        history.addCells(&character, 1);
        history.addLine(false, 1000 + i);
    }
    for (int row = 0; row < history.getLines(); ++row) {
        const auto character = history.getCell(row, 0);
        require(history.lineTimestamp(row) == 1000 + character.character - 'a',
                "history ring paired timestamp with wrong text");
    }
    history.setMaxNbLines(8);
    for (int row = 0; row < history.getLines(); ++row) {
        const auto character = history.getCell(row, 0);
        require(history.lineTimestamp(row) == 1000 + character.character - 'a',
                "history resize paired timestamp with wrong text");
    }

    // Alternate screens use their own metadata and emit timestamped log lines.
    feed(terminal, "\033[?1049h\033[2J\033[Halternate\r\n");
    require(lines.last() == QStringLiteral("alternate") && !timestamps.last().isEmpty(),
            "alternate screen did not emit timestamped output");
    require(window->screen() != screen && !window->screen()->lineTimestamp(0).isEmpty(),
            "alternate screen timestamp missing");
    feed(terminal, "\033[?1049l");
    require(window->screen() == screen && screen->lineTimestamp(0) == historyTimestamp,
            "alternate screen corrupted primary timestamps");

    terminal.setTerminalTimestampEnabled(false);
    QApplication::processEvents();
    require(gutter->isHidden(), "disabling timestamps left the gutter visible");
    require(exported(terminal, true, 0, 0) == exported(terminal, false, 0, 0),
            "disabled timestamps still appear in buffered logs");
    feed(terminal, "\r\ndisabled\r\n");
    require(timestamps.last().isEmpty(), "disabled timestamp still emitted to log");
    terminal.setTerminalTimestampEnabled(true);
    feed(terminal, "enabled\r\n");
    require(!timestamps.last().isEmpty(), "re-enabling timestamps failed");

    // Render a representative terminal for visual inspection when requested.
    if (argc > 1) {
        terminal.setHistorySize(100);
        feed(terminal, "\033[2J\033[HConnected to device\r\n\033[32mStatus: ready\033[0m\r\n"
                       "Progress: 100%\r\n\r\nuser@host:~$ ");
        window->setTrackOutput(true);
        window->notifyOutputChanged();
        QApplication::processEvents();
        require(terminal.grab().save(QString::fromLocal8Bit(argv[1])), "failed to save UI screenshot");
    }
    terminal.hide();
    return 0;
}
