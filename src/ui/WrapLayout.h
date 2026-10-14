#ifndef WRAPLAYOUT_H
#define WRAPLAYOUT_H

#include <QtWidgets/QLayout>
#include <QtCore/QList>

// 按宽度自动折行的横向布局。
//
// 三栏布局之后中栏最窄只有 380px，而接收区工具栏那一排控件加起来要
// 700px 上下。放进 QHBoxLayout 的直接后果不是"被挤扁"而是"看不见"：
// 每个控件拿到自己的 minimumSize 后仍然放不下，QLayout 就把多余的宽度
// 从同一个 row 里按比例扣掉，标签最后被截成 "保"、"时"、"显示" 这种
// 谁也认不出的碎片。最小宽度护不住这个 —— 最小宽度只会让 splitter 把
// 整个窗口撑大，撑不下时就直接溢出屏幕。
//
// 折行是这里唯一不牺牲功能的做法：控件宽度永远够用，放不下的整块挪到
// 下一行，只多占一点高度。
//
// Qt 没有内置流式布局，这个类只实现「一排放不下就换行」的最小功能：
// 不处理 addStretch / addItem 的手工项，全部子项一视同仁按控件排布。
class WrapLayout : public QLayout
{
public:
    explicit WrapLayout(QWidget* parent = nullptr);
    WrapLayout(int margin, int hSpacing, int vSpacing, QWidget* parent = nullptr);
    ~WrapLayout() override;

    void addItem(QLayoutItem* item) override;

    int horizontalSpacing() const;
    int verticalSpacing() const;

    QSize sizeHint() const override;
    QSize minimumSize() const override;
    int heightForWidth(int width) const override;
    bool hasHeightForWidth() const override;
    void setGeometry(const QRect& rect) override;
    Qt::Orientations expandingDirections() const override;
    void invalidate() override;

private:
    int count() const;
    QLayoutItem* itemAt(int index) const;
    QLayoutItem* takeAt(int index);
    QList<QRect> computeRects(const QRect& rect) const;
    static void stretchRow(QList<QRect>& rects, const QList<int>& maxHeights,
                           qsizetype from, int rowHeight);
    QSize boundingSize(const QRect& rect) const;

    QList<QLayoutItem*> m_items;
    int m_hSpacing = 6;
    int m_vSpacing = 4;
};

#endif // WRAPLAYOUT_H