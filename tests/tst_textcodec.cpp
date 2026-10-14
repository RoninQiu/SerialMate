#include "core/TextCodec.h"

#include <QTest>

// 编码层的回归测试。
//
// 最初这里三处收发路径写死 toLatin1/fromLatin1，中文被替换成 '?'，
// 比 SSCOM（隐式系统 ANSI / GBK）更差。这组用例就是把那条行为钉死，
// 防止再有人改回写死的 Latin-1。
class TestTextCodec : public QObject
{
    Q_OBJECT

private slots:
    void utf8_roundtrip_chinese();
    void utf8_encodes_chinese_as_multibyte();
    void latin1_replaces_unmappable();        // 钉住已知的有损行为
    void local_is_system_codepage();
    void latin1_decode_is_lossless();
    void ascii_identical_across_all();
    void encoding_int_roundtrip();
    void encoding_int_out_of_range_falls_back();
    void empty_input();
};

void TestTextCodec::utf8_roundtrip_chinese()
{
    const QString src = QStringLiteral("AT+温度");
    const QByteArray enc = encodeText(src, TextEncoding::Utf8);
    QCOMPARE(decodeText(enc, TextEncoding::Utf8), src);
}

void TestTextCodec::utf8_encodes_chinese_as_multibyte()
{
    // "温" 是 U+6E29，UTF-8 下占 3 字节。Latin-1 下只会剩 1 个 '?'。
    const QByteArray enc = encodeText(QStringLiteral("温"), TextEncoding::Utf8);
    QCOMPARE(enc.size(), 3);
    QCOMPARE(encodeText(QStringLiteral("温"), TextEncoding::Latin1), QByteArray("?"));
}

void TestTextCodec::latin1_replaces_unmappable()
{
    // 这是 Latin-1 的固有语义而非 bug：不可映射的字符变 '?'。
    // 真正要无损传二进制请用 HEX 发送。钉住它是为了让行为变化能被看见。
    const QByteArray enc = encodeText(QStringLiteral("温"), TextEncoding::Latin1);
    QCOMPARE(enc, QByteArray("?"));
}

void TestTextCodec::local_is_system_codepage()
{
    // Local 必须等于系统 ANSI 码页（实现就是 toLocal8Bit），而不是写死的
    // Latin-1 —— 这正是本组用例的来由，要钉住的就是它。
    //
    // 刻意不写成「中文编码后不等于 Latin-1」：那条只在系统 ANSI 是 CJK 多字节
    // 码页（CP936）时成立。CP1252 这类单字节西文码页上中文同样变成 '?'，
    // 与 Latin-1 结果一模一样，于是测试在所有非中文 Windows 上都必挂 ——
    // 等于把「开发机装的是中文系统」写进了断言，公开仓库的贡献者全都跑不了。
    const QString src = QStringLiteral("AT+温度");
    QCOMPARE(encodeText(src, TextEncoding::Local), src.toLocal8Bit());

    // 「€」是把两者分开的探针：CP1252 编成 0x80，Latin-1 只能给 '?'。
    // Windows 的 ANSI 码页没有一个是 ISO-8859-1 本身，所以它在中文机和英文机
    // 上都成立 —— 谁把 Local 改回 toLatin1()，在两边都会当场失败。
    const QString euro = QStringLiteral("€");
    QCOMPARE(encodeText(euro, TextEncoding::Local), euro.toLocal8Bit());
    QVERIFY(encodeText(euro, TextEncoding::Local) != QByteArray("?"));
}

void TestTextCodec::latin1_decode_is_lossless()
{
    // 任意 0x00-0xFF 字节都必须能原样映射回来，这是二进制流的安全兜底
    QByteArray raw;
    for (int i = 0; i < 256; ++i)
        raw.append(static_cast<char>(i));
    QCOMPARE(encodeText(decodeText(raw, TextEncoding::Latin1), TextEncoding::Latin1), raw);
}

void TestTextCodec::ascii_identical_across_all()
{
    const QString src = QStringLiteral("AT+RESET\r\n");
    const QByteArray expect = src.toLatin1();
    QCOMPARE(encodeText(src, TextEncoding::Utf8), expect);
    QCOMPARE(encodeText(src, TextEncoding::Latin1), expect);
    QCOMPARE(encodeText(src, TextEncoding::Local), expect);
}

void TestTextCodec::encoding_int_roundtrip()
{
    for (TextEncoding enc : { TextEncoding::Utf8, TextEncoding::Local, TextEncoding::Latin1 }) {
        QCOMPARE(textEncodingFromInt(textEncodingToInt(enc)), enc);
        QVERIFY(!textEncodingName(enc).isEmpty());
    }
}

void TestTextCodec::encoding_int_out_of_range_falls_back()
{
    // 配置被手改过时不能变成未知状态，一律回默认 UTF-8
    QCOMPARE(textEncodingFromInt(-1), TextEncoding::Utf8);
    QCOMPARE(textEncodingFromInt(99), TextEncoding::Utf8);
}

void TestTextCodec::empty_input()
{
    for (TextEncoding enc : { TextEncoding::Utf8, TextEncoding::Local, TextEncoding::Latin1 }) {
        QVERIFY(encodeText(QString(), enc).isEmpty());
        QVERIFY(decodeText(QByteArray(), enc).isEmpty());
    }
}

QTEST_MAIN(TestTextCodec)
#include "tst_textcodec.moc"