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

QTEST_MAIN(TestSendQueueManager)
#include "tst_sendqueuemanager.moc"
