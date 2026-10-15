#include "ui/Theme.h"

#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

namespace {
// applyTheme() 记下当前生效的是哪一档。默认 System：还没调 applyTheme
// 的时候（单测、探针直接 new 控件）走的正是加主题之前的行为。
Theme g_current = Theme::System;
} // namespace

Theme currentTheme()
{
    return g_current;
}

bool themeIsDark(Theme theme)
{
    if (theme == Theme::Dark)
        return true;
    if (theme == Theme::Light)
        return false;

    // System：优先问 Qt 自己。QStyleHints::colorScheme 是 Qt 6.5 才有的
    // 显式接口，比"看 Base 亮不亮"可靠 —— 它直接报系统意图，不受我们自己
    // setPalette 的影响。本机实测（Windows 浅色）它给 Light，与注册表
    // AppsUseLightTheme 一致，默认调色板 Base 同样是 #ffffff。
    //
    // 退回 Base.lightness() 只在 Qt 说 Unknown 时才走：老 Qt、或某些
    // 平台插件根本不报这个值。原来的代码一直用启发式，留着当兜底。
    switch (QGuiApplication::styleHints()->colorScheme()) {
    case Qt::ColorScheme::Dark:
        return true;
    case Qt::ColorScheme::Light:
        return false;
    case Qt::ColorScheme::Unknown:
        break;
    }
    return QApplication::palette().color(QPalette::Base).lightness() < 128;
}

QPalette themePalette(bool dark)
{
    if (!dark) {
        // 浅色交回系统默认值。刻意**不**自己写一套浅色：系统浅色方案
        // 覆盖了跟随系统、强制浅色、以及 Linux/macOS 上的偏好，
        // 自写一套只会把它压掉，还会跟「跟随系统」对不上。
        return QPalette();
    }

    QPalette p;
    p.setColor(QPalette::Window, QColor(0x2B, 0x2B, 0x2B));
    p.setColor(QPalette::WindowText, QColor(0xE8, 0xE8, 0xE8));
    p.setColor(QPalette::Base, QColor(0x1E, 0x1E, 0x1E));
    p.setColor(QPalette::AlternateBase, QColor(0x28, 0x28, 0x28));
    p.setColor(QPalette::Text, QColor(0xE8, 0xE8, 0xE8));
    p.setColor(QPalette::Button, QColor(0x3C, 0x3C, 0x3C));
    p.setColor(QPalette::ButtonText, QColor(0xE8, 0xE8, 0xE8));
    p.setColor(QPalette::BrightText, QColor(0xFF, 0x6B, 0x6B));
    p.setColor(QPalette::Highlight, QColor(0x2D, 0x5A, 0x8C));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x6C, 0xB6, 0xFF));
    p.setColor(QPalette::LinkVisited, QColor(0xC0, 0x8A, 0xE0));
    p.setColor(QPalette::ToolTipBase, QColor(0x3C, 0x3C, 0x3C));
    p.setColor(QPalette::ToolTipText, QColor(0xE8, 0xE8, 0xE8));
    p.setColor(QPalette::PlaceholderText, QColor(0x88, 0x88, 0x88));

    // Disabled 三组不给就沿用启用色，禁用的「发送」按钮会和启用的一模一样，
    // 用户看不出哪条现在发得出去。
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x7A, 0x7A, 0x7A));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x7A, 0x7A, 0x7A));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x7A, 0x7A, 0x7A));
    return p;
}

void applyTheme(Theme theme)
{
    // 先记下当前档，后面 themeIsDark(currentTheme()) 才答得上来。
    g_current = theme;

    // 先换 style 再换调色板：Fusion 的控件尺寸/绘制规则与原生不同，
    // 顺序反了会先按新调色板画一遍、再被新 style 重画一遍。
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // setPalette 会发 paletteChanged，DirectionHighlighter 已经监听了它
    // （ReceiveView 里接的），那之后不需要再单独通知。
    QApplication::setPalette(themePalette(themeIsDark(theme)));

    // 改了 style 之后已有控件不会自动重新 polish：菜单、按钮、下拉框
    // 会留着旧 style 画出来的样子直到重启。不 polish 的话切主题只有背景色变了。
    for (QWidget* w : QApplication::allWidgets()) {
        if (QStyle* s = w->style()) {
            s->unpolish(w);
            s->polish(w);
        }
        w->update();
    }
}
