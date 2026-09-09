#pragma once

#include <QSerialPort>
#include <functional>
#include <memory>
#include <sol/sol.hpp>
#include <vector>

class LuaSerialPort {
public:
    LuaSerialPort(const std::string &path, const sol::table &options, std::function<bool()> shouldStop);
    void close();
    bool isOpen() const;
    std::string readText(sol::optional<int> maxBytes, sol::optional<int> timeoutMs);
    sol::as_table_t<std::vector<int>> readBinary(sol::optional<int> maxBytes, sol::optional<int> timeoutMs);
    qint64 writeText(const std::string &text, sol::optional<int> timeoutMs);
    qint64 writeBinary(const sol::object &data, sol::optional<int> timeoutMs);

private:
    void checkReady() const;
    void checkError() const;
    QByteArray read(int maxBytes, int timeoutMs);
    qint64 write(const QByteArray &data, int timeoutMs);

    std::unique_ptr<QSerialPort> port_;
    std::function<bool()> shouldStop_;
};

class LuaSerialModule {
public:
    void registerAPIs(sol::state &lua, sol::table &qshell, const std::function<bool()> &shouldStop);
    void closeAll();

private:
    std::vector<std::shared_ptr<LuaSerialPort>> ports_;
};
