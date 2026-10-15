#pragma once

#include "core/TextCodec.h"
#include "models.h"

#include <QAbstractItemModel>
#include <QGroupBox>
#include <QList>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTableWidget>

class QCheckBox;
class QEvent;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

// 内容列的编辑器代理。FR-04-6「内容列回车即提交并发送」靠它实现。
//
// 为什么必须是 delegate：原实现是 m_table->installEventFilter(this)，
// 过滤��里比对 watched == m_table->viewport()。实测探针（结论见开发记录）证明
// 该分支**永不进入**——
//   ① 装在 m_table 上的过滤器确实能收到投递给 viewport 的事件，但 Qt 转发时
//      watched 并不是 viewport 本身，所以那个等式恒假；
//   ② 编辑单元格时回车送的是编辑器 QLineEdit（viewport 的子控件），
//      根本不经由 m_table 的过滤器链。
// 两点叠起来，FR-04-6 从写下来那天起就没生效过，且不报任何错。
//
// 换成 delegate 的 eventFilter 后已实测：QStyledItemDelegate 的 eventFilter
// 确实会收到编辑器上的 KeyPress。
class ContentDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit ContentDelegate(QObject* parent = nullptr);

signals:
    // 编辑器所在行回车确认。发出时该行的内容已经写回模型。
    void enterAccepted(int row);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
};

// 「间隔」列的编辑器代理。
//
// 刻意用 delegate 而不是每行 setCellWidget 一个 QSpinBox：表格有 99 行，
// 常驻 99 个 QSpinBox 意味着约 300 个子控件，构造时间和内存都会明显上一个台阶，
// 滚动也会发涩。delegate 只在用户真正编辑某一格时才建一个编辑器。
//
// 值存在 Qt::UserRole，显示文本另存 —— 用 UserRole 作唯一真值来源，
// 免得"看到的数字"和"实际用的数字"在配置文件被手改后对不上。
class IntervalDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit IntervalDelegate(QObject* parent = nullptr);

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    // Qt 6.12 的 setModelData 取**非 const** 的 QAbstractItemModel*（Qt5 及
    // 更早是 const）。照着记忆写 const 会在 override 校验上直接报错。
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override;
};

// 本表用的表格。只为把「用户正在编辑某格」这件事问出来而存在。
//
// Qt 6.12 把 QAbstractItemView::state() 收成了 protected（它返回的
// State 枚举同样是 protected），同时 editorsAboutToBeShown /
// editorDestroyed 也不再是公开信号 —— 公开 API 里没有任何查询入口，
// 而自动滚动必须先回答这个问题：在用户正打字时把视图滚走，
// 手里的编辑器就没影了。
//
// 顺带一个负面结论：焦点代理法（viewport()->isAncestorOf(focusWidget())）
// 在 offscreen 下**测不出来** —— 窗口从不激活，focusWidget() 恒为 null，
// 拿它当守卫等于没测。state() 没有这个依赖。
//
// 实测见 build/probe_highlight：编辑中读出 3，正是 EditingState
// （枚举顺序 NoState/DraggingState/DragSelectingState/EditingState/...）。
class MultiSendTableWidget : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;

    // 语义化出口：子类内部用 protected 的 state()，外部只看这个。
    bool isEditingCell() const { return state() == QAbstractItemView::EditingState; }
};

// 多字符串发送表（需求 FR-04）。循环开关是全局的，不在每行上（规格 §3.9）。
class MultiSendTable : public QGroupBox
{
    Q_OBJECT

public:
    explicit MultiSendTable(QWidget* parent = nullptr);

    QList<SendItem> items() const;
    void setItems(const QList<SendItem>& items);

    bool loopEnabled() const;
    int  loopIntervalMs() const;      // 「全部间隔」输入框当前值，不参与发送节奏
    void stopLoop();
    void applyIntervalToAll();        // 把「全部间隔」一键写进每一行

    // 串口关闭时调用，禁掉循环开关与每行的「发送」按钮。
    void setSendEnabled(bool on);

    // 发送编码，与 SendPanel / SendQueueManager 共用，由 MainWindow 同步
    void setTextEncoding(TextEncoding enc);

    // 标出「当前正在发送」的那一行；传 -1 清除。
    //
    // 循环发送时由 MainWindow 连 SendQueueManager::rowSent 驱动，停机时
    // 连 runningChanged(false) 清；单发时本类自己调，随后自动淡出。
    // 越界下标按"没有当前行"处理，不画到半路。
    void setCurrentRow(int row);

signals:
    void sendRequested(const QByteArray& data);
    void loopToggled(bool on);
    void loopIntervalChanged(int ms);
    // 表里的条目被用户改动了（内容/注释/使能/HEX/间隔任一），或点了「应用到全部」。
    // MainWindow 据此把 SendQueueManager 里的快照换成新的 —— 否则循环会一直按
    // 「开循环那一刻」的值发，表格上显示一个间隔、实际发出去是另一个。
    //
    // 这是**去抖之后**的单发信号，不是每次单元格变化都发一次：
    // 间隔列的 delegate 一次提交就写 UserRole + DisplayRole 两格，
    // 内容列每敲一个字符也都会触发，直接连过去等于每敲一下就重建一次队列。
    void itemsEdited();

private slots:
    void onLoopToggled(bool on);
    void onIntervalChanged(int ms);
    void onEditDebounced();
    void onFlashTimeout();

private:
    void sendRow(int row);
    void scheduleItemsEdited();
    void paintRow(int row, bool active);
    void updateStatusText(int row);
    // 单发反馈：高亮一下再自己淡出。循环正在跑时不走这条路 ——
    // 循环已经占着高亮，手动插一脚会让指示在长达数秒的间隔里指着错的行。
    void flashRow(int row);

    MultiSendTableWidget* m_table   = nullptr;
    ContentDelegate*  m_delegate     = nullptr;
    IntervalDelegate* m_intervalDel  = nullptr;
    QCheckBox*        m_loopCheck    = nullptr;
    QSpinBox*         m_intervalSpin = nullptr;
    QPushButton*      m_applyAllBtn  = nullptr;
    // 「当前 07/99 · 注释 · 间隔 5000 ms」。颜色告诉你哪一行，
    // 这行字告诉你那行到底是什么 —— 99 行的表里「第 7 行」本身信息量很低。
    QLabel*           m_statusLabel  = nullptr;
    // 单发时的去抖定时器（只做一次性延迟，不是去抖）。常驻，构造时建好。
    QTimer*           m_editDebounce = nullptr;
    QTimer*           m_flashTimer   = nullptr;
    TextEncoding      m_encoding     = TextEncoding::Utf8;
    // 串口是否已打开。按钮的 enabled 只是给人看的，sendRow() 里还要靠它兜底。
    bool              m_sendEnabled  = false;
    // 当前高亮行与「正在淡出的是哪一行」。两者分开才能在单发淡出时
    // 不擦掉循环刚挪过去的指示。
    int               m_currentRow   = -1;
    int               m_flashRow     = -1;
};