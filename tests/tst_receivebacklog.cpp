#include "ui/ReceiveBacklog.h"

#include <QTest>

// 暂停期间的数据暂存。
//
// 评审发现的缺陷：ReceiveView::onFlush() 每 40ms 把数据从工作线程整块取走，
// 暂停时取完就丢 —— 数据永久消失，而 chunkReceived 照常发出，状态栏写着
// "接收: N 字节"、接收区却少了中间一大段，界面上完全看不出中间丢过。
//
// 这里测的是有界缓冲本身：先得保得住，再谈上限与记账。
class TestReceiveBacklog : public QObject
{
    Q_OBJECT

private slots:
    void append_keeps_everything_under_the_cap();
    void over_cap_drops_the_oldest_and_accounts_for_it();
    void dropped_bytes_are_never_counted_twice();
    void take_returns_and_resets();
    void clear_resets_both_buffers();
    void empty_append_is_a_no_op();
    void cap_never_goes_to_zero();
};

void TestReceiveBacklog::append_keeps_everything_under_the_cap()
{
    ReceiveBacklog b;
    b.setCap(100);
    b.append(QByteArray(60, 'a'));
    b.append(QByteArray(30, 'b'));      // 合计 90，仍在上限内

    QByteArray out;
    qint64 dropped = -1;
    b.take(&out, &dropped);
    QCOMPARE(out.size(), 90);
    QCOMPARE(dropped, 0);
    QCOMPARE(out, QByteArray(60, 'a') + QByteArray(30, 'b'));
}

void TestReceiveBacklog::over_cap_drops_the_oldest_and_accounts_for_it()
{
    ReceiveBacklog b;
    b.setCap(10);
    b.append(QByteArray(6, 'x'));
    b.append(QByteArray(6, 'y'));      // 合计 12 -> 丢最早 2 个 x

    QByteArray out;
    qint64 dropped = -1;
    b.take(&out, &dropped);
    QCOMPARE(out.size(), 10);
    QCOMPARE(dropped, 2);
    QCOMPARE(out, QByteArray(4, 'x') + QByteArray(6, 'y'));
}

void TestReceiveBacklog::dropped_bytes_are_never_counted_twice()
{
    // 累计丢弃量要跨多次 append 累加，且 take() 之后归零。
    // 写成"本次丢弃"而不是"累计"的话，恢复提示里的数字会越来越小。
    ReceiveBacklog b;
    b.setCap(10);
    b.append(QByteArray(15, 'a'));    // 15 -> 留最后 10 个 a，丢 5
    b.append(QByteArray(13, 'b'));    // 23 -> 留最后 10 个 b，再丢 13，累计 18

    QByteArray out;
    qint64 dropped = -1;
    b.take(&out, &dropped);
    QCOMPARE(dropped, 18);            // 丢掉的 10 个 a 在第二次追加时才真的丢掉
    QCOMPARE(out.size(), 10);
    QCOMPARE(out, QByteArray(10, 'b'));

    b.append(QByteArray(12, 'c'));    // 取空之后再丢 2
    b.take(&out, &dropped);
    QCOMPARE(dropped, 2);             // 不是 18+2
}

void TestReceiveBacklog::take_returns_and_resets()
{
    ReceiveBacklog b;
    b.setCap(100);
    b.append(QByteArray(10, 'z'));

    QByteArray out;
    qint64 dropped = -1;
    b.take(&out, &dropped);
    QCOMPARE(out.size(), 10);
    QCOMPARE(dropped, 0);

    QVERIFY(b.isEmpty());
    b.take(&out, &dropped);
    QCOMPARE(out.size(), 0);
    QCOMPARE(dropped, 0);
}

void TestReceiveBacklog::clear_resets_both_buffers()
{
    // 「清空接收区」要连暂停暂存一起清掉，否则恢复时又倒出一大段数据。
    ReceiveBacklog b;
    b.setCap(10);
    b.append(QByteArray(20, 'a'));    // 留下 10，丢弃 10
    b.clear();

    QVERIFY(b.isEmpty());
    QCOMPARE(b.size(), 0);
    QCOMPARE(b.dropped(), 0);

    QByteArray out;
    qint64 dropped = -1;
    b.take(&out, &dropped);
    QCOMPARE(out.size(), 0);
    QCOMPARE(dropped, 0);
}

void TestReceiveBacklog::empty_append_is_a_no_op()
{
    ReceiveBacklog b;
    b.setCap(10);
    b.append(QByteArray());
    QCOMPARE(b.size(), 0);
    QCOMPARE(b.dropped(), 0);
    QVERIFY(b.isEmpty());
}

void TestReceiveBacklog::cap_never_goes_to_zero()
{
    // 上限为 0 或负数时必须退回默认，不能照单全收：若真的设成 0，每次 append
    // 都会命中 "size > cap" 并把整批丢光，看起来像暂存彻底坏了。
    ReceiveBacklog b;
    b.setCap(0);
    b.append(QByteArray(1000, 'a'));
    QCOMPARE(b.size(), 1000);
    QCOMPARE(b.dropped(), 0);

    b.setCap(-5);
    b.append(QByteArray(1000, 'b'));
    QCOMPARE(b.size(), 2000);         // 容量仍是默认(1 MiB)，没把这批吃掉
    QCOMPARE(b.dropped(), 0);
}

QTEST_MAIN(TestReceiveBacklog)
#include "tst_receivebacklog.moc"
