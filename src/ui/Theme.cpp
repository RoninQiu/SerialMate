#include "ui/Theme.h"

#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

#include <memory>

namespace {

// applyTheme() 记下当前生效的是哪一档。默认 System：还没调 applyTheme
// 的时候（单测、探针直接 new 控件）走的正是加主题之前的行为。
Theme g_current = Theme::System;

// 原生 style 的工厂键。
//
// 为什么需要它：setStyle 是**粘的** —— 一旦设成 Fusion，后续不调用它
// 并不会退回原生。所以浅色档必须显式设回去，不能只是"不碰"。
//
// 键不能直接拿 objectName 当。平台默认值两边不一样：Win11 是 windows11，
// Win10 落到 windowsvista。直接用 objectName 是赌平台碰巧同名，
// 一旦某平台上同名键不在 QStyleFactory::keys() 里，create() 返回
// nullptr，界面就再也切不回浅色了。
//
// 所以逐个建一遍、拿建出来的 objectName 与默认的**比对**，只返回真的
// 建得出来的那一个；一个都对不上就退回 keys() 的第一个 —— 那按定义
// 一定是有效键。建出来的 style 只用来比对，不进应用。
//
// 取值时机：第一次 applyTheme()，此时 QApplication::style() 还是平台
// 默认那个（main.cpp 在建 MainWindow 之前就调这里）。
QString nativeStyleKey()
{
    static const QString key = [] {
        const QString defaultName = QApplication::style()->objectName();
        const QStringList keys = QStyleFactory::keys();
        for (const QString& k : keys) {
            const std::unique_ptr<QStyle> probe(QStyleFactory::create(k));
            if (probe && probe->objectName() == defaultName)
                return k;
        }
        return keys.isEmpty() ? QString() : keys.first();
    }();
    return key;
}

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
    // setPalette 的影响。
    //
    // 退回 Base.lightness() 不是保险，是**Win10 的实际路径**：系统深浅色
    // 那个设置 Win10 1809 就有，但 Qt 在 Win10 上未必把 colorScheme 报成
    // Light/Dark，一旦报 Unknown 就得靠这一支。原来的代码一直用这个启发式，
    // 留着它 Win10 才不会退化成"永远浅色"。
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
    const bool dark = themeIsDark(theme);

    // 浅色保留原生 Windows style，深色切 Fusion。
    //
    // 深色**必须**换 style：只 setPalette() 的话，原生 windows11 style 的
    // 滚动条仍用系统色，在深底上是一条白杠（探针 build/probe_theme 实测，
    // 两张截图可直接对比）。换 Fusion 后滚动条跟随调色板，代价是复选框
    // 指示器对比度掉一截 —— 而这张表每行有「使能」「HEX」两个复选框，
    // 共 99 行，所以浅色档保留原生、只让深色档付这个代价。
    //
    // 两档都要显式 setStyle：setStyle 是**粘的**，深色设上去的 Fusion 不会
    // 因为下一次不调用它就自己消失。
    //
    // 先换 style 再换调色板：Fusion 的控件尺寸/绘制规则与原生不同，
    // 顺序反了会先按新调色板画一遍、再被新 style 重画一遍。
    if (QStyle* style = QStyleFactory::create(dark ? QStringLiteral("Fusion")
                                                   : nativeStyleKey())) {
        QApplication::setStyle(style);
    }
    // create() 返 nullptr 就**保持原样**：一个建不出来的 style 传进去，
    // 界面会停在上一个档位（深色切浅色时尤其糟）。

    // setPalette 会发 paletteChanged，DirectionHighlighter 已经监听了它
    // （ReceiveView 里接的），那之后不需要再单独通知。
    QApplication::setPalette(themePalette(dark));

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
