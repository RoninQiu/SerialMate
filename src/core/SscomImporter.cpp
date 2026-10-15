#include "core/SscomImporter.h"

#include <QFont>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

#include <windows.h>

namespace {

// SSCOM 的 ini 里所有值前面都带一个逗号，是它自己的写法（=,COM1 / =,Y）。
// 这个逗号不是数据的一部分，读的时候要去掉。
QString stripLeadingComma(const QString& value)
{
    return value.startsWith(QLatin1Char(',')) ? value.mid(1) : value;
}

bool parseYesNo(const QString& value, bool fallback)
{
    const QString v = value.trimmed().toUpper();
    if (v == QLatin1String("Y") || v == QLatin1String("YES") || v == QLatin1String("1"))
        return true;
    if (v == QLatin1String("N") || v == QLatin1String("NO") || v == QLatin1String("0"))
        return false;
    return fallback;
}

// SSCOM 的 N 键用位数区分用途：
//   N1   .. N99    多字符串的**内容**（前缀 H=HEX / A=ASCII 串）
//   N101 .. N199   上面同一批内容的**注释**行，序号减 100 即对应第几条
//   N1000+         程序设置
// 注意 N1051 这类四位设置键不能被当成 N1 两位内容键，
// 因此判据是"数值落在哪个区间"而不是"字符串长度"。
enum class KeyKind { Content, ItemMeta, Setting, Unknown };

struct ParsedKey
{
    KeyKind kind = KeyKind::Unknown;
    int     index = 0;    // Content/ItemMeta 用的是 0 基下标
    int     id    = 0;    // Setting 用的原始键号，便于读代码时对上 ini
};

ParsedKey classify(const QString& key)
{
    ParsedKey out;
    if (!key.startsWith(QLatin1Char('N'), Qt::CaseInsensitive))
        return out;

    bool okNum = false;
    const int n = key.mid(1).toInt(&okNum);
    if (!okNum || n <= 0)
        return out;

    if (n <= 99) {
        out.kind = KeyKind::Content;
        out.index = n - 1;
    } else if (n >= 101 && n <= 199) {
        // 下界必须是 101 而不是 100：N101 才是第 1 条的注释行，
        // N100 这个键 SSCOM 根本不存在，让它落进来会算出 index = -1，
        // 往哈希里插一个负数键，永远读不到，纯属脏数据。
        out.kind = KeyKind::ItemMeta;
        out.index = n - 101;   // N101 -> 0, N199 -> 98
    } else {
        out.kind = KeyKind::Setting;
        out.id = n;
    }
    return out;
}

} // namespace

QString SscomImporter::decodeSscomIni(const QByteArray& gbkRaw)
{
    if (gbkRaw.isEmpty())
        return QString();

    const int need = MultiByteToWideChar(936, 0, gbkRaw.constData(), gbkRaw.size(), nullptr, 0);
    if (need <= 0)
        return QString();

    QString wide(static_cast<int>(need), Qt::Uninitialized);   // wchar_t 在 Windows 上是 16 位
    const int got = MultiByteToWideChar(936, 0, gbkRaw.constData(), gbkRaw.size(),
                                        reinterpret_cast<wchar_t*>(wide.data()), need);
    if (got <= 0)
        return QString();
    wide.resize(got);
    return wide;
}

SscomImportReport SscomImporter::import(const QByteArray& gbkRaw, AppConfig& target)
{
    SscomImportReport rep;

    const QString text = decodeSscomIni(gbkRaw);
    if (text.isEmpty() && !gbkRaw.isEmpty()) {
        rep.error = QStringLiteral("无法按 GBK(CP936) 解码该文件，它可能不是 SSCOM 5.13.1 的 sscom51.ini。");
        return rep;
    }
    if (text.isEmpty()) {
        rep.error = QStringLiteral("文件为空。");
        return rep;
    }

    // ---- 第一遍：按 key 收进哈希，行序无关 ----
    // SSCOM 写文件时把 N101..N199 排在前面、N1051 之后才写设置项，
    // 但 ini 的语义与行序无关，用哈希收比边读边用更不容易出错。
    QHash<QString, QString> values;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        values.insert(line.left(eq).trimmed(), line.mid(eq + 1));
    }

    // 「这是不是 SSCOM 的 ini」必须按**能认出来的 N 键**判，不能只看有没有 key=value。
    //
    // 原判据是 sawAnyKey（任意一行 key=value 就算数），而文件对话框里还开着
    // 「所有文件 (*)」——用户很容易误选本程序自己的 SerialMate.ini 或任何普通
    // ini。那种文件满是 key=value，一律被判成"导入成功"，接着多字符串那段会拿
    // 99 个空条目覆盖用户现有的配置，MainWindow 随即把它落盘：辛苦攒的 99 条
    // 就此消失，而报告里一句提示都没有。
    //
    // classify() 已经把 N 键分成 Content / ItemMeta / Setting / Unknown，
    // 直接数认出来的有几个即可。顺带让下面那句报错文案与判据对得上 ——
    // 原先文案说的是"N 开头条目"，判据却是有 key=value，两回事。
    int recognized = 0;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (classify(it.key()).kind != KeyKind::Unknown)
            ++recognized;
    }
    if (recognized == 0) {
        rep.error = QStringLiteral(
            "文件里没有可识别的 SSCOM 条目（形如 N1=...、N1080=... 的 N 键），"
            "它可能不是 SSCOM 5.13.1 的 sscom51.ini。本次未改动任何配置。");
        return rep;
    }

    auto take = [&values](int id) -> QString {
        return values.value(QStringLiteral("N%1").arg(id));
    };
    auto has = [&values](int id) {
        return values.contains(QStringLiteral("N%1").arg(id));
    };

    // ---- 串口 ----
    // SSCOM 只持久化串口号与波特率。数据位/校验位/停止位/流控这四项它压根不存
    // （sscom51.ini 70 个键里无一条相关），原版写死 8N1 无流控 —— 实测运行原版
    // 状态栏显示 "115200bps,8,1,None,None" 可证。这里刻意**不读不写**，
    // 保持 SerialMate 自己的默认值，因为那也正好是 8N1 无流控，行为一致。
    rep.notes.append(QStringLiteral(
        "串口的【数据位/校验位/停止位/流控】未导入：SSCOM 本身就不保存这四项，"
        "原版固定为 8 数据位 / 无校验 / 1 停止位 / 无流控，与本程序默认值一致。"));

    if (has(1080)) {
        const QString port = stripLeadingComma(take(1080)).trimmed();
        if (!port.isEmpty())
            target.serial.portName = port;
    }
    if (has(1081)) {
        bool ok = false;
        const int baud = stripLeadingComma(take(1081)).trimmed().toInt(&ok);
        if (ok && baud > 0)
            target.serial.baudRate = baud;
    }
    if (has(1061))
        target.serial.dtr = parseYesNo(stripLeadingComma(take(1061)), target.serial.dtr);
    if (has(1062))
        target.serial.rts = parseYesNo(stripLeadingComma(take(1062)), target.serial.rts);

    // ---- 发送主面板 ----
    // 关键：SSCOM 只有一个主发送框，N1053(ASC) 与 N1054(HEX) 是**同一个框的两份
    // 草稿**，由 N1055(发送方式 A/H) 决定当前用哪一份。
    // 这不是看文档猜的 —— 实跑原版点「HEX发送」复选框，框内文本当场从
    // "abcdefg"(N1053) 变成 "12 FF 00 30"(N1054)；关闭程序后逐键 diff，
    // 只有 N1055 由 A 变 H，两份草稿都原样保留。
    const QString sendMode = stripLeadingComma(take(1055)).trimmed().toUpper();
    const bool useHexDraft = (sendMode == QLatin1String("H"));
    const QString primary = stripLeadingComma(take(useHexDraft ? 1054 : 1053));
    if (useHexDraft || has(1053)) {
        target.sendText = primary;
        target.sendHexMode = useHexDraft;
    }
    if (useHexDraft && has(1053)) {
        rep.notes.append(QStringLiteral(
            "主发送框当前是 HEX 模式，已按 SSCOM 的做法只取 HEX 草稿；"
            "ASC 草稿（abcdefg 一类）在 SerialMate 里没有对应物，未导入。"));
    }
    if (has(1056)) {
        bool ok = false;
        const int ms = stripLeadingComma(take(1056)).trimmed().toInt(&ok);
        if (ok && ms >= 1)
            target.sendIntervalMs = ms;
    }
    if (has(1057))
        target.sendAppendCrlf = parseYesNo(stripLeadingComma(take(1057)), target.sendAppendCrlf);

    // ---- 接收 ----
    if (has(1059))
        target.recvHex = parseYesNo(stripLeadingComma(take(1059)), target.recvHex);
    if (has(1067)) {
        bool ok = false;
        const qint64 limit = stripLeadingComma(take(1067)).trimmed().toLongLong(&ok);
        if (ok && limit > 0)
            target.recvBufferLimit = limit;
    }

    // SSCOM 的时间戳与分包显示是**同一个复选框**「加时间戳和分包显示」，
    // ini 里也只有 N1064 一个键，没有独立的时间戳开关。因此时间戳无法从 ini
    // 还原，只能保持本程序自己的默认值。
    rep.notes.append(QStringLiteral(
        "接收区的【时间戳】未导入：SSCOM 的「加时间戳」与「分包显示」是同一个复选框，"
        "配置文件里只存了分包显示，没有独立的时间戳设置。"));

    // ---- 字体 ----
    // SSCOM 的 N1088/N1089 是**主窗口**字体，但它只作用于接收区（发送区是固定的），
    // 所以映射到本程序的接收区字体在效果上是对的。
    const QString fontFamily = stripLeadingComma(take(1088)).trimmed();
    const QString fontSizeStr = stripLeadingComma(take(1089)).trimmed();
    if (!fontFamily.isEmpty() || !fontSizeStr.isEmpty()) {
        int size = 9;
        if (!fontSizeStr.isEmpty()) {
            bool ok = false;
            const int s = fontSizeStr.toInt(&ok);
            if (ok && s >= 4 && s <= 72)
                size = s;
        }
        // 不能写成 QFont(cond ? QFont() : QFont(family), size)：三元表达式两边
        // 都是 QFont，会匹配到 QFont(const QFont&, const QPaintDevice*) 重载，
        // 把字号当成绘制设备指针传进去，编译直接报错。
        QFont font;
        if (!fontFamily.isEmpty())
            font.setFamily(fontFamily);
        font.setPointSize(size);
        target.recvFont = font.toString();
    }

    // ---- 多字符串 ----
    QList<SendItem> items;
    items.reserve(kSendItemCount);
    int seenContent = 0;
    int dropped = 0;

    // 先扫内容键（N1..N99）确定"这个用户到底用到了第几条"。
    // 不能按 1..99 一路补空 —— SSCOM 默认会预写满 99 条，
    // 全搬过来会让 SerialMate 里凭空多出 90+ 行空白。
    QHash<int, QString> contents;   // 0 基下标 -> "H,13 00 FF 88"
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const ParsedKey pk = classify(it.key());
        if (pk.kind == KeyKind::Content && pk.index < kSendItemCount)
            contents.insert(pk.index, it.value());
    }

    // 有内容的行按行号升序，紧凑地排进前若干条
    QList<int> ordered;
    ordered.reserve(contents.size());
    for (auto it = contents.constBegin(); it != contents.constEnd(); ++it)
        ordered.append(it.key());
    std::sort(ordered.begin(), ordered.end());

    QHash<int, QString> names;      // 0 基下标 -> 注释
    QHash<int, int> intervals;      // 0 基下标 -> 每条发送间隔(ms)
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const ParsedKey pk = classify(it.key());
        if (pk.kind == KeyKind::ItemMeta) {
            // N101=1,十六进制数据串1,1000 —— 去掉前导逗号后按逗号分段。
            // 第二段是注释；第三段是**这一条发完后的间隔(ms)**。
            // 第三段的语义来自实测：一份长期使用的真实配置里，绝大多数条目
            //   取默认值，少数明显更慢的（校准/维护类指令）取更大的值，
            //   个别高频轮询指令取更小的值 —— 取值与条目用途语义自洽，
            //   不是别的含义。
            // 第一段(1/3/2)的含义 ini 头部没有说明，本程序也不需要，不取。
            const QString v = stripLeadingComma(it.value());
            const QStringList parts = v.split(QLatin1Char(','));
            if (parts.size() >= 2)
                names.insert(pk.index, parts.at(1).trimmed());
            if (parts.size() >= 3) {
                bool ok = false;
                const int ms = parts.at(2).trimmed().toInt(&ok);
                if (ok && ms >= kMinItemIntervalMs && ms <= kMaxItemIntervalMs)
                    intervals.insert(pk.index, ms);
            }
        }
    }

    for (int srcRow : ordered) {
        if (items.size() >= kSendItemCount) {
            ++dropped;
            continue;
        }
        const QString raw = stripLeadingComma(contents.value(srcRow));
        const int comma = raw.indexOf(QLatin1Char(','));
        const QString type = (comma >= 0 ? raw.left(comma) : raw).trimmed().toUpper();
        const QString body = (comma >= 0 ? raw.mid(comma + 1) : QString()).trimmed();

        SendItem item;
        item.hexMode = (type == QLatin1String("H"));
        item.content = body;
        item.name = names.value(srcRow);
        item.intervalMs = intervals.value(srcRow, kDefaultItemIntervalMs);
        // SSCOM 的 N1xx 首段疑似"使能"标记，但取值分布（仅前三条非零）不足以
        // 定论，ini 头部也没解释。这里不猜，一律保持未勾选，让用户自己勾 ——
        // 猜错的后果是用户以为在循环发送、实际一条都没发。
        item.enabled = false;
        items.append(item);
        ++seenContent;
    }
    // 一条多字符串都没认出来时**不碰**用户现有的 99 条。
    // 认出来的 N 键可能只是别的设置项（N1080 之类），多字符串那部分对不上号；
    // 用 99 个空条目盖上去，用户攒的内容当场消失且无法找回。
    if (seenContent > 0) {
        while (items.size() < kSendItemCount)
            items.append(SendItem());
        target.sendItems = items;
        rep.itemsImported = seenContent;
        rep.itemsDropped  = dropped;
        if (dropped > 0)
            rep.notes.append(QStringLiteral("有 %1 条多字符串超出 99 条上限，未导入。").arg(dropped));
    } else {
        rep.notes.append(QStringLiteral(
            "文件里没有可识别的多字符串条目（N1..N99），现有 99 条保持原样、未被覆盖。"));
    }
    if (seenContent > 0) {
        rep.notes.append(QStringLiteral(
            "多字符串的【使能】未导入：SSCOM 配置文件里该列的取值含义无法确定，"
            "导入后全部为未勾选，请按需自行勾选。"));
    }

    // ---- 编码：唯一被无条件改写的项 ----
    // SSCOM 没有编码选项，中文 Windows 上隐式走系统 ANSI(GBK)。它 ini 里存下来的
    // 中文字符串本来就是 GBK 编码的，若沿用本程序默认的 UTF-8 再发出去，
    // 设备收到的字节与在 SSCOM 下完全不同 —— 表现为"迁移后中文全乱码"。
    // 所以导入时必须显式切到 Local 保住原行为，用户要 UTF-8 可随时在界面上改。
    target.sendEncoding = TextEncoding::Local;
    target.recvEncoding = TextEncoding::Local;
    rep.notes.append(QStringLiteral(
        "收发编码已设为【系统 ANSI(GBK)】以匹配 SSCOM 的行为。若你的设备用的是 "
        "UTF-8，请在接收区与发送区的编码下拉里手动切换。"));

    // ---- 刻意不迁的项 ----
    rep.notes.append(QStringLiteral(
        "以下 SSCOM 功能本程序没有对应物，未导入：文件发送、校验方式与校验位置、"
        "分包显示与分包超时、接收区背景色、网络串口(TCP/UDP)、按键立即发送、"
        "终端仿真、发送区显隐、窗口尺寸与位置、语言中英切换。"));
    rep.notes.append(QStringLiteral(
        "多条字符串发送间隔(N1058)未导入：它指「把多条字符串一次性依次发出」时的间隔，"
        "与本程序【循环发送】不是一回事，故不套用 —— 循环时请逐条设置「间隔」列。"));

    rep.ok = true;
    return rep;
}