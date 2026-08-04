#include "core/TerminalLineTimestamp.h"

#include <QDebug>

int main() {
    const QByteArray timestamp("[2026-08-04 12:34:56.789] ");

    TerminalLineTimestamp formatter;
    const QByteArray first =
            formatter.process("first part", true, timestamp);
    const QByteArray second =
            formatter.process(" and rest\r\nsecond\n\n", true, timestamp);
    const QByteArray expected =
            timestamp + "first part and rest\r\n" +
            timestamp + "second\n" + timestamp + "\n";
    if (first + second != expected) {
        qCritical() << "timestamps were not added once per streamed line";
        return 1;
    }

    TerminalLineTimestamp toggledFormatter;
    if (toggledFormatter.process("existing", false, {}) != "existing") {
        qCritical() << "disabled formatting changed terminal data";
        return 1;
    }
    const QByteArray toggled =
            toggledFormatter.process(" line\nnew line", true, timestamp);
    if (toggled != QByteArray(" line\n") + timestamp + "new line") {
        qCritical() << "enabling timestamps added a prefix in the middle of a line";
        return 1;
    }

    return 0;
}
