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

    // 分块解码：串口按 40ms 批次送数据，批次边界与字符边界无关
    void utf8_tail_keeps_only_incomplete_sequences();
    void utf8_tail_is_zero_for_complete_text();
    void gbk_tail_keeps_a_lone_lead_byte();
    void latin1_never_has_a_tail();
    void take_prefix_carries_the_tail_over();
    void take_prefix_is_transparent_for_latin1();
    void split_stream_reassembles_to_the_original_text();
};

void TestTextCodec::utf8_tail_keeps_only_incomplete_sequences()
{
    // 「温」= E6 B8 A9。从末尾往前找最多 3 步找首字节，按首字节算出该序列有多长。
    const QByteArray wen = QStringLiteral("温").toUtf8();
    QCOMPARE(wen.size(), 3);

    QCOMPARE(incompleteTailBytes(wen.left(1), TextEncoding::Utf8), 1);
    QCOMPARE(incompleteTailBytes(wen.left(2), TextEncoding::Utf8), 2);
    QCOMPARE(incompleteTailBytes(wen, TextEncoding::Utf8), 0);

    // 「😀」= F0 9F 98 80，4 字节序列。前导字节后面凑不满就不算完整。
    const QByteArray emoji = QStringLiteral("😀").toUtf8();
    QCOMPARE(emoji.size(), 4);
    QCOMPARE(incompleteTailBytes(emoji.left(1), TextEncoding::Utf8), 1);
    QCOMPARE(incompleteTailBytes(emoji.left(2), TextEncoding::Utf8), 2);
    QCOMPARE(incompleteTailBytes(emoji.left(3), TextEncoding::Utf8), 3);
    QCOMPARE(incompleteTailBytes(emoji, TextEncoding::Utf8), 0);

    // 尾部是 ASCII 时前面一定都完整（ASCII 前面不可能是半个多字节序列）
    QCOMPARE(incompleteTailBytes(QByteArray("温A"), TextEncoding::Utf8), 0);
    QCOMPARE(incompleteTailBytes(QByteArray("A"), TextEncoding::Utf8), 0);

    // 孤立的续字节本来就非法，该当场变成 U+FFFD，不该被当成"还没收完"留下来
    QCOMPARE(incompleteTailBytes(QByteArray::fromHex("E6B8"), TextEncoding::Utf8), 2);
    QCOMPARE(incompleteTailBytes(QByteArray::fromHex("80"), TextEncoding::Utf8), 0);
    QCOMPARE(incompleteTailBytes(QByteArray(), TextEncoding::Utf8), 0);
}

void TestTextCodec::utf8_tail_is_zero_for_complete_text()
{
    const QByteArray whole = QStringLiteral("温度正常\r\n").toUtf8();
    QCOMPARE(incompleteTailBytes(whole, TextEncoding::Utf8), 0);
}

void TestTextCodec::gbk_tail_keeps_a_lone_lead_byte()
{
    // 刻意用硬编码的 GBK 字节而不是 toLocal8Bit()：
    // 后者取的是 Qt 的 C locale 代码页，在非中文 Windows 上与系统 ACP 不一致，
    // 写进用例就会变成"本机过、CI 跳过"，等于没测。这里测的是纯字节分析，
    // 不该跟运行环境绑定。「温」在 GBK 里是 CE C2。
    const QByteArray full = QByteArray::fromHex("CEC2");

    QCOMPARE(incompleteTailBytes(full, TextEncoding::Local), 0);
    QCOMPARE(incompleteTailBytes(full.left(1), TextEncoding::Local), 1);   // 只有前导字节
    QCOMPARE(incompleteTailBytes(QByteArray::fromHex("41"), TextEncoding::Local), 0);
    QCOMPARE(incompleteTailBytes(QByteArray::fromHex("CEC241"), TextEncoding::Local), 0);  // 温A
    QCOMPARE(incompleteTailBytes(QByteArray(), TextEncoding::Local), 0);

    // 关键用例：CE C2 CE = 「温」+ 半个「温」。前导字节与尾字节在 0x81..0xFE
    // 上重叠，从末尾倒推会误判成"最后两个已成对、尾巴为 0"；正向扫才认得出
    // 必须切出 1 字节。这条用例是 GBK 分支必须正向扫描的理由。
    QCOMPARE(incompleteTailBytes(QByteArray::fromHex("CEC2CE"), TextEncoding::Local), 1);

    // 连续三个汉字完整时不该留尾巴
    QCOMPARE(incompleteTailBytes(QByteArray::fromHex("CEC2D6C8B0A1"), TextEncoding::Local), 0);
}

void TestTextCodec::latin1_never_has_a_tail()
{
    // Latin1 逐字节映射，每个字节本身就是完整字符，不存在半个。
    QCOMPARE(incompleteTailBytes(QByteArray::fromHex("5A"), TextEncoding::Latin1), 0);
    QCOMPARE(incompleteTailBytes(QByteArray::fromHex("E6B8A9"), TextEncoding::Latin1), 0);
}

void TestTextCodec::take_prefix_carries_the_tail_over()
{
    // onFlush 真正走的那条路径：合并 -> 切尾 -> 返回完整段 + 写回尾巴。
    const QByteArray bytes = QStringLiteral("温A").toUtf8();   // E6 B8 A9 41
    QByteArray partial;

    QCOMPARE(takeCompletePrefix(&partial, bytes.left(2), TextEncoding::Utf8), QByteArray());
    QCOMPARE(partial, bytes.left(2));

    QCOMPARE(takeCompletePrefix(&partial, bytes.mid(2), TextEncoding::Utf8), bytes);
    QCOMPARE(partial.size(), 0);

    // 尾巴指针为 nullptr 时退化成原样返回，不崩
    QCOMPARE(takeCompletePrefix(nullptr, bytes, TextEncoding::Utf8), bytes);
}

void TestTextCodec::take_prefix_is_transparent_for_latin1()
{
    QByteArray partial;
    const QByteArray raw = QByteArray::fromHex("E6B8A9FF00");
    QCOMPARE(takeCompletePrefix(&partial, raw, TextEncoding::Latin1), raw);
    QCOMPARE(partial.size(), 0);
}

void TestTextCodec::split_stream_reassembles_to_the_original_text()
{
    // 这条钉住的是 2026-10-15 评审发现的缺陷：每批单独解码会让跨批的汉字
    // 炸成 U+FFFD。模拟 40ms 批次把 UTF-8 流切成 7 字节一段。
    const QString src = QStringLiteral("AT+温度正常，风速 3.2m/s。");
    const QByteArray bytes = src.toUtf8();

    QString rebuilt;
    QByteArray partial;
    for (qsizetype i = 0; i < bytes.size(); i += 7)
        rebuilt += decodeText(takeCompletePrefix(&partial, bytes.mid(i, 7), TextEncoding::Utf8),
                              TextEncoding::Utf8);
    // 末尾若还有残留，说明末尾本来就不是完整字符；本次输入应以完整字符收尾
    QCOMPARE(partial.size(), 0);
    QCOMPARE(rebuilt, src);

    // 不做重组、逐批直接解码，就是修复前的行为 —— 用来证明上面确实修好了什么
    QString naive;
    for (qsizetype i = 0; i < bytes.size(); i += 7)
        naive += decodeText(bytes.mid(i, 7), TextEncoding::Utf8);
    QVERIFY2(naive != src, "这段输入劈不出替换符，换一段更长的再试；测试本身失去意义");
    QVERIFY(naive.contains(QChar(0xFFFD)));
}

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