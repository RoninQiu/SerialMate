#include "core/SscomImporter.h"

#include <QFile>
#include <QTest>
#include <QVector>

#include <windows.h>

// 测试文件里造 GBK 字节用，和生产代码解码用的是同一对 Win32 API，
// 但方向相反 —— 这样用例不依赖仓库里 ref/ 的原版文件。
static QByteArray toGbk(const QString& text)
{
    // 不能直接传 QString::utf16()：它返回 const ushort*，而 Win32 的 LPCWCH
    // 是 const wchar_t*。两者在 MinGW 下是两个互不相干的类型，强转会报错。
    QVector<wchar_t> wide;
    wide.reserve(text.size());
    for (const QChar c : text)
        wide.append(static_cast<wchar_t>(c.unicode()));

    const int need = WideCharToMultiByte(936, 0, wide.constData(), wide.size(),
                                         nullptr, 0, nullptr, nullptr);
    if (need <= 0)
        return QByteArray();
    QByteArray out;
    out.resize(need);
    WideCharToMultiByte(936, 0, wide.constData(), wide.size(),
                        out.data(), need, nullptr, nullptr);
    return out;
}

// SSCOM ini 的导入回归测试。
//
// 这批用例钉住的是**实测得来的**语义，不是照着文档猜的。最关键的一条是
// N1053/N1054 同一个框的两份草稿：实跑原版点「HEX发送」，框内文本当场从
// abcdfg 变成 12 FF 00 30；关闭后逐键 diff，只有 N1055 由 A 变 H。
// 少写这一条，导入就会把 HEX 草稿当成 ASC 内容搬过来。
class TestSscomImporter : public QObject
{
    Q_OBJECT

private slots:
    void gbk_decode_chinese();
    void rejects_empty_file();
    void rejects_non_ini_file();
    void maps_serial_and_receive();
    void asc_draft_selected_when_mode_is_a();
    void hex_draft_selected_when_mode_is_h();
    void maps_send_interval_and_crlf();
    void maps_buffer_limit();
    void imports_multi_string_items_with_names();
    void imports_per_item_interval();
    void per_item_interval_out_of_range_falls_back();
    void key_number_ranges_do_not_collide();
    void dropped_counter_guards_future_count_changes();
    void sets_encoding_to_local_always();

    // 选错文件时必须拒绝，而不是当成导入成功
    void rejects_a_file_that_merely_has_key_value_lines();
    void rejected_file_leaves_target_untouched();
    void keeps_existing_items_when_no_multi_string_found();
    void preserves_keys_absent_from_file();
    void real_sscom_ini_maps_as_expected();
};

// ---- GBK 解码 ----

void TestSscomImporter::gbk_decode_chinese()
{
    // "温" 在 GBK 下是 0xCE 0xC2 两字节；UTF-8 会解成乱码。
    const QByteArray gbk = toGbk(QStringLiteral("N1100=,汉语") + QChar(0x4E2D));
    QCOMPARE(gbk.size(), 13);           // 7 ASCII + 6 字节双字节汉字
    const QString text = SscomImporter::decodeSscomIni(gbk);
    QCOMPARE(text, QStringLiteral("N1100=,汉语中"));
}

void TestSscomImporter::rejects_empty_file()
{
    AppConfig cfg;
    const SscomImportReport rep = SscomImporter::import(QByteArray(), cfg);
    QVERIFY(!rep.ok);
    QVERIFY(!rep.error.isEmpty());
}

void TestSscomImporter::rejects_non_ini_file()
{
    // 一个随便的文本文件：没有 N 开头的条目，必须明确拒绝而不是导出一堆默认值
    AppConfig cfg;
    const SscomImportReport rep =
        SscomImporter::import(QByteArray("hello world\r\nnot an ini\r\n"), cfg);
    QVERIFY(!rep.ok);
    QVERIFY(rep.error.contains(QStringLiteral("N")));
}

// ---- 键位映射 ----

void TestSscomImporter::maps_serial_and_receive()
{
    AppConfig cfg;
    const SscomImportReport rep = SscomImporter::import(toGbk(
        "N1080=,COM7\r\nN1081=,19200\r\nN1061=,N\r\nN1062=,Y\r\nN1059=,Y\r\n"), cfg);
    QVERIFY(rep.ok);
    QCOMPARE(cfg.serial.portName, QStringLiteral("COM7"));
    QCOMPARE(cfg.serial.baudRate, 19200);
    QCOMPARE(cfg.serial.dtr, false);     // N1061=N
    QCOMPARE(cfg.serial.rts, true);      // N1062=Y
    QCOMPARE(cfg.recvHex, true);         // N1059=Y
    // SSCOM 不存数据位/校验位/停止位/流控，必须保持默认值 8N1 无流控
    QCOMPARE(int(cfg.serial.dataBits), 8);
    QCOMPARE(int(cfg.serial.parity), 0);
    QCOMPARE(int(cfg.serial.stopBits), 1);
    QCOMPARE(int(cfg.serial.flowControl), 0);
}

void TestSscomImporter::asc_draft_selected_when_mode_is_a()
{
    AppConfig cfg;
    SscomImporter::import(toGbk(
        "N1053=,abcdefg\r\nN1054=,12 FF 00 30\r\nN1055=,A\r\n"), cfg);
    QCOMPARE(cfg.sendText, QStringLiteral("abcdefg"));
    QCOMPARE(cfg.sendHexMode, false);
}

void TestSscomImporter::hex_draft_selected_when_mode_is_h()
{
    // 钉住实测结论：N1055=H 时应该取 HEX 草稿，而不是 ASC 草稿
    AppConfig cfg;
    const SscomImportReport rep = SscomImporter::import(toGbk(
        "N1053=,abcdefg\r\nN1054=,12 FF 00 30\r\nN1055=,H\r\n"), cfg);
    QCOMPARE(cfg.sendText, QStringLiteral("12 FF 00 30"));
    QCOMPARE(cfg.sendHexMode, true);

    // 另一份草稿被丢了，报告里必须说出来，不能静默
    bool mentioned = false;
    for (const QString& n : rep.notes) {
        if (n.contains(QStringLiteral("草稿")))
            mentioned = true;
    }
    QVERIFY(mentioned);
}

void TestSscomImporter::maps_send_interval_and_crlf()
{
    AppConfig cfg;
    SscomImporter::import(toGbk("N1056=,2500\r\nN1057=,Y\r\n"), cfg);
    QCOMPARE(cfg.sendIntervalMs, 2500);
    QCOMPARE(cfg.sendAppendCrlf, true);
}

void TestSscomImporter::maps_buffer_limit()
{
    AppConfig cfg;
    SscomImporter::import(toGbk("N1067=,2000000\r\n"), cfg);
    QCOMPARE(cfg.recvBufferLimit, 2000000LL);
}

// ---- 多字符串 ----

void TestSscomImporter::imports_multi_string_items_with_names()
{
    AppConfig cfg;
    const SscomImportReport rep = SscomImporter::import(toGbk(
        "N101=1,十六进制数据串1,1000\r\nN1=H,13 00 FF 88\r\n"
        "N102=3,字符串1,1000\r\nN2=A,output string\r\n"
        "N103=2,欢迎语,1000\r\nN3=A,欢迎您使用SSCOM!\r\n"), cfg);
    QVERIFY(rep.ok);
    QCOMPARE(rep.itemsImported, 3);
    QCOMPARE(rep.itemsDropped, 0);

    QCOMPARE(cfg.sendItems.size(), kSendItemCount);   // 恒定补满 99 条
    QCOMPARE(cfg.sendItems.at(0).hexMode, true);
    QCOMPARE(cfg.sendItems.at(0).content, QStringLiteral("13 00 FF 88"));
    QCOMPARE(cfg.sendItems.at(0).name, QStringLiteral("十六进制数据串1"));
    QCOMPARE(cfg.sendItems.at(1).hexMode, false);
    QCOMPARE(cfg.sendItems.at(1).content, QStringLiteral("output string"));
    QCOMPARE(cfg.sendItems.at(1).name, QStringLiteral("字符串1"));
    QCOMPARE(cfg.sendItems.at(2).content, QStringLiteral("欢迎您使用SSCOM!"));
    // 使能语义未确证，一律不勾选
    QCOMPARE(cfg.sendItems.at(0).enabled, false);

    // N199 之外的行不该被当成注释行
    QCOMPARE(cfg.sendItems.at(kSendItemCount - 1).name, QString());
}

// ---- 每条间隔（N1xx 第三段）----

void TestSscomImporter::imports_per_item_interval()
{
    // 第三段是「这一条发完之后停多久」。取值取自一份真实配置：
    // 1000 默认、10000 慢操作、500 高频轮询。
    AppConfig cfg;
    const SscomImportReport rep = SscomImporter::import(toGbk(
        "N101=1,快轮询,500\r\nN1=A,ping\r\n"
        "N102=0,慢操作,10000\r\nN2=A,calib\r\n"
        "N103=0,普通,1000\r\nN3=A,norm\r\n"), cfg);
    QVERIFY(rep.ok);
    QCOMPARE(cfg.sendItems.at(0).intervalMs, 500);
    QCOMPARE(cfg.sendItems.at(1).intervalMs, 10000);
    QCOMPARE(cfg.sendItems.at(2).intervalMs, 1000);
    // 注释仍然来自第二段，不能被第三段挤掉
    QCOMPARE(cfg.sendItems.at(0).name, QStringLiteral("快轮询"));
}

void TestSscomImporter::per_item_interval_out_of_range_falls_back()
{
    // 越界值不能让 QTimer 拿到 0 或负数 —— 那是主线程忙等，会把界面卡死。
    // 一律回落到默认 1000ms。
    AppConfig cfg;
    SscomImporter::import(toGbk(
        "N101=1,零,0\r\nN1=A,a\r\n"
        "N102=0,负,-5\r\nN2=A,b\r\n"
        "N103=0,过大,999999999\r\nN3=A,c\r\n"
        "N104=0,缺省段,A\r\nN4=A,d\r\n"), cfg);
    QCOMPARE(cfg.sendItems.at(0).intervalMs, kDefaultItemIntervalMs);
    QCOMPARE(cfg.sendItems.at(1).intervalMs, kDefaultItemIntervalMs);
    QCOMPARE(cfg.sendItems.at(2).intervalMs, kDefaultItemIntervalMs);
    QCOMPARE(cfg.sendItems.at(3).intervalMs, kDefaultItemIntervalMs);
}

void TestSscomImporter::key_number_ranges_do_not_collide()
{
    // SSCOM 用位数区分 N 键用途：N1..N99 是内容，N101..N199 是对应注释，
    // N1000+ 是设置。所以 N100 **不是**第 100 条内容，它落在注释区间里。
    // 这条用例把边界钉死：真有人日后把分类器放宽成 "n < 1000 都是内容"，
    // 注释行会被当内容搬进来。
    QString ini;
    for (int i = 1; i <= 99; ++i) {
        ini += QStringLiteral("N%1=A,item%1\r\n").arg(i);
        // 两个占位符要不同的值：键号是 i+100，注释文字是 i
        ini += QStringLiteral("N%1=1,note%2,1000\r\n").arg(i + 100).arg(i);
    }
    ini += QStringLiteral("N100=7,不该被当成内容,1000\r\n");

    AppConfig cfg;
    const SscomImportReport rep = SscomImporter::import(toGbk(ini), cfg);
    QVERIFY(rep.ok);
    QCOMPARE(rep.itemsImported, 99);
    QCOMPARE(rep.itemsDropped, 0);
    QCOMPARE(cfg.sendItems.at(0).content, QStringLiteral("item1"));
    QCOMPARE(cfg.sendItems.at(0).name, QStringLiteral("note1"));
    QCOMPARE(cfg.sendItems.at(98).content, QStringLiteral("item99"));
    QCOMPARE(cfg.sendItems.at(98).name, QStringLiteral("note99"));
    // 注释行的第三段是间隔，全部是 1000
    QCOMPARE(cfg.sendItems.at(0).intervalMs, 1000);
}

void TestSscomImporter::dropped_counter_guards_future_count_changes()
{
    // itemsDropped 只在 kSendItemCount 被调小、或将来出现多于 99 条的
    // SSCOM 配置时才可能非零 —— SSCOM 5.13.1 恰好就是 99 条，正常导入下恒为 0。
    // 这里只能验证它确实是 0，其余场景靠 real_sscom_ini 那条覆盖。
    AppConfig cfg;
    const SscomImportReport rep = SscomImporter::import(toGbk("N1=A,x\r\n"), cfg);
    QVERIFY(rep.ok);
    QCOMPARE(rep.itemsDropped, 0);
}

// ---- 编码与缺省 ----

void TestSscomImporter::sets_encoding_to_local_always()
{
    // SSCOM 没有编码选项，中文 Windows 上隐式走 GBK。沿用 UTF-8 会让用户
    // 迁移后发出去的中文全变成乱码，所以导入必须无条件切到 Local。
    AppConfig cfg;
    SscomImporter::import(toGbk("N1080=,COM1\r\n"), cfg);
    QCOMPARE(cfg.sendEncoding, TextEncoding::Local);
    QCOMPARE(cfg.recvEncoding, TextEncoding::Local);
}

void TestSscomImporter::preserves_keys_absent_from_file()
{
    // 文件里没有的键不该被无谓重置
    AppConfig cfg;
    cfg.sendIntervalMs = 4321;
    cfg.recvBufferLimit = 777777;
    SscomImporter::import(toGbk("N1080=,COM3\r\n"), cfg);
    QCOMPARE(cfg.serial.portName, QStringLiteral("COM3"));
    QCOMPARE(cfg.sendIntervalMs, 4321);
    QCOMPARE(cfg.recvBufferLimit, 777777LL);
}

// ---- 用原版真实配置做黄金用例 ----

void TestSscomImporter::real_sscom_ini_maps_as_expected()
{
    // ref/ 未纳入版本控制，文件不在就跳过，不能让 clone 下来的人跑不过测试
    QFile f(QStringLiteral("../ref/sscom5.13.1/sscom51.ini"));
    if (!f.open(QIODevice::ReadOnly))
        QSKIP("ref/sscom5.13.1/sscom51.ini 不存在，跳过原版黄金用例");

    AppConfig cfg;
    const SscomImportReport rep = SscomImporter::import(f.readAll(), cfg);
    QVERIFY2(rep.ok, qPrintable(rep.error));

    QCOMPARE(cfg.serial.portName, QStringLiteral("COM1"));
    QCOMPARE(cfg.serial.baudRate, 115200);
    QCOMPARE(cfg.serial.dtr, true);      // N1061=Y
    QCOMPARE(cfg.serial.rts, false);     // N1062=N
    QCOMPARE(cfg.recvHex, true);         // N1059=Y
    QCOMPARE(cfg.recvBufferLimit, 1000000LL);
    QCOMPARE(cfg.sendIntervalMs, 1000);
    QCOMPARE(cfg.sendAppendCrlf, false); // N1057=N
    // N1055=A -> 取 ASC 草稿
    QCOMPARE(cfg.sendText, QStringLiteral("abcdefg"));
    QCOMPARE(cfg.sendHexMode, false);

    QCOMPARE(rep.itemsImported, 99);
    QCOMPARE(rep.itemsDropped, 0);
    QCOMPARE(cfg.sendItems.at(0).content, QStringLiteral("13 00 FF 88"));
    QCOMPARE(cfg.sendItems.at(0).name, QStringLiteral("十六进制数据串1"));
    QCOMPARE(cfg.sendItems.at(2).content, QStringLiteral("欢迎您使用SSCOM!"));
    QCOMPARE(cfg.sendItems.at(2).name, QStringLiteral("欢迎语"));
    QCOMPARE(cfg.sendEncoding, TextEncoding::Local);
}

void TestSscomImporter::rejects_a_file_that_merely_has_key_value_lines()
{
    // 评审发现的缺陷：判据原是"有没有任意一行 key=value"。而文件对话框里开着
    // 「所有文件 (*)」，用户很容易误选本程序自己的 SerialMate.ini —— 那种文件
    // 满是 key=value，一律被判成"导入成功"，接着 99 个空条目就盖上去了。
    // 判据必须落在 classify() 认得出的 N 键上。
    const QString plain = QStringLiteral(
        "[serial]\r\nport=COM5\r\nbaud=115200\r\ndataBits=8\r\n"
        "[recv]\r\nhex=true\r\ntimestamp=false\r\nwrap=true\r\n"
        "[sendItems]\r\nsize=99\r\n1\\enabled=false\r\n1\\content=AT\r\n");

    AppConfig cfg;                                   // 空的基底
    cfg.serial.portName = QStringLiteral("COM1");
    cfg.sendItems.clear();
    for (int i = 0; i < kSendItemCount; ++i)
        cfg.sendItems.append(SendItem());

    const SscomImportReport rep =
        SscomImporter::import(toGbk(plain), cfg);

    QVERIFY2(!rep.ok, "普通 ini 不该被判成 SSCOM 配置导入成功");
    QVERIFY2(rep.error.contains(QStringLiteral("N")),
             qPrintable(QStringLiteral("报错应说明缺少可识别的 N 键，实际：%1").arg(rep.error)));
}

void TestSscomImporter::rejected_file_leaves_target_untouched()
{
    const QString plain = QStringLiteral("[serial]\r\nport=COM5\r\nbaud=9600\r\n");

    AppConfig cfg;
    cfg.serial.portName  = QStringLiteral("COM9");
    cfg.serial.baudRate  = 57600;
    cfg.sendText         = QStringLiteral("别动我");
    cfg.recvBufferLimit  = 200000;
    cfg.sendItems.clear();
    for (int i = 0; i < kSendItemCount; ++i) {
        SendItem it;
        it.content = QStringLiteral("我自己的%1").arg(i);
        cfg.sendItems.append(it);
    }

    const SscomImportReport rep = SscomImporter::import(toGbk(plain), cfg);

    QVERIFY(!rep.ok);
    QCOMPARE(cfg.serial.portName, QStringLiteral("COM9"));
    QCOMPARE(cfg.serial.baudRate, 57600);
    QCOMPARE(cfg.sendText, QStringLiteral("别动我"));
    QCOMPARE(cfg.recvBufferLimit, qsizetype(200000));
    QCOMPARE(cfg.sendItems.size(), kSendItemCount);
    QCOMPARE(cfg.sendItems.at(42).content, QStringLiteral("我自己的42"));
    // 编码也不能被顺手改成 Local —— 那是"导入成功"才该做的事
    QCOMPARE(cfg.sendEncoding, TextEncoding::Utf8);
}

void TestSscomImporter::keeps_existing_items_when_no_multi_string_found()
{
    // 是 SSCOM 的 ini、但一条多字符串都没有（N1..N99 缺失）时，
    // 也不能拿 99 个空条目盖掉用户现有的 —— 认出来的可能只是别的设置项。
    // 注意 SSCOM 的 ini 里值是**前导逗号**格式（"N1080=,COM7"），
    // stripLeadingComma() 剥的是那个逗号，写成 "N1080=3,COM7" 会被整段当成串口号。
    const QString serialOnly = QStringLiteral("N1080=,COM7\r\nN1081=,19200\r\n");

    AppConfig cfg;
    cfg.sendItems.clear();
    for (int i = 0; i < kSendItemCount; ++i) {
        SendItem it;
        it.content = QStringLiteral("保留%1").arg(i);
        cfg.sendItems.append(it);
    }

    const SscomImportReport rep = SscomImporter::import(toGbk(serialOnly), cfg);

    QVERIFY2(rep.ok, qPrintable(QStringLiteral("有 N 键就该认作 SSCOM 配置：%1").arg(rep.error)));
    QCOMPARE(cfg.serial.portName, QStringLiteral("COM7"));
    QCOMPARE(rep.itemsImported, 0);
    QCOMPARE(cfg.sendItems.size(), kSendItemCount);
    QCOMPARE(cfg.sendItems.at(0).content, QStringLiteral("保留0"));
    QCOMPARE(cfg.sendItems.at(98).content, QStringLiteral("保留98"));

    bool mentioned = false;
    for (const QString& n : rep.notes)
        if (n.contains(QStringLiteral("保持原样"))) mentioned = true;
    QVERIFY2(mentioned, "notes 里应说明现有 99 条未被覆盖");
}

QTEST_MAIN(TestSscomImporter)
#include "tst_sscomimporter.moc"