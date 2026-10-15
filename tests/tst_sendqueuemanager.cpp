#include "core/SendQueueManager.h"

#include <QSignalSpy>
#include <QTest>

class TestSendQueueManager : public QObject
{
    Q_OBJECT

private slots:
    void start_fires_first_item_immediately();
    void per_item_interval_drives_next_delay();
    void per_item_interval_is_clamped();
    void start_stop_emits_runningChanged();
    void tick_sends_first_enabled_item();
    void tick_skips_disabled_items();
    void tick_uses_interval_of_the_item_just_sent();
    void tick_honours_hex_mode();
    void tick_skips_invalid_hex();
    void tick_stops_after_one_round_when_loop_off();
    void tick_wraps_when_loop_on();
    void tick_stops_when_all_disabled_and_loop_off();
    void tick_keeps_running_when_all_disabled_and_loop_on();

    // ---- updateItems：循环跑着的时候改表（2026-10-15 评审缺陷）----
    void updateItems_changes_what_gets_sent_next();
    void updateItems_does_not_restart_the_cycle();
    void updateItems_leaves_the_in_flight_interval_alone();
    void updateItems_tolerates_a_shorter_list();

    // ---- rowSent：让表格能标出「当前正在发送」的行 ----
    // 表格与队列的下标是 1:1 的（m_items 就是表格 99 行的原序快照），
    // 这组用例守的就是这个映射不被改坏。
    void rowSent_reports_the_table_row_of_every_send();
    void rowSent_skips_disabled_rows_without_repeating_a_number();
    void rowSent_is_not_emitted_for_an_unparsable_hex_row();
    void rowSent_stops_when_the_manager_stops();
    void rowSent_keeps_reporting_table_rows_after_updateItems();
    void rowSent_is_emitted_before_the_payload();

private:
    static QList<SendItem> makeItems(const QStringList& contents, bool hexMode);
    static void invokeTick(SendQueueManager* mgr);
};

QList<SendItem> TestSendQueueManager::makeItems(const QStringList& contents, bool hexMode)
{
    QList<SendItem> items;
    for (const QString& c : contents) {
        SendItem it;
        it.enabled = true;
        it.hexMode = hexMode;
        it.content = c;
        items.append(it);
    }
    return items;
}

void TestSendQueueManager::invokeTick(SendQueueManager* mgr)
{
    // 直接驱动定时回调而不依赖真实计时，使测试无时序抖动
    QVERIFY(QMetaObject::invokeMethod(mgr, "onTick", Qt::DirectConnection));
}

// ---- 逐条间隔（多字符串循环的核心行为）----

void TestSendQueueManager::start_fires_first_item_immediately()
{
    // 间隔的语义是「这条发完停多久」，所以它不该影响第一条何时发出去。
    // 用户勾上循环却要干等 10 秒才看到动作，体感就是卡死。
    SendQueueManager mgr;
    QList<SendItem> items = makeItems({QStringLiteral("A")}, false);
    items[0].intervalMs = 10000;
    mgr.setItems(items);

    mgr.start();
    QCOMPARE(mgr.pendingIntervalMs(), 0);
}

void TestSendQueueManager::per_item_interval_drives_next_delay()
{
// 方案 A：发完第 N 条，等**第 N 条自己**的间隔再发第 N+1 条。
    SendQueueManager mgr;
    QList<SendItem> items = makeItems({QStringLiteral("A"), QStringLiteral("B"),
                                      QStringLiteral("C")}, false);
    items[0].intervalMs = 1000;
    items[1].intervalMs = 10000;   // 校准类慢操作
    items[2].intervalMs = 500;     // 高频轮询指令
    mgr.setItems(items);
    mgr.setLoopEnabled(true);
    mgr.start();

    QSignalSpy spy(&mgr, &SendQueueManager::sendRequested);

    invokeTick(&mgr);   // 发 A -> 等 A 的 1000
    QCOMPARE(spy.count(), 1);
    QCOMPARE(mgr.pendingIntervalMs(), 1000);

    invokeTick(&mgr);   // 发 B -> 等 B 的 10000
    QCOMPARE(spy.count(), 2);
    QCOMPARE(mgr.pendingIntervalMs(), 10000);

    invokeTick(&mgr);   // 发 C -> 等 C 的 500
    QCOMPARE(spy.count(), 3);
    QCOMPARE(mgr.pendingIntervalMs(), 500);

    invokeTick(&mgr);   // 回绕到 A
    QCOMPARE(spy.count(), 4);
    QCOMPARE(mgr.pendingIntervalMs(), 1000);
}

void TestSendQueueManager::per_item_interval_is_clamped()
{
    // 配置被手改坏、或从 SSCOM 导入到越界值时，0 或负数会让 QTimer 变成忙等，
    // 在主线程上足以让界面卡死 —— 必须在夹取后再交给 QTimer。
    SendQueueManager mgr;
    QList<SendItem> items = makeItems({QStringLiteral("A"), QStringLiteral("B")}, false);
    items[0].intervalMs = 0;
    items[1].intervalMs = 999999999;
    mgr.setItems(items);
    mgr.setLoopEnabled(true);
    mgr.start();

    invokeTick(&mgr);
    QCOMPARE(mgr.pendingIntervalMs(), kMinItemIntervalMs);

    invokeTick(&mgr);
    QCOMPARE(mgr.pendingIntervalMs(), kMaxItemIntervalMs);
}

void TestSendQueueManager::tick_uses_interval_of_the_item_just_sent()
{
    // 钉住最容易写错的那一处：用的是**刚发出去的那条**的间隔，
    // 不是下一条的，也不是全局的。
    SendQueueManager mgr;
    QList<SendItem> items = makeItems({QStringLiteral("A"), QStringLiteral("B")}, false);
    items[0].intervalMs = 1111;
    items[1].intervalMs = 2222;
    mgr.setItems(items);
    mgr.setLoopEnabled(true);
    mgr.start();

    invokeTick(&mgr);
    QCOMPARE(mgr.pendingIntervalMs(), 1111);
}

void TestSendQueueManager::start_stop_emits_runningChanged()
{
    SendQueueManager mgr;
    // 间隔拉到 10 分钟上限，保证定时器不会在测试期间触发
    QList<SendItem> items = makeItems({QStringLiteral("A")}, false);
    items[0].intervalMs = kMaxItemIntervalMs;
    mgr.setItems(items);

    QSignalSpy spy(&mgr, &SendQueueManager::runningChanged);

    mgr.start();
    QVERIFY(mgr.isRunning());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    mgr.stop();
    QVERIFY(!mgr.isRunning());
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);

    // 重复调用不应重复发信号
    mgr.stop();
    QCOMPARE(spy.count(), 2);
}

void TestSendQueueManager::tick_sends_first_enabled_item()
{
    SendQueueManager mgr;
    mgr.setItems(makeItems({QStringLiteral("A"), QStringLiteral("B")}, false));

    QSignalSpy spy(&mgr, &SendQueueManager::sendRequested);
    invokeTick(&mgr);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toByteArray(), QByteArray("A"));
}

void TestSendQueueManager::tick_skips_disabled_items()
{
    QList<SendItem> items = makeItems({QStringLiteral("A"), QStringLiteral("B")}, false);
    items[0].enabled = false;

    SendQueueManager mgr;
    mgr.setItems(items);

    QSignalSpy spy(&mgr, &SendQueueManager::sendRequested);
    invokeTick(&mgr);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toByteArray(), QByteArray("B"));
}

void TestSendQueueManager::tick_honours_hex_mode()
{
    SendQueueManager mgr;
    mgr.setItems(makeItems({QStringLiteral("01 02 0D 0A")}, true));

    QSignalSpy spy(&mgr, &SendQueueManager::sendRequested);
    invokeTick(&mgr);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toByteArray(), QByteArray::fromHex("01020D0A"));
}

void TestSendQueueManager::tick_skips_invalid_hex()
{
    SendQueueManager mgr;
    mgr.setItems(makeItems({QStringLiteral("ZZ"), QStringLiteral("01")}, true));

    QSignalSpy spy(&mgr, &SendQueueManager::sendRequested);
    invokeTick(&mgr);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toByteArray(), QByteArray::fromHex("01"));
}

void TestSendQueueManager::tick_stops_after_one_round_when_loop_off()
{
    SendQueueManager mgr;
    mgr.setLoopEnabled(false);
    mgr.setItems(makeItems({QStringLiteral("A"), QStringLiteral("B")}, false));
    mgr.start();

    QSignalSpy sendSpy(&mgr, &SendQueueManager::sendRequested);
    invokeTick(&mgr);   // A
    QCOMPARE(sendSpy.count(), 1);
    invokeTick(&mgr);   // B
    QCOMPARE(sendSpy.count(), 2);
    invokeTick(&mgr);   // 越过末尾，循环关闭 -> 停止

    QVERIFY(!mgr.isRunning());
    QCOMPARE(sendSpy.count(), 2);
}

void TestSendQueueManager::tick_wraps_when_loop_on()
{
    SendQueueManager mgr;
    mgr.setLoopEnabled(true);
    mgr.setItems(makeItems({QStringLiteral("A"), QStringLiteral("B")}, false));
    mgr.start();

    QSignalSpy sendSpy(&mgr, &SendQueueManager::sendRequested);

    invokeTick(&mgr); QCOMPARE(sendSpy.count(), 1);
    QCOMPARE(sendSpy.at(0).at(0).toByteArray(), QByteArray("A"));
    invokeTick(&mgr); QCOMPARE(sendSpy.count(), 2);
    QCOMPARE(sendSpy.at(1).at(0).toByteArray(), QByteArray("B"));
    invokeTick(&mgr); QCOMPARE(sendSpy.count(), 3);   // 回绕
    QCOMPARE(sendSpy.at(2).at(0).toByteArray(), QByteArray("A"));

    QVERIFY(mgr.isRunning());
}

void TestSendQueueManager::tick_stops_when_all_disabled_and_loop_off()
{
    QList<SendItem> items = makeItems({QStringLiteral("A"), QStringLiteral("B")}, false);
    for (SendItem& it : items)
        it.enabled = false;

    SendQueueManager mgr;
    mgr.setItems(items);
    mgr.start();

    QSignalSpy sendSpy(&mgr, &SendQueueManager::sendRequested);
    invokeTick(&mgr);

    QVERIFY(!mgr.isRunning());
    QCOMPARE(sendSpy.count(), 0);
}

void TestSendQueueManager::tick_keeps_running_when_all_disabled_and_loop_on()
{
    QList<SendItem> items = makeItems({QStringLiteral("A"), QStringLiteral("B")}, false);
    for (SendItem& it : items)
        it.enabled = false;

    SendQueueManager mgr;
    mgr.setLoopEnabled(true);
    mgr.setItems(items);
    mgr.start();

    QSignalSpy sendSpy(&mgr, &SendQueueManager::sendRequested);
    invokeTick(&mgr);

    // 循环开启且整轮无可发送项时：既不能停机（用户可能稍后再勾选条目），
    // 也不能死循环 —— onTick 里的 scanned 计数器正是为此存在。
    // 缺了 scanned，这一条会挂死测试进程，是它唯一能暴露的场景。
    //
    // 另外定时器必须被重新拉起：节奏只在真正发出数据后才重启，
    // 这里不补的话，用户稍后再勾选条目就永远发不出去了。
    QVERIFY(mgr.isRunning());
    QCOMPARE(sendSpy.count(), 0);
    QCOMPARE(mgr.pendingIntervalMs(), kDefaultItemIntervalMs);
}

// ---- updateItems：循环跑着的时候改表 ----
//
// 评审发现的缺陷：MainWindow 只在 loopToggled 和 applyConfig 两处调 setItems()，
// 而 setItems() 会把 m_index 清零 —— 于是循环开着的时候，表格上显示的间隔
// 和队列实际用的间隔是两套值，而且没有任何迹象说明后者没跟上。
// 用户唯一的察觉方式是"循环怎么不听我的"。
//
// updateItems 是同一个换快照的动作，但刻意保留 m_index。下面的用例必须
// 同时钉住两件事：换了（跟着走）和没重置（不从头来）—— 只测前者的话，
// 把 updateItems 直接实现成 setItems 一样会全绿。

void TestSendQueueManager::updateItems_changes_what_gets_sent_next()
{
    SendQueueManager mgr;
    mgr.setLoopEnabled(true);
    mgr.setItems(makeItems({QStringLiteral("A"), QStringLiteral("B"),
                             QStringLiteral("C")}, false));
    mgr.start();

    QSignalSpy spy(&mgr, &SendQueueManager::sendRequested);
    invokeTick(&mgr);   // A，index -> 1
    invokeTick(&mgr);   // B，index -> 2
    QCOMPARE(spy.count(), 2);

    // 用户把第 3 行的内容改了
    QList<SendItem> edited = makeItems({QStringLiteral("A"), QStringLiteral("B"),
                                        QStringLiteral("C-EDITED")}, false);
    mgr.updateItems(edited);

    invokeTick(&mgr);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(2).at(0).toByteArray(), QByteArray("C-EDITED"));
}

void TestSendQueueManager::updateItems_does_not_restart_the_cycle()
{
    // 这条是 updateItems 与 setItems 的**唯一区别**，必须单独钉：
    // 若实现里顺手写了 m_index = 0，下面第 3 次 tick 发出来的会是 A 而不是 C。
    SendQueueManager mgr;
    mgr.setLoopEnabled(true);
    mgr.setItems(makeItems({QStringLiteral("A"), QStringLiteral("B"),
                             QStringLiteral("C")}, false));
    mgr.start();

    QSignalSpy spy(&mgr, &SendQueueManager::sendRequested);
    invokeTick(&mgr);   // A
    invokeTick(&mgr);   // B
    QCOMPARE(spy.count(), 2);

    // 改的是**已经发过**的那一行。若进度被清零，下一条又轮到 A。
    mgr.updateItems(makeItems({QStringLiteral("A-EDITED"), QStringLiteral("B"),
                                QStringLiteral("C")}, false));

    invokeTick(&mgr);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(2).at(0).toByteArray(), QByteArray("C"));
}

void TestSendQueueManager::updateItems_leaves_the_in_flight_interval_alone()
{
    // 改表不该改写"正在走的那一步"：QTimer 的等待在 emit sendRequested 时
    // 就定死了。这里同时钉住两侧 —— 改之前发出去的那次仍按旧值等，
    // 该行下次轮到它时才用新值。
    SendQueueManager mgr;
    mgr.setLoopEnabled(true);
    QList<SendItem> items = makeItems({QStringLiteral("A"), QStringLiteral("B")}, false);
    items[0].intervalMs = 1111;
    items[1].intervalMs = 2222;
    mgr.setItems(items);
    mgr.start();

    QSignalSpy spy(&mgr, &SendQueueManager::sendRequested);
    invokeTick(&mgr);                 // 发 A，按 A 当时的 1111 等
    QCOMPARE(mgr.pendingIntervalMs(), 1111);

    QList<SendItem> edited = items;
    edited[0].intervalMs = 3333;
    mgr.updateItems(edited);

    // 正在进行的那一次不被追溯改写
    QCOMPARE(mgr.pendingIntervalMs(), 1111);

    invokeTick(&mgr);                 // 发 B
    invokeTick(&mgr);                 // 回绕，再发 A —— 这次用新值
    QCOMPARE(spy.count(), 3);
    QCOMPARE(mgr.pendingIntervalMs(), 3333);
}

void TestSendQueueManager::updateItems_tolerates_a_shorter_list()
{
    // MultiSendTable::items() 恒返回 99 行，所以界面上碰不到列表变短；
    // 但 updateItems 是公开 API，越界后队列仍必须可用 —— 否则会读到
    // 列表范围外或者干脆停摆。
    //
    // 守的是**行为**而不是某一行实现：这里刻意不去钉 "updateItems 有没有
    // 夹 m_index"。夹了和不夹在这里表现完全一样（onTick 开头的
    // `m_index >= n` 本来就回绕），既然区分不了，钉它就是给一条假断言。
    SendQueueManager mgr;
    mgr.setLoopEnabled(true);
    mgr.setItems(makeItems({QStringLiteral("A"), QStringLiteral("B"),
                             QStringLiteral("C")}, false));
    mgr.start();

    QSignalSpy spy(&mgr, &SendQueueManager::sendRequested);
    invokeTick(&mgr);   // A
    invokeTick(&mgr);   // B，index = 2，已经越过了新列表的长度
    QCOMPARE(spy.count(), 2);

    mgr.updateItems(makeItems({QStringLiteral("ONLY")}, false));

    // 不崩、不越界、循环继续：新列表的第一条被发出
    invokeTick(&mgr);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(2).at(0).toByteArray(), QByteArray("ONLY"));
    QVERIFY(mgr.isRunning());
}

// ---- rowSent：把「正在发第几行」交出去 ----

void TestSendQueueManager::rowSent_reports_the_table_row_of_every_send()
{
    SendQueueManager mgr;
    mgr.setItems(makeItems({QStringLiteral("A"), QStringLiteral("B"),
                            QStringLiteral("C")}, false));
    QSignalSpy rowSpy(&mgr, &SendQueueManager::rowSent);
    QSignalSpy dataSpy(&mgr, &SendQueueManager::sendRequested);

    invokeTick(&mgr);
    invokeTick(&mgr);
    invokeTick(&mgr);

    QCOMPARE(rowSpy.count(), 3);
    QCOMPARE(rowSpy.at(0).at(0).toInt(), 0);
    QCOMPARE(rowSpy.at(1).at(0).toInt(), 1);
    QCOMPARE(rowSpy.at(2).at(0).toInt(), 2);
    // 每次发送都得配一个行号，否则表格会有一格不知道该高亮哪
    QCOMPARE(dataSpy.count(), rowSpy.count());
}

void TestSendQueueManager::rowSent_skips_disabled_rows_without_repeating_a_number()
{
    QList<SendItem> items = makeItems({QStringLiteral("A"), QStringLiteral("B"),
                                      QStringLiteral("C")}, false);
    items[1].enabled = false;

    SendQueueManager mgr;
    mgr.setItems(items);
    mgr.setLoopEnabled(true);
    QSignalSpy rowSpy(&mgr, &SendQueueManager::rowSent);

    for (int i = 0; i < 4; ++i)
        invokeTick(&mgr);

    QList<int> rows;
    for (int i = 0; i < rowSpy.count(); ++i)
        rows << rowSpy.at(i).at(0).toInt();
    // 第 1 行(0-based)被禁用，报告的行号必须是 0,2,0,2 —— 既不跳号也不重复
    QCOMPARE(rows, (QList<int>{ 0, 2, 0, 2 }));
}

void TestSendQueueManager::rowSent_is_not_emitted_for_an_unparsable_hex_row()
{
    QList<SendItem> items = makeItems({QStringLiteral("ZZ")}, true);
    SendQueueManager mgr;
    mgr.setItems(items);
    QSignalSpy rowSpy(&mgr, &SendQueueManager::rowSent);
    QSignalSpy dataSpy(&mgr, &SendQueueManager::sendRequested);

    invokeTick(&mgr);

    // 没发出去就不该报行号：否则表格会高亮一条根本没发送的指令
    QCOMPARE(dataSpy.count(), 0);
    QCOMPARE(rowSpy.count(), 0);
}

void TestSendQueueManager::rowSent_stops_when_the_manager_stops()
{
    SendQueueManager mgr;
    mgr.setItems(makeItems({QStringLiteral("A")}, false));
    QSignalSpy rowSpy(&mgr, &SendQueueManager::rowSent);

    // start() 只是把定时器设成 0ms，**不会同步发出第一条**；
    // 这里手动驱动一次，和其它用例保持同一套无时序抖动的写法。
    mgr.start();
    invokeTick(&mgr);
    QCOMPARE(rowSpy.count(), 1);

    mgr.stop();
    invokeTick(&mgr);
    QCOMPARE(rowSpy.count(), 1);
}

void TestSendQueueManager::rowSent_keeps_reporting_table_rows_after_updateItems()
{
    SendQueueManager mgr;
    mgr.setItems(makeItems({QStringLiteral("A"), QStringLiteral("B"),
                            QStringLiteral("C")}, false));
    QSignalSpy rowSpy(&mgr, &SendQueueManager::rowSent);

    invokeTick(&mgr);     // 行 0
    // 循环跑着改表：换了快照但不清进度，行号必须仍按新快照的表格行报
    mgr.updateItems(makeItems({QStringLiteral("X"), QStringLiteral("Y"),
                               QStringLiteral("Z")}, false));
    invokeTick(&mgr);     // 行 1
    invokeTick(&mgr);     // 行 2

    QCOMPARE(rowSpy.count(), 3);
    QCOMPARE(rowSpy.at(1).at(0).toInt(), 1);
    QCOMPARE(rowSpy.at(2).at(0).toInt(), 2);
}

void TestSendQueueManager::rowSent_is_emitted_before_the_payload()
{
    // 顺序不是审美问题：UI 要在数据真的写进串口之前就把高亮摆好，
    // 否则用户会先看见发送区动了一下、表格才跟上。
    SendQueueManager mgr;
    mgr.setItems(makeItems({QStringLiteral("A")}, false));

    QString order;
    QObject::connect(&mgr, &SendQueueManager::rowSent,
                     [&order] { order += QStringLiteral("R"); });
    QObject::connect(&mgr, &SendQueueManager::sendRequested,
                     [&order](const QByteArray&) { order += QStringLiteral("D"); });

    invokeTick(&mgr);
    QCOMPARE(order, QStringLiteral("RD"));
}

QTEST_MAIN(TestSendQueueManager)
#include "tst_sendqueuemanager.moc"