#include "scriptengine/LuaSerialModule.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <thread>
#include <unistd.h>

namespace {
class PseudoTerminal {
public:
    PseudoTerminal() {
        fd_ = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0 || grantpt(fd_) != 0 || unlockpt(fd_) != 0) throw std::runtime_error("cannot create PTY");
        path_ = ptsname(fd_);
    }
    ~PseudoTerminal() { closePeer(); }
    void closePeer() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }
    [[nodiscard]] const std::string &path() const { return path_; }
    void send(const std::string &bytes) const {
        if (::write(fd_, bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size())) throw std::runtime_error("PTY write failed");
    }
    [[nodiscard]] std::string receive(int size, int timeout = 1000) const {
        std::string result;
        QElapsedTimer timer;
        timer.start();
        do {
            pollfd descriptor{fd_, POLLIN, 0};
            if (poll(&descriptor, 1, std::max(0, timeout - static_cast<int>(timer.elapsed()))) <= 0) break;
            char buffer[4096];
            const auto count = ::read(fd_, buffer, std::min(sizeof(buffer), static_cast<size_t>(size) - result.size()));
            if (count <= 0) break;
            result.append(buffer, count);
        } while (result.size() < static_cast<size_t>(size) && timer.elapsed() < timeout);
        return result;
    }

private:
    int fd_ = -1;
    std::string path_;
};
}// namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        PseudoTerminal board;
        PseudoTerminal ir;
        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
        auto qshell = lua.create_named_table("qshell");
        std::atomic<bool> stopped{false};
        LuaSerialModule serial;
        serial.registerAPIs(lua, qshell, [&] { return stopped.load(); });
        lua["boardPath"] = board.path();
        lua["irPath"] = ir.path();
        lua.set_function("boardSend", [&](const std::string &bytes) { board.send(bytes); });
        lua.set_function("irSend", [&](const std::string &bytes) { ir.send(bytes); });
        lua.set_function("boardReceive", [&](int count) { return board.receive(count); });
        lua.set_function("irReceive", [&](int count) { return ir.receive(count); });
        lua.set_function("irHasNoOutput", [&] { return ir.receive(1, 20).empty(); });
        lua.script(R"lua(
            board = qshell.serial.open(boardPath)
            ir = qshell.serial.open(irPath, {baudRate = 9600, dataBits = 8,
                parity = "none", stopBits = 1, flowControl = "none"})
            assert(board:isOpen() and ir:isOpen())
            assert(not pcall(qshell.serial.open, boardPath))
            assert(board:writeText("echo hello\r") == 11)
            assert(boardReceive(11) == "echo hello\r")
            boardSend("你好\r\nconsole:/ $ ")
            assert(board:readText(4096, 200) == "你好\r\nconsole:/ $ ")
            assert(ir:writeBinary(0xC0) == 1)
            assert(irReceive(1) == string.char(0xC0))
            local bytes, chars = {}, {}
            for i = 0, 255 do bytes[i + 1] = i; chars[i + 1] = string.char(i) end
            local raw = table.concat(chars)
            assert(ir:writeBinary(bytes) == 256)
            assert(irReceive(256) == raw)
            assert(ir:writeBinary(raw) == 256)
            assert(irReceive(256) == raw)
            assert(board:writeText(raw) == 256)
            assert(boardReceive(256) == raw)
            irSend(raw)
            local received = {}
            while #received < 256 do
                local chunk = ir:readBinary(17, 200)
                assert(#chunk > 0 and #chunk <= 17)
                for _, byte in ipairs(chunk) do received[#received + 1] = byte end
            end
            for i = 1, 256 do assert(received[i] == bytes[i]) end
            boardSend(raw)
            assert(board:readText(256, 200) == raw)
            boardSend("ABCDEF")
            assert(board:readText(2, 200) == "AB")
            assert(board:readText(4, 0) == "CDEF")
            assert(board:readText(20, 0) == "")
            boardSend("poll")
            assert(board:readText(4, 0) == "poll")
            assert(#ir:readBinary(20, 0) == 0)
            for _, bad in ipairs({-1, 256, 1.5, math.huge, 0/0, true,
                    {0xC0, 256}, {1, "2"}, {[1] = 1, [3] = 2}, {[0] = 1}, {x = 1}}) do
                assert(not pcall(function() ir:writeBinary(bad) end))
            end
            assert(not pcall(function() ir:writeBinary(nil) end))
            assert(ir:writeBinary({}) == 0 and ir:writeText("") == 0)
            assert(irHasNoOutput())
            assert(not pcall(function() ir:readText(0) end))
            assert(not pcall(function() ir:readBinary(1, -1) end))
            assert(not pcall(function() ir:writeBinary(0xC0, -1) end))
            assert(not pcall(qshell.serial.open, "/no/such/serial/port"))
            for _, options in ipairs({{baudRate = 0}, {dataBits = 9}, {stopBits = 3},
                    {parity = "bad"}, {flowControl = "bad"}}) do
                assert(not pcall(qshell.serial.open, irPath, options))
            end
        )lua");
        QElapsedTimer elapsed;
        elapsed.start();
        lua.script("assert(board:readText(1, 120) == '')");
        if (elapsed.elapsed() < 100 || elapsed.elapsed() > 1000) throw std::runtime_error("read timeout out of bounds");
        std::thread sender([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            board.send("delayed");
        });
        auto delayed = lua.safe_script("assert(board:readText(7, 500) == 'delayed')", sol::script_pass_on_error);
        sender.join();
        if (!delayed.valid()) throw sol::error(delayed);
        std::thread stopper([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            stopped = true;
        });
        elapsed.restart();
        auto interrupted = lua.safe_script("board:readText(1, 10000)", sol::script_pass_on_error);
        stopper.join();
        if (interrupted.valid() || elapsed.elapsed() > 1000) throw std::runtime_error("read not interrupted promptly");
        stopped = false;
        lua.script(R"lua(
            local ok, err = pcall(function() ir:writeBinary(string.rep("x", 1024 * 1024), 30) end)
            assert(not ok and err:find("write timeout"))
            assert(not ir:isOpen())
        )lua");
        serial.closeAll();
        lua.script(R"lua(
            assert(not board:isOpen() and not ir:isOpen())
            board:close(); board:close()
            assert(not pcall(function() board:readText() end))
            assert(not pcall(function() board:writeText("x") end))
            board = qshell.serial.open(boardPath)
        )lua");
        std::thread writeStopper([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            stopped = true;
        });
        elapsed.restart();
        auto writeInterrupted = lua.safe_script("board:writeBinary(string.rep('x', 1024 * 1024), 10000)", sol::script_pass_on_error);
        writeStopper.join();
        if (writeInterrupted.valid() || elapsed.elapsed() > 1000) throw std::runtime_error("write not interrupted promptly");
        stopped = false;
        lua.script("assert(not board:isOpen()); board = qshell.serial.open(boardPath)");
        board.closePeer();
        lua.script("assert(not pcall(function() board:readText(1, 200) end))");
        serial.closeAll();
        std::cout << "Lua serial text/binary, multiple ports, validation, timeout, interruption and cleanup passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
