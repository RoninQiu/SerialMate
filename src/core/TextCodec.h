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

// 下拉框显示名与提示文案
QString textEncodingName(TextEncoding enc);
QString textEncodingTooltip(TextEncoding enc);

// 枚举 <-> 整数，用于配置持久化。越界值一律回落到默认（UTF-8）。
TextEncoding textEncodingFromInt(int value);
int         textEncodingToInt(TextEncoding enc);