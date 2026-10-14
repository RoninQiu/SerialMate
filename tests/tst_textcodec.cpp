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
    const QString src = QStringLiteral("中文");
    const QByteArray enc = encodeText(src, TextEncoding::Local);
    // 系统 ANSI 与 UTF-8 不可能恰好一致，且中文 ANSI 编码后比 Latin-1 长
    QVERIFY(enc != encodeText(src, TextEncoding::Latin1));
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