#ifndef QSHELL_UNIX_X11_DISPLAY_H
#define QSHELL_UNIX_X11_DISPLAY_H

#include <QByteArray>
#include <QString>

class UnixX11Display {
public:
    bool prepare();
    int connectSocket() const;
    QString displayName() const { return displayName_; }
    QByteArray authenticationProtocol() const { return QByteArrayLiteral("MIT-MAGIC-COOKIE-1"); }
    QByteArray authenticationCookie() const { return cookie_; }
    int screen() const { return screen_; }
    QString lastError() const { return lastError_; }

private:
    QString displayName_;
    QString socketPath_;
    QString host_;
    quint16 port_ = 0;
    int screen_ = 0;
    QByteArray cookie_;
    QString lastError_;
};

#endif
