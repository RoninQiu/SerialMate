#include "core/SendQueueManager.h"

#include "core/DataFormatter.h"

SendQueueManager::SendQueueManager(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &SendQueueManager::onTick);
}

void SendQueueManager::setItems(const QList<SendItem>& items)
{
    m_items = items;
    m_index = 0;
}

void SendQueueManager::updateItems(const QList<SendItem>& items)
{
    m_items = items;
    // 刻意不动 m_index：改表不是"重新开始一轮"。
    // 若在这里清零，循环正跑到第 40 行时用户改一下第 5 行的间隔，
    // 序号靠前的那些高频读指令会把后面整轮指令重新推一遍 ——
    // 用户改个间隔的观感就是"循环卡住 / 重启了"，比不跟随还糟。
    //
    // 已经发出去的那一条也不受影响：它在 emit sendRequested 时就把
    // QTimer 的等待定死了，改动从这一行**下次轮到它**开始生效。
    // 这正是期望的语义 —— 正在走的那一步不该被中途改写。
    //
    // 刻意不夹 m_index：列表变短后越界时，onTick 开头的 `m_index >= n`
    // 已经处理了（回绕或收尾），再加一次夹取是同一件事写两遍，
    // 而且没有任何一条断言能把它和"没夹"区分开 —— 不可观测的防御代码
    // 只会慢慢变成没人敢删的遗产。
    // （对应用例 updateItems_tolerates_a_shorter_list 守的是行为：
    //  变短之后不崩、循环不停、发出去的是新列表的内容。）
}

void SendQueueManager::setLoopEnabled(bool on)
{
    m_loopOn = on;
}

void SendQueueManager::setTextEncoding(TextEncoding enc)
{
    m_encoding = enc;
}

void SendQueueManager::start()
{
    if (m_running)
        return;
    m_running = true;
    m_index   = 0;
    // 0ms 立刻发第一条：间隔的语义是「这条发完停多久」，
    // 所以它不该影响第一条何时发出去。用户勾上循环后干等 10 秒才看到动作，
    // 体感就是卡死了。
    m_timer->start(0);
    emit runningChanged(true);
}

void SendQueueManager::stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_timer->stop();
    emit runningChanged(false);
}

bool SendQueueManager::isRunning() const
{
    return m_running;
}

int SendQueueManager::pendingIntervalMs() const
{
    return m_timer->interval();
}

void SendQueueManager::onTick()
{
    const int n = m_items.size();
    if (n == 0) {
        stop();
        return;
    }

    // scanned 防止整轮全是禁用项时死循环
    int scanned = 0;
    while (scanned < n) {
        if (m_index >= n) {
            if (!m_loopOn) {
                stop();
                return;
            }
            m_index = 0;
        }

        const SendItem& item = m_items.at(m_index);
        ++m_index;
        ++scanned;

        if (!item.enabled)
            continue;

        QByteArray data;
        if (item.hexMode) {
            bool ok = false;
            data = DataFormatter::hexStringToBytes(item.content, &ok);
            // 非法 HEX 只跳过这一项，不打断整轮循环
            if (!ok || data.isEmpty())
                continue;
        } else {
            // 文本模式按所选编码发送，默认 UTF-8
            data = encodeText(item.content, m_encoding);
            if (data.isEmpty())
                continue;
        }

        emit sendRequested(data);
        // 节奏的关键一行：停多久由**刚发出的这一条**决定，不是全局值，
        // 也不是下一条的值。下一次 timeout 就发第 N+1 条。
        //
        // 夹取在这里做而不是靠调用方：配置里的值可能是手改坏的，
        // 而 setInterval(ms) 传 0 或负数会让 QTimer 变成忙等，
        // 在主线程上足以让界面卡死。
        const int wait = qBound(kMinItemIntervalMs, item.intervalMs,
                                kMaxItemIntervalMs);
        m_timer->start(wait);
        return;
    }

    // 扫完一整轮都没找到可发送项
    if (!m_loopOn) {
        stop();
        return;
    }
    // 循环开着但整轮无可发送项：必须重新拉起定时器。
    // 定时器只在真正发出数据后才会被重启（见上面 emit sendRequested 那段），
    // 这里不补一次的话，用户稍后再勾选条目就永远发不出去了 ——
    // m_running 还是 true，界面上看着在循环，实则已经停摆。
    m_timer->start(kDefaultItemIntervalMs);
}
