#include "ui/MultiSendTable.h"
#include "ui/OutgoingAccent.h"
#include "ui/Theme.h"

// kSendItemCount 定义在这里（MultiSendTable.h 只 include models.h，
// 是刻意不让 UI 层头文件把 AppConfig 拖进来）。
#include "core/AppConfigTypes.h"
// 只有下面那一组"改表 → 队列跟着换"的用例需要它。其余用例不碰这个头，
// 目的就是让"这个测试目标只测 MultiSendTable 的接线"这件事保持可读。
#include "core/SendQueueManager.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>

// 这一层此前完全没有测试，而 2026-10-15 的评审恰好在这里挖出一个已发布的缺陷：
// 间隔列的 QTableWidgetItem 少了 Qt::ItemIsEditable，导致 QAbstractItemView::edit()
// 在向 delegate 要编辑器**之前**就返回了 —— IntervalDelegate 那 40 行从未被调用过一次。
// 逐条间隔因此在界面上根本设不了，唯一入口只剩「应用到全部」（把 99 行设成同一个值）。
//
// 为什么单测没抓到：tst_sendqueuemanager 把逐条间隔测得极细且全绿，但它测的是
// SendQueueManager（纯逻辑），从没碰过 MultiSendTable 里那 40 行 delegate。
// **core 层全绿盖不住 UI 层的接线错误**，所以这个文件专门测接线。
//
// 判据沿用那个教训：每条断言都必须经过出问题的那段代码。
class TestMultiSendTable : public QObject
{
    Q_OBJECT

private slots:
    // ---- 回归：间隔列必须真的能打开编辑器 ----
    void interval_column_opens_the_spinbox_editor();
    void interval_bounds_come_from_the_shared_constants();

    // ---- UserRole 是真值来源，DisplayRole 只是显示 ----
    void interval_editor_loads_user_role_not_display_text();
    void interval_commit_writes_both_roles();
    void interval_commit_is_clamped_to_the_legal_range();
    void cancelled_edit_keeps_the_old_interval();
    void items_reports_the_edited_interval();

    // ---- 同一张表上的另一根接线：内容列回车即发（FR-04-6）----
    void content_enter_sends_that_row();

    // ---- 串口没开时不允许发送 ----
    void send_is_blocked_while_disabled();
    void set_send_enabled_toggles_the_row_buttons();

    // ---- 改表要发出 itemsEdited，MainWindow 据此换队列快照 ----
    // 评审发现的缺陷：循环跑着的时候改表，表格上显示一个间隔、
    // 队列实际按开循环那一刻的旧值发，全程没有任何迹象说明它没跟上。
    void content_edit_emits_itemsEdited_after_the_debounce();
    void interval_editor_commit_emits_itemsEdited();
    void setItems_does_not_emit_itemsEdited();
    void apply_interval_to_all_emits_itemsEdited();
    void rapid_edits_collapse_into_a_single_signal();
    void editing_the_table_changes_what_the_queue_sends();

    // ---- 标出「当前正在发送」的那一行 ----
    void setCurrentRow_paints_the_whole_row();
    void setCurrentRow_marks_the_index_and_name_cells_only();
    void setCurrentRow_clears_the_row_it_replaces();
    void setCurrentRow_rejects_a_row_outside_the_table();
    // 回归钉：染色绝不能被当成"用户改了表"。QTableWidgetItem 的
    // setBackground/setForeground 实测每次都发一次 itemChanged，而本表的
    // itemChanged 接着就是 300ms 去抖 -> itemsEdited -> MainWindow 换队列快照。
    // 少了 QSignalBlocker，循环每发一条就白白重建一次 99 项快照。
    void highlighting_does_not_emit_itemsEdited();
    void setCurrentRow_scrolls_a_row_that_is_out_of_sight();
    void setCurrentRow_does_not_scroll_while_a_cell_is_being_edited();
    void status_text_names_the_row_and_its_interval();
    void status_text_is_cleared_together_with_the_highlight();
    void single_send_highlights_that_row_then_fades();
    // 竞态：单发的余晖到期时，循环可能已经把指示挪到别的行了。
    // 无差别清会把正在发送的指示一起擦掉。
    void fading_single_send_does_not_erase_a_newer_highlight();
    // 换主题后高亮行必须重画。底色是 setCurrentRow 那一刻写进 item 的，
    // 调色板换了不会回头重取 —— 实测切深色后仍是一条浅米色的带。
    void palette_change_repaints_the_highlighted_row();
    void palette_change_without_a_highlight_does_nothing();

private:
    static QTableWidget* tableOf(MultiSendTable& m);
    static QSpinBox* openIntervalEditor(QTableWidget* t, int row);
    // 移开当前格是提交编辑器最省事也最接近真实的做法：
    // closeEditor() 是 protected，QTest::keyClick 在这里反而绕过了 delegate 的提交路径。
    static void commitEditor(QTableWidget* t, int row, int col);
    // 去抖窗口是 300ms（见 MultiSendTable.cpp 的 kEditDebounceMs）。
    // 等它的同时也给了本进程里其它定时器机会，所以调用前务必确认
    // 被测对象自己的定时器被推到了不会自己触发的间隔上。
    static void waitOutTheDebounce() { QTest::qWait(450); }
};

QTableWidget* TestMultiSendTable::tableOf(MultiSendTable& m)
{
    return m.findChild<QTableWidget*>();
}

QSpinBox* TestMultiSendTable::openIntervalEditor(QTableWidget* t, int row)
{
    static const int kIntervalCol = 4;   // Column 枚举在 MultiSendTable.cpp 的匿名 namespace 里，这里只能按序号
    const QModelIndex idx = t->model()->index(row, kIntervalCol);
    t->setCurrentIndex(idx);
    t->edit(idx);
    // delegate 建出来的编辑器挂在 viewport 上，不是 indexWidget
    return t->viewport()->findChild<QSpinBox*>();
}

void TestMultiSendTable::commitEditor(QTableWidget* t, int row, int col)
{
    t->setCurrentIndex(t->model()->index(row, col + 1));
}

void TestMultiSendTable::interval_column_opens_the_spinbox_editor()
{
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    QVERIFY(t);
    QCOMPARE(t->rowCount(), kSendItemCount);

    static const int kIntervalCol = 4;
    for (int row = 0; row < kSendItemCount; row += 17) {
        QVERIFY2(t->item(row, kIntervalCol)->flags() & Qt::ItemIsEditable,
                 qPrintable(QStringLiteral("第 %1 行缺 ItemIsEditable，编辑器根本不会弹").arg(row)));

        QSpinBox* spin = openIntervalEditor(t, row);
        QVERIFY2(spin, qPrintable(QStringLiteral("第 %1 行弹不出 QSpinBox 编辑器").arg(row)));

        // 按 Esc 关掉：走的是 QStyledItemDelegate 自己的 revertAndCloseEditor 路径，
        // 与用户实际按 Esc 一致。closePersistentEditor() 是标记"常驻"用的，不负责关闭。
        QTest::keyClick(spin, Qt::Key_Escape);
    }
}

void TestMultiSendTable::interval_bounds_come_from_the_shared_constants()
{
    // 界面显示的区间、ConfigManager 夹取的区间、SendQueueManager 交给 QTimer 的区间
    // 必须来自同一组常量。任一处单独写死数字，用户就会遇到
    // 「界面显示一个值、实际发出去另一个值」。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    QSpinBox* spin = openIntervalEditor(t, 0);
    QVERIFY(spin);

    QCOMPARE(spin->minimum(), kMinItemIntervalMs);
    QCOMPARE(spin->maximum(), kMaxItemIntervalMs);
    QCOMPARE(kMinItemIntervalMs, 1);
    QCOMPARE(kMaxItemIntervalMs, 600000);
}

void TestMultiSendTable::interval_editor_loads_user_role_not_display_text()
{
    // 故意让两个 role 不一致：UserRole 才是真值。
    // 若 setEditorData 读了 DisplayRole，用户一进去看到的就是错的初值。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kIntervalCol = 4;
    const int row = 11;
    t->item(row, kIntervalCol)->setData(Qt::UserRole, 3333);
    t->item(row, kIntervalCol)->setText(QStringLiteral("9999"));

    QSpinBox* spin = openIntervalEditor(t, row);
    QVERIFY(spin);
    QCOMPARE(spin->value(), 3333);
}

void TestMultiSendTable::interval_commit_writes_both_roles()
{
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kIntervalCol = 4;
    const int row = 2;
    t->item(row, kIntervalCol)->setData(Qt::UserRole, 1000);
    t->item(row, kIntervalCol)->setText(QStringLiteral("1000"));

    QSpinBox* spin = openIntervalEditor(t, row);
    QVERIFY(spin);
    spin->setValue(2500);
    commitEditor(t, row, kIntervalCol);

    QCOMPARE(t->item(row, kIntervalCol)->data(Qt::UserRole).toInt(), 2500);
    QCOMPARE(t->item(row, kIntervalCol)->text(), QStringLiteral("2500"));
}

void TestMultiSendTable::interval_commit_is_clamped_to_the_legal_range()
{
    // setModelData 自己也 qBound 一次，不能只指望 QSpinBox 的 range。
    // 这里直接驱动 delegate，绕开控件自身的夹取。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kIntervalCol = 4;
    const int row = 5;

    auto* spin = new QSpinBox;
    spin->setRange(-999999, 999999);          // 故意放宽，模拟控件没夹住
    const QModelIndex idx = t->model()->index(row, kIntervalCol);
    qobject_cast<QAbstractItemDelegate*>(t->itemDelegateForColumn(kIntervalCol))
        ->setModelData(spin, t->model(), idx);

    QCOMPARE(t->item(row, kIntervalCol)->data(Qt::UserRole).toInt(), kMinItemIntervalMs);

    spin->setValue(999999);
    qobject_cast<QAbstractItemDelegate*>(t->itemDelegateForColumn(kIntervalCol))
        ->setModelData(spin, t->model(), idx);
    QCOMPARE(t->item(row, kIntervalCol)->data(Qt::UserRole).toInt(), kMaxItemIntervalMs);

    delete spin;
}

void TestMultiSendTable::cancelled_edit_keeps_the_old_interval()
{
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kIntervalCol = 4;
    const int row = 8;
    t->item(row, kIntervalCol)->setData(Qt::UserRole, 1000);
    t->item(row, kIntervalCol)->setText(QStringLiteral("1000"));

    QSpinBox* spin = openIntervalEditor(t, row);
    QVERIFY(spin);
    spin->setValue(9999);
    QTest::keyClick(spin, Qt::Key_Escape);   // 用户按 Esc：丢弃编辑、关闭编辑器

    QCOMPARE(t->item(row, kIntervalCol)->data(Qt::UserRole).toInt(), 1000);
    QCOMPARE(m.items().at(row).intervalMs, 1000);
}

void TestMultiSendTable::items_reports_the_edited_interval()
{
    // 上层（MainWindow / SendQueueManager）只认 items()，所以最终要落到这里。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kIntervalCol = 4;
    const int row = 20;

    QSpinBox* spin = openIntervalEditor(t, row);
    QVERIFY(spin);
    spin->setValue(12345);
    commitEditor(t, row, kIntervalCol);

    QCOMPARE(m.items().size(), kSendItemCount);
    QCOMPARE(m.items().at(row).intervalMs, 12345);
    // 别的行不能被这次编辑带偏
    QCOMPARE(m.items().at(row + 1).intervalMs, kDefaultItemIntervalMs);
}

void TestMultiSendTable::content_enter_sends_that_row()
{
    // 同一张表的另一根接线：内容列回车即发（FR-04-6）。
    // 走的是 ContentDelegate::eventFilter 这条路径，与上面的 IntervalDelegate 无关，
    // 两者是彼此独立的失败点，所以都要钉住。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kContentCol = 5;
    const int row = 7;

    // 串口没开时不该发得出去 —— MainWindow 在 opened/closed 里调它
    m.setSendEnabled(true);
    QSignalSpy spy(&m, &MultiSendTable::sendRequested);

    const QModelIndex idx = t->model()->index(row, kContentCol);
    t->setCurrentIndex(idx);
    t->edit(idx);

    // 默认 delegate 在 Qt 6 用 QExpandingLineEdit，它是 QLineEdit 的子类
    QLineEdit* editor = t->viewport()->findChild<QLineEdit*>();
    QVERIFY(editor);
    editor->setText(QStringLiteral("WORLD"));

    QTest::keyClick(editor, Qt::Key_Return);

    // ContentDelegate 刻意用 Qt::QueuedConnection 发 enterAccepted：
    // 在编辑器的事件处理里直接发信号会重入表格。所以这里必须让事件循环转起来，
    // 用 QTRY_COMPARE 而不是 QCOMPARE —— 后者在同步测试里根本等不到那次排队调用。
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toByteArray(), QByteArray("WORLD"));
}

void TestMultiSendTable::send_is_blocked_while_disabled()
{
    // 评审发现的缺陷：串口没开时发送被静默丢弃，而 MainWindow 那边的
    // 发送计数与「→ 发」回显已经做完了 —— 用户看到"发成功了"。
    // 防线有两层：按钮禁用（给人看）与这里的 sendEnabled 兜底
    // （按钮的 enabled 与串口状态隔着一次队列连接，拔线那一瞬仍点得动）。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kContentCol = 5;
    const int row = 9;

    m.setSendEnabled(false);
    t->item(row, kContentCol)->setText(QStringLiteral("PAYLOAD"));

    QSignalSpy spy(&m, &MultiSendTable::sendRequested);

    const QModelIndex idx = t->model()->index(row, kContentCol);
    t->setCurrentIndex(idx);
    t->edit(idx);
    QLineEdit* editor = t->viewport()->findChild<QLineEdit*>();
    QVERIFY(editor);
    editor->setText(QStringLiteral("PAYLOAD"));
    QTest::keyClick(editor, Qt::Key_Return);

    // 必须"先等再断言"，不能用 QTRY_COMPARE(spy.count(), 0)：后者在第一次检查时
    // 当前值就是 0，立刻通过 —— 那次 QueuedConnection 的信号根本还没投递，
    // 是一条空断言，删掉 sendRow() 里的守卫它照样绿（实测踩过）。
    QTest::qWait(250);
    QCOMPARE(spy.count(), 0);
}

void TestMultiSendTable::set_send_enabled_toggles_the_row_buttons()
{
    // 99 行的「发送」按钮都是 setCellWidget 挂进去的，setSendEnabled 要逐个改。
    // 漏掉任何一行都会留下"这一行还能在串口没开时发出去"的口子。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kSendCol = 6;

    m.setSendEnabled(false);
    for (int row = 0; row < t->rowCount(); ++row) {
        auto* btn = qobject_cast<QPushButton*>(t->cellWidget(row, kSendCol));
        QVERIFY2(btn, qPrintable(QStringLiteral("第 %1 行没有发送按钮").arg(row)));
        QVERIFY2(!btn->isEnabled(), qPrintable(QStringLiteral("第 %1 行禁用后仍可点").arg(row)));
    }

    m.setSendEnabled(true);
    for (int row = 0; row < t->rowCount(); ++row) {
        auto* btn = qobject_cast<QPushButton*>(t->cellWidget(row, kSendCol));
        QVERIFY(btn->isEnabled());
    }
}

// ---- 改表 → itemsEdited → 队列快照 ----

void TestMultiSendTable::content_edit_emits_itemsEdited_after_the_debounce()
{
    // 「先断后连」：编辑发生的那一刻必须**还没有**信号。
    // 这条是去抖存在的理由 —— 没有它的话 interval delegate 一次提交写两格、
    // 内容列每敲一个字符都触发，直接连过去等于每敲一下换一次队列快照。
    // 只在末尾断言"最终收到 1 个"的话，去掉去抖（改成立即发）一样全绿。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kContentCol = 5;
    const int row = 3;

    QSignalSpy spy(&m, &MultiSendTable::itemsEdited);

    t->item(row, kContentCol)->setText(QStringLiteral("AT"));
    QCOMPARE(spy.count(), 0);

    t->item(row, kContentCol)->setText(QStringLiteral("ATS"));
    QCOMPARE(spy.count(), 0);

    waitOutTheDebounce();
    QCOMPARE(spy.count(), 1);
    // 内容确实改了，不是空事件
    QCOMPARE(m.items().at(row).content, QStringLiteral("ATS"));
}

void TestMultiSendTable::interval_editor_commit_emits_itemsEdited()
{
    // 走的是真实用户路径：打开编辑器 -> 改值 -> 移开提交。
    // setModelData 里连写 UserRole 与 DisplayRole，所以这里一次提交会发两次
    // itemChanged —— 去抖必须把它们并成一个信号。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kIntervalCol = 4;
    const int row = 6;

    QSignalSpy spy(&m, &MultiSendTable::itemsEdited);

    QSpinBox* spin = openIntervalEditor(t, row);
    QVERIFY(spin);
    spin->setValue(2500);
    commitEditor(t, row, kIntervalCol);

    QCOMPARE(spy.count(), 0);      // 还没到去抖窗口
    waitOutTheDebounce();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m.items().at(row).intervalMs, 2500);
}

void TestMultiSendTable::setItems_does_not_emit_itemsEdited()
{
    // 填表不是"用户改了表"。配置加载时一次写 99 行 × 5 格，
    // 不挡 itemChanged 的话启动后会凭空冒一个 itemsEdited 出去。
    MultiSendTable m;

    QList<SendItem> items;
    for (int i = 0; i < kSendItemCount; ++i) {
        SendItem it;
        it.enabled = (i % 2 == 0);
        it.content = QStringLiteral("ROW%1").arg(i);
        it.intervalMs = 100 + i;
        items.append(it);
    }

    QSignalSpy spy(&m, &MultiSendTable::itemsEdited);
    m.setItems(items);
    QCOMPARE(spy.count(), 0);

    waitOutTheDebounce();
    QCOMPARE(spy.count(), 0);

    // 反面确认：setItems 确实把内容写进去了，"没发信号"不是因为它什么都没做
    QCOMPARE(m.items().at(4).enabled, true);
    QCOMPARE(m.items().at(5).enabled, false);
    QCOMPARE(m.items().at(0).content, QStringLiteral("ROW0"));
    QCOMPARE(m.items().at(0).intervalMs, 100);
}

void TestMultiSendTable::apply_interval_to_all_emits_itemsEdited()
{
    // 「应用到全部」改了 99 行的间隔，必须通知出去。
    // 它是整条路径上唯一一次改动量超过 99×2 格的批量操作，
    // 漏掉的话用户点完按钮，循环还在按 99 个旧间隔发。
    MultiSendTable m;

    // 找到「全部间隔」输入框：它是 QGroupBox 的直接子控件。
    // 必须按父对象区分 —— 表格的间隔列虽然也是 QSpinBox，但那是
    // IntervalDelegate 在编辑时才现造的，挂在 viewport 上且此处还没造。
    QSpinBox* globalSpin = nullptr;
    for (QSpinBox* s : m.findChildren<QSpinBox*>()) {
        if (s->parent() == &m) {
            globalSpin = s;
            break;
        }
    }
    QVERIFY(globalSpin);
    QCOMPARE(globalSpin->value(), kDefaultItemIntervalMs);

    QSignalSpy spy(&m, &MultiSendTable::itemsEdited);

    globalSpin->setValue(1234);
    QCOMPARE(spy.count(), 0);
    m.applyIntervalToAll();

    QCOMPARE(spy.count(), 0);
    waitOutTheDebounce();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m.items().at(0).intervalMs, 1234);
    QCOMPARE(m.items().at(kSendItemCount - 1).intervalMs, 1234);
}

void TestMultiSendTable::rapid_edits_collapse_into_a_single_signal()
{
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kEnabledCol = 2;
    static const int kContentCol = 5;

    QSignalSpy spy(&m, &MultiSendTable::itemsEdited);

    // 模拟用户连着敲一串内容 + 反复勾选
    for (int i = 0; i < 10; ++i)
        t->item(0, kContentCol)->setText(QStringLiteral("SEQ%1").arg(i));
    for (int i = 0; i < 5; ++i)
        t->item(0, kEnabledCol)->setCheckState(i % 2 ? Qt::Checked : Qt::Unchecked);

    waitOutTheDebounce();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m.items().at(0).content, QStringLiteral("SEQ9"));
    // 勾了 5 次，最后一次 i=4 是偶数 -> 未勾选。写这个断言时先算一遍，
    // 别照着循环体想当然写 true。
    QCOMPARE(m.items().at(0).enabled, false);
}

void TestMultiSendTable::editing_the_table_changes_what_the_queue_sends()
{
    // 这一条是上面那一整组缺陷的**用户可见症状**，也是唯一一条同时
    // 穿过 MultiSendTable 和 SendQueueManager 两个类的用例。
    // 按 MainWindow 里的原样连线（含 loopToggled 那条 setItems）搭起来，
    // 验证"改表 → 队列换快照 → 下一条按新的发"。
    //
    // 刻意**不**调 queue.start()：它会 m_timer->start(0)，而下面为了等去抖
    // 必须转事件循环，那个 0ms 定时器会在等待期间自己触发、抢先发出一条，
    // 把 spy 的计数搅乱。手动 invokeMethod(onTick) 才是不依赖真实计时的做法。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kEnabledCol = 2;
    static const int kContentCol = 5;
    static const int kIntervalCol = 4;

    const int rowA = 0;
    const int rowB = 1;
    t->item(rowA, kEnabledCol)->setCheckState(Qt::Checked);
    t->item(rowB, kEnabledCol)->setCheckState(Qt::Checked);
    t->item(rowA, kContentCol)->setText(QStringLiteral("KEEP"));
    t->item(rowB, kContentCol)->setText(QStringLiteral("OLD"));
    // 把真正会自己触发的定时器推到 10 分钟，测试期间它绝不会自己响。
    // 这一条是上面那个"不调 start()"的前提条件，缺了它用例本身就是假的。
    t->item(rowA, kIntervalCol)->setData(Qt::UserRole, kMaxItemIntervalMs);
    t->item(rowA, kIntervalCol)->setText(QString::number(kMaxItemIntervalMs));

    SendQueueManager queue;
    // 与 MainWindow::wireSignals 里完全相同的两条连线
    QObject::connect(&m, &MultiSendTable::loopToggled, &queue, [&](bool on) {
        queue.setItems(m.items());
        queue.setLoopEnabled(on);
        if (on)
            queue.start();
        else
            queue.stop();
    });
    QObject::connect(&m, &MultiSendTable::itemsEdited, &queue, [&] {
        queue.updateItems(m.items());
    });

    queue.setItems(m.items());
    queue.setLoopEnabled(true);

    QSignalSpy sendSpy(&queue, &SendQueueManager::sendRequested);
    QSignalSpy editSpy(&m, &MultiSendTable::itemsEdited);

    auto tick = [&] {
        QVERIFY(QMetaObject::invokeMethod(&queue, "onTick", Qt::DirectConnection));
    };

    tick();
    QCOMPARE(sendSpy.count(), 1);
    QCOMPARE(sendSpy.at(0).at(0).toByteArray(), QByteArray("KEEP"));
    QCOMPARE(queue.pendingIntervalMs(), kMaxItemIntervalMs);

    // 用户改了第 2 行的内容
    t->item(rowB, kContentCol)->setText(QStringLiteral("NEW"));
    waitOutTheDebounce();
    QCOMPARE(editSpy.count(), 1);
    QCOMPARE(sendSpy.count(), 1);        // 等待期间没有多发 —— 定时器确实被推远了

    tick();
    QCOMPARE(sendSpy.count(), 2);
    // 队列跟上了：发的是改过的内容。
    // 若 updateItems 退化成 setItems（m_index 清零），这里会是 "KEEP"。
    QCOMPARE(sendSpy.at(1).at(0).toByteArray(), QByteArray("NEW"));
}

// ---- 标出「当前正在发送」的那一行 ----

void TestMultiSendTable::setCurrentRow_paints_the_whole_row()
{
    // 判据来自 SSCOM 的做法为什么不够用：它只染「注释」列的字体，
    // 注释为空时那一格没有字可染，界面上等于什么都没发生。
    // 所以底色必须铺满整行，且不挑有没有字。
    //
    // 7 列全数：第 7 列（发送）挂的是 setCellWidget 的按钮，**另外还留了一个
    // 空 item**。那个空 item 就是为了底色带不在这里断掉 —— 截图实测过，
    // 少它的话橙色带止于内容列边缘，右侧留一截白，恰好断在
    // "哪一条正在发"的视觉末尾。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kCols = 7;

    m.setCurrentRow(4);

    for (int col = 0; col < kCols; ++col) {
        const QBrush bg = t->item(4, col)->background();
        QCOMPARE(bg.style(), Qt::SolidPattern);
        QVERIFY(bg.color().isValid());
    }
    QVERIFY(t->cellWidget(4, 6));   // 按钮还在，空 item 是加上去的，不是替代
}

void TestMultiSendTable::setCurrentRow_marks_the_index_and_name_cells_only()
{
    // 序号与注释是"这条是谁"的两条标识。其余列（使能/HEX/间隔/内容）保持原样，
    // 否则用户分不清"这是状态"还是"这是当前发送"。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kIndexCol = 0, kNameCol = 1, kIntervalCol = 4;

    m.setCurrentRow(4);

    for (int col : { kIndexCol, kNameCol }) {
        QVERIFY(t->item(4, col)->font().bold());
        QCOMPARE(t->item(4, col)->foreground().style(), Qt::SolidPattern);
    }
    QVERIFY(!t->item(4, kIntervalCol)->font().bold());
    QCOMPARE(t->item(4, kIntervalCol)->foreground().style(), Qt::NoBrush);
}

void TestMultiSendTable::setCurrentRow_clears_the_row_it_replaces()
{
    MultiSendTable m;
    QTableWidget* t = tableOf(m);

    m.setCurrentRow(4);
    m.setCurrentRow(9);

    QCOMPARE(t->item(4, 0)->background().style(), Qt::NoBrush);
    QCOMPARE(t->item(4, 0)->foreground().style(), Qt::NoBrush);
    QVERIFY(!t->item(4, 0)->font().bold());
    QCOMPARE(t->item(9, 0)->background().style(), Qt::SolidPattern);
}

void TestMultiSendTable::setCurrentRow_rejects_a_row_outside_the_table()
{
    MultiSendTable m;
    QTableWidget* t = tableOf(m);

    m.setCurrentRow(4);
    m.setCurrentRow(9999);      // 越界按"没有当前行"处理，绝不能画到半路

    QCOMPARE(t->item(4, 0)->background().style(), Qt::NoBrush);
    m.setCurrentRow(-3);
    QCOMPARE(t->item(4, 0)->background().style(), Qt::NoBrush);
}

void TestMultiSendTable::highlighting_does_not_emit_itemsEdited()
{
    // 这条是整个高亮功能里最容易回归的一处。QTableWidgetItem 的
    // setBackground/setForeground/setFont 实测每次各发一次 itemChanged
    // （探针见 build/probe_highlight），而本表 itemChanged 后面接着的是
    // 300ms 去抖 -> itemsEdited -> MainWindow 换队列快照。
    // 少一层 QSignalBlocker，循环每发一条就重建一次 99 项快照 ——
    // 功能上"看不出坏了"，代价是无休止的无用功，而且用户改表与
    // 循环指示会互相踩到。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    QSignalSpy spy(&m, &MultiSendTable::itemsEdited);

    for (int row = 0; row < 6; ++row)
        m.setCurrentRow(row);
    m.setCurrentRow(-1);
    waitOutTheDebounce();

    QCOMPARE(spy.count(), 0);
    Q_UNUSED(t);
}

void TestMultiSendTable::setCurrentRow_scrolls_a_row_that_is_out_of_sight()
{
    // 99 行的表，视口只有十几行 —— 不滚动的话高亮大半时间在视野外。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    m.resize(900, 380);
    m.show();

    m.setCurrentRow(0);
    const int before = t->verticalScrollBar()->value();
    m.setCurrentRow(80);
    const int after = t->verticalScrollBar()->value();

    QVERIFY(after > before);
    const QRect r = t->visualItemRect(t->item(80, 0));
    QVERIFY(t->viewport()->rect().contains(r));
}

void TestMultiSendTable::setCurrentRow_does_not_scroll_while_a_cell_is_being_edited()
{
    // 用户正在第 2 行敲字时，循环滚到第 80 行会把编辑器带出视野。
    // 实测（build/probe_highlight）编辑中 scrollToItem 照滚不误，
    // 所以这个守卫是必需的，不是保险。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    m.resize(900, 380);
    m.show();

    static const int kIntervalCol = 4;
    const QModelIndex idx = t->model()->index(2, kIntervalCol);
    t->setCurrentIndex(idx);
    t->edit(idx);
    QVERIFY(t->viewport()->findChild<QSpinBox*>());

    const int before = t->verticalScrollBar()->value();
    m.setCurrentRow(80);
    QCOMPARE(t->verticalScrollBar()->value(), before);

    t->setCurrentCell(0, 0);   // 移开当前格以提交并关掉编辑器
}

void TestMultiSendTable::status_text_names_the_row_and_its_interval()
{
    // 99 行的表里「第 7 行」本身信息量很低，这行字要能直接回答
    // "发的是什么、多久一次"。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    QLabel* status = m.findChild<QLabel*>();
    QVERIFY(status);

    t->item(6, 1)->setText(QStringLiteral("读状态"));
    const int ms = 5000;
    t->item(6, 4)->setData(Qt::UserRole, ms);
    t->item(6, 4)->setText(QString::number(ms));

    m.setCurrentRow(6);

    const QString text = status->text();
    QVERIFY2(text.contains(QStringLiteral("07")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("读状态")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("5000")), qPrintable(text));
}

void TestMultiSendTable::status_text_is_cleared_together_with_the_highlight()
{
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    QLabel* status = m.findChild<QLabel*>();
    QVERIFY(status);

    m.setCurrentRow(3);
    QVERIFY(!status->text().isEmpty());
    m.setCurrentRow(-1);
    QVERIFY(status->text().isEmpty());
    Q_UNUSED(t);
}

void TestMultiSendTable::single_send_highlights_that_row_then_fades()
{
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kContentCol = 5, kSendCol = 6;
    m.setSendEnabled(true);
    t->item(2, kContentCol)->setText(QStringLiteral("AT+HELLO"));

    auto* btn = qobject_cast<QPushButton*>(t->cellWidget(2, kSendCol));
    QVERIFY(btn);
    btn->click();

    QCOMPARE(t->item(2, 0)->background().style(), Qt::SolidPattern);

    // kFlashMs 是 900ms；等过它就该自己淡出
    QTRY_VERIFY_WITH_TIMEOUT(t->item(2, 0)->background().style() == Qt::NoBrush, 3000);
}

void TestMultiSendTable::fading_single_send_does_not_erase_a_newer_highlight()
{
    // 竞态：单发余晖还有 900ms 时，循环已经把指示挪到别的行了。
    // 若无差别清，正在发送的那行会被擦成"没有当前行" ——
    // 指示指错比余晖多留一会儿糟得多。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kContentCol = 5, kSendCol = 6;
    m.setSendEnabled(true);
    t->item(2, kContentCol)->setText(QStringLiteral("AT+HELLO"));

    auto* btn = qobject_cast<QPushButton*>(t->cellWidget(2, kSendCol));
    QVERIFY(btn);
    btn->click();
    QCOMPARE(t->item(2, 0)->background().style(), Qt::SolidPattern);

    m.setCurrentRow(9);          // 循环走到第 10 行
    QTest::qWait(1400);          // 让单发的余晖到期

    QCOMPARE(t->item(9, 0)->background().style(), Qt::SolidPattern);
    QCOMPARE(t->item(2, 0)->background().style(), Qt::NoBrush);
}

void TestMultiSendTable::palette_change_repaints_the_highlighted_row()
{
    // 这条钉的是实测出来的破相：底色写进了 QTableWidgetItem，
    // 调色板换了不会回头重取，切到深色后高亮行仍是一条浅米色的带。
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kIndexCol = 0;

    applyTheme(Theme::Light);
    m.setCurrentRow(6);
    const QColor lightBg = t->item(6, kIndexCol)->background().color();

    applyTheme(Theme::Dark);
    const QColor darkBg = t->item(6, kIndexCol)->background().color();

    // 必须真的换掉了 —— 若只是 setPalette 而没重画，这里会是同一个颜色
    QVERIFY(lightBg != darkBg);
    QCOMPARE(darkBg, outgoingAccent().soft);
    QVERIFY(t->item(6, 1)->foreground().color() == outgoingAccent().text);
    QVERIFY(t->item(6, 1)->font().bold());   // 加粗也在重画范围内，别丢

    applyTheme(Theme::System);   // 别把全局状态留给后面的用例
}

void TestMultiSendTable::palette_change_without_a_highlight_does_nothing()
{
    MultiSendTable m;
    QTableWidget* t = tableOf(m);
    static const int kIndexCol = 0;

    applyTheme(Theme::Light);
    const QBrush before = t->item(6, kIndexCol)->background();

    applyTheme(Theme::Dark);

    // 没有当前行就不该有任何格子被染色
    QCOMPARE(t->item(6, kIndexCol)->background().style(), before.style());
    QCOMPARE(t->item(6, kIndexCol)->background().color(), before.color());

    applyTheme(Theme::System);
}

QTEST_MAIN(TestMultiSendTable)
#include "tst_multisendtable.moc"