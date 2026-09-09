#include "LuaSerialModule.h"

#include <QElapsedTimer>
#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace {
int optionEnum(const sol::table &options, const char *name, const char *defaultValue,
               std::initializer_list<std::pair<const char *, int>> values) {
    const auto value = options.get_or<std::string>(name, defaultValue);
    for (const auto &entry: values) {
        if (value == entry.first) return entry.second;
    }
    throw std::runtime_error(std::string("serial: invalid ") + name);
}
}// namespace

LuaSerialPort::LuaSerialPort(const std::string &path, const sol::table &options, std::function<bool()> shouldStop)
    : port_(std::make_unique<QSerialPort>()), shouldStop_(std::move(shouldStop)) {
    const int baudRate = options.get_or("baudRate", 115200);
    const int dataBits = options.get_or("dataBits", 8);
    const double stopBits = options.get_or("stopBits", 1.0);
    if (path.empty() || path.find('\0') != std::string::npos || baudRate <= 0 ||
        dataBits < 5 || dataBits > 8 || (stopBits != 1 && stopBits != 1.5 && stopBits != 2)) {
        throw std::runtime_error("serial: invalid port name, baudRate, dataBits or stopBits");
    }
    const auto parity = static_cast<QSerialPort::Parity>(optionEnum(options, "parity", "none", {{"none", QSerialPort::NoParity}, {"even", QSerialPort::EvenParity}, {"odd", QSerialPort::OddParity}, {"space", QSerialPort::SpaceParity}, {"mark", QSerialPort::MarkParity}}));
    const auto flowControl = static_cast<QSerialPort::FlowControl>(optionEnum(options, "flowControl", "none", {{"none", QSerialPort::NoFlowControl}, {"hardware", QSerialPort::HardwareControl}, {"software", QSerialPort::SoftwareControl}}));
    port_->setPortName(QString::fromStdString(path));
    if (!port_->setBaudRate(baudRate) ||
        !port_->setDataBits(static_cast<QSerialPort::DataBits>(dataBits)) ||
        !port_->setParity(parity) ||
        !port_->setStopBits(stopBits == 1.5 ? QSerialPort::OneAndHalfStop : static_cast<QSerialPort::StopBits>(static_cast<int>(stopBits))) ||
        !port_->setFlowControl(flowControl) || !port_->open(QIODevice::ReadWrite)) {
        throw std::runtime_error("serial: " + path + ": " + port_->errorString().toStdString());
    }
}

void LuaSerialPort::close() {
    // Destroy the QObject in the script thread, even if Lua retains this handle.
    port_.reset();
}

bool LuaSerialPort::isOpen() const {
    return port_ && port_->isOpen();
}

void LuaSerialPort::checkReady() const {
    if (shouldStop_()) throw std::runtime_error("serial: script interrupted");
    if (!isOpen()) throw std::runtime_error("serial: port is closed");
    checkError();
}

void LuaSerialPort::checkError() const {
    if (port_->error() != QSerialPort::NoError && port_->error() != QSerialPort::TimeoutError) {
        throw std::runtime_error("serial: " + port_->errorString().toStdString());
    }
}

QByteArray LuaSerialPort::read(int maxBytes, int timeoutMs) {
    if (maxBytes <= 0 || timeoutMs < 0) throw std::runtime_error("serial: maxBytes must be positive and timeoutMs nonnegative");
    checkReady();
    QElapsedTimer timer;
    timer.start();
    while (port_->bytesAvailable() == 0) {
        checkReady();
        const int remaining = static_cast<int>(std::max<qint64>(0, timeoutMs - timer.elapsed()));
        // A zero timeout still polls the OS when no Qt event loop is running.
        port_->waitForReadyRead(std::min(50, remaining));
        checkReady();
        if (timer.elapsed() >= timeoutMs) break;
    }
    return port_->read(maxBytes);
}

qint64 LuaSerialPort::write(const QByteArray &data, int timeoutMs) {
    if (timeoutMs < 0) throw std::runtime_error("serial: timeoutMs must be nonnegative");
    checkReady();
    if (data.isEmpty()) return 0;
    const qint64 accepted = port_->write(data);
    if (accepted != data.size()) {
        const auto error = port_->errorString().toStdString();
        close();
        throw std::runtime_error("serial: write failed (data may have been partially sent): " + error);
    }
    QElapsedTimer timer;
    timer.start();
    try {
        while (port_->bytesToWrite() > 0) {
            checkReady();
            const int remaining = static_cast<int>(std::max<qint64>(0, timeoutMs - timer.elapsed()));
            port_->waitForBytesWritten(std::min(50, remaining));
            checkReady();
            if (port_->bytesToWrite() > 0 && timer.elapsed() >= timeoutMs) {
                throw std::runtime_error("serial: write timeout (data may have been partially sent)");
            }
        }
    } catch (...) {
        // Do not leave queued bytes to be sent by an unrelated later operation.
        close();
        throw;
    }
    return accepted;
}

std::string LuaSerialPort::readText(sol::optional<int> maxBytes, sol::optional<int> timeoutMs) {
    return read(maxBytes.value_or(4096), timeoutMs.value_or(1000)).toStdString();
}

sol::as_table_t<std::vector<int>> LuaSerialPort::readBinary(sol::optional<int> maxBytes, sol::optional<int> timeoutMs) {
    const auto data = read(maxBytes.value_or(4096), timeoutMs.value_or(1000));
    std::vector<int> bytes;
    bytes.reserve(data.size());
    for (unsigned char byte: data) bytes.push_back(byte);
    return sol::as_table(std::move(bytes));
}

qint64 LuaSerialPort::writeText(const std::string &text, sol::optional<int> timeoutMs) {
    return write(QByteArray::fromStdString(text), timeoutMs.value_or(1000));
}

qint64 LuaSerialPort::writeBinary(const sol::object &data, sol::optional<int> timeoutMs) {
    QByteArray bytes;
    auto appendByte = [&bytes](const sol::object &value) {
        if (value.get_type() != sol::type::number) throw std::runtime_error("serial: expected an integer byte (0..255)");
        const double number = value.as<double>();
        if (!(number >= 0 && number <= 255) || number != static_cast<int>(number)) {
            throw std::runtime_error("serial: expected an integer byte (0..255)");
        }
        bytes.append(static_cast<char>(static_cast<unsigned char>(number)));
    };
    if (data.get_type() == sol::type::string) {
        bytes = QByteArray::fromStdString(data.as<std::string>());
    } else if (data.get_type() == sol::type::table) {
        const auto values = data.as<sol::table>();
        const auto size = std::distance(values.begin(), values.end());
        for (qsizetype i = 0; i < size; ++i) {
            appendByte(values.raw_get<sol::object>(i + 1));
        }
    } else {
        appendByte(data);
    }
    return write(bytes, timeoutMs.value_or(1000));
}

void LuaSerialModule::registerAPIs(sol::state &lua, sol::table &qshell, const std::function<bool()> &shouldStop) {
    auto serial = qshell.create_named("serial");
    serial.new_usertype<LuaSerialPort>("Port", sol::no_constructor,
                                       "close", &LuaSerialPort::close, "isOpen", &LuaSerialPort::isOpen,
                                       "readText", &LuaSerialPort::readText, "readBinary", &LuaSerialPort::readBinary,
                                       "writeText", &LuaSerialPort::writeText, "writeBinary", &LuaSerialPort::writeBinary);
    serial.set_function("open", [this, &lua, shouldStop](const std::string &path, const sol::optional<sol::table> &options) {
        if (shouldStop()) throw std::runtime_error("serial: script interrupted");
        auto port = std::make_shared<LuaSerialPort>(path, options.value_or(lua.create_table()), shouldStop);
        ports_.erase(std::remove_if(ports_.begin(), ports_.end(), [](const auto &entry) { return !entry->isOpen(); }), ports_.end());
        ports_.push_back(port);
        return port;
    });
}

void LuaSerialModule::closeAll() {
    for (const auto &port: ports_) port->close();
    ports_.clear();
}
