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