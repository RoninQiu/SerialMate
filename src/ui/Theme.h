#pragma once

#include "models.h"

#include <QPalette>

// 主题：跟随系统 / 浅色 / 深色。
//
// QStyle 的取舍（实测见头注释里说的探针）：
//   深色必须切到 Fusion —— 只调调色板的话，原生 windows11 style 的滚动条
//   仍用系统色，在深底上是一条白杠，而这张表有 99 行、滚动条天天在眼前。
//   浅色保留原生 —— Fusion 的复选框指示器对比度掉一截，而「使能」「HEX」
//   两列各 99 行是这个界面的主要控件；浅色本来也没有那条白杠要治。
//
// 代价：浅↔深来回切时控件外观会跳一下。这是明知的取舍，不是 bug。

// 当前该用深色还是浅色。Theme::System 时按 Qt 报的系统配色方案判定。
bool themeIsDark(Theme theme);

// 当前生效的主题，由 applyTheme() 记下。
// UI 各处问深浅时走这个，而不是各自去猜调色板 —— 强制浅色/深色时，
// "调色板长什么样"和"用户选了什么"未必是同一个答案。
Theme currentTheme();

// 深色下要套的那套配色。
QPalette themePalette(bool dark);

// 把主题落到 QApplication 上：换 QStyle、换调色板、重新 polish 全部控件。
// 必须在 MainWindow 建出来**之前**调一次，否则控件是按旧 style 建的，
// unpolish/polish 一遍能补救但会闪。
void applyTheme(Theme theme);
