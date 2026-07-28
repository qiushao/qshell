#include "SSHTerminal.h"
#include "iptyprocess.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTime>
#include <QtCore/QEventLoop>

SSHTerminal::SSHTerminal(const SessionData &session, QWidget *parent) : BaseTerminal(parent) {
    startLocalShell();
    sessionData_ = session;
}

SSHTerminal::~SSHTerminal() = default;

/*******************************************************************************
 1. @函数:    getRandString
 2. @作者:    ut000439 wangpeili
 3. @日期:    2020-08-11
 4. @说明:    获取随机字符串
*******************************************************************************/
static QString getRandString()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

/*******************************************************************************
 1. @函数:    createShellFile
 2. @作者:    ut000610 戴正文
 3. @日期:    2020-07-31
 4. @说明:    创建连接远程的的临时shell文件
*******************************************************************************/
static QString createShellFile(const SessionData &sessionData)
{
    // 首先读取通用模板
    QFile sourceFile(":/script/ssh_login.sh");
    QString fileString;
    // 打开文件
    if (sourceFile.open(QIODevice::ReadOnly)) {
        //读取文件
        fileString = sourceFile.readAll();
        // 关闭文件
        sourceFile.close();
    }

    // 用远程管理数据替换文件内的关键字
    fileString.replace("<<USER>>", sessionData.sshConfig.username);
    fileString.replace("<<SERVER>>", sessionData.sshConfig.host);
    fileString.replace("<<PORT>>", QString::number(sessionData.sshConfig.port));
    // 根据是否有证书，替换关键字
    if (sessionData.sshConfig.authMethod == SSHAuthMethod::Password) {
        fileString.replace("<<PRIVATE_KEY>>", "");
        //将匹配到的特殊字符[", $, \]，替换为其转义形式
        static const QRegularExpression rx(R"((["$\\]))");
        QString password = sessionData.sshConfig.password;
        password.replace(rx, "\\\\1");
        fileString.replace("<<PASSWORD>>", password);
        fileString.replace("<<AUTHENTICATION>>", "no");
    } else {
        fileString.replace("<<PRIVATE_KEY>>", QString("-i " + sessionData.sshConfig.privateKeyPath));
        fileString.replace("<<PASSWORD>>", "");
        fileString.replace("<<AUTHENTICATION>>", "yes");
    }
    // 添加远程提示
    QString remoteCommand = "";
    fileString.replace("<<REMOTE_COMMAND>>", remoteCommand);

    // 创建临时文件
    QString toFileStr = "/tmp/qshell-" + getRandString();
    QFile toFile(toFileStr);
    toFile.open(QIODevice::WriteOnly | QIODevice::Text);
    // 写文件
    toFile.write(fileString.toUtf8());
    // 退出文件
    toFile.close();
    return toFileStr;
}

void SSHTerminal::connect() {
    QString shellFile = createShellFile(sessionData_);
    QString strTxt = "expect -f " + shellFile + "\n";
    sendText(strTxt);

    QTime dieTime = QTime::currentTime().addMSecs(200);
    while (QTime::currentTime() < dieTime) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    if (localShell_->hasChildProcess()) {
        qDebug() << "connect server success";
        connect_ = true;
    } else {
        qDebug() << "connect server failed!!!";
        connect_ = false;
    }
}

void SSHTerminal::disconnect() {
    if (!connect_) {
        return;
    }
    connect_ = false;
}

void SSHTerminal::writeToBackend(const QByteArray &data) {
    if (localShell_) {
        localShell_->write(data);
    }
}
