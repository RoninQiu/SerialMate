#pragma once

#include <QByteArray>

// 暂停期间收到的原始字节的暂存。
//
// 为什么需要它：ReceiveView::onFlush() 每 40ms 从工作线程把数据整块取走。
// 以前暂停时取完就丢 —— 数据永久消失，而 chunkReceived 仍照常发出，
// 状态栏写着"接收: N 字节"、接收区却少了中间一大段，"保存接收数据"落盘的
// 也是一份带静默缺口的日志，界面上完全看不出中间丢过。
//
// 为什么必须有上限：攒在这里与攒在工作线程里同样会 OOM，而"暂停时定时器不能停"
// 正是为了避免后者。没有上限就等于把同一个问题挪了个地方。
//
// 超出上限时丢**最早**的，并把丢弃的字节数记在 dropped 上，由调用方在恢复时
// 如实告诉用户 —— 宁可少数据，也不能"少得没痕迹"。
class ReceiveBacklog
{
public:
    // 上限 1 MiB：115200 8N1 下约 7 秒的量。够覆盖一次"被打断去干别的事"，
    // 又不会让一个忘了解除暂停的连接把内存吃光。
    static constexpr qsizetype kDefaultCap = 1024 * 1024;

    void setCap(qsizetype cap) { m_cap = cap > 0 ? cap : kDefaultCap; }

    void append(const QByteArray& data)
    {
        if (data.isEmpty())
            return;
        m_buf.append(data);
        if (m_buf.size() <= m_cap)
            return;

        const qsizetype over = m_buf.size() - m_cap;
        m_buf.remove(0, over);
        m_dropped += over;
    }

    // 取出并清空。out 拿到还留着的字节，dropped 拿到累计丢弃量。
    void take(QByteArray* out, qint64* dropped)
    {
        if (out)
            *out = m_buf;
        if (dropped)
            *dropped = m_dropped;
        m_buf.clear();
        m_dropped = 0;
    }

    void clear()
    {
        m_buf.clear();
        m_dropped = 0;
    }

    qsizetype size()  const { return m_buf.size(); }
    qint64     dropped() const { return m_dropped; }
    bool       isEmpty()  const { return m_buf.isEmpty() && m_dropped == 0; }

private:
    QByteArray m_buf;
    qint64     m_dropped = 0;
    qsizetype  m_cap     = kDefaultCap;
};
