#ifndef QSHELL_ZMODEM_PROTOCOL_H
#define QSHELL_ZMODEM_PROTOCOL_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <array>

namespace Zmodem {

enum class FrameType : quint8 {
    Zrqinit = 0,
    Zrinit = 1,
    Zsinit = 2,
    Zack = 3,
    Zfile = 4,
    Zskip = 5,
    Znak = 6,
    Zabort = 7,
    Zfin = 8,
    Zrpos = 9,
    Zdata = 10,
    Zeof = 11,
    Zferr = 12,
    Zcrc = 13,
    Zchallenge = 14,
    Zcompl = 15,
    Zcan = 16,
    Zfreecnt = 17,
    Zcommand = 18,
    Zstderr = 19
};

enum class FrameEnd : quint8 {
    End = 0x68,
    Continue = 0x69,
    ContinueAck = 0x6a,
    EndAck = 0x6b
};

enum class HeaderFormat {
    Hex,
    Binary16,
    Binary32
};

constexpr quint8 canFdx = 0x01;
constexpr quint8 canOvio = 0x02;
constexpr quint8 canFc32 = 0x20;
constexpr quint8 escapeCtl = 0x40;
constexpr quint8 binaryFile = 0x01;

struct Header {
    FrameType type = FrameType::Zrqinit;
    std::array<quint8, 4> data{};
    HeaderFormat format = HeaderFormat::Hex;

    quint32 position() const;
    bool usesCrc32() const;
};

struct ParseItem {
    enum class Kind {
        Header,
        Data,
        PlainText,
        Cancel,
        Error
    };

    Kind kind = Kind::PlainText;
    Header header;
    QByteArray data;
    QByteArray raw;
    FrameEnd frameEnd = FrameEnd::End;
    QString error;
};

class Parser {
public:
    void addData(const QByteArray &data);
    bool next(ParseItem *item);
    void expectDataSubpacket();
    void expectHeader();
    bool hasPendingData() const;
    QByteArray takePendingData();
    void reset();

private:
    bool parseHeader(ParseItem *item);
    bool parseHexHeader(int prefixOffset, ParseItem *item);
    bool parseBinaryHeader(int prefixOffset, bool crc32, ParseItem *item);
    bool parseDataSubpacket(ParseItem *item);
    bool takeCancel(ParseItem *item);

    QByteArray buffer_;
    bool expectData_ = false;
    bool dataUsesCrc32_ = false;
    bool lastHeaderUsesCrc32_ = false;
    bool discardCancelTail_ = false;
};

QByteArray encodeHexHeader(FrameType type, const std::array<quint8, 4> &data = {});
QByteArray encodeHexHeader(FrameType type, quint32 position);
QByteArray encodeBinaryHeader(FrameType type,
                              const std::array<quint8, 4> &data,
                              bool crc32,
                              bool escapeControlCharacters);
QByteArray encodeBinaryHeader(FrameType type,
                              quint32 position,
                              bool crc32,
                              bool escapeControlCharacters);
QByteArray encodeDataSubpacket(const QByteArray &data,
                               FrameEnd frameEnd,
                               bool crc32,
                               bool escapeControlCharacters);
QByteArray abortSequence();

}// namespace Zmodem

#endif// QSHELL_ZMODEM_PROTOCOL_H
