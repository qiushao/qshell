#ifndef QSHELL_MCPSHELLENVIRONMENT_H
#define QSHELL_MCPSHELLENVIRONMENT_H

#include <QString>

class McpShellEnvironment {
public:
    static bool configure(const QString &bearerToken, QString *errorMessage = nullptr);
};

#endif// QSHELL_MCPSHELLENVIRONMENT_H
