#include "XyModemProtocol.h"

#include <QtGlobal>

namespace {

quint8 byteValue(char value) {
    return static_cast<quint8>(static_cast<unsigned char>(value));
}

}// namespace

namespace XyModem {

quint16 crc16(const QByteArray &data) {
    quint16 crc = 0;
    for (const char value: data) {
        crc ^= static_cast<quint16>(byteValue(value)) << 8U;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0U
                          ? static_cast<quint16>((crc << 1U) ^ 0x1021U)
                          : static_cast<quint16>(crc << 1U);
        }
    }
    return crc;
}

quint8 checksum(const QByteArray &data) {
    quint8 result = 0;
    for (const char value: data) {
        result = static_cast<quint8>(result + byteValue(value));
    }
    return result;
}

int blockSize(const char startByte) {
    if (startByte == soh) {
        return shortBlockSize;
    }
    if (startByte == stx) {
        return longBlockSize;
    }
    return 0;
}

qsizetype encodedPacketSize(const char startByte,
                            const CheckMode checkMode) {
    const int payloadSize = blockSize(startByte);
    if (payloadSize == 0) {
        return 0;
    }
    return 3 + payloadSize + (checkMode == CheckMode::Crc16 ? 2 : 1);
}

QByteArray encodePacket(const quint8 number,
                        const QByteArray &data,
                        const int payloadSize,
                        const CheckMode checkMode,
                        const char paddingByte) {
    Q_ASSERT(payloadSize == shortBlockSize || payloadSize == longBlockSize);
    Q_ASSERT(data.size() <= payloadSize);

    QByteArray payload = data.left(payloadSize);
    payload.append(QByteArray(payloadSize - payload.size(),
                              paddingByte));

    QByteArray encoded;
    encoded.reserve(3 + payloadSize + (checkMode == CheckMode::Crc16 ? 2 : 1));
    encoded.append(payloadSize == shortBlockSize ? soh : stx);
    encoded.append(static_cast<char>(number));
    encoded.append(static_cast<char>(0xffU - number));
    encoded.append(payload);
    if (checkMode == CheckMode::Crc16) {
        const quint16 value = crc16(payload);
        encoded.append(static_cast<char>((value >> 8U) & 0xffU));
        encoded.append(static_cast<char>(value & 0xffU));
    } else {
        encoded.append(static_cast<char>(checksum(payload)));
    }
    return encoded;
}

bool decodePacket(const QByteArray &encoded,
                  const CheckMode checkMode,
                  Packet *packet,
                  QString *errorMessage) {
    if (packet == nullptr) {
        return false;
    }
    const qsizetype expectedSize =
            encodedPacketSize(encoded.isEmpty() ? '\0' : encoded.at(0),
                              checkMode);
    if (expectedSize == 0 || encoded.size() != expectedSize) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                    "Invalid X/YMODEM packet length.");
        }
        return false;
    }

    const quint8 number = byteValue(encoded.at(1));
    const quint8 complement = byteValue(encoded.at(2));
    if (static_cast<quint8>(number + complement) != 0xffU) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                    "Invalid X/YMODEM block number.");
        }
        return false;
    }

    const int payloadSize = blockSize(encoded.at(0));
    const QByteArray payload = encoded.mid(3, payloadSize);
    if (checkMode == CheckMode::Crc16) {
        const quint16 received =
                static_cast<quint16>(
                        byteValue(encoded.at(3 + payloadSize)))
                        << 8U |
                byteValue(encoded.at(4 + payloadSize));
        if (received != crc16(payload)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral(
                        "Invalid X/YMODEM CRC.");
            }
            return false;
        }
    } else if (byteValue(encoded.at(3 + payloadSize)) != checksum(payload)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                    "Invalid X/YMODEM checksum.");
        }
        return false;
    }

    packet->number = number;
    packet->data = payload;
    return true;
}

QByteArray cancelSequence() {
    QByteArray sequence(8, can);
    sequence.append(QByteArray(8, '\b'));
    return sequence;
}

}// namespace XyModem
