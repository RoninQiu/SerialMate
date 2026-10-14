#include "ui/WrapLayout.h"

#include <QtWidgets/QWidget>
#include <QtCore/QMargins>

#include <climits>

WrapLayout::WrapLayout(QWidget* parent)
    : QLayout(parent)
{
}

WrapLayout::WrapLayout(int margin, int hSpacing, int vSpacing, QWidget* parent)
    : QLayout(parent)
    , m_hSpacing(hSpacing)
    , m_vSpacing(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

WrapLayout::~WrapLayout() = default;

void WrapLayout::addItem(QLayoutItem* item)
{
    m_items.append(item);
}

int WrapLayout::horizontalSpacing() const
{
    return m_hSpacing;
}

int WrapLayout::verticalSpacing() const
{
    return m_vSpacing;
}

int WrapLayout::count() const
{
    return m_items.size();
}

QLayoutItem* WrapLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem* WrapLayout::takeAt(int index)
{
    if (index < 0 || index >= m_items.size())
        return nullptr;
    return m_items.takeAt(index);
}

Qt::Orientations WrapLayout::expandingDirections() const
{
    // 横向不许"扩张"：一旦返回 Expanding，父布局会把多余宽度塞进来，
    // 折行就再也触发不了。
    return Qt::Orientations();
}

bool WrapLayout::hasHeightForWidth() const
{
    return true;
}

QSize WrapLayout::sizeHint() const
{
    // 父布局问"你想要多大"时给一个足够宽的值（= 单行铺开），
    // 真正的窄宽表现由 heightForWidth / setGeometry 决定。
    return boundingSize(QRect(0, 0, INT_MAX, 0));
}

int WrapLayout::heightForWidth(int width) const
{
    return boundingSize(QRect(0, 0, width, 0)).height();
}

QSize WrapLayout::minimumSize() const
{
    QSize min;
    for (const QLayoutItem* item : m_items)
        min = min.expandedTo(item->minimumSize());
    const QMargins margins = contentsMargins();
    return min + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

void WrapLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    // 只有这一处允许写子控件几何。sizeHint()/heightForWidth() 走的是
    // computeRects() 那条纯计算路径 —— 早期版本在 sizeHint() 里顺手
    // setGeometry，Qt 每次问尺寸就把所有控件拍到 (0,0)，工具栏因此
    // 整个贴到分组框顶部、把标题盖住了。
    const QList<QRect> rects = computeRects(rect);
    int slot = 0;
    for (QLayoutItem* item : m_items) {
        const QWidget* widget = item->widget();
        if (widget && widget->isHidden())
            continue;
        // computeRects() 对隐藏控件不占位，这里必须用同一个游标推进，
        // 直接拿 m_items 的下标会在中间有隐藏控件时整体错位
        if (slot < rects.size())
            item->setGeometry(rects.at(slot));
        ++slot;
    }
}

void WrapLayout::invalidate()
{
    QLayout::invalidate();
}

// 把 rect 横向切成若干行，放不下的整块换到下一行。返回每个可见子项的
// 几何，顺序与 m_items 一致；隐藏控件不占位，所以返回的列表可能比
// m_items 短 —— 调用方按下标顺序消费即可。
//
// 同一行内所有子项**统一撑到该行最高的高度**。不这么做的话，紧贴文字的
// 控件会和带边框内边距的控件差出一大截：QLabel::sizeForText() 只返回
// 文字高度（本项目实测 15px），而 QPushButton 的 sizeHint 含边框是 25px。
// 顶部对齐的结果是这个标签比邻居矮 10px，中文在 15px 行高里没有上下余量，
// 看上去就像字体比周围小一号、被削了一刀 —— 实测就是这么来的。
// 撑满之后 QLabel 默认垂直居中，基线才和按钮、复选框对得上。
QList<QRect> WrapLayout::computeRects(const QRect& rect) const
{
    QList<QRect> out;
    const QMargins margins = contentsMargins();
    const QRect effective = rect.adjusted(margins.left(), margins.top(),
                                          -margins.right(), -margins.bottom());
    if (effective.width() <= 0)
        return out;

    int x = effective.x();
    int y = effective.y();
    int rowHeight = 0;
    qsizetype rowStart = 0;   // 本行第一个子项在 out 里的下标
    QList<int> maxHeights;    // 与 out 同下标；rects 跳过了隐藏控件，
                              // 不能回头用 itemAt(i) 反查，只能边走边记
    for (const QLayoutItem* item : m_items) {
        const QWidget* widget = item->widget();
        if (widget && widget->isHidden())
            continue;

        const QSize hint = item->sizeHint();
        if (x != effective.x() && x + hint.width() > effective.right() + 1) {
            // 本行放不下，整块换到下一行；先把上一行等高铺好
            stretchRow(out, maxHeights, rowStart, rowHeight);
            x = effective.x();
            y = y + rowHeight + m_vSpacing;
            rowHeight = 0;
            rowStart = out.size();
        }
        out.append(QRect(QPoint(x, y), hint));
        maxHeights.append(widget ? widget->maximumHeight() : QWIDGETSIZE_MAX);
        x = x + hint.width() + m_hSpacing;
        rowHeight = qMax(rowHeight, hint.height());
    }
    stretchRow(out, maxHeights, rowStart, rowHeight);   // 最后一行
    return out;
}

// 把 [from, out.size()) 这一段的几何高度统一成 rowHeight。
// maxHeights 是同下标记录的上限：显式设过 maximumHeight 的控件不能被撑破。
// 只改高度，不动 y 和宽度 —— 位置和宽度是上一轮按换行算好的。
void WrapLayout::stretchRow(QList<QRect>& rects, const QList<int>& maxHeights,
                            qsizetype from, int rowHeight)
{
    if (rowHeight <= 0)
        return;
    for (qsizetype i = from; i < rects.size(); ++i) {
        const int maxH = (i < maxHeights.size()) ? maxHeights.at(i) : QWIDGETSIZE_MAX;
        QRect r = rects.at(i);
        r.setHeight(qMin(rowHeight, maxH));
        rects[i] = r;
    }
}

QSize WrapLayout::boundingSize(const QRect& rect) const
{
    const QList<QRect> rects = computeRects(rect);
    if (rects.isEmpty())
        return QSize(rect.width(), 0);

    QRect united = rects.first();
    for (const QRect& r : rects)
        united = united.united(r);
    const QMargins margins = contentsMargins();
    return QSize(united.width() + margins.left() + margins.right(),
                 united.height() + margins.top() + margins.bottom());
}