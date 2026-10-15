#pragma once

#include "ui/Theme.h"

#include <QColor>

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

// 深浅判定走 themeIsDark() 而不是"看 Base 亮不亮"：用户可以强制浅色
// 或强制深色，这时它俩可能指向不同答案，菜单勾了什么就该是什么。
//
// 注意它**不能**替代"高亮行重画"：颜色是取到了，但已经写进单元格的旧颜色
// 不会自己变，MultiSendTable 里那��� paletteChanged 重画是另一件事。
inline OutgoingAccent outgoingAccent()
{
    return themeIsDark(currentTheme())
             ? OutgoingAccent{ QColor(0xFF, 0xB7, 0x4D), QColor(0x3B, 0x2A, 0x14) }
             : OutgoingAccent{ QColor(0xA3, 0x4A, 0x00), QColor(0xFC, 0xEB, 0xD9) };
}
