#ifndef QSHELL_XYMODEM_PROTOCOL_H
#define QSHELL_XYMODEM_PROTOCOL_H

#include <QByteArray>
#include <QString>

namespace XyModem {

constexpr char soh = 0x01;
constexpr char stx = 0x02;
constexpr char eot = 0x04;
constexpr char ack = 0x06;
constexpr char nak = 0x15;
constexpr char can = 0x18;
constexpr char crcRequest = 0x43;
constexpr char padding = 0x1a;

constexpr int shortBlockSize = 128;
constexpr int longBlockSize = 1024;

enum class CheckMode {
    Checksum,
    Crc16
};

struct Packet {
    quint8 number = 0;
    QByteArray data;
};

quint16 crc16(const QByteArray &data);
quint8 checksum(const QByteArray &data);
int blockSize(char startByte);
qsizetype encodedPacketSize(char startByte, CheckMode checkMode);
QByteArray encodePacket(quint8 number,
                        const QByteArray &data,
                        int payloadSize,
                        CheckMode checkMode,
                        char paddingByte = padding);
bool decodePacket(const QByteArray &encoded,
                  CheckMode checkMode,
                  Packet *packet,
                  QString *errorMessage = nullptr);
QByteArray cancelSequence();

}// namespace XyModem

#endif// QSHELL_XYMODEM_PROTOCOL_H
