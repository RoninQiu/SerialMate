#include "core/DataFormatter.h"

#include <QDateTime>
#include <QStringList>

namespace {

// 返回 0..15，非法字符返回 -1
int hexDigit(QChar c)
{
    const ushort u = c.unicode();
    if (u >= '0' && u <= '9') return u - '0';
    if (u >= 'a' && u <= 'f') return u - 'a' + 10;
    if (u >= 'A' && u <= 'F') return u - 'A' + 10;
    return -1;
}

} // namespace

QByteArray DataFormatter::hexStringToBytes(const QString& text, bool* ok)
{
    if (ok)
        *ok = true;

    QByteArray out;
    bool sawDigit = false;
    int  high     = -1;   // 已收到的高位半字节，-1 表示还没有

    int i = 0;
    const int n = text.size();
    while (i < n) {
        const QChar c = text.at(i);

        if (c.isSpace()) {
            ++i;
            continue;
        }

        // 跳过 "0x" / "0X" 前缀
        if (c == QLatin1Char('0') && i + 1 < n
            && (text.at(i + 1) == QLatin1Char('x') || text.at(i + 1) == QLatin1Char('X'))) {
            i += 2;
            continue;
        }

        const int v = hexDigit(c);
        if (v < 0) {
            if (ok)
                *ok = false;
            return QByteArray();
        }
        sawDigit = true;

        if (high < 0) {
            high = v;
        } else {
            out.append(static_cast<char>((high << 4) | v));
            high = -1;
        }
        ++i;
    }

    // 奇数个半字节：结尾还挂着一个高位
    if (high >= 0) {
        if (ok)
            *ok = false;
        return QByteArray();
    }

    // 输入非空却一个数字都没读到（例如只输入了 "0x"）
    if (!sawDigit && !text.trimmed().isEmpty()) {
        if (ok)
            *ok = false;
        return QByteArray();
    }

    return out;
}

QString DataFormatter::bytesToHexString(const QByteArray& data)
{
    return QString::fromLatin1(data.toHex(' ')).toUpper();
}

QString DataFormatter::currentTimestamp()
{
    return QStringLiteral("[%1]").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")));
}

QString DataFormatter::directionMarker(Direction dir)
{
    // 必须用 QChar(码位) 显式构造，不能写 QStringLiteral("\xE2\x86\x90 ")。
    //
    // QStringLiteral 的 \x 产生的是**Latin-1 码位**，不是 UTF-8 解码：
    // 那三个字节会变成 U+00E2 U+0086 U+0090 三个字符，UTF-8 再编码出去就成了
    // c3a2 c286 c290 —— 双重编码。实测过：size 会是 4 而不是 2，界面上显示为
    // "â\u0086\u0090" 这样的乱码。\u 也不行，它同样受源文件编码影响。
    // 直接给码位是唯一没有歧义的写法。
    switch (dir) {
    case Direction::Incoming:
        return QString(QChar(0x2190)) + QLatin1Char(' ');   // ←
    case Direction::Outgoing:
        return QString(QChar(0x2192)) + QLatin1Char(' ');   // →
    case Direction::None:
        break;
    }
    return QString();
}

QString DataFormatter::stripDirectionMarkers(const QString& text)
{
    // 逐行处理而不是整段替换：markers 只可能出现在行首（formatChunk 拼在最前面），
    // 数据中间出现的 "← " 是设备自己发的内容，不能动。
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString& line : lines) {
        // 上一行若是 \r\n 结尾，这里的 \r 还挂在行尾，行首判定不受影响
        if (line.startsWith(directionMarker(Direction::Incoming)))
            line.remove(0, directionMarker(Direction::Incoming).size());
        else if (line.startsWith(directionMarker(Direction::Outgoing)))
            line.remove(0, directionMarker(Direction::Outgoing).size());
    }
    return lines.join(QLatin1Char('\n'));
}

QString DataFormatter::formatChunk(const QByteArray& data, const DisplayOptions& opt)
{
    if (data.isEmpty())
        return QString();

    const QString body = opt.hexMode ? bytesToHexString(data)
                                     : decodeText(data, opt.encoding);
    QString line = directionMarker(opt.direction);
    if (opt.timestamp)
        line += currentTimestamp() + QLatin1Char(' ');
    return line + body;
}
