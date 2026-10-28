#include "unixptyprocess.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        qCritical() << message;
        std::exit(1);
    }
}

QByteArray payload(int size) {
    QByteArray data(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        data[i] = static_cast<char>(i % 251);
    return data;
}

void writeAll(const QByteArray &data) {
    qsizetype offset = 0;
    while (offset < data.size()) {
        const ssize_t count = ::write(STDOUT_FILENO, data.constData() + offset, data.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            _exit(2);
        offset += count;
    }
}

QByteArray readBytes(int size) {
    QByteArray data(size, Qt::Uninitialized);
    int offset = 0;
    while (offset < size) {
        const ssize_t count = ::read(STDIN_FILENO, data.data() + offset, size - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            _exit(3);
        offset += static_cast<int>(count);
    }
    return data;
}

int runChild(const QString &mode, int size) {
    // Nonblocking mode belongs only to the master, not the application's stdin.
    if (fcntl(STDIN_FILENO, F_GETFL) & O_NONBLOCK)
        return 4;
    termios settings{};
    if (tcgetattr(STDIN_FILENO, &settings) != 0)
        return 5;
    cfmakeraw(&settings);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &settings) != 0)
        return 6;
    writeAll("R");
    if (readBytes(1) != "G")
        return 7;

    if (mode == "boundary") {
        writeAll(QByteArray(size, 'x'));
        usleep(800000);
        writeAll("!");
    } else if (mode == "burst") {
        writeAll(payload(size));
    } else if (mode == "echo") {
        // Force the master to encounter short writes/EAGAIN before reading.
        usleep(500000);
        writeAll(readBytes(size));
    } else if (mode == "idle") {
        sleep(5);
    } else if (mode == "size") {
        winsize size{};
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) != 0)
            return 8;
        writeAll(QByteArray::number(size.ws_col) + "x" + QByteArray::number(size.ws_row));
    }
    return 0;
}

bool waitUntil(const std::function<bool()> &condition, int timeout = 3000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

class Session {
public:
    Session() {
        elapsed.start();
        QObject::connect(pty.notifier(), &QIODevice::readyRead, &context, [this]() {
            if (firstDelivery < 0)
                firstDelivery = elapsed.elapsed();
            received += pty.readAll();
        });
        QObject::connect(&heartbeat, &QTimer::timeout, &context, [this]() {
            const qint64 now = elapsed.elapsed();
            maxGap = qMax(maxGap, now - previousTick);
            previousTick = now;
            ++ticks;
        });
    }

    void start(const QString &mode, int size = 0) {
        received.clear();
        require(pty.startProcess(QCoreApplication::applicationFilePath(),
                                 {"--child", mode, QString::number(size)},
                                 "/tmp", {}, 80, 24), "PTY start failed");
        require(waitUntil([this]() { return received == "R"; }), "child did not become ready");
        received.clear();
        firstDelivery = -1;
        maxGap = 0;
        previousTick = 0;
        ticks = 0;
        elapsed.start();
        heartbeat.start(10);
    }

    void go() { require(pty.write("G") == 1, "start signal was not accepted"); }

    void finished() {
        auto *process = qobject_cast<QProcess *>(pty.notifier());
        require(process != nullptr, "PTY notifier is not a process");
        require(waitUntil([process]() { return process->state() == QProcess::NotRunning; }),
                "child did not exit");
        require(process->exitCode() == 0 && process->exitStatus() == QProcess::NormalExit,
                "child failed");
    }

    UnixPtyProcess pty;
    QObject context;
    QByteArray received;
    QElapsedTimer elapsed;
    QTimer heartbeat;
    qint64 firstDelivery = -1;
    qint64 maxGap = 0;
    qint64 previousTick = 0;
    int ticks = 0;
};

void testBoundary() {
    for (int size : {1023, 1024, 1025, 2048, 4096}) {
        Session session;
        session.start("boundary", size);
        session.go();
        require(waitUntil([&]() { return session.received.size() == size + 1; }),
                "boundary output was not delivered");
        require(session.received == QByteArray(size, 'x') + "!", "boundary output corrupted");
        require(session.firstDelivery < 400, "read blocked until the child's next output");
        require(session.maxGap < 300 && session.ticks > 20, "PTY read starved the event loop");
        session.finished();
        qInfo() << "boundary" << size << "first delivery ms" << session.firstDelivery
                << "maximum timer gap ms" << session.maxGap;
    }
}

void testBurst() {
    Session session;
    const QByteArray expected = payload(8 * 1024 * 1024);
    session.start("burst", static_cast<int>(expected.size()));
    session.go();
    require(waitUntil([&]() { return session.received.size() == expected.size(); }, 5000),
            "continuous output did not drain");
    require(session.received == expected, "continuous output lost or reordered bytes");
    require(session.maxGap < 300 && session.ticks > 0, "continuous output starved timers");
    session.finished();
}

void testWrite() {
    Session session;
    const QByteArray first = payload(2 * 1024 * 1024);
    const QByteArray second("trailing input after queued data");
    session.start("echo", static_cast<int>(first.size() + second.size()));
    session.go();
    QElapsedTimer elapsed;
    elapsed.start();
    require(session.pty.write(first) == first.size(), "large input was not accepted");
    require(session.pty.write(second) == second.size(), "additional input was not queued");
    require(elapsed.elapsed() < 250, "PTY write blocked on a full input queue");
    require(waitUntil([&]() { return session.received.size() == first.size() + second.size(); }, 5000),
            "queued input did not drain");
    require(session.received == first + second, "queued input lost or reordered bytes");
    require(session.maxGap < 300 && session.ticks > 20, "backpressure starved timers");
    session.finished();
}

void testLifecycle() {
    Session session;
    session.start("idle");
    session.go();
    require(session.pty.write(payload(2 * 1024 * 1024)) > 0, "input was not queued");
    session.pty.kill();
    require(session.pty.write("closed") == -1, "closed PTY accepted input");
    session.start("size");
    require(session.pty.resize(120, 40), "resize failed after releasing parent's slave fd");
    session.go();
    require(waitUntil([&]() { return session.received == "120x40"; }),
            "restart leaked pending input or resize did not reach the child");
    session.finished();
    require(waitUntil([&]() {
        for (auto *notifier : session.pty.notifier()->findChildren<QSocketNotifier *>()) {
            if (notifier->isEnabled())
                return false;
        }
        return true;
    }), "PTY hangup left an active notifier");
    require(session.pty.write("exited") == -1, "exited PTY accepted input");
    session.start("size");
    session.go();
    require(waitUntil([&]() { return session.received == "80x24"; }),
            "restart after natural exit failed");
    session.finished();
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.value(1) == "--child")
        return runChild(args.value(2), args.value(3).toInt());
    const QString mode = args.value(1);
    if (mode == "boundary") testBoundary();
    else if (mode == "burst") testBurst();
    else if (mode == "write") testWrite();
    else if (mode == "lifecycle") testLifecycle();
    else return 1;
    return 0;
}
