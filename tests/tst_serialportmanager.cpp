#include "core/SerialPortManager.h"

#include <QSignalSpy>
#include <QTest>

// 串口管理器里**不需要真实串口**的那部分契约。
//
// 2026-10-15 的独立评审发现：串口没打开时 writeData() 会把数据交给
// flushSendQueue()，那边发现端口没开就把队列清空、**不发任何信号**；
// 而调用方 MainWindow::sendToPort 在投递之前就已经加了发送计数、
// 把「→ 发」回显写进了接收区。用户看到"发送: N 字节"、接收区多了一行，
// 实际一个字节都没出去 —— 调试设备时这是最坏的一种撒谎。
//
// 这些用例不需要 QThread：writeData / takePending 直接调用即可，
// 它们只在工作线程内访问的成员上动手，单线程调用不改变被测逻辑。
class TestSerialPortManager : public QObject
{
    Q_OBJECT

private slots:
    void write_data_on_a_closed_port_reports_the_failure();
    void write_data_on_a_closed_port_emits_exactly_one_error();
    void empty_payload_is_silent_even_when_closed();
    void take_pending_starts_empty();
};

// 回归用例：串口没打开时写数据必须报错，而不是静默丢弃。
void TestSerialPortManager::write_data_on_a_closed_port_reports_the_failure()
{
    SerialPortManager mgr;   // 从未 openPort()，m_open 为 false
    QSignalSpy err(&mgr, &SerialPortManager::errorOccurred);

    const QByteArray data("AT+VERSION?");
    mgr.writeData(data);

    QCOMPARE(err.count(), 1);
    const QString msg = err.at(0).at(0).toString();
    QVERIFY2(msg.contains(QStringLiteral("未发送")),
             qPrintable(QStringLiteral("报错文案必须说清没发出去，实际是：%1").arg(msg)));
    // 字节数直接用 data.size()，不手数 —— 上一个版本在这里写死 12 而实际是 11
    QVERIFY2(msg.contains(QString::number(data.size())),
             qPrintable(QStringLiteral("报错应带上被丢弃的 %1 字节，实际是：%2")
                            .arg(data.size()).arg(msg)));
}

// 定时发送开着时每次 tick 都会撞上这条路径，报错必须只来一次，不能刷屏。
void TestSerialPortManager::write_data_on_a_closed_port_emits_exactly_one_error()
{
    SerialPortManager mgr;
    QSignalSpy err(&mgr, &SerialPortManager::errorOccurred);

    mgr.writeData(QByteArray("A"));
    QCOMPARE(err.count(), 1);
    mgr.writeData(QByteArray("B"));
    QCOMPARE(err.count(), 2);   // 每次调用一次，不叠加、不合并
    QCOMPARE(err.at(1).at(0).toString(), QStringLiteral("串口未打开，1 字节未发送"));
}

void TestSerialPortManager::empty_payload_is_silent_even_when_closed()
{
    SerialPortManager mgr;
    QSignalSpy err(&mgr, &SerialPortManager::errorOccurred);

    mgr.writeData(QByteArray());

    // 空数据不是"发送失败"，调用方 buildPayload() 本来就会滤掉空内容，
    // 不该为它打扰用户。
    QCOMPARE(err.count(), 0);
}

void TestSerialPortManager::take_pending_starts_empty()
{
    SerialPortManager mgr;
    QCOMPARE(mgr.takePending().size(), 0);
}

QTEST_MAIN(TestSerialPortManager)
#include "tst_serialportmanager.moc"
