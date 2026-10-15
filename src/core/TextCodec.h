#pragma once

#include <QByteArray>
#include <QString>

// 文本与字节之间的编码选择。
//
// 背景：SSCOM 5.13.1 压根没有编码选项（它的 sscom51.ini 里 70 个键无一条与编码
// 相关），隐式走系统 ANSI 代码页 —— 中文 Windows 上就是 GBK，所以中文能收发，
// 但发不出 UTF-8。
//
// 本项目早先写死 toLatin1/fromLatin1，比 SSCOM 更糟：中文码点大于 255，
// toLatin1() 直接替换成 '?'，两个汉字变成两个问号，信息彻底丢失；
// 接收侧 fromLatin1() 则把设备发来的 GBK 逐字节映射成 U+0000..U+00FF，
// 显示为乱码。
enum class TextEncoding
{
    Utf8,      // UTF-8。默认 —— 现代设备/固件普遍用它
    Local,     // 系统 ANSI（中文 Windows = GBK），与 SSCOM 行为一致
    Latin1,    // 字节直通：每字节映射成 U+0000..U+00FF，一个字节都不丢
};

// 编码成待发送字节。Latin1 之外的编码下不可映射的字符会被替换，
// 但绝不会静默丢字节 —— 真正的二进制数据请走 HEX 发送或 Latin1。
QByteArray encodeText(const QString& text, TextEncoding enc);

// 字节解码成显示文本。Latin1 是唯一对任意字节都无损的解码方式。
QString decodeText(const QByteArray& data, TextEncoding enc);

// 末尾不构成完整字符的字节数（0..3）。
//
// 串口数据是按 40ms 批次送上来的，批次边界与字符边界毫无关系。单独解码
// 每一批时，落在边界中间的多字节字符会被拆成 U+FFFD —— UTF-8 下实测一个
// 汉字能炸成 3 个替换符，且连续流下几乎每几批就命中一次，接收窗直接不可用。
//
// 用法：把上一批留下的尾巴接到本批开头，切掉本批末尾同样不完整的部分，
// 只把中间完整的那段拿去解码，尾巴留给下一批。
//
// Latin1 恒返回 0：它逐字节映射，每个字节本身就是完整字符，不存在半个。
int incompleteTailBytes(const QByteArray& data, TextEncoding enc);

// 把 chunk 与 *partial 拼起来，切掉末尾不完整的那几个字节。
// 返回可以安全解码的完整部分；切下来的尾巴写回 *partial 留给下一批。
//
// 刻意做成 core 里的无状态入口 + 一个显式的尾巴指针，而不是把拼接逻辑埋在
// ReceiveView::onFlush 里：合并/切分是这段修复的全部风险所在，放在这里才能被
// 单测直接钉住，不必为了测它去给 UI 层开数据注入的后门。
QByteArray takeCompletePrefix(QByteArray* partial, const QByteArray& chunk, TextEncoding enc);

// 下拉框显示名与提示文案
QString textEncodingName(TextEncoding enc);
QString textEncodingTooltip(TextEncoding enc);

// 枚举 <-> 整数，用于配置持久化。越界值一律回落到默认（UTF-8）。
TextEncoding textEncodingFromInt(int value);
int         textEncodingToInt(TextEncoding enc);