#pragma once

#include "core/TextCodec.h"

#include <QByteArray>
#include <QString>

// 全静态、无状态、不依赖 QApplication，因此可脱离 GUI 做单元测试。
class DataFormatter
{
public:
    // 收发方向。显示在每一行行首，用来区分「设备回的」和「自己发的」。
    //
    // SSCOM 5.13.1 **没有**这个概念 —— 它的 sscom51.ini 里 45 项配置逐条看过，
    // 与显示相关的只有 HEX 显示 / 分包显示 / 接收窗背景色 / 回显，没有任何方向标记。
    // 「显示发送」勾上之后，SSCOM 把发送内容原样塞进接收区，与接收内容长得一模一样。
    // 所以这是超越原版的增强，不是对齐。
    enum class Direction {
        None,      // 不加标记（保持原版观感）
        Incoming,  // 设备 -> 本机，行首 ←
        Outgoing,  // 本机 -> 设备，行首 →
    };

    struct DisplayOptions
    {
        bool hexMode   = false;   // 对应接收区 HEX 复选框
        bool timestamp = false;   // 对应时间戳复选框
        // 接收解码方式。HEX 模式下不参与渲染，只在文本模式下生效。
        TextEncoding encoding = TextEncoding::Utf8;
        // 行首方向标记。None 时输出与旧版本逐字节相同。
        Direction direction = Direction::None;
    };

    // 把用户输入的十六进制字符串解析为字节。
    // 空白字符一律忽略，"0x"/"0X" 前缀一律跳过，
    // 因此 "01 02 0D 0A" / "01020D0A" / "01 02 0d 0a" / "0x010x02" 结果相同。
    // 半字节数为奇数、或出现非十六进制字符、或输入非空却一个数字都没有时，
    // 返回空 QByteArray 并把 *ok 置为 false（ok 为 nullptr 时静默失败）。
    static QByteArray hexStringToBytes(const QString& text, bool* ok = nullptr);

    // 反向：大写、空格分隔，每字节两位。QByteArray{0xDE,0xAD} -> "DE AD"
    static QString bytesToHexString(const QByteArray& data);

    // 当前时间，形如 "[14:23:05.123]"
    static QString currentTimestamp();

    // 按选项把一批原始数据渲染为可 append 到接收区的文本。
    // 方向标记排在最前，其次是时间戳，最后是数据本体：
    //     "→ [14:23:05.123] DE AD BE EF"
    static QString formatChunk(const QByteArray& data, const DisplayOptions& opt);

    // 方向标记的文本形式，Direction::None 返回空串。
    static QString directionMarker(Direction dir);

    // 逐行去掉行首的方向标记，用于「保存时只要干净数据」。
    // 判据是「行首正好是标记 + 一个空格」，因此设备自己发来的数据若恰好以
    // "→ " 开头，也会被一并去掉 —— 这个取舍是有意的，见 README。
    static QString stripDirectionMarkers(const QString& text);
};
