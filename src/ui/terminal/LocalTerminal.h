#ifndef QSHELL_LOCAL_TERMINAL_H
#define QSHELL_LOCAL_TERMINAL_H

#include "BaseTerminal.h"



class LocalTerminal : public BaseTerminal {
public:
    explicit LocalTerminal(QWidget *parent);
    ~LocalTerminal() override;
    void connect() override;
    void disconnect() override;

protected:
    void writeToBackend(const QByteArray &data) override;
};


#endif //QSHELL_LOCAL_TERMINAL_H
