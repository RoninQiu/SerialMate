#include "core/ConfigManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

namespace {

const char* kConfigFileName = "SerialMate.ini";

const int kMinInterval = 1;
const int kMaxInterval = 60000;

// exe 同目录的 ini。
//
// QCoreApplication::applicationDirPath() 在没有 QCoreApplication 实例时会
// 返回空串（并打一条 qWarning）。测试里直接调 load() 而不建实例就会踩到，
// 所以这里显式退回当前工作目录，而不是把空路径交给 QSettings ——
// QSettings 收到空文件名会静默落到 NativeFormat，注册表污染又回来了。
QString resolveConfigDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty())
        return appDir;
    return QDir::currentPath();
}

QString resolveConfigPath()
{
    return QDir(resolveConfigDir()).absoluteFilePath(QLatin1String(kConfigFileName));
}

QSettings openSettings()
{
    // 显式传 IniFormat：QSettings(QString) 这个构造函数在文件名以 .ini 结尾时
    // 会自动选 IniFormat，但依赖文件名约定太脆，格式直接写死更清楚。
    return QSettings(resolveConfigPath(), QSettings::IniFormat);
}

// QSettings 只有 status() 枚举，没有 errorString()。这里把枚举翻成人话，
// 否则弹窗里只能甩一个 "AccessError"，用户看不懂。
QString describeStatus(QSettings::Status st)
{
    switch (st) {
    case QSettings::NoError:
        return QString();
    case QSettings::AccessError:
        return QStringLiteral("无法打开配置文件（目录不存在或无写权限）");
    case QSettings::FormatError:
        return QStringLiteral("配置文件格式损坏，已按默认值重新生成");
    }
    // 不逐值枚举完：Qt 各版本的 QSettings::Status 成员并不一致
    // （6.12 的 mingw 头文件里就没有 IniFormatError），多写的分支会直接编译失败。
    // 交给兜底文案即可。
    return QStringLiteral("未知错误");
}

int clampInterval(int v, int fallback)
{
    if (v < kMinInterval || v > kMaxInterval)
        return fallback;
    return v;
}

// 单条间隔有更大的上界（10 分钟），不共用上面那个。
// 顺带在这里兜住 0：SendQueueManager 里 qBound 的下界也是 1，
// 两处保持一致，免得"配置里写着 0，界面显示 0，实际发出去间隔却是 1ms"。
int clampItemInterval(int v)
{
    if (v < kMinItemIntervalMs || v > kMaxItemIntervalMs)
        return kDefaultItemIntervalMs;
    return v;
}

// 手改配置文件后可能写入越界枚举值，逐项校验并回落默认值。
//
// 逐值枚举和区间校验都能起到校验作用，但只有前者对「枚举取值不连续」免疫：
// 区间校验成立的前提，是合法枚举值恰好铺满 [最小值, 最大值]。
//
// 实测 Qt 6.5.3 qserialport.h 的取值：
//   DataBits    = 5,6,7,8                                                连续
//   StopBits    OneStop=1, TwoStop=2，另有 OneAndHalfStop=3               非连续；有意只支持前两个
//   FlowControl NoFlowControl=0, HardwareControl=1, SoftwareControl=2    连续，区间校验成立
//   Parity      NoParity=0, EvenParity=2, OddParity=3, SpaceParity=4, MarkParity=5
//               1 号位是 Qt4 时代 UndefinedParity 留下的空洞，非连续，只能逐值枚举
//
// 若将来升级 Qt 改动了这些枚举的取值或顺序，下面唯一一处区间校验（validFlowControl）
// 必须改回逐值枚举。
QSerialPort::DataBits   validDataBits(int v)
{
    return (v == QSerialPort::Data5 || v == QSerialPort::Data6 ||
            v == QSerialPort::Data7 || v == QSerialPort::Data8)
               ? static_cast<QSerialPort::DataBits>(v)
               : QSerialPort::Data8;
}

QSerialPort::Parity validParity(int v)
{
    // Qt 6.5 的 Parity 取值并不连续：NoParity=0, EvenParity=2, OddParity=3,
    // SpaceParity=4, MarkParity=5。1 号位是 Qt4 时代 UndefinedParity 留下的空洞，
    // 因此不能用区间校验 —— 那样会让 1 混过去并被 Qt 当成 EvenParity，静默变偶校验。
    switch (v) {
    case QSerialPort::NoParity:
    case QSerialPort::EvenParity:
    case QSerialPort::OddParity:
    case QSerialPort::SpaceParity:
    case QSerialPort::MarkParity:
        return static_cast<QSerialPort::Parity>(v);
    default:
        return QSerialPort::NoParity;
    }
}

QSerialPort::StopBits validStopBits(int v)
{
    // Qt 6.5 的 StopBits 取值为 OneStop=1, TwoStop=2, OneAndHalfStop=3。
    // 这里有意不支持 1.5 停止位：需求 §5.1 的停止位下拉只有 1 和 2，
    // SSCOM 也同样只提供这两项，因此 3 落回 OneStop。
    return (v == QSerialPort::OneStop || v == QSerialPort::TwoStop)
               ? static_cast<QSerialPort::StopBits>(v)
               : QSerialPort::OneStop;
}

QSerialPort::FlowControl validFlowControl(int v)
{
    // Qt 6.5 的 FlowControl 取值是连续的 NoFlowControl=0, HardwareControl=1,
    // SoftwareControl=2，区间校验成立。
    return (v >= QSerialPort::NoFlowControl && v <= QSerialPort::SoftwareControl)
               ? static_cast<QSerialPort::FlowControl>(v)
               : QSerialPort::NoFlowControl;
}

} // namespace

QString ConfigManager::configFilePath()
{
    return resolveConfigPath();
}

bool ConfigManager::checkWritable(QString* error)
{
    const QString path = resolveConfigPath();

    // 用「真正打开写」来判断，而不是 QFileInfo::isWritable() ——
    // 后者只看目录属性，对 U 盘只读、组策略锁定、企业环境重定向的目录
    // 判断并不可靠。打开一次比看属性诚实。
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        if (error) {
            *error = QStringLiteral("配置文件无法写入：%1\n原因：%2")
                         .arg(path, f.errorString());
        }
        return false;
    }
    f.close();
    return true;
}

AppConfig ConfigManager::load()
{
    QSettings s = openSettings();
    AppConfig cfg;

    cfg.serial.portName    = s.value(QStringLiteral("serial/port")).toString();
    if (cfg.serial.portName.isEmpty())
        cfg.serial.portName = QStringLiteral("COM1");
    cfg.serial.baudRate    = s.value(QStringLiteral("serial/baud"), 115200).toInt();
    if (cfg.serial.baudRate <= 0)
        cfg.serial.baudRate = 115200;
    cfg.serial.dataBits    = validDataBits(s.value(QStringLiteral("serial/dataBits")).toInt());
    cfg.serial.parity      = validParity(s.value(QStringLiteral("serial/parity")).toInt());
    cfg.serial.stopBits    = validStopBits(s.value(QStringLiteral("serial/stopBits")).toInt());
    cfg.serial.flowControl = validFlowControl(s.value(QStringLiteral("serial/flow")).toInt());
    cfg.serial.dtr         = s.value(QStringLiteral("serial/dtr"), true).toBool();
    cfg.serial.rts         = s.value(QStringLiteral("serial/rts"), true).toBool();

    const int stored = s.beginReadArray(QStringLiteral("sendItems"));
    QList<SendItem> items;
    items.reserve(kSendItemCount);
    for (int i = 0; i < kSendItemCount; ++i) {
        SendItem it;
        if (i < stored) {
            // beginReadArray 只把数组索引置 0 一次。循环内不逐轮 setArrayIndex，
            // 32 行会全部读成第 0 行，且不报错不崩溃。
            s.setArrayIndex(i);
            it.enabled = s.value(QStringLiteral("enabled")).toBool();
            it.hexMode = s.value(QStringLiteral("hexMode")).toBool();
            it.name    = s.value(QStringLiteral("name")).toString();
            it.intervalMs = clampItemInterval(
                s.value(QStringLiteral("intervalMs"), kDefaultItemIntervalMs).toInt());
            it.content = s.value(QStringLiteral("content")).toString();
        }
        items.append(it);
    }
    s.endArray();
    cfg.sendItems = items;

    cfg.sendText      = s.value(QStringLiteral("send/text")).toString();
    cfg.sendAppendCrlf= s.value(QStringLiteral("send/appendCrlf"), false).toBool();
    cfg.sendHexMode   = s.value(QStringLiteral("send/hexMode"), false).toBool();
    cfg.sendIntervalMs= clampInterval(s.value(QStringLiteral("send/intervalMs"), 1000).toInt(), 1000);
    cfg.sendEncoding  = textEncodingFromInt(
        s.value(QStringLiteral("send/encoding"),
                textEncodingToInt(TextEncoding::Utf8)).toInt());

    cfg.loopEnabled    = s.value(QStringLiteral("loop/enabled"), false).toBool();
    cfg.loopIntervalMs = clampInterval(s.value(QStringLiteral("loop/intervalMs"), 100).toInt(), 100);

    cfg.recvHex       = s.value(QStringLiteral("recv/hex"), false).toBool();
    cfg.recvTimestamp = s.value(QStringLiteral("recv/timestamp"), false).toBool();
    cfg.recvWrap      = s.value(QStringLiteral("recv/wrap"), true).toBool();
    cfg.recvShowSent  = s.value(QStringLiteral("recv/showSent"), false).toBool();
    cfg.recvFont      = s.value(QStringLiteral("recv/font")).toString();
    cfg.recvEncoding  = textEncodingFromInt(
        s.value(QStringLiteral("recv/encoding"),
                textEncodingToInt(TextEncoding::Utf8)).toInt());
    cfg.recvBufferLimit = s.value(QStringLiteral("recv/bufferLimit"),
                                  static_cast<qint64>(kDefaultReceiveLimit)).toLongLong();
    // 上限必须是正数。配置被手改过时这里兜底，否则 ReceiveView 会拿到
    // 0 或负数，把整个接收区清空还没法再长回来。
    if (cfg.recvBufferLimit <= 0)
        cfg.recvBufferLimit = kDefaultReceiveLimit;

    cfg.geometry = s.value(QStringLiteral("window/geometry")).toByteArray();
    cfg.state    = s.value(QStringLiteral("window/state")).toByteArray();
    // 默认 0 而不是 kLayoutVersion：读不到版本号就意味着这是没有版本号的老配置，
    // 按"结构不匹配"处理，MainWindow 会跳过 state 恢复。写成 kLayoutVersion 会
    // 把老配置当成新结构，反而把面板恢复成错的样子。
    cfg.layoutVersion = s.value(QStringLiteral("window/layoutVersion"), 0).toInt();

    return cfg;
}

void ConfigManager::save(const AppConfig& cfg)
{
    saveChecked(cfg, nullptr);
}

bool ConfigManager::saveChecked(const AppConfig& cfg, QString* error)
{
    QSettings s = openSettings();

    s.setValue(QStringLiteral("serial/port"),      cfg.serial.portName);
    s.setValue(QStringLiteral("serial/baud"),      cfg.serial.baudRate);
    s.setValue(QStringLiteral("serial/dataBits"),  int(cfg.serial.dataBits));
    s.setValue(QStringLiteral("serial/parity"),    int(cfg.serial.parity));
    s.setValue(QStringLiteral("serial/stopBits"),  int(cfg.serial.stopBits));
    s.setValue(QStringLiteral("serial/flow"),      int(cfg.serial.flowControl));
    s.setValue(QStringLiteral("serial/dtr"),       cfg.serial.dtr);
    s.setValue(QStringLiteral("serial/rts"),       cfg.serial.rts);

    s.beginWriteArray(QStringLiteral("sendItems"), kSendItemCount);
    for (int i = 0; i < kSendItemCount; ++i) {
        // 不足 32 条时补空项，而不是缩短循环。QSettings 不会删除未被覆盖的键，
        // 一旦某次写入了短列表，旧的残留项会一直留在配置里。
        const SendItem blank;
        const SendItem& it = (i < cfg.sendItems.size()) ? cfg.sendItems.at(i) : blank;
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("enabled"), it.enabled);
        s.setValue(QStringLiteral("hexMode"), it.hexMode);
        s.setValue(QStringLiteral("name"),    it.name);
        s.setValue(QStringLiteral("intervalMs"), it.intervalMs);
        s.setValue(QStringLiteral("content"), it.content);
    }
    s.endArray();

    s.setValue(QStringLiteral("send/text"),       cfg.sendText);
    s.setValue(QStringLiteral("send/appendCrlf"), cfg.sendAppendCrlf);
    s.setValue(QStringLiteral("send/hexMode"),    cfg.sendHexMode);
    s.setValue(QStringLiteral("send/intervalMs"), cfg.sendIntervalMs);
    s.setValue(QStringLiteral("send/encoding"),   textEncodingToInt(cfg.sendEncoding));

    s.setValue(QStringLiteral("loop/enabled"),    cfg.loopEnabled);
    s.setValue(QStringLiteral("loop/intervalMs"), cfg.loopIntervalMs);

    s.setValue(QStringLiteral("recv/hex"),        cfg.recvHex);
    s.setValue(QStringLiteral("recv/timestamp"),  cfg.recvTimestamp);
    s.setValue(QStringLiteral("recv/wrap"),       cfg.recvWrap);
    s.setValue(QStringLiteral("recv/showSent"),   cfg.recvShowSent);
    s.setValue(QStringLiteral("recv/font"),       cfg.recvFont);
    s.setValue(QStringLiteral("recv/bufferLimit"),
               static_cast<qint64>(cfg.recvBufferLimit));
    s.setValue(QStringLiteral("recv/encoding"),
               textEncodingToInt(cfg.recvEncoding));

    s.setValue(QStringLiteral("window/geometry"), cfg.geometry);
    s.setValue(QStringLiteral("window/state"),    cfg.state);
    s.setValue(QStringLiteral("window/layoutVersion"), kLayoutVersion);

    // 必须 sync() 再看 status()：QSettings 的写入是异步的，
    // 不 sync 的话 status() 反映的不是本次保存的结果。
    s.sync();
    if (s.status() != QSettings::NoError) {
        if (error) {
            *error = QStringLiteral("配置保存失败：%1\n原因：%2")
                         .arg(resolveConfigPath(), describeStatus(s.status()));
        }
        return false;
    }
    return true;
}
