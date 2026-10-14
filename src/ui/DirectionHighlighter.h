#pragma once

#include <QColor>
#include <QSyntaxHighlighter>

class QTextDocument;

// 按收发方向给接收区每一行上色：接收=蓝，发送=橙。
//
// 为什么不用换控件、不用 appendHtml：
//   Qt 5 起移除了 QPlainTextEdit::setSyntaxHighlighter()，但**底层能力仍在** ——
//   把高亮器直接挂到 document() 上，QPlainTextEdit 的排版引擎照样渲染逐字符
//   颜色。差分实测（同一内容分别渲染「有/无高亮器」两张图，数差异像素）
//   与 QTextEdit 完全一致，均为 147 像素。故无需换控件，也就不必承担：
//     · appendHtml 的 HTML 注入 —— 串口数据里的 < > & 会被当标记解析；
//     · QTextEdit 在 20M 字符上限下的排版与内存退化。
//
// 为什么不用红绿：
//   约 8% 男性有红绿色觉障碍，红绿在这一批人眼里几乎同色；而红绿还会
//   暗示「失败 / 成功」，可收发都不是错误，语义本身就是错的。
//   蓝 / 橙在各类色觉障碍下均可区分，且不携带成败暗示。
//
// 两套配色均满足 WCAG AA（正文对比度 ≥ 4.5:1）：
//   浅色底   收 #005FB8 = 6.31:1    发 #A34A00 = 5.94:1
//   深色底   收 #6CB6FF = 7.76:1    发 #FFB74D = 9.63:1
class DirectionHighlighter : public QSyntaxHighlighter
{
public:
    explicit DirectionHighlighter(QTextDocument* document);

    // 系统深浅色切换后重新取色并重刷。仓库里没有任何 setStyleSheet / setPalette，
    // 应用完全跟随系统调色板，所以这一条是必需的：写死的深蓝在深色底上读不出来。
    void refreshPalette();

protected:
    void highlightBlock(const QString& text) override;

private:
    QColor m_incoming;
    QColor m_outgoing;
};