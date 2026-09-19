#include "core/ConfigManager.h"
#include "ui/terminal/LocalTerminal.h"
#include "ui/terminal/SSHTerminal.h"
#include "ui/terminal/SerialTerminal.h"

#include <QApplication>
#include <QDebug>
#include <QEnterEvent>
#include <QProxyStyle>
#include <QScrollBar>
#include <QStandardPaths>
#include <cstdio>
#include <cstdlib>

namespace {
class TransientScrollBarStyle : public QProxyStyle {
public:
    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr, QStyleHintReturn *data = nullptr) const override {
        if (hint == SH_ScrollBar_Transient) {
            return true;
        }
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

void checkScrollBarBounds(BaseTerminal &terminal, QScrollBar *scrollBar) {
    const QRect bounds(scrollBar->mapTo(&terminal, QPoint()), scrollBar->size());
    require(terminal.rect().contains(bounds),
            "scrollbar must stay inside the session when timestamps are enabled or resized");
    require(scrollBar->visibleRegion().boundingRect() == scrollBar->rect(),
            "scrollbar must not be clipped by the terminal layout");
}

void checkScrollBar(BaseTerminal &terminal) {
    terminal.resize(800, 400);
    terminal.show();
    QApplication::processEvents();
    auto *scrollBar = terminal.findChild<QScrollBar *>();
    require(scrollBar && scrollBar->isVisible(), "empty session must show its scrollbar");
    checkScrollBarBounds(terminal, scrollBar);
    require(!scrollBar->style()->styleHint(QStyle::SH_ScrollBar_Transient, nullptr, scrollBar),
            "terminal scrollbar must not auto-hide or collapse");
    const int width = scrollBar->width();
    require(width >= 12 && width == scrollBar->sizeHint().width(),
            "scrollbar must start at its full style width");
    require(scrollBar->maximum() == 0, "new session should have no scrollback");

    for (int i = 0; i < 100; ++i) {
        terminal.recvData("scrollback line\r\n", 17);
    }
    QApplication::processEvents();
    require(scrollBar->maximum() > 0, "output must create scrollable history");
    scrollBar->setValue(0);
    require(scrollBar->value() == 0, "scrollbar must scroll to the start of history");
    scrollBar->setValue(scrollBar->maximum());

    QEnterEvent enter(QPointF(1, 1), QPointF(1, 1), QPointF(scrollBar->mapToGlobal(QPoint(1, 1))));
    QApplication::sendEvent(scrollBar, &enter);
    QApplication::processEvents();
    require(scrollBar->isVisible() && scrollBar->width() == width,
            "hover must preserve scrollbar visibility and width");
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(scrollBar, &leave);
    terminal.resize(1000, 500);
    QApplication::processEvents();
    require(scrollBar->isVisible() && scrollBar->width() == width,
            "leaving and resizing must preserve scrollbar visibility and width");
    checkScrollBarBounds(terminal, scrollBar);
    for (const bool timestamps : {false, true, false, true}) {
        terminal.setTerminalTimestampEnabled(timestamps);
        QApplication::processEvents();
        terminal.resize(timestamps ? 700 : 900, 400);
        QApplication::processEvents();
        checkScrollBarBounds(terminal, scrollBar);
    }
    terminal.hide();
}
}

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    Q_INIT_RESOURCE(qtermwidget);
    QStandardPaths::setTestModeEnabled(true);
    QApplication::setApplicationName(QStringLiteral("qshell-scrollbar-test"));
    // Reproduce a desktop style that enables overlay scrollbars, even offscreen.
    QApplication::setStyle(new TransientScrollBarStyle);

    for (const bool timestamps : {true, false}) {
        auto settings = ConfigManager::instance()->globalSettings();
        settings.terminalTimestamp = timestamps;
        ConfigManager::instance()->setGlobalSettings(settings);
        SessionData session;
        session.protocolType = ProtocolType::Serial;
        SerialTerminal serial(session, nullptr);
        checkScrollBar(serial);
        session.serialConfig.dataMode = SerialDataMode::Bin;
        SerialTerminal binarySerial(session, nullptr);
        checkScrollBar(binarySerial);
        session.protocolType = ProtocolType::LocalShell;
        LocalTerminal local(session, nullptr);
        checkScrollBar(local);
        session.protocolType = ProtocolType::SSH;
        SSHTerminal ssh(session);
        checkScrollBar(ssh);
    }
    qInfo() << "All terminal scrollbar checks passed";
    return 0;
}
