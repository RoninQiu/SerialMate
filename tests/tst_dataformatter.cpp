#include "core/DataFormatter.h"

#include <QTest>

class TestDataFormatter : public QObject
{
    Q_OBJECT

private slots:
    void hex_accepts_spaced();
    void hex_accepts_contiguous();
    void hex_accepts_lowercase();
    void hex_accepts_0x_prefix();
    void hex_ignores_all_whitespace_forms();
    void hex_empty_input_is_ok();
    void hex_rejects_odd_nibbles();
    void hex_rejects_non_hex_chars();
    void hex_rejects_prefix_only();
    void toHex_uppercase_space_separated();
    void toHex_roundtrip();
    void timestamp_shape();
    void formatChunk_hex();
    void formatChunk_ascii();
    void formatChunk_with_timestamp();
    void formatChunk_empty();

    // 方向标记
    void direction_marker_is_real_single_codepoint();
    void direction_none_leaves_output_untouched();
    void direction_marker_precedes_timestamp_and_body();
    void direction_incoming_and_outgoing_differ();
    void strip_removes_leading_markers_only();
    void strip_keeps_markers_appearing_mid_line();
    void strip_handles_crlf_and_blank_lines();
    void strip_round_trips_formatChunk_output();
};

// ---- hexStringToBytes ----

void TestDataFormatter::hex_accepts_spaced()
{
    bool ok = false;
    QCOMPARE(DataFormatter::hexStringToBytes(QStringLiteral("01 02 0D 0A"), &ok),
             QByteArray::fromHex("01020D0A"));
    QVERIFY(ok);
}

void TestDataFormatter::hex_accepts_contiguous()
{
    bool ok = false;
    QCOMPARE(DataFormatter::hexStringToBytes(QStringLiteral("01020D0A"), &ok),
             QByteArray::fromHex("01020D0A"));
    QVERIFY(ok);
}

void TestDataFormatter::hex_accepts_lowercase()
{
    bool ok = false;
    QCOMPARE(DataFormatter::hexStringToBytes(QStringLiteral("de ad be ef"), &ok),
             QByteArray::fromHex("DEADBEEF"));
    QVERIFY(ok);
}

void TestDataFormatter::hex_accepts_0x_prefix()
{
    bool ok = false;
    QCOMPARE(DataFormatter::hexStringToBytes(QStringLiteral("0x01 0X02"), &ok),
             QByteArray::fromHex("0102"));
    QVERIFY(ok);
}

void TestDataFormatter::hex_ignores_all_whitespace_forms()
{
    bool ok = false;
    // 空白一律忽略，因此 "0 1" 与 "01" 等价 —— 都是 1 字节 0x01，
    // 不是 2 字节。断言写错这一点会掩盖真实的解析结果。
    QCOMPARE(DataFormatter::hexStringToBytes(QStringLiteral("0 1"), &ok),
             QByteArray::fromHex("01"));
    QVERIFY(ok);
    // 各种空白形态都应被跳过，结果与无空白的 "0102" 相同
    ok = false;
    QCOMPARE(DataFormatter::hexStringToBytes(QStringLiteral("  01\t02\n"), &ok),
             QByteArray::fromHex("0102"));
    QVERIFY(ok);
}

void TestDataFormatter::hex_empty_input_is_ok()
{
    bool ok = false;
    QVERIFY(DataFormatter::hexStringToBytes(QString(), &ok).isEmpty());
    QVERIFY(ok);
    ok = false;
    QVERIFY(DataFormatter::hexStringToBytes(QStringLiteral("   "), &ok).isEmpty());
    QVERIFY(ok);
}

void TestDataFormatter::hex_rejects_odd_nibbles()
{
    bool ok = true;
    QVERIFY(DataFormatter::hexStringToBytes(QStringLiteral("123"), &ok).isEmpty());
    QVERIFY(!ok);
    ok = true;
    QVERIFY(DataFormatter::hexStringToBytes(QStringLiteral("01 0"), &ok).isEmpty());
    QVERIFY(!ok);
}

void TestDataFormatter::hex_rejects_non_hex_chars()
{
    bool ok = true;
    QVERIFY(DataFormatter::hexStringToBytes(QStringLiteral("GG"), &ok).isEmpty());
    QVERIFY(!ok);
    ok = true;
    QVERIFY(DataFormatter::hexStringToBytes(QStringLiteral("01 ZZ 02"), &ok).isEmpty());
    QVERIFY(!ok);
}

void TestDataFormatter::hex_rejects_prefix_only()
{
    bool ok = true;
    QVERIFY(DataFormatter::hexStringToBytes(QStringLiteral("0x"), &ok).isEmpty());
    QVERIFY(!ok);
}

// ---- bytesToHexString ----

void TestDataFormatter::toHex_uppercase_space_separated()
{
    QCOMPARE(DataFormatter::bytesToHexString(QByteArray::fromHex("DEADBEEF")),
             QStringLiteral("DE AD BE EF"));
}

void TestDataFormatter::toHex_roundtrip()
{
    const QByteArray original = QByteArray::fromHex("0102030405060708");
    bool ok = false;
    QCOMPARE(DataFormatter::hexStringToBytes(DataFormatter::bytesToHexString(original), &ok),
             original);
    QVERIFY(ok);
}

// ---- 时间戳与格式化 ----

void TestDataFormatter::timestamp_shape()
{
    const QString ts = DataFormatter::currentTimestamp();
    QCOMPARE(ts.size(), 14);                       // 1 + "HH:mm:ss.zzz"(12) + 1
    QCOMPARE(ts.left(1), QStringLiteral("["));
    QCOMPARE(ts.right(1), QStringLiteral("]"));
}

void TestDataFormatter::formatChunk_hex()
{
    DataFormatter::DisplayOptions opt;
    opt.hexMode = true;
    QCOMPARE(DataFormatter::formatChunk(QByteArray::fromHex("DEADBEEF"), opt),
             QStringLiteral("DE AD BE EF"));
}

void TestDataFormatter::formatChunk_ascii()
{
    DataFormatter::DisplayOptions opt;
    QCOMPARE(DataFormatter::formatChunk(QByteArray("AB"), opt), QStringLiteral("AB"));
}

void TestDataFormatter::formatChunk_with_timestamp()
{
    DataFormatter::DisplayOptions opt;
    opt.timestamp = true;
    const QString s = DataFormatter::formatChunk(QByteArray("AB"), opt);
    QVERIFY(s.startsWith(QLatin1Char('[')));
    QVERIFY(s.endsWith(QStringLiteral("AB")));
    QVERIFY(s.size() >= 17);                      // 14 位时间戳 + 空格 + 2 个字符
}

void TestDataFormatter::formatChunk_empty()
{
    DataFormatter::DisplayOptions opt;
    opt.hexMode   = true;
    opt.timestamp = true;
    QVERIFY(DataFormatter::formatChunk(QByteArray(), opt).isEmpty());
}

// ---- 方向标记 ----

namespace {
// 期望值也必须用码位构造。用 "\xE2\x86\x90 " 写期望值会与实现里的同一个
// 错误互相抵消 —— 两边一致地错，断言照样通过。实测踩过：QStringLiteral 的
// \x 是 Latin-1 码位而非 UTF-8 解码，产出 U+00E2 U+0086 U+0090 三个字符，
// 真实字节是 c3a2 c286 c290（双重编码），界面上是乱码。
const QString kIn  = QString(QChar(0x2190)) + QLatin1Char(' ');
const QString kOut = QString(QChar(0x2192)) + QLatin1Char(' ');
}

void TestDataFormatter::direction_marker_is_real_single_codepoint()
{
    // 这一条就是上面那个 bug 的守门人：箭头必须是一个码位 + 一个空格。
    // 若实现退回 "\xE2\x86\x90 " 写法，size 会变成 4，这里立刻红。
    QCOMPARE(DataFormatter::directionMarker(DataFormatter::Direction::Incoming).size(), 2);
    QCOMPARE(DataFormatter::directionMarker(DataFormatter::Direction::Outgoing).size(), 2);

    // 并且 UTF-8 编码必须是 e2 86 90 / e2 86 92，不能出现 c3 a2 这种双重编码前缀
    QCOMPARE(DataFormatter::directionMarker(DataFormatter::Direction::Incoming)
                 .toUtf8().left(3).toHex(), QByteArray("e28690"));
    QCOMPARE(DataFormatter::directionMarker(DataFormatter::Direction::Outgoing)
                 .toUtf8().left(3).toHex(), QByteArray("e28692"));
}

void TestDataFormatter::direction_none_leaves_output_untouched()
{
    // Direction::None 是默认值，必须与本特性加入之前的输出逐字节相同，
    // 否则老配置/老存档显示出来的内容会平白多出东西
    DataFormatter::DisplayOptions opt;
    opt.hexMode = true;
    opt.direction = DataFormatter::Direction::None;
    QCOMPARE(DataFormatter::formatChunk(QByteArray::fromHex("DEADBEEF"), opt),
             QStringLiteral("DE AD BE EF"));
    QVERIFY(DataFormatter::directionMarker(DataFormatter::Direction::None).isEmpty());
}

void TestDataFormatter::direction_marker_precedes_timestamp_and_body()
{
    DataFormatter::DisplayOptions opt;
    opt.hexMode   = true;
    opt.timestamp = true;
    opt.direction = DataFormatter::Direction::Outgoing;

    const QString s = DataFormatter::formatChunk(QByteArray::fromHex("DEADBEEF"), opt);
    QCOMPARE(s.left(kOut.size()), kOut);             // 标记在最前
    QVERIFY(s.startsWith(kOut));
    QVERIFY(s.endsWith(QStringLiteral("DE AD BE EF")));
    // 时间戳在标记之后
    QVERIFY(s.indexOf(QLatin1Char('[')) == kOut.size());
}

void TestDataFormatter::direction_incoming_and_outgoing_differ()
{
    DataFormatter::DisplayOptions in, out;
    in.direction  = DataFormatter::Direction::Incoming;
    out.direction = DataFormatter::Direction::Outgoing;
    QVERIFY(DataFormatter::directionMarker(in.direction)
            != DataFormatter::directionMarker(out.direction));
    QCOMPARE(DataFormatter::directionMarker(in.direction),  kIn);
    QCOMPARE(DataFormatter::directionMarker(out.direction), kOut);
}

void TestDataFormatter::strip_removes_leading_markers_only()
{
    const QString text = kIn + QStringLiteral("DE AD\n") + kOut + QStringLiteral("00 5F\n");
    // 末尾那个换行必须原样保留：strip 是「去掉标记」而不是「重组行」，
    // 保住行结构才能让 strip(formatChunk(...)) 严格可逆。
    QCOMPARE(DataFormatter::stripDirectionMarkers(text),
             QStringLiteral("DE AD\n00 5F\n"));
}

void TestDataFormatter::strip_keeps_markers_appearing_mid_line()
{
    // 判据必须是「行首正好是标记+空格」，不是全文替换。
    // 设备自己发来的内容里出现箭头形状是合法数据，不能被顺手删掉。
    const QString text = QStringLiteral("AB ") + kIn.trimmed() + QStringLiteral(" CD");
    QCOMPARE(DataFormatter::stripDirectionMarkers(text), text);

    // 行首没有空格隔开时也不动：那是数据的一部分，不是我们拼的标记
    const QString noSpace = QString(QChar(0x2192)) + QStringLiteral("AB");
    QCOMPARE(DataFormatter::stripDirectionMarkers(noSpace), noSpace);
}

void TestDataFormatter::strip_handles_crlf_and_blank_lines()
{
    const QString text = kIn + QStringLiteral("A\r\n\r\n") + kOut
                         + QStringLiteral("B\r\n");
    QCOMPARE(DataFormatter::stripDirectionMarkers(text),
             QStringLiteral("A\r\n\r\nB\r\n"));
}

void TestDataFormatter::strip_round_trips_formatChunk_output()
{
    // 最关键的一条：多行混合收发全部 strip 之后必须还原成裸数据。
    DataFormatter::DisplayOptions in, out;
    in.hexMode = out.hexMode = true;
    in.direction  = DataFormatter::Direction::Incoming;
    out.direction = DataFormatter::Direction::Outgoing;

    QString text;
    const char* hexes[] = { "0102", "0304", "0506", "0708" };
    for (int i = 0; i < 4; ++i) {
        text += DataFormatter::formatChunk(QByteArray::fromHex(hexes[i]),
                                           (i % 2 == 0) ? in : out);
        if (i < 3)
            text += QLatin1Char('\n');
    }

    QCOMPARE(DataFormatter::stripDirectionMarkers(text),
             QStringLiteral("01 02\n03 04\n05 06\n07 08"));
}

QTEST_MAIN(TestDataFormatter)
#include "tst_dataformatter.moc"
