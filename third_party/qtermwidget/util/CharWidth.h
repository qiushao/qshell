#ifndef CHARWIDTH_H
#define CHARWIDTH_H

#include <QString>
#include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QApplication>
#include <QVector>
#include <string>

class CharWidth
{
public:
    CharWidth(QFont font);
    ~CharWidth();

    void setFont(QFont font);
    int font_width(uint ucs);
    int font_width(const QChar & c);
    int string_font_width( const std::wstring & wstr );
    int string_font_width( const QVector<uint> & codePoints );
    int string_font_width( const QString & str );

    static int unicode_width(uint ucs, bool fix_width = true);
    static int unicode_width(const QChar & c, bool fix_width = true);
    static int string_unicode_width(const std::wstring & wstr, bool fix_width = true);
    static int string_unicode_width(const QVector<uint> & codePoints, bool fix_width = true);
    static int string_unicode_width(const QString & str, bool fix_width = true);

private:
    QFontMetrics *fm;
};

#endif // CHARWIDTH_H
