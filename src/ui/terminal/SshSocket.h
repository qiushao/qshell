#ifndef QSHELL_SSH_SOCKET_H
#define QSHELL_SSH_SOCKET_H

#include <QtGlobal>
#include <libssh2.h>

#ifdef Q_OS_WIN
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace SshSocket {
inline void close(libssh2_socket_t socket) {
#ifdef Q_OS_WIN
    closesocket(socket);
#else
    ::close(socket);
#endif
}

inline int lastError() {
#ifdef Q_OS_WIN
    return WSAGetLastError();
#else
    return errno;
#endif
}

inline bool wouldBlock(int error) {
#ifdef Q_OS_WIN
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

inline bool setNonBlocking(libssh2_socket_t socket) {
#ifdef Q_OS_WIN
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

inline void suppressSigPipe(libssh2_socket_t socket) {
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#else
    Q_UNUSED(socket);
#endif
}

#ifdef MSG_NOSIGNAL
constexpr int sendFlags = MSG_NOSIGNAL;
#else
constexpr int sendFlags = 0;
#endif
}// namespace SshSocket

#endif
