#include "core/ConfigManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTest>

// ConfigManager 是唯一「自己往磁盘写东西」的 core 类，也是唯一没有测试的
// 纯逻辑类。它手里的三个坑都静默无报错，所以测试重点不是「能不能跑通」，
// 而是「存进去再读出来是不是同一个东西」以及「值被改坏了会回落到哪」。
//
// 隔离方式：ConfigManager 刻意把 ini 放在 exe 同目录（绿色软件判据），
// 本测试的 exe 就在 build/ 下，写到的正是 build/SerialMate.ini ——
// 而 build.bat 启动的 SerialMate.exe 用的也是这一个。所以 initTestCase 先把
// 原文件整个备份，cleanupTestCase 原样放回去：跑一次单测不该抹掉开发机
// 上刚摆好的窗口布局。没有为此给产品代码开测试后门。
class TestConfigManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    // ---- 默认值：机器上还没有配置文件时的形状 ----
    void missing_file_yields_documented_defaults();
    void truncated_array_pads_with_defaults();
    void garbage_file_yields_defaults_without_crashing();

    // ---- 往返：存进去再读出来必须是同一个东西 ----
    void roundtrip_preserves_every_scalar_field();
    void roundtrip_keeps_all_99_items_independent();
    void roundtrip_survives_ini_hostile_text();
    void blanking_items_clears_their_previous_content();

    // ---- 越界回落：配置被手改坏时不能静默变成别的东西 ----
    void parity_value_1_falls_back_to_none();
    void data_bits_are_validated_per_value();
    void stop_bits_are_validated_per_value();
    void flow_control_is_validated_per_value();
    void non_positive_baud_falls_back_to_115200();
    void empty_port_name_falls_back_to_com1();
    void non_positive_buffer_limit_falls_back_to_default();
    void out_of_range_intervals_fall_back();
    void out_of_range_item_interval_falls_back();
    void invalid_encoding_falls_back_to_utf8();

    // ---- 布局版本：老配置不能被当成新结构 ----
    void save_stamps_layout_version();
    void missing_layout_version_reads_as_zero();

    // ---- 主题：往返 + 坏值回落 ----
    void theme_round_trips_through_the_ini();
    void missing_theme_reads_as_system();
    // ini 是给人手改的。手改成看不懂的值时必须回落到 System ——
    // 那是加这一项之前的实际行为，比猜错强。
    void unrecognised_theme_value_reads_as_system();
    void theme_string_conversion_is_case_insensitive();

    // ---- 配置读坏了必须报出来 ----
    void corrupt_file_is_reported_by_load_checked();
    void healthy_file_reports_no_error();

    // ---- 绿色软件契约 ----
    void config_file_lives_next_to_the_exe();
    void save_checked_reports_success_on_a_writable_dir();
    void check_writable_succeeds_next_to_the_exe();

private:
    // 每条用例都从"磁盘上没有配置文件"开始，读出来的必须是纯默认值。
    static void writeRaw(const QString& key, const QVariant& value);
    static void writeItemField(int index, const QString& field, const QVariant& value);

    static AppConfig loadWith(const QString& key, const QVariant& value);
    static AppConfig loadWithItemInterval(int storedValue);

    // 每个字段都刻意偏离默认值，只要有一处没存住就会在这里露出来。
    static AppConfig populated();

    QByteArray m_originalConfig;
    bool       m_hadOriginal = false;
};

void TestConfigManager::initTestCase()
{
    QFile existing(ConfigManager::configFilePath());
    if (existing.open(QIODevice::ReadOnly)) {
        m_originalConfig = existing.readAll();
        m_hadOriginal = true;
    }
}

void TestConfigManager::cleanupTestCase()
{
    const QString path = ConfigManager::configFilePath();
    QFile::remove(path);
    if (!m_hadOriginal)
        return;
    QFile restore(path);
    if (restore.open(QIODevice::WriteOnly | QIODevice::Truncate))
        restore.write(m_originalConfig);
}

void TestConfigManager::init()
{
    QFile::remove(ConfigManager::configFilePath());
}

void TestConfigManager::writeRaw(const QString& key, const QVariant& value)
{
    QSettings s(ConfigManager::configFilePath(), QSettings::IniFormat);
    s.setValue(key, value);
    s.sync();
}

void TestConfigManager::writeItemField(int index, const QString& field, const QVariant& value)
{
    QSettings s(ConfigManager::configFilePath(), QSettings::IniFormat);
    // 必须连 size 一起写：beginReadArray 靠 size 决定返回几，
    // 只写 "sendItems/3\\intervalMs" 这种裸键的话 load() 会走
    // "一条都没存" 的分支，测的就不是字段本身而是默认值了。
    s.beginWriteArray(QStringLiteral("sendItems"), kSendItemCount);
    s.setArrayIndex(index);
    s.setValue(field, value);
    s.endArray();
    s.sync();
}

AppConfig TestConfigManager::loadWith(const QString& key, const QVariant& value)
{
    writeRaw(key, value);
    return ConfigManager::load();
}

AppConfig TestConfigManager::loadWithItemInterval(int storedValue)
{
    writeItemField(3, QStringLiteral("intervalMs"), storedValue);
    return ConfigManager::load();
}

AppConfig TestConfigManager::populated()
{
    AppConfig c;
    c.serial.portName    = QStringLiteral("COM17");
    c.serial.baudRate    = 57600;
    c.serial.dataBits    = QSerialPort::Data7;
    c.serial.parity      = QSerialPort::OddParity;
    c.serial.stopBits    = QSerialPort::TwoStop;
    c.serial.flowControl = QSerialPort::SoftwareControl;
    c.serial.dtr         = false;
    c.serial.rts         = true;

    // 每条都用下标派生的不同值 —— 全是同一个值的话，
    // 一旦读的时候没逐轮 setArrayIndex 也照样能过。
    c.sendItems.clear();
    for (int i = 0; i < kSendItemCount; ++i) {
        SendItem it;
        it.enabled    = (i % 3 == 0);
        it.hexMode    = (i % 2 == 0);
        it.name       = QStringLiteral("N%1").arg(i + 1);
        it.intervalMs = 100 + i * 37;
        it.content    = QStringLiteral("line-%1").arg(i);
        c.sendItems.append(it);
    }

    c.sendText       = QStringLiteral("AT+CMGS=\"hello\"\r\n");
    c.sendAppendCrlf = true;
    c.sendHexMode    = true;
    c.sendIntervalMs = 2500;
    c.sendEncoding   = TextEncoding::Latin1;
    c.loopEnabled    = true;
    c.loopIntervalMs = 777;

    c.recvHex         = true;
    c.recvTimestamp   = true;
    c.recvWrap        = false;   // 默认是 true，这里刻意反过来
    c.recvShowSent    = true;
    c.recvBufferLimit = 200000;
    c.recvEncoding    = TextEncoding::Local;
    c.recvFont        = QStringLiteral("Consolas,10,-1,5,400,0,0,0,0,0");

    c.geometry = QByteArray::fromHex("0102030405");
    c.state    = QByteArray::fromHex("aabbccdd");
    return c;
}

// ---- 默认值 ----

void TestConfigManager::missing_file_yields_documented_defaults()
{
    // init() 已经把 ini 删了 —— 这就是新机器第一次启动的形状。
    // 这些值同时是 README「配置存放位置」一节承诺给用户的形状。
    const AppConfig cfg = ConfigManager::load();

    QCOMPARE(cfg.serial.portName, QStringLiteral("COM1"));
    QCOMPARE(cfg.serial.baudRate, 115200);
    QCOMPARE(int(cfg.serial.dataBits), int(QSerialPort::Data8));
    QCOMPARE(int(cfg.serial.parity), int(QSerialPort::NoParity));
    QCOMPARE(int(cfg.serial.stopBits), int(QSerialPort::OneStop));
    QCOMPARE(int(cfg.serial.flowControl), int(QSerialPort::NoFlowControl));
    QVERIFY(cfg.serial.dtr);
    QVERIFY(cfg.serial.rts);

    QCOMPARE(cfg.sendItems.size(), kSendItemCount);
    for (const SendItem& it : cfg.sendItems) {
        QVERIFY(!it.enabled);
        QVERIFY(!it.hexMode);
        QVERIFY(it.name.isEmpty());
        QCOMPARE(it.intervalMs, kDefaultItemIntervalMs);
    }

    QVERIFY(cfg.sendText.isEmpty());
    QVERIFY(!cfg.sendAppendCrlf);
    QVERIFY(!cfg.sendHexMode);
    QCOMPARE(cfg.sendIntervalMs, 1000);
    QCOMPARE(cfg.sendEncoding, TextEncoding::Utf8);
    QVERIFY(!cfg.loopEnabled);
    QCOMPARE(cfg.loopIntervalMs, 100);

    QVERIFY(!cfg.recvHex);
    QVERIFY(!cfg.recvTimestamp);
    QVERIFY(cfg.recvWrap);          // 自动换行默认开
    QVERIFY(!cfg.recvShowSent);
    QCOMPARE(cfg.recvBufferLimit, kDefaultReceiveLimit);
    QCOMPARE(cfg.recvEncoding, TextEncoding::Utf8);
    QVERIFY(cfg.recvFont.isEmpty());

    QVERIFY(cfg.geometry.isEmpty());
    QVERIFY(cfg.state.isEmpty());
    QCOMPARE(cfg.layoutVersion, 0);
}

void TestConfigManager::truncated_array_pads_with_defaults()
{
    // 配置被手改成只有 3 条。读回来仍必须是 99 条 ——
    // MultiSendTable 按这个列表建行，短一截就是界面上凭空少 96 行。
    QSettings s(ConfigManager::configFilePath(), QSettings::IniFormat);
    s.beginWriteArray(QStringLiteral("sendItems"), 3);
    s.setArrayIndex(0);
    s.setValue(QStringLiteral("content"), QStringLiteral("第一条"));
    s.setArrayIndex(2);
    s.setValue(QStringLiteral("content"), QStringLiteral("第三条"));
    s.endArray();
    s.sync();

    const AppConfig cfg = ConfigManager::load();
    QCOMPARE(cfg.sendItems.size(), kSendItemCount);
    QCOMPARE(cfg.sendItems.at(0).content, QStringLiteral("第一条"));
    QCOMPARE(cfg.sendItems.at(2).content, QStringLiteral("第三条"));
    QVERIFY(cfg.sendItems.at(1).content.isEmpty());
    QVERIFY(cfg.sendItems.at(50).content.isEmpty());
    QCOMPARE(cfg.sendItems.at(50).intervalMs, kDefaultItemIntervalMs);
    QVERIFY(!cfg.sendItems.at(50).enabled);
}

void TestConfigManager::garbage_file_yields_defaults_without_crashing()
{
    // 磁盘没写完、文件被别的程序改过：QSettings 报 FormatError。
    // load() 必须给出完整默认值，而不是半截数据或崩溃 ——
    // 它是 MainWindow 构造函数里的第一行，出事就是白屏起不来。
    QFile f(ConfigManager::configFilePath());
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("<<< not an ini file at all >>>\n[[[broken\nkey without value\n");
    f.close();

    const AppConfig cfg = ConfigManager::load();
    QCOMPARE(cfg.serial.portName, QStringLiteral("COM1"));
    QCOMPARE(cfg.serial.baudRate, 115200);
    QCOMPARE(cfg.sendItems.size(), kSendItemCount);
    QVERIFY(cfg.sendText.isEmpty());
    QCOMPARE(cfg.recvBufferLimit, kDefaultReceiveLimit);
}

// ---- 往返 ----

void TestConfigManager::roundtrip_preserves_every_scalar_field()
{
    const AppConfig in = populated();
    QString err;
    QVERIFY2(ConfigManager::saveChecked(in, &err), qPrintable(err));

    const AppConfig out = ConfigManager::load();

    QCOMPARE(out.serial.portName, in.serial.portName);
    QCOMPARE(out.serial.baudRate, in.serial.baudRate);
    QCOMPARE(int(out.serial.dataBits), int(in.serial.dataBits));
    QCOMPARE(int(out.serial.parity), int(in.serial.parity));
    QCOMPARE(int(out.serial.stopBits), int(in.serial.stopBits));
    QCOMPARE(int(out.serial.flowControl), int(in.serial.flowControl));
    QCOMPARE(out.serial.dtr, in.serial.dtr);
    QCOMPARE(out.serial.rts, in.serial.rts);

    QCOMPARE(out.sendText, in.sendText);
    QCOMPARE(out.sendAppendCrlf, in.sendAppendCrlf);
    QCOMPARE(out.sendHexMode, in.sendHexMode);
    QCOMPARE(out.sendIntervalMs, in.sendIntervalMs);
    QCOMPARE(out.sendEncoding, in.sendEncoding);
    QCOMPARE(out.loopEnabled, in.loopEnabled);
    QCOMPARE(out.loopIntervalMs, in.loopIntervalMs);

    QCOMPARE(out.recvHex, in.recvHex);
    QCOMPARE(out.recvTimestamp, in.recvTimestamp);
    QCOMPARE(out.recvWrap, in.recvWrap);
    QCOMPARE(out.recvShowSent, in.recvShowSent);
    QCOMPARE(out.recvBufferLimit, in.recvBufferLimit);
    QCOMPARE(out.recvEncoding, in.recvEncoding);
    QCOMPARE(out.recvFont, in.recvFont);

    // saveGeometry() 的产物是含 NUL 的二进制，QSettings 走 @ByteArray() 存
    QCOMPARE(out.geometry, in.geometry);
    QCOMPARE(out.state, in.state);
}

void TestConfigManager::roundtrip_keeps_all_99_items_independent()
{
    // 这条用例钉住 ConfigManager 里唯一一个「不报错、不崩溃、结果全错」的地方：
    // QSettings::beginReadArray 只把数组下标置 0 一次，循环内不逐轮
    // setArrayIndex 的话，99 条会全部读成第 0 条 —— 而且下次 save() 会把
    // 这个结果原样写回去，用户三条不同的指令从此变成同一条的九十九份。
    const AppConfig in = populated();
    QVERIFY(ConfigManager::saveChecked(in, nullptr));

    const AppConfig out = ConfigManager::load();
    QCOMPARE(out.sendItems.size(), kSendItemCount);
    for (int i = 0; i < kSendItemCount; ++i) {
        const SendItem& a = in.sendItems.at(i);
        const SendItem& b = out.sendItems.at(i);
        QCOMPARE(b.enabled, a.enabled);
        QCOMPARE(b.hexMode, a.hexMode);
        QCOMPARE(b.name, a.name);
        QCOMPARE(b.intervalMs, a.intervalMs);
        QCOMPARE(b.content, a.content);
    }
}

void TestConfigManager::roundtrip_survives_ini_hostile_text()
{
    // ini 格式有自己的转义规则（\n 写成 \\n、反斜杠是转义符、行首分号是注释），
    // 而串口指令里这些字符一个都不算怪 —— 每一条都是用户真会输进去的东西。
    const QStringList nasty = {
        QStringLiteral("AT+GMR\r\n"),
        QStringLiteral("; 这一行看起来像注释"),
        QStringLiteral("[方括号]"),
        QStringLiteral("key=value"),
        QStringLiteral("反斜杠\\与百分号%"),
        QStringLiteral("  首尾有空格  "),
        QStringLiteral("AT+CGDCONT=1,\"IP\",\"cmnet\""),
        QStringLiteral("中文"),
    };

    AppConfig in = populated();
    for (int i = 0; i < nasty.size(); ++i) {
        in.sendItems[i].content    = nasty.at(i);
        in.sendItems[i].name       = nasty.at(i);
        in.sendItems[i].enabled    = true;
        in.sendItems[i].intervalMs = 500;
    }
    in.sendText = nasty.join(QLatin1Char('|'));

    QVERIFY(ConfigManager::saveChecked(in, nullptr));
    const AppConfig out = ConfigManager::load();

    for (int i = 0; i < nasty.size(); ++i) {
        const QString what = QStringLiteral("第 %1 条：<%2>").arg(i).arg(nasty.at(i));
        QVERIFY2(out.sendItems.at(i).content == nasty.at(i), qPrintable(what));
        QVERIFY2(out.sendItems.at(i).name == nasty.at(i), qPrintable(what));
    }
    QCOMPARE(out.sendText, in.sendText);
}

void TestConfigManager::blanking_items_clears_their_previous_content()
{
    // QSettings 不会删除没被覆盖的键。save() 若按实际长度写短列表，
    // 上一次残留的内容就会一直躺在配置里、这次启动又被读回来 ——
    // 用户明明清空了一条，下次打开却又出现了。
    AppConfig full = populated();
    QVERIFY(ConfigManager::saveChecked(full, nullptr));

    AppConfig sparse;
    sparse.sendItems.clear();
    for (int i = 0; i < 3; ++i) {
        SendItem it;
        it.content = QStringLiteral("新的%1").arg(i);
        sparse.sendItems.append(it);
    }
    QVERIFY(ConfigManager::saveChecked(sparse, nullptr));

    const AppConfig out = ConfigManager::load();
    QCOMPARE(out.sendItems.size(), kSendItemCount);
    QCOMPARE(out.sendItems.at(0).content, QStringLiteral("新的0"));
    QCOMPARE(out.sendItems.at(2).content, QStringLiteral("新的2"));
    for (int i = 3; i < kSendItemCount; ++i) {
        const QString what = QStringLiteral("第 %1 条残留了：<%2>").arg(i).arg(out.sendItems.at(i).content);
        QVERIFY2(out.sendItems.at(i).content.isEmpty(), qPrintable(what));
        QVERIFY2(!out.sendItems.at(i).enabled, qPrintable(what));
    }
}

// ---- 越界回落 ----

void TestConfigManager::parity_value_1_falls_back_to_none()
{
    // Qt 6 的 Parity 取值并不连续：NoParity=0, EvenParity=2, OddParity=3,
    // SpaceParity=4, MarkParity=5 —— 1 号位是 Qt4 时代 UndefinedParity
    // 留下的空洞。用区间校验的话 1 会混过去，界面显示"无校验"，
    // 串口上跑的却是偶校验，且没有任何提示。只能逐值枚举。
    const AppConfig cfg = loadWith(QStringLiteral("serial/parity"), 1);
    QCOMPARE(int(cfg.serial.parity), int(QSerialPort::NoParity));
    QVERIFY(cfg.serial.parity != QSerialPort::EvenParity);

    // 5 个合法值必须原样放行，逐值枚举最容易在这里漏放行
    QCOMPARE(int(loadWith(QStringLiteral("serial/parity"), 0).serial.parity), int(QSerialPort::NoParity));
    QCOMPARE(int(loadWith(QStringLiteral("serial/parity"), 2).serial.parity), int(QSerialPort::EvenParity));
    QCOMPARE(int(loadWith(QStringLiteral("serial/parity"), 3).serial.parity), int(QSerialPort::OddParity));
    QCOMPARE(int(loadWith(QStringLiteral("serial/parity"), 4).serial.parity), int(QSerialPort::SpaceParity));
    QCOMPARE(int(loadWith(QStringLiteral("serial/parity"), 5).serial.parity), int(QSerialPort::MarkParity));
}

void TestConfigManager::data_bits_are_validated_per_value()
{
    // DataBits 是 Data5..Data8 = 5..8。4 和 9 都非法。
    QCOMPARE(int(loadWith(QStringLiteral("serial/dataBits"), 4).serial.dataBits), int(QSerialPort::Data8));
    QCOMPARE(int(loadWith(QStringLiteral("serial/dataBits"), 9).serial.dataBits), int(QSerialPort::Data8));
    QCOMPARE(int(loadWith(QStringLiteral("serial/dataBits"), 5).serial.dataBits), int(QSerialPort::Data5));
    QCOMPARE(int(loadWith(QStringLiteral("serial/dataBits"), 6).serial.dataBits), int(QSerialPort::Data6));
    QCOMPARE(int(loadWith(QStringLiteral("serial/dataBits"), 7).serial.dataBits), int(QSerialPort::Data7));
    QCOMPARE(int(loadWith(QStringLiteral("serial/dataBits"), 8).serial.dataBits), int(QSerialPort::Data8));
}

void TestConfigManager::stop_bits_are_validated_per_value()
{
    // 取值 OneStop=1, TwoStop=2, OneAndHalfStop=3。3 是合法枚举但本项目
    // 有意不支持（需求 §5.1 与 SSCOM 都只给 1 和 2），落回 OneStop。
    QCOMPARE(int(loadWith(QStringLiteral("serial/stopBits"), 3).serial.stopBits), int(QSerialPort::OneStop));
    QCOMPARE(int(loadWith(QStringLiteral("serial/stopBits"), 0).serial.stopBits), int(QSerialPort::OneStop));
    QCOMPARE(int(loadWith(QStringLiteral("serial/stopBits"), 9).serial.stopBits), int(QSerialPort::OneStop));
    QCOMPARE(int(loadWith(QStringLiteral("serial/stopBits"), 1).serial.stopBits), int(QSerialPort::OneStop));
    QCOMPARE(int(loadWith(QStringLiteral("serial/stopBits"), 2).serial.stopBits), int(QSerialPort::TwoStop));
}

void TestConfigManager::flow_control_is_validated_per_value()
{
    QCOMPARE(int(loadWith(QStringLiteral("serial/flow"), 3).serial.flowControl), int(QSerialPort::NoFlowControl));
    QCOMPARE(int(loadWith(QStringLiteral("serial/flow"), -1).serial.flowControl), int(QSerialPort::NoFlowControl));
    QCOMPARE(int(loadWith(QStringLiteral("serial/flow"), 0).serial.flowControl), int(QSerialPort::NoFlowControl));
    QCOMPARE(int(loadWith(QStringLiteral("serial/flow"), 1).serial.flowControl), int(QSerialPort::HardwareControl));
    QCOMPARE(int(loadWith(QStringLiteral("serial/flow"), 2).serial.flowControl), int(QSerialPort::SoftwareControl));
}

void TestConfigManager::non_positive_baud_falls_back_to_115200()
{
    QCOMPARE(loadWith(QStringLiteral("serial/baud"), 0).serial.baudRate, 115200);
    QCOMPARE(loadWith(QStringLiteral("serial/baud"), -9600).serial.baudRate, 115200);
    QCOMPARE(loadWith(QStringLiteral("serial/baud"), 9600).serial.baudRate, 9600);
    QCOMPARE(loadWith(QStringLiteral("serial/baud"), 230400).serial.baudRate, 230400);
}

void TestConfigManager::empty_port_name_falls_back_to_com1()
{
    // 空串会让 SerialConfigPanel 落到"没有选中任何串口"的错位状态。
    QCOMPARE(loadWith(QStringLiteral("serial/port"), QString()).serial.portName, QStringLiteral("COM1"));
    QCOMPARE(loadWith(QStringLiteral("serial/port"), QStringLiteral("COM17")).serial.portName, QStringLiteral("COM17"));
}

void TestConfigManager::non_positive_buffer_limit_falls_back_to_default()
{
    // 0 或负数会让 ReceiveView 把整个接收区清空后再也长不回来。
    QCOMPARE(loadWith(QStringLiteral("recv/bufferLimit"), 0).recvBufferLimit, kDefaultReceiveLimit);
    QCOMPARE(loadWith(QStringLiteral("recv/bufferLimit"), -5).recvBufferLimit, kDefaultReceiveLimit);
    QCOMPARE(loadWith(QStringLiteral("recv/bufferLimit"), 200000).recvBufferLimit, qsizetype(200000));
}

void TestConfigManager::out_of_range_intervals_fall_back()
{
    // 定时发送与循环间隔的合法区间是 1..60000。0 尤其危险：
    // 直接交给 QTimer 就是忙等，在主线程上足以让界面卡死。
    QCOMPARE(loadWith(QStringLiteral("send/intervalMs"), 0).sendIntervalMs, 1000);
    QCOMPARE(loadWith(QStringLiteral("send/intervalMs"), -1).sendIntervalMs, 1000);
    QCOMPARE(loadWith(QStringLiteral("send/intervalMs"), 60001).sendIntervalMs, 1000);
    QCOMPARE(loadWith(QStringLiteral("send/intervalMs"), 500).sendIntervalMs, 500);
    QCOMPARE(loadWith(QStringLiteral("send/intervalMs"), 60000).sendIntervalMs, 60000);

    QCOMPARE(loadWith(QStringLiteral("loop/intervalMs"), 0).loopIntervalMs, 100);
    QCOMPARE(loadWith(QStringLiteral("loop/intervalMs"), 60001).loopIntervalMs, 100);
    QCOMPARE(loadWith(QStringLiteral("loop/intervalMs"), 250).loopIntervalMs, 250);
}

void TestConfigManager::out_of_range_item_interval_falls_back()
{
    // 逐条间隔的区间更大（1..600000），但下界同样是 1 ——
    // "配置里写着 0、界面显示 0、实际发出去是 1ms" 这种错位最难发现。
    QCOMPARE(loadWithItemInterval(0).sendItems.at(3).intervalMs, kDefaultItemIntervalMs);
    QCOMPARE(loadWithItemInterval(-1).sendItems.at(3).intervalMs, kDefaultItemIntervalMs);
    QCOMPARE(loadWithItemInterval(600001).sendItems.at(3).intervalMs, kDefaultItemIntervalMs);
    QCOMPARE(loadWithItemInterval(1).sendItems.at(3).intervalMs, kMinItemIntervalMs);
    QCOMPARE(loadWithItemInterval(250).sendItems.at(3).intervalMs, 250);
    QCOMPARE(loadWithItemInterval(600000).sendItems.at(3).intervalMs, kMaxItemIntervalMs);
}

void TestConfigManager::invalid_encoding_falls_back_to_utf8()
{
    QCOMPARE(loadWith(QStringLiteral("send/encoding"), 1).sendEncoding, TextEncoding::Local);
    QCOMPARE(loadWith(QStringLiteral("send/encoding"), 2).sendEncoding, TextEncoding::Latin1);
    QCOMPARE(loadWith(QStringLiteral("send/encoding"), 99).sendEncoding, TextEncoding::Utf8);
    QCOMPARE(loadWith(QStringLiteral("send/encoding"), -1).sendEncoding, TextEncoding::Utf8);
    QCOMPARE(loadWith(QStringLiteral("recv/encoding"), 1).recvEncoding, TextEncoding::Local);
    QCOMPARE(loadWith(QStringLiteral("recv/encoding"), 99).recvEncoding, TextEncoding::Utf8);
}

// ---- 布局版本 ----

void TestConfigManager::save_stamps_layout_version()
{
    // 版本号由 save 盖章，调用方给的 layoutVersion 不作数 ——
    // MainWindow 收集配置时不会去填这个字段，漏盖就等于老配置当新结构用。
    AppConfig c = populated();
    c.layoutVersion = 0;
    QVERIFY(ConfigManager::saveChecked(c, nullptr));
    QCOMPARE(ConfigManager::load().layoutVersion, kLayoutVersion);
}

void TestConfigManager::missing_layout_version_reads_as_zero()
{
    // 读不到版本号 = 这份配置来自没有版本号的老版本，按"结构不匹配"处理，
    // MainWindow 会跳过 state 恢复。若这里回落到 kLayoutVersion，
    // 旧 blob 就会被套到新控件树上，面板会恢复成错的样子而且不报错。
    QCOMPARE(ConfigManager::load().layoutVersion, 0);
}

// ---- 主题 ----

void TestConfigManager::theme_round_trips_through_the_ini()
{
    // 存字符串而不是 int：ini 会被人手改，看到 "dark" 比看到 "2" 靠谱。
    for (Theme t : { Theme::System, Theme::Light, Theme::Dark }) {
        AppConfig cfg;
        cfg.theme = t;
        ConfigManager::save(cfg);
        QCOMPARE(ConfigManager::load().theme, t);
    }
}

void TestConfigManager::missing_theme_reads_as_system()
{
    // 没有这个键 = 加这一项之前的配置，那时界面就是跟随系统的。
    // 回落到 Light 或 Dark 都等于升级就改了用户的界面。
    QCOMPARE(ConfigManager::load().theme, Theme::System);
}

void TestConfigManager::unrecognised_theme_value_reads_as_system()
{
    // 手改成看不懂的值时宁可按 System 起，也不要猜：猜错是界面无缘无故
    // 变样，而且用户根本不知道自己"改"过什么。
    for (const char* bad : { "", "  ", "DARKISH", "深色", "2", "yes" }) {
        writeRaw(QStringLiteral("app/theme"), QString::fromLatin1(bad));
        QCOMPARE(ConfigManager::load().theme, Theme::System);
    }
}

void TestConfigManager::theme_string_conversion_is_case_insensitive()
{
    // 手写 ini 的人不会严格照着大小写打。
    QCOMPARE(themeFromString(QStringLiteral("DARK")), Theme::Dark);
    QCOMPARE(themeFromString(QStringLiteral(" Light ")), Theme::Light);
    QCOMPARE(themeFromString(QStringLiteral("System")), Theme::System);
    QCOMPARE(themeToString(Theme::Dark), QStringLiteral("dark"));
}

// ---- 配置读坏了必须报出来 ----

void TestConfigManager::corrupt_file_is_reported_by_load_checked()
{
    // 评审发现的缺陷：load() 全程不看 status()，QSettings 遇到 FormatError 时
    // 每条 value() 都静默返回默认值，读回来是一份全新的默认配置。用户那份
    // 99 条、发送草稿、窗口布局全部"消失"，而退出时 MainWindow 又把这份
    // 默认值原样写回去 —— 损坏的文件连同里面能抢救的内容一起没了。
    QFile f(ConfigManager::configFilePath());
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("<<< 不是 ini >>>\n[[[坏掉的键没有等号\n另一个坏的\n");
    f.close();

    QString err;
    const AppConfig cfg = ConfigManager::loadChecked(&err);

    QVERIFY2(!err.isEmpty(), "配置文件读坏了却一个字都没报");
    // 坏文件仍要给出一份可用的默认值，程序要能起来，不能崩在读配置这一步
    QCOMPARE(cfg.sendItems.size(), kSendItemCount);
}

void TestConfigManager::healthy_file_reports_no_error()
{
    // 反过来也必须成立：正常配置不能误报，否则 MainWindow 会一直提示"读坏了"
    // 并且永远不保存配置。
    QVERIFY(ConfigManager::saveChecked(populated(), nullptr));

    QString err;
    ConfigManager::loadChecked(&err);
    QVERIFY2(err.isEmpty(), qPrintable(QStringLiteral("正常配置被误报为损坏：%1").arg(err)));

    // 也没有配置文件（全新机器）时，同样不该报错
    QFile::remove(ConfigManager::configFilePath());
    err.clear();
    ConfigManager::loadChecked(&err);
    QVERIFY2(err.isEmpty(), qPrintable(QStringLiteral("没有配置文件被误报：%1").arg(err)));
}

// ---- 绿色软件契约 ----

void TestConfigManager::config_file_lives_next_to_the_exe()
{
    // 「绿色软件」的判据：配置在 exe 同目录，不是注册表、不是 %APPDATA%。
    // 早期版本用 QSettings(org, app)，在 Windows 上落到 HKCU\Software\SerialMate，
    // 用户看不见、删不掉、换机带不走、卸载后还留残留。
    const QString expected =
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("SerialMate.ini"));
    QCOMPARE(ConfigManager::configFilePath(), expected);
    QVERIFY(!ConfigManager::configFilePath().contains(QLatin1String("AppData"), Qt::CaseInsensitive));
}

void TestConfigManager::save_checked_reports_success_on_a_writable_dir()
{
    QString err;
    QVERIFY2(ConfigManager::saveChecked(populated(), &err), qPrintable(err));
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QVERIFY(QFile::exists(ConfigManager::configFilePath()));
}

void TestConfigManager::check_writable_succeeds_next_to_the_exe()
{
    // 只覆盖成功路径。只读目录这条要靠 chmod 造，Windows 上不可靠，
    // 而且它需要给产品代码开一个指定目录的后门 —— 收益不抵代价。
    QString err;
    QVERIFY2(ConfigManager::checkWritable(&err), qPrintable(err));
}

QTEST_MAIN(TestConfigManager)
#include "tst_configmanager.moc"
