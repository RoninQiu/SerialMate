#include "ui/DirectionHighlighter.h"

#include "core/DataFormatter.h"
#include "ui/OutgoingAccent.h"

#include <QApplication>
#include <QPalette>
#include <QTextCharFormat>

namespace {

// 「接收蓝」留在本文件；发送橙已抽到 OutgoingAccent.h，
// 因为多字符串发送表标「当前正在发送的行」要用同一个语义色。
// 深浅两套都不能省：应用跟随系统调色板，用户开深色模式时
// 深蓝会糊在深色底上。对比度实测见头文件注释。
const QColor kIncomingOnLight = QColor(0x00, 0x5F, 0xB8);
const QColor kIncomingOnDark  = QColor(0x6C, 0xB6, 0xFF);

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
    m_incoming = dark ? kIncomingOnDark : kIncomingOnLight;
    m_outgoing = outgoingAccent().text;
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