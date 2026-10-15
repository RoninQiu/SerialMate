#pragma once

#include "models.h"

#include <QPalette>

// 主题：跟随系统 / 浅色 / 深色。
//
// 为什么必须连 QStyle 一起换：实测（探针 build/probe_theme，三张截图）
// 只调 QApplication::setPalette() 的话，原生 windows11 style 的**滚动条
// 仍用系统色**，在深底上是一条明显的白杠 —— 表格有 99 行，滚动条天天在眼前。
// 换成 Fusion 后滚动条跟随调色板，代价是复选框指示器对比度掉一截
// （每行「使能」「HEX」两个复选框，是这个界面的主要控件）。
//
// 三档一律 Fusion，而不是只有深色切：否则浅/深来回切时外观会整跳一下，
// 像两个不同的软件。三档观感一致更省心。

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
