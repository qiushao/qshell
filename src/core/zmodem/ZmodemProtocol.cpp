#include "ZmodemProtocol.h"

#include <algorithm>
#include <limits>

namespace Zmodem {
namespace {

constexpr char zpad = 0x2a;
constexpr char zdle = 0x18;
constexpr char zbin = 0x41;
constexpr char zhex = 0x42;
constexpr char zbin32 = 0x43;
constexpr char xon = 0x11;
constexpr char xoff = 0x13;
constexpr char backspace = 0x08;
constexpr char zrub0 = 0x6c;
constexpr char zrub1 = 0x6d;
constexpr qsizetype maxDataSubpacketSize = 1024 * 1024;

enum class DecodeStatus {
    Complete,
    NeedMore,
    FrameEnd,
    Invalid
};

std::array<quint8, 4> littleEndian(quint32 value) {
    return {
            static_cast<quint8>(value & 0xffU),
            static_cast<quint8>((value >> 8U) & 0xffU),
            static_cast<quint8>((value >> 16U) & 0xffU),
            static_cast<quint8>((value >> 24U) & 0xffU)};
}

const std::array<quint16, 256> &crc16Table() {
    static const std::array<quint16, 256> table = [] {
        std::array<quint16, 256> result{};
        for (std::size_t value = 0; value < result.size(); ++value) {
            auto entry = static_cast<quint16>(value << 8U);
            for (int bit = 0; bit < 8; ++bit) {
                if ((entry & 0x8000U) != 0U) {
                    entry = static_cast<quint16>((entry << 1U) ^ 0x1021U);
                } else {
                    entry = static_cast<quint16>(entry << 1U);
                }
            }
            result[value] = entry;
        }
        return result;
    }();
    return table;
}

quint16 updateCrc16(quint16 crc, quint8 byte) {
    return static_cast<quint16>(crc16Table()[(crc >> 8U) & 0xffU] ^ static_cast<quint16>(crc << 8U) ^ byte);
}

quint16 calculateCrc16(const QByteArray &data) {
    quint16 crc = 0;
    for (const char byte: data) {
        crc = updateCrc16(crc, static_cast<quint8>(byte));
    }
    crc = updateCrc16(crc, 0);
    return updateCrc16(crc, 0);
}

const std::array<quint32, 256> &crc32Table() {
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> result{};
        for (quint32 value = 0; value < result.size(); ++value) {
            quint32 entry = value;
            for (int bit = 0; bit < 8; ++bit) {
                if ((entry & 1U) != 0U) {
                    entry = (entry >> 1U) ^ 0xedb88320U;
                } else {
                    entry >>= 1U;
                }
            }
            result[value] = entry;
        }
        return result;
    }();
    return table;
}

quint32 updateCrc32(quint32 crc, quint8 byte) {
    return crc32Table()[(crc ^ byte) & 0xffU] ^ (crc >> 8U);
}

quint32 calculateCrc32(const QByteArray &data) {
    quint32 crc = std::numeric_limits<quint32>::max();
    for (const char byte: data) {
        crc = updateCrc32(crc, static_cast<quint8>(byte));
    }
    return ~crc;
}

char hexDigit(quint8 value) {
    return value < 10U ? static_cast<char>('0' + value)
                       : static_cast<char>('a' + value - 10U);
}

void appendHexByte(QByteArray *output, quint8 value) {
    output->append(hexDigit(static_cast<quint8>(value >> 4U)));
    output->append(hexDigit(static_cast<quint8>(value & 0x0fU)));
}

bool decodeHexDigit(char value, quint8 *decoded) {
    if (value >= '0' && value <= '9') {
        *decoded = static_cast<quint8>(value - '0');
        return true;
    }
    if (value >= 'a' && value <= 'f') {
        *decoded = static_cast<quint8>(value - 'a' + 10);
        return true;
    }
    if (value >= 'A' && value <= 'F') {
        *decoded = static_cast<quint8>(value - 'A' + 10);
        return true;
    }
    return false;
}

bool decodeHexByte(const QByteArray &input, qsizetype offset, quint8 *decoded) {
    quint8 high = 0;
    quint8 low = 0;
    if (!decodeHexDigit(input.at(offset), &high) || !decodeHexDigit(input.at(offset + 1), &low)) {
        return false;
    }
    *decoded = static_cast<quint8>((high << 4U) | low);
    return true;
}

bool isFlowControl(quint8 value) {
    return value == static_cast<quint8>(xon) || value == static_cast<quint8>(xoff) || value == (static_cast<quint8>(xon) | 0x80U) || value == (static_cast<quint8>(xoff) | 0x80U);
}

DecodeStatus decodeEscapedByte(const QByteArray &input,
                               qsizetype *offset,
                               quint8 *decoded) {
    qsizetype cursor = *offset;
    while (cursor < input.size()) {
        const auto value = static_cast<quint8>(input.at(cursor));
        if (isFlowControl(value)) {
            ++cursor;
            continue;
        }
        if (value != static_cast<quint8>(zdle)) {
            *decoded = value;
            *offset = cursor + 1;
            return DecodeStatus::Complete;
        }
        if (cursor + 1 >= input.size()) {
            return DecodeStatus::NeedMore;
        }

        const auto escaped = static_cast<quint8>(input.at(cursor + 1));
        if (escaped >= static_cast<quint8>(FrameEnd::End) && escaped <= static_cast<quint8>(FrameEnd::EndAck)) {
            *decoded = escaped;
            *offset = cursor + 2;
            return DecodeStatus::FrameEnd;
        }
        if (escaped == static_cast<quint8>(zrub0)) {
            *decoded = 0x7fU;
        } else if (escaped == static_cast<quint8>(zrub1)) {
            *decoded = 0xffU;
        } else if ((escaped & 0x60U) == 0x40U) {
            *decoded = escaped ^ 0x40U;
        } else {
            *offset = cursor + 2;
            return DecodeStatus::Invalid;
        }
        *offset = cursor + 2;
        return DecodeStatus::Complete;
    }
    return DecodeStatus::NeedMore;
}

bool shouldEscape(quint8 value, bool escapeControlCharacters) {
    if (value == static_cast<quint8>(zdle) || value == 0x10U || value == 0x90U || isFlowControl(value) || value == 0x0dU || value == 0x8dU) {
        return true;
    }
    return escapeControlCharacters && (value & 0x60U) == 0U;
}

void appendEscapedByte(QByteArray *output,
                       quint8 value,
                       bool escapeControlCharacters) {
    if (escapeControlCharacters && (value == 0x7fU || value == 0xffU)) {
        output->append(zdle);
        output->append(
                value == 0x7fU ? zrub0 : zrub1);
        return;
    }
    if (shouldEscape(value, escapeControlCharacters)) {
        output->append(zdle);
        output->append(static_cast<char>(value ^ 0x40U));
    } else {
        output->append(static_cast<char>(value));
    }
}

void appendEscaped(QByteArray *output,
                   const QByteArray &data,
                   bool escapeControlCharacters) {
    for (const char value: data) {
        appendEscapedByte(output,
                          static_cast<quint8>(value),
                          escapeControlCharacters);
    }
}

QByteArray headerPayload(FrameType type,
                         const std::array<quint8, 4> &data) {
    QByteArray payload;
    payload.reserve(5);
    payload.append(static_cast<char>(type));
    for (const quint8 byte: data) {
        payload.append(static_cast<char>(byte));
    }
    return payload;
}

qsizetype matchingSuffixLength(const QByteArray &buffer,
                               const QByteArray &prefix) {
    const qsizetype maximum = std::min(buffer.size(), prefix.size() - 1);
    for (qsizetype length = maximum; length > 0; --length) {
        if (buffer.endsWith(prefix.left(length))) {
            return length;
        }
    }
    return 0;
}

}// namespace

quint32 Header::position() const {
    return static_cast<quint32>(data[0]) | (static_cast<quint32>(data[1]) << 8U) | (static_cast<quint32>(data[2]) << 16U) | (static_cast<quint32>(data[3]) << 24U);
}

bool Header::usesCrc32() const {
    return format == HeaderFormat::Binary32;
}

void Parser::addData(const QByteArray &data) {
    buffer_.append(data);
}

bool Parser::next(ParseItem *item) {
    if (item == nullptr) {
        return false;
    }
    if (discardCancelTail_) {
        while (!buffer_.isEmpty() && (buffer_.front() == zdle || buffer_.front() == backspace)) {
            buffer_.remove(0, 1);
        }
        if (buffer_.isEmpty()) {
            return false;
        }
        discardCancelTail_ = false;
    }
    if (buffer_.isEmpty()) {
        return false;
    }
    *item = ParseItem{};

    if (takeCancel(item)) {
        return true;
    }
    qsizetype partialCancelSize = 0;
    while (partialCancelSize < buffer_.size() && partialCancelSize < 5 && buffer_.at(buffer_.size() - partialCancelSize - 1) == zdle) {
        ++partialCancelSize;
    }
    if (partialCancelSize > 0 && partialCancelSize < 5) {
        return false;
    }
    if (expectData_) {
        return parseDataSubpacket(item);
    }
    return parseHeader(item);
}

void Parser::expectDataSubpacket() {
    expectData_ = true;
    dataUsesCrc32_ = lastHeaderUsesCrc32_;
}

void Parser::expectHeader() {
    expectData_ = false;
}

bool Parser::hasPendingData() const {
    return !buffer_.isEmpty();
}

QByteArray Parser::takePendingData() {
    QByteArray data;
    data.swap(buffer_);
    expectData_ = false;
    return data;
}

void Parser::reset() {
    buffer_.clear();
    expectData_ = false;
    dataUsesCrc32_ = false;
    lastHeaderUsesCrc32_ = false;
    discardCancelTail_ = false;
}

bool Parser::takeCancel(ParseItem *item) {
    constexpr qsizetype cancelCount = 5;
    const QByteArray cancelPrefix(cancelCount, zdle);
    const qsizetype cancelOffset = buffer_.indexOf(cancelPrefix);
    if (cancelOffset >= 0) {
        if (cancelOffset > 0) {
            item->kind = ParseItem::Kind::PlainText;
            item->data = buffer_.left(cancelOffset);
            item->raw = item->data;
            buffer_.remove(0, cancelOffset);
            return true;
        }
        qsizetype consumed = cancelOffset + cancelCount;
        while (consumed < buffer_.size() && buffer_.at(consumed) == zdle) {
            ++consumed;
        }
        while (consumed < buffer_.size() && buffer_.at(consumed) == backspace) {
            ++consumed;
        }
        item->kind = ParseItem::Kind::Cancel;
        item->raw = buffer_.left(consumed);
        buffer_.remove(0, consumed);
        expectData_ = false;
        discardCancelTail_ = true;
        return true;
    }
    return false;
}

bool Parser::parseHeader(ParseItem *item) {
    static const QByteArray hexPrefix =
            QByteArrayLiteral("**\x18"
                              "B");
    static const QByteArray binary16Prefix =
            QByteArrayLiteral("*\x18"
                              "A");
    static const QByteArray binary32Prefix =
            QByteArrayLiteral("*\x18"
                              "C");

    const qsizetype hexOffset = buffer_.indexOf(hexPrefix);
    const qsizetype binary16Offset = buffer_.indexOf(binary16Prefix);
    const qsizetype binary32Offset = buffer_.indexOf(binary32Prefix);

    qsizetype prefixOffset = -1;
    enum class Candidate {
        Hex,
        Binary16,
        Binary32
    };
    Candidate candidate = Candidate::Hex;

    const auto chooseCandidate = [&prefixOffset, &candidate](
                                         qsizetype offset,
                                         Candidate value) {
        if (offset >= 0 && (prefixOffset < 0 || offset < prefixOffset)) {
            prefixOffset = offset;
            candidate = value;
        }
    };
    chooseCandidate(hexOffset, Candidate::Hex);
    chooseCandidate(binary16Offset, Candidate::Binary16);
    chooseCandidate(binary32Offset, Candidate::Binary32);

    if (prefixOffset > 0) {
        item->kind = ParseItem::Kind::PlainText;
        item->data = buffer_.left(prefixOffset);
        item->raw = item->data;
        buffer_.remove(0, prefixOffset);
        return true;
    }

    if (prefixOffset < 0) {
        const qsizetype suffixLength = std::max(
                {matchingSuffixLength(buffer_, hexPrefix),
                 matchingSuffixLength(buffer_, binary16Prefix),
                 matchingSuffixLength(buffer_, binary32Prefix)});
        const qsizetype plainLength = buffer_.size() - suffixLength;
        if (plainLength <= 0) {
            return false;
        }
        item->kind = ParseItem::Kind::PlainText;
        item->data = buffer_.left(plainLength);
        item->raw = item->data;
        buffer_.remove(0, plainLength);
        return true;
    }

    switch (candidate) {
        case Candidate::Hex:
            return parseHexHeader(0, item);
        case Candidate::Binary16:
            return parseBinaryHeader(0, false, item);
        case Candidate::Binary32:
            return parseBinaryHeader(0, true, item);
    }
    return false;
}

bool Parser::parseHexHeader(int prefixOffset, ParseItem *item) {
    constexpr qsizetype prefixSize = 4;
    constexpr qsizetype encodedPayloadSize = 14;
    constexpr qsizetype minimumSize = prefixSize + encodedPayloadSize;
    if (buffer_.size() < prefixOffset + minimumSize) {
        return false;
    }

    QByteArray decoded;
    decoded.reserve(7);
    for (qsizetype index = 0; index < 7; ++index) {
        quint8 byte = 0;
        if (!decodeHexByte(buffer_,
                           prefixOffset + prefixSize + index * 2,
                           &byte)) {
            item->kind = ParseItem::Kind::Error;
            item->error = QStringLiteral("Invalid hexadecimal ZMODEM header");
            item->raw = buffer_.left(prefixOffset + 1);
            buffer_.remove(0, prefixOffset + 1);
            return true;
        }
        decoded.append(static_cast<char>(byte));
    }

    const QByteArray payload = decoded.left(5);
    const quint16 expectedCrc = calculateCrc16(payload);
    const quint16 receivedCrc =
            (static_cast<quint16>(static_cast<quint8>(decoded.at(5))) << 8U) | static_cast<quint8>(decoded.at(6));
    qsizetype consumed = prefixOffset + minimumSize;
    if (consumed < buffer_.size() && buffer_.at(consumed) == '\r') {
        ++consumed;
    }
    if (consumed < buffer_.size() && (buffer_.at(consumed) == '\n' || static_cast<quint8>(buffer_.at(consumed)) == 0x8aU)) {
        ++consumed;
    }
    if (consumed < buffer_.size() && buffer_.at(consumed) == xon) {
        ++consumed;
    }

    item->raw = buffer_.left(consumed);
    buffer_.remove(0, consumed);
    if (receivedCrc != expectedCrc) {
        item->kind = ParseItem::Kind::Error;
        item->error = QStringLiteral("Invalid CRC in hexadecimal ZMODEM header");
        return true;
    }

    item->kind = ParseItem::Kind::Header;
    item->header.type =
            static_cast<FrameType>(static_cast<quint8>(payload.at(0)));
    for (qsizetype index = 0; index < 4; ++index) {
        item->header.data[static_cast<std::size_t>(index)] =
                static_cast<quint8>(payload.at(index + 1));
    }
    item->header.format = HeaderFormat::Hex;
    lastHeaderUsesCrc32_ = false;
    return true;
}

bool Parser::parseBinaryHeader(int prefixOffset,
                               bool crc32,
                               ParseItem *item) {
    constexpr qsizetype prefixSize = 3;
    const qsizetype decodedSize = crc32 ? 9 : 7;
    qsizetype cursor = prefixOffset + prefixSize;
    QByteArray decoded;
    decoded.reserve(decodedSize);

    while (decoded.size() < decodedSize) {
        quint8 byte = 0;
        const DecodeStatus status =
                decodeEscapedByte(buffer_, &cursor, &byte);
        if (status == DecodeStatus::NeedMore) {
            return false;
        }
        if (status != DecodeStatus::Complete) {
            item->kind = ParseItem::Kind::Error;
            item->error = QStringLiteral("Invalid escaped ZMODEM header");
            item->raw = buffer_.left(std::max<qsizetype>(1, cursor));
            buffer_.remove(0, item->raw.size());
            return true;
        }
        decoded.append(static_cast<char>(byte));
    }

    item->raw = buffer_.left(cursor);
    buffer_.remove(0, cursor);
    const QByteArray payload = decoded.left(5);
    bool validCrc = false;
    if (crc32) {
        const quint32 expectedCrc = calculateCrc32(payload);
        const quint32 receivedCrc =
                static_cast<quint8>(decoded.at(5)) | (static_cast<quint32>(static_cast<quint8>(decoded.at(6))) << 8U) | (static_cast<quint32>(static_cast<quint8>(decoded.at(7))) << 16U) | (static_cast<quint32>(static_cast<quint8>(decoded.at(8))) << 24U);
        validCrc = receivedCrc == expectedCrc;
    } else {
        const quint16 expectedCrc = calculateCrc16(payload);
        const quint16 receivedCrc =
                (static_cast<quint16>(
                         static_cast<quint8>(decoded.at(5)))
                 << 8U) |
                static_cast<quint8>(decoded.at(6));
        validCrc = receivedCrc == expectedCrc;
    }
    if (!validCrc) {
        item->kind = ParseItem::Kind::Error;
        item->error = QStringLiteral("Invalid CRC in binary ZMODEM header");
        return true;
    }

    item->kind = ParseItem::Kind::Header;
    item->header.type =
            static_cast<FrameType>(static_cast<quint8>(payload.at(0)));
    for (qsizetype index = 0; index < 4; ++index) {
        item->header.data[static_cast<std::size_t>(index)] =
                static_cast<quint8>(payload.at(index + 1));
    }
    item->header.format =
            crc32 ? HeaderFormat::Binary32 : HeaderFormat::Binary16;
    lastHeaderUsesCrc32_ = crc32;
    return true;
}

bool Parser::parseDataSubpacket(ParseItem *item) {
    qsizetype cursor = 0;
    QByteArray decoded;

    while (true) {
        quint8 byte = 0;
        const DecodeStatus status =
                decodeEscapedByte(buffer_, &cursor, &byte);
        if (status == DecodeStatus::NeedMore) {
            return false;
        }
        if (status == DecodeStatus::Invalid) {
            item->kind = ParseItem::Kind::Error;
            item->error = QStringLiteral("Invalid ZDLE escape in ZMODEM data");
            item->raw = buffer_.left(std::max<qsizetype>(1, cursor));
            buffer_.remove(0, item->raw.size());
            expectData_ = false;
            return true;
        }
        if (status == DecodeStatus::Complete) {
            decoded.append(static_cast<char>(byte));
            if (decoded.size() > maxDataSubpacketSize) {
                item->kind = ParseItem::Kind::Error;
                item->error = QStringLiteral("ZMODEM data subpacket is too large");
                item->raw = buffer_.left(cursor);
                buffer_.remove(0, cursor);
                expectData_ = false;
                return true;
            }
            continue;
        }

        const auto frameEnd = static_cast<FrameEnd>(byte);
        const int crcSize = dataUsesCrc32_ ? 4 : 2;
        QByteArray receivedCrc;
        receivedCrc.reserve(crcSize);
        for (int index = 0; index < crcSize; ++index) {
            quint8 crcByte = 0;
            const DecodeStatus crcStatus =
                    decodeEscapedByte(buffer_, &cursor, &crcByte);
            if (crcStatus == DecodeStatus::NeedMore) {
                return false;
            }
            if (crcStatus != DecodeStatus::Complete) {
                item->kind = ParseItem::Kind::Error;
                item->error = QStringLiteral("Invalid CRC encoding in ZMODEM data");
                item->raw = buffer_.left(std::max<qsizetype>(1, cursor));
                buffer_.remove(0, item->raw.size());
                expectData_ = false;
                return true;
            }
            receivedCrc.append(static_cast<char>(crcByte));
        }

        QByteArray crcPayload = decoded;
        crcPayload.append(static_cast<char>(frameEnd));
        bool validCrc = false;
        if (dataUsesCrc32_) {
            const quint32 expectedCrc = calculateCrc32(crcPayload);
            const quint32 actualCrc =
                    static_cast<quint8>(receivedCrc.at(0)) | (static_cast<quint32>(static_cast<quint8>(receivedCrc.at(1))) << 8U) | (static_cast<quint32>(static_cast<quint8>(receivedCrc.at(2))) << 16U) | (static_cast<quint32>(static_cast<quint8>(receivedCrc.at(3))) << 24U);
            validCrc = actualCrc == expectedCrc;
        } else {
            const quint16 expectedCrc = calculateCrc16(crcPayload);
            const quint16 actualCrc =
                    (static_cast<quint16>(
                             static_cast<quint8>(receivedCrc.at(0)))
                     << 8U) |
                    static_cast<quint8>(receivedCrc.at(1));
            validCrc = actualCrc == expectedCrc;
        }

        item->raw = buffer_.left(cursor);
        buffer_.remove(0, cursor);
        expectData_ = false;
        if (!validCrc) {
            item->kind = ParseItem::Kind::Error;
            item->error = QStringLiteral("Invalid CRC in ZMODEM data");
            return true;
        }
        item->kind = ParseItem::Kind::Data;
        item->data = decoded;
        item->frameEnd = frameEnd;
        return true;
    }
}

QByteArray encodeHexHeader(FrameType type,
                           const std::array<quint8, 4> &data) {
    const QByteArray payload = headerPayload(type, data);
    const quint16 crc = calculateCrc16(payload);

    QByteArray output;
    output.reserve(21);
    output.append(zpad);
    output.append(zpad);
    output.append(zdle);
    output.append(zhex);
    for (const char byte: payload) {
        appendHexByte(&output, static_cast<quint8>(byte));
    }
    appendHexByte(&output, static_cast<quint8>(crc >> 8U));
    appendHexByte(&output, static_cast<quint8>(crc & 0xffU));
    output.append('\r');
    output.append(static_cast<char>(0x8a));
    if (type != FrameType::Zfin && type != FrameType::Zack) {
        output.append(xon);
    }
    return output;
}

QByteArray encodeHexHeader(FrameType type, quint32 position) {
    return encodeHexHeader(type, littleEndian(position));
}

QByteArray encodeBinaryHeader(FrameType type,
                              const std::array<quint8, 4> &data,
                              bool crc32,
                              bool escapeControlCharacters) {
    const QByteArray payload = headerPayload(type, data);
    QByteArray output;
    output.reserve(24);
    output.append(zpad);
    output.append(zdle);
    output.append(crc32 ? zbin32 : zbin);
    appendEscaped(&output, payload, escapeControlCharacters);

    if (crc32) {
        quint32 crc = calculateCrc32(payload);
        for (int index = 0; index < 4; ++index) {
            appendEscapedByte(&output,
                              static_cast<quint8>(crc & 0xffU),
                              escapeControlCharacters);
            crc >>= 8U;
        }
    } else {
        const quint16 crc = calculateCrc16(payload);
        appendEscapedByte(&output,
                          static_cast<quint8>(crc >> 8U),
                          escapeControlCharacters);
        appendEscapedByte(&output,
                          static_cast<quint8>(crc & 0xffU),
                          escapeControlCharacters);
    }
    return output;
}

QByteArray encodeBinaryHeader(FrameType type,
                              quint32 position,
                              bool crc32,
                              bool escapeControlCharacters) {
    return encodeBinaryHeader(type,
                              littleEndian(position),
                              crc32,
                              escapeControlCharacters);
}

QByteArray encodeDataSubpacket(const QByteArray &data,
                               FrameEnd frameEnd,
                               bool crc32,
                               bool escapeControlCharacters) {
    QByteArray output;
    output.reserve(data.size() * 2 + 8);
    appendEscaped(&output, data, escapeControlCharacters);
    output.append(zdle);
    output.append(static_cast<char>(frameEnd));

    QByteArray crcPayload = data;
    crcPayload.append(static_cast<char>(frameEnd));
    if (crc32) {
        quint32 crc = calculateCrc32(crcPayload);
        for (int index = 0; index < 4; ++index) {
            appendEscapedByte(&output,
                              static_cast<quint8>(crc & 0xffU),
                              escapeControlCharacters);
            crc >>= 8U;
        }
    } else {
        const quint16 crc = calculateCrc16(crcPayload);
        appendEscapedByte(&output,
                          static_cast<quint8>(crc >> 8U),
                          escapeControlCharacters);
        appendEscapedByte(&output,
                          static_cast<quint8>(crc & 0xffU),
                          escapeControlCharacters);
    }
    if (frameEnd == FrameEnd::EndAck) {
        output.append(xon);
    }
    return output;
}

QByteArray abortSequence() {
    QByteArray output(10, zdle);
    output.append(QByteArray(10, backspace));
    return output;
}

}// namespace Zmodem
