#include "ui/DirectionHighlighter.h"

#include "core/DataFormatter.h"

#include <QApplication>
#include <QPalette>
#include <QTextCharFormat>

namespace {

struct DirectionColors
{
    QColor incoming;
    QColor outgoing;
};

// 对比度实测见头文件注释。两套都不能省：应用跟随系统调色板，
// 用户开深色模式时第一套的深蓝/深橙会糊在深色底上。
const DirectionColors kOnLight{ QColor(0x00, 0x5F, 0xB8), QColor(0xA3, 0x4A, 0x00) };
const DirectionColors kOnDark { QColor(0x6C, 0xB6, 0xFF), QColor(0xFF, 0xB7, 0x4D) };

} // namespace

DirectionHighlighter::DirectionHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document)
{
    refreshPalette();
}

void DirectionHighlighter::refreshPalette()
{
    // 用 Base 而不是 Window 判断底色：接收区是编辑区，底色取自 Base 角色。
    const bool dark = QApplication::palette().color(QPalette::Base).lightness() < 128;
    const DirectionColors& c = dark ? kOnDark : kOnLight;
    m_incoming = c.incoming;
    m_outgoing = c.outgoing;
    rehighlight();
}

void DirectionHighlighter::highlightBlock(const QString& text)
{
    // 没有方向标记的行必须 O(1) 早退。整行染色走的是 setFormat(0, 长度)，
    // 对超长块调用会让连续追加退化成二次复杂度；设备发来的裸二进制
    // （不以标记开头）正好走这条早退路径。
    const QString in  = DataFormatter::directionMarker(DataFormatter::Direction::Incoming);
    const QString out = DataFormatter::directionMarker(DataFormatter::Direction::Outgoing);

    QTextCharFormat fmt;
    if (text.startsWith(in))
        fmt.setForeground(m_incoming);
    else if (text.startsWith(out))
        fmt.setForeground(m_outgoing);
    else
        return;

    setFormat(0, int(text.size()), fmt);
}