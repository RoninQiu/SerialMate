#pragma once

#include <QApplication>
#include <QColor>
#include <QPalette>

// 「本机发出的东西」这个语义色的**唯一出处**。
//
// 接收区用它给"发送"行上色，多字符串发送表用它标出"当前正在发送的行" ——
// 同一个语义写在两个 .cpp 里，早晚会长歪成两个不一样的橙。
//
// 为什么不用红绿（详见 DirectionHighlighter 头注释）：约 8% 男性有红绿色觉障碍，
// 红绿在他们眼里几乎同色，而且红绿自带"失败 / 成功"暗示，发收都不是错误。
//
// 对比度（WCAG，正文需 ≥ 4.5:1）：
//   浅色底 发 #A34A00 配 soft #FCEBD9 = 5.10:1
//   深色底 发 #FFB74D 配 soft #3B2A14 = 7.94:1
struct OutgoingAccent
{
    QColor text;   // 文字色，正文级
    QColor soft;   // 整行底色用的淡橙
};

// 应用不设任何样式表，完全跟随系统调色板，所以深浅两套都要取。
// 用 Base 而不是 Window 判断底色：表格和接收区都是编辑区，底色取自 Base 角色。
inline OutgoingAccent outgoingAccent()
{
    const bool dark = QApplication::palette().color(QPalette::Base).lightness() < 128;
    return dark ? OutgoingAccent{ QColor(0xFF, 0xB7, 0x4D), QColor(0x3B, 0x2A, 0x14) }
                : OutgoingAccent{ QColor(0xA3, 0x4A, 0x00), QColor(0xFC, 0xEB, 0xD9) };
}
