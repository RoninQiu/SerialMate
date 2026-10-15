#pragma once

#include "core/TextCodec.h"
#include "models.h"

#include <QAbstractItemModel>
#include <QGroupBox>
#include <QList>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>

class QCheckBox;
class QEvent;
class QPushButton;
class QSpinBox;
class QTableWidget;

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

signals:
    void sendRequested(const QByteArray& data);
    void loopToggled(bool on);
    void loopIntervalChanged(int ms);

private slots:
    void onLoopToggled(bool on);
    void onIntervalChanged(int ms);

private:
    void sendRow(int row);

    QTableWidget*     m_table        = nullptr;
    ContentDelegate*  m_delegate     = nullptr;
    IntervalDelegate* m_intervalDel  = nullptr;
    QCheckBox*        m_loopCheck    = nullptr;
    QSpinBox*         m_intervalSpin = nullptr;
    QPushButton*      m_applyAllBtn  = nullptr;
    TextEncoding      m_encoding     = TextEncoding::Utf8;
    // 串口是否已打开。按钮的 enabled 只是给人看的，sendRow() 里还要靠它兜底。
    bool              m_sendEnabled  = false;
};