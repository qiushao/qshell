#ifndef QTWIDGET_QT_COMPAT_CODEC_H
#define QTWIDGET_QT_COMPAT_CODEC_H

#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#include <QStringDecoder>
#include <QStringEncoder>
#else
#include <QTextCodec>
#include <optional>

namespace QStringConverter {
enum Encoding {
    Utf8,
    System,
    Utf16
};

inline std::optional<Encoding> encodingForName(const QByteArray &name) {
    const QByteArray lowerName = name.toLower();
    if (lowerName.contains("utf-8") || lowerName == "utf8") {
        return Utf8;
    }
    if (lowerName.contains("utf-16") || lowerName == "utf16") {
        return Utf16;
    }
    return System;
}
} // namespace QStringConverter

class QStringEncoder {
public:
    enum {
        Utf8 = QStringConverter::Utf8,
        Utf16 = QStringConverter::Utf16
    };

    explicit QStringEncoder(QStringConverter::Encoding encoding = QStringConverter::System) {
        setEncoding(encoding);
    }
    explicit QStringEncoder(int encoding) {
        setEncoding(static_cast<QStringConverter::Encoding>(encoding));
    }

    bool isValid() const {
        return codec_ != nullptr;
    }

    QByteArray name() const {
        return codec_ != nullptr ? codec_->name() : QByteArray();
    }

    QByteArray operator()(const QString &text) const {
        return codec_ != nullptr ? codec_->fromUnicode(text) : text.toLocal8Bit();
    }

private:
    void setEncoding(QStringConverter::Encoding encoding) {
        switch (encoding) {
            case QStringConverter::Utf8:
                codec_ = QTextCodec::codecForName("UTF-8");
                break;
            case QStringConverter::Utf16:
                codec_ = QTextCodec::codecForName("UTF-16");
                break;
            case QStringConverter::System:
            default:
                codec_ = QTextCodec::codecForLocale();
                break;
        }
    }

    QTextCodec *codec_ = nullptr;
};

class QStringDecoder {
public:
    explicit QStringDecoder(QStringConverter::Encoding encoding = QStringConverter::System) {
        setEncoding(encoding);
    }

    QString operator()(const QByteArray &bytes) const {
        return codec_ != nullptr ? codec_->toUnicode(bytes) : QString::fromLocal8Bit(bytes);
    }

private:
    void setEncoding(QStringConverter::Encoding encoding) {
        switch (encoding) {
            case QStringConverter::Utf8:
                codec_ = QTextCodec::codecForName("UTF-8");
                break;
            case QStringConverter::Utf16:
                codec_ = QTextCodec::codecForName("UTF-16");
                break;
            case QStringConverter::System:
            default:
                codec_ = QTextCodec::codecForLocale();
                break;
        }
    }

    QTextCodec *codec_ = nullptr;
};
#endif

#endif // QTWIDGET_QT_COMPAT_CODEC_H
