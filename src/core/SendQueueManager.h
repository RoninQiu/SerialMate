#pragma once

#include "core/TextCodec.h"
#include "models.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QTimer>

// 多字符串列表的循环发送驱动。运行在主线程，但不含任何 GUI 依赖。
//
// 节奏是**逐条**的：发完第 N 条就按第 N 条自己的 intervalMs 停多久，
// 然后发第 N+1 条。不再有一个全局间隔 —— 那样表达不了
// 「高频轮询的读指令」和「低频的校准/维护指令」混在一张表里这种需求。
// 这是 SSCOM 5.13.1 的做法：它的 sscom51.ini 里 N1xx 第三段就是每条的间隔。
class SendQueueManager : public QObject
{
    Q_OBJECT

public:
    explicit SendQueueManager(QObject* parent = nullptr);

    void setItems(const QList<SendItem>& items);
    // 换掉快照但**保留当前进度**。给"循环跑着的时候用户改了表"用 ——
    // 见实现处为什么不能直接调 setItems。
    void updateItems(const QList<SendItem>& items);
    void setLoopEnabled(bool on);
    void setTextEncoding(TextEncoding enc);   // 文本模式的编码，默认 UTF-8
    void start();
    void stop();
    bool isRunning() const;

    // 距下一次发送还差多少毫秒。刚发完一条时，这个值就是**刚发的那条**的间隔；
    // start() 之后为 0（第一条立刻发）。
    // 暴露出来是为了让「逐条间隔」这条核心逻辑可被单元测试观测 ——
    // 否则只能靠真实计时去等，测试必然带时序抖动。
    int  pendingIntervalMs() const;

signals:
    void sendRequested(const QByteArray& data);   // 已完成 HEX 解析
    void runningChanged(bool running);

private slots:
    void onTick();

private:
    QList<SendItem> m_items;
    QTimer*         m_timer    = nullptr;
    int             m_index    = 0;
    bool            m_loopOn   = false;
    bool            m_running  = false;
    TextEncoding    m_encoding = TextEncoding::Utf8;
};
