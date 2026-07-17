#include "qtermwidget.h"

#include <QApplication>
#include <QDebug>

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    QTermWidget terminal;
    constexpr int historySize = 128000;
    terminal.setHistorySize(historySize);

    int actualLineCount = 0;
    QString failure;
    QObject::connect(&terminal, &QTermWidget::onNewLine,
                     [&actualLineCount, &failure](const QString &line) {
                         const QString expected = QStringLiteral("line-%1").arg(actualLineCount);
                         if (failure.isEmpty() && line != expected) {
                             failure = QStringLiteral("line %1: expected '%2', got '%3'")
                                               .arg(actualLineCount)
                                               .arg(expected, line);
                         }
                         ++actualLineCount;
                     });

    const int lineCount = terminal.screenLinesCount() + historySize + 5;
    for (int i = 0; i < lineCount; ++i) {
        const QString line = QStringLiteral("line-%1").arg(i);
        const QByteArray data = (line + QStringLiteral("\r\n")).toUtf8();
        terminal.recvData(data.constData(), static_cast<int>(data.size()));
    }

    if (!failure.isEmpty()) {
        qCritical().noquote() << failure;
        return 1;
    }
    if (actualLineCount != lineCount) {
        qCritical() << "expected" << lineCount << "lines, got" << actualLineCount;
        return 1;
    }

    return 0;
}
