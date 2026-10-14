#pragma once

#include <QMetaType>
#include <QSerialPort>
#include <QString>

// 单条多字符串发送完成后的默认等待。与 SSCOM 5.13.1 一致 ——
// 它 sscom51.ini 的 N1xx 第三段默认写 1000。
//
// 定义在这里而不是 AppConfigTypes.h：SendItem 用它做默认成员初值，
// 而 AppConfigTypes.h 又 include models.h，放过去会形成循环包含。
constexpr int kDefaultItemIntervalMs = 1000;

// 单条间隔的合法区间。上界取 10 分钟：再长的话用户不如自己去点发送，
// 而且循环发送会退化成看起来像卡死。
constexpr int kMinItemIntervalMs = 1;
constexpr int kMaxItemIntervalMs = 600000;

// 串口参数。默认值原对应需求 FR-01-5 的「COM1 / 9600 / 8 / None / 1 / 无流控」，
// 其中波特率已按实测修正为 115200 —— SSCOM 5.13.1 的 ref/sscom5.13.1/sscom51.ini
// 里 N1081=115200，115200 才是原版的真实默认值，需求文档写 9600 是因为
// 编写时没有原软件可查。其余五项仍与 FR-01-5 一致。
//
// DTR / RTS 一律默认勾选，依据要说清楚：DTR 与 SSCOM 一致（ini N1061=Y），
// 但 RTS **不一致** —— SSCOM 实测 N1062=N（默认不勾）。FR-01-5 未规定这两项，
// 保留勾选是产品决定，不再声称"取 SSCOM 默认值"。
struct SerialConfig
{
    QString                 portName    = QStringLiteral("COM1");
    qint32                  baudRate    = 115200;
    QSerialPort::DataBits    dataBits    = QSerialPort::Data8;
    QSerialPort::Parity      parity      = QSerialPort::NoParity;
    QSerialPort::StopBits    stopBits    = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    bool                     dtr         = true;
    bool                     rts         = true;
};

// 多字符串发送列表中的一项。循环开关是全局的，不在每行上（规格 §3.9）。
//
// name 是「注释」列，只给人看，不参与收发。SSCOM 5.13.1 的多字符串表有这一列
// （sscom51.ini 的 N101=1,十六进制数据串1,1000 里第二段就是），用户会拿它标注
// 「欢迎语」「读版本号」这类用途；从 SSCOM 迁移时若没有这一列，这些标注会整个丢掉。
//
// intervalMs 是**这一条发完之后**停多久再发下一条。
// SSCOM 把它存在 sscom51.ini 的 N1xx 第三段。实测一份长期使用的真实配置：
// 绝大多数条目取默认值，少数明显更慢的（校准/维护类指令）取更大的值，
// 个别高频轮询指令取更小的值 —— 取值与条目用途语义自洽，不是别的含义。
// 原先本项目只有一个全局循环间隔，逐条节奏表达不了，那些非默认值会整个丢失。
struct SendItem
{
    bool    enabled   = false;
    bool    hexMode   = false;
    int     intervalMs = kDefaultItemIntervalMs;
    QString name;
    QString content;
};

// SerialConfig 要跨线程以队列连接传给 SerialPortManager::openPort()，
// 而它是自定义类型 —— 未经注册时 invokeMethod 的队列连接会在运行时直接
// 报 "Cannot queue arguments of type 'SerialConfig'"。
// Q_DECLARE_METATYPE 在这里声明，实际注册调用放在 MainWindow 构造函数里。
Q_DECLARE_METATYPE(SerialConfig)
