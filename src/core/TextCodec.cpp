#include "core/TextCodec.h"

QByteArray encodeText(const QString& text, TextEncoding enc)
{
    switch (enc) {
    case TextEncoding::Utf8:
        return text.toUtf8();
    case TextEncoding::Local:
        // 中文 Windows 上就是 GBK，与 SSCOM 的隐式行为一致
        return text.toLocal8Bit();
    case TextEncoding::Latin1:
        break;
    }
    // 注意：Latin1 下不可映射的字符会被替换成 '?'。这是 Latin1 的固有语义，
    // 不是 bug —— 要无损传二进制请用 HEX 发送。
    return text.toLatin1();
}

QString decodeText(const QByteArray& data, TextEncoding enc)
{
    switch (enc) {
    case TextEncoding::Utf8:
        return QString::fromUtf8(data);
    case TextEncoding::Local:
        return QString::fromLocal8Bit(data);
    case TextEncoding::Latin1:
        break;
    }
    // Latin1 对任意字节都无损，是二进��流的安全兜底
    return QString::fromLatin1(data);
}

namespace {

// UTF-8 的前导字节与续字节。
//
// 序列长度由首字节决定：110xxxxx 是 2 字节、1110xxxx 是 3 字节、11110xxx 是 4 字节。
// 所以只要从末尾往前找最多 3 步找到那个首字节，就算出这个序列该有多长。
int incompleteUtf8Tail(const QByteArray& d)
{
    const int n = int(d.size());
    for (int back = 1; back <= 3 && back <= n; ++back) {
        const unsigned char c = static_cast<unsigned char>(d.at(n - back));
        if (c < 0x80)
            return 0;                     // 落在 ASCII 上，前面的部分一定是完整的
        if (c < 0xC0)
            continue;                     // 10xxxxxx 是续字节，继续往前找首字节
        const int need = (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        return (back < need) ? back : 0;  // 首字节后面凑不够，剩下这些留给下一批
    }
    // 全是孤立的续字节：它们本来就非法（该变成 U+FFFD），不是"没收到完"，不保留。
    return 0;
}

// GBK（中文 Windows 的系统 ANSI 代码页）的前导字节与尾字节。
//
// 字符要么 1 字节（ASCII），要么 2 字节（前导 0x81..0xFE + 尾字节 0x40..0x7E / 0x80..0xFE）。
//
// 必须**正向**扫描，不能从末尾倒推：0x81..0xFE 既是合法前驱也是合法尾字节，
// [CE][C2][CE] 单看末尾是"成对完整"还是"温 + 半个"根本分不出来
//（前者把 CE C2 当一个字符、剩 CE；后者把 C2 CE 当一个字符、剩 CE ——
//  两种读法都合法）。正向扫没有这个问题：调用方保证 d[0] 一定是字符边界
//（每轮只切掉完整的前缀、尾巴带到下一轮），从 0 步进唯一确定边界在哪。
int incompleteGbkTail(const QByteArray& d)
{
    const int n = int(d.size());
    int i = 0;
    while (i < n) {
        if (static_cast<unsigned char>(d.at(i)) < 0x81)
            ++i;        // ASCII 单字节字符
        else
            i += 2;      // 双字节字符，可能越过末尾 —— 那就是尾巴
    }
    return (i > n) ? (i - n) : 0;
}

} // namespace

int incompleteTailBytes(const QByteArray& data, TextEncoding enc)
{
    switch (enc) {
    case TextEncoding::Utf8:
        return incompleteUtf8Tail(data);
    case TextEncoding::Local:
        return incompleteGbkTail(data);
    case TextEncoding::Latin1:
        break;
    }
    return 0;
}

QByteArray takeCompletePrefix(QByteArray* partial, const QByteArray& chunk, TextEncoding enc)
{
    if (!partial)
        return chunk;

    QByteArray merged = *partial;
    merged.append(chunk);
    partial->clear();

    const qsizetype tail = incompleteTailBytes(merged, enc);
    if (tail <= 0)
        return merged;

    const qsizetype cut = merged.size() - tail;
    *partial = merged.mid(cut);
    return merged.left(cut);
}

QString textEncodingName(TextEncoding enc)
{
    switch (enc) {
    case TextEncoding::Utf8:   return QStringLiteral("UTF-8");
    case TextEncoding::Local:  return QStringLiteral("系统ANSI");
    case TextEncoding::Latin1: return QStringLiteral("Latin-1");
    }
    return QStringLiteral("UTF-8");
}

QString textEncodingTooltip(TextEncoding enc)
{
    switch (enc) {
    case TextEncoding::Utf8:
        return QStringLiteral("UTF-8，现代设备与固件的常见编码");
    case TextEncoding::Local:
        return QStringLiteral("系统 ANSI 代码页（中文 Windows 上是 GBK），SSCOM 用的就是这个");
    case TextEncoding::Latin1:
        return QStringLiteral("逐字节映射，不丢字节，适合二进制流查看");
    }
    return QString();
}

TextEncoding textEncodingFromInt(int value)
{
    switch (value) {
    case 1:  return TextEncoding::Local;
    case 2:  return TextEncoding::Latin1;
    default: break;
    }
    // 配置可能被手改过，越界一律回默认
    return TextEncoding::Utf8;
}

int textEncodingToInt(TextEncoding enc)
{
    switch (enc) {
    case TextEncoding::Local:  return 1;
    case TextEncoding::Latin1: return 2;
    case TextEncoding::Utf8:   break;
    }
    return 0;
}