#include "ui/ReceiveView.h"

#include "core/SerialPortManager.h"
#include "ui/WrapLayout.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFontDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

namespace {

struct LimitPreset
{
    const char* label;
    qsizetype   chars;
};

// 取自 SSCOM 5.13.1 的 ref/sscom5.13.1/sscom51.ini：N1067 默认 1000000，
// 其上方注释逐项列出可选值 200K,500K,1M,2M,5M,10M,20M
const LimitPreset kLimitPresets[] = {
    { "200K",  200000 }, { "500K",  500000 }, { "1M",   1000000 },
    { "2M",   2000000 }, { "5M",   5000000 }, { "10M", 10000000 },
    { "20M", 20000000 },
};

// setMaximumBlockCount 的兜底值。字符上限是主约束，但块数必须另设一道：
// QPlainTextEdit 每个块都是独立对象，满屏无换行的二进制流会产生
// "字符数 == 块数" 的病态文档，此时 20M 字符的实际内存远大于 20M 字符本身。
// 按平均 80 字符/行折算，正常文本下字符上限先生效，块数上限只在病态场景兜底。
qsizetype blockCapFor(qsizetype chars)
{
    const qsizetype cap = chars / 80;
    return cap < 10000 ? 10000 : cap;
}

int indexOfLimit(qsizetype chars)
{
    for (int i = 0; i < int(std::size(kLimitPresets)); ++i) {
        if (kLimitPresets[i].chars == chars)
            return i;
    }
    return -1;
}

} // namespace

ReceiveView::ReceiveView(QWidget* parent)
    : QGroupBox(tr("接收显示"), parent)
    , m_text(new QPlainTextEdit(this))
    , m_hexCheck(new QCheckBox(tr("HEX显示"), this))
    , m_tsCheck(new QCheckBox(tr("时间戳"), this))
    , m_wrapCheck(new QCheckBox(tr("自动换行"), this))
    , m_sentCheck(new QCheckBox(tr("显示发送"), this))
    , m_pauseCheck(new QCheckBox(tr("暂停"), this))
    , m_clearButton(new QPushButton(tr("清空"), this))
    , m_saveButton(new QPushButton(tr("保存"), this))
    , m_fontButton(new QPushButton(tr("字体"), this))
    , m_limitCombo(new QComboBox(this))
    , m_encCombo(new QComboBox(this))
    , m_timer(new QTimer(this))
{
    m_text->setReadOnly(true);
    m_text->setFont(QFont(QStringLiteral("Consolas"), 10));
    m_text->setLineWrapMode(QPlainTextEdit::WidgetWidth);   // 规格 §3.5
    m_wrapCheck->setChecked(true);

    // 缓冲上限下拉。SSCOM 用字节制且预设固定，这里沿用同一组档位。
    for (const LimitPreset& p : kLimitPresets) {
        // 存 qint64 而不是 qsizetype：qsizetype 是平台相关别名，
        // 不保证是已注册的元类型，QVariant 取不回来
        m_limitCombo->addItem(QString::fromLatin1(p.label),
                              QVariant::fromValue(static_cast<qint64>(p.chars)));
    }
    m_limitCombo->setToolTip(tr("接收区缓冲上限"));
    m_limitCombo->setCurrentIndex(indexOfLimit(kDefaultReceiveLimit));
    m_limitChars = kDefaultReceiveLimit;
    m_text->setMaximumBlockCount(blockCapFor(m_limitChars));

    // 折行布局：中栏最窄 380px 时这一排要 700px 上下，硬塞一排会把标签
    // 截成"保""时""显示"这种认不出的碎片。放不下就整块换到下一行。
    //
    // 第四个参数不能传 this：QLayout(parent) 会当场把这个布局装成 ReceiveView
    // 的主布局，接着 new QVBoxLayout(this) 就失败（"already has a layout"），
    // 控件全被塞进流式布局里，toolbar 跑到顶部把分组框标题都盖住了。
    // 作为子布局就得让 QVBoxLayout::addLayout 接管所有权。
    auto* toolbar = new WrapLayout(4, 6, 4);
    toolbar->addWidget(m_clearButton);
    toolbar->addWidget(m_pauseCheck);
    toolbar->addWidget(m_hexCheck);
    toolbar->addWidget(m_saveButton);
    toolbar->addWidget(m_tsCheck);
    toolbar->addWidget(m_wrapCheck);
    toolbar->addWidget(m_sentCheck);
    toolbar->addWidget(m_fontButton);
    // 光一个写着 2M 的下拉没人知道是什么，补个标签
    auto* limitLabel = new QLabel(tr("缓冲上限"), this);
    limitLabel->setToolTip(tr("接收区最多保留多少内容，超出后最早的数据被丢弃"));
    toolbar->addWidget(limitLabel);
    toolbar->addWidget(m_limitCombo);
    // 接收解码编码。SSCOM 没有这个选项，隐式按系统 ANSI 解；这里默认 UTF-8。
    for (TextEncoding enc : { TextEncoding::Utf8, TextEncoding::Local, TextEncoding::Latin1 }) {
        m_encCombo->addItem(textEncodingName(enc), textEncodingToInt(enc));
        m_encCombo->setItemData(m_encCombo->count() - 1, textEncodingTooltip(enc),
                                Qt::ToolTipRole);
    }
    m_encCombo->setToolTip(tr("接收数据的解码方式"));
    m_encCombo->setCurrentIndex(m_encCombo->findData(textEncodingToInt(TextEncoding::Utf8)));
    toolbar->addWidget(m_encCombo);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_text, 1);
    layout->addLayout(toolbar);

    // 批量抽取周期。规格建议 30~50ms，取 40ms。
    m_timer->setInterval(40);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &ReceiveView::onFlush);
    m_timer->start();

    connect(m_clearButton, &QPushButton::clicked, this, &ReceiveView::clearText);
    connect(m_saveButton, &QPushButton::clicked, this, [this] { emit saveRequested(); });
    connect(m_fontButton, &QPushButton::clicked, this, &ReceiveView::chooseFont);
    connect(m_limitCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        m_limitChars = static_cast<qsizetype>(m_limitCombo->currentData().toLongLong());
        m_text->setMaximumBlockCount(blockCapFor(m_limitChars));
        // 调小上限时立刻裁掉已经超出的部分，不必等下一批数据到达
        enforceBufferLimit();
    });

    connect(m_wrapCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_text->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    });

    // FR-02-10 右键菜单：复制 / 清空 / 保存 / 暂停
    m_text->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_text, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction(tr("复制"), this, [this] { m_text->copy(); });
        menu.addAction(tr("清空"), this, &ReceiveView::clearText);
        menu.addAction(tr("保存..."), this, [this] { emit saveRequested(); });
        menu.addSeparator();
        QAction* pause = menu.addAction(tr("暂停"));
        pause->setCheckable(true);
        pause->setChecked(m_pauseCheck->isChecked());
        connect(pause, &QAction::toggled, m_pauseCheck, &QCheckBox::setChecked);
        menu.exec(m_text->viewport()->mapToGlobal(pos));
    });
}

void ReceiveView::setSerialSource(SerialPortManager* source)
{
    m_source = source;
}

DataFormatter::DisplayOptions ReceiveView::displayOptions(
    DataFormatter::Direction dir) const
{
    DataFormatter::DisplayOptions opt;
    opt.hexMode   = m_hexCheck->isChecked();
    opt.timestamp = m_tsCheck->isChecked();
    opt.encoding  = textEncodingFromInt(m_encCombo->currentData().toInt());
    opt.direction = dir;
    return opt;
}

void ReceiveView::setTextEncoding(TextEncoding enc)
{
    const int idx = m_encCombo->findData(textEncodingToInt(enc));
    if (idx >= 0 && m_encCombo->currentIndex() != idx) {
        const QSignalBlocker blocker(m_encCombo);
        m_encCombo->setCurrentIndex(idx);
    }
}

bool ReceiveView::isPaused() const
{
    return m_pauseCheck->isChecked();
}

void ReceiveView::setOptions(bool hex, bool timestamp, bool wrap, bool showSent)
{
    m_hexCheck->setChecked(hex);
    m_tsCheck->setChecked(timestamp);
    m_wrapCheck->setChecked(wrap);
    m_sentCheck->setChecked(showSent);
}

void ReceiveView::onFlush()
{
    if (!m_source)
        return;

    // 暂停时也必须继续抽取，否则工作线程的接收缓冲区会无限增长（规格 §3.4）
    const QByteArray data = m_source->takePending();
    if (data.isEmpty())
        return;

    // static_cast 而非 int()：QByteArray::size() 在 Qt 6 返回 qsizetype，
    // MSVC /W4 下窄化会报 C4244
    emit chunkReceived(static_cast<int>(data.size()));

    if (isPaused())
        return;

    // 方向标记 ← ：设备 -> 本机。与发送那条是**两次独立 append**（这里是 40ms
    // 定时器，发送那边是 sendCompleted 信号），所以一批数据里不会混着两种方向，
    // 行首标记永远只描述这一整段。
    appendText(DataFormatter::formatChunk(
        data, displayOptions(DataFormatter::Direction::Incoming)));
}

void ReceiveView::appendSent(const QByteArray& data)
{
    if (data.isEmpty())
        return;
    if (!m_sentCheck->isChecked())
        return;
    // 方向标记 → ：本机 -> 设备。
    // 注意这里**没有** isPaused() 判断，与上面的接收不对称：暂停的语义是
    // 「停止刷新接收画面」，而发送是用户主动动作，把刚发的东西也一并藏掉
    // 会让人以为没发出去。属于有意为之，不要"顺手补上"。
    appendText(DataFormatter::formatChunk(
        data, displayOptions(DataFormatter::Direction::Outgoing)));
}

bool ReceiveView::isScrolledToBottom() const
{
    const QScrollBar* bar = m_text->verticalScrollBar();
    return bar->value() >= bar->maximum() - 2;
}

void ReceiveView::appendText(const QString& text)
{
    if (text.isEmpty())
        return;

    // 用户正在往上翻看历史时不要把他拽回底部
    const bool stick = isScrolledToBottom();
    m_text->appendPlainText(text);
    // 必须先裁剪再调滚动条：裁剪会改变文档长度，顺序反了滚动值会落在
    // 错误的 maximum 上，表现为满速接收时视图往回跳。
    enforceBufferLimit();
    if (stick) {
        // 直接推滚动条，不要 moveCursor —— 后者会折叠用户正在选的选区，
        // 与 FR-02-10 的「复制」直接冲突。Qt 的 QPlainTextEditPrivate::append()
        // 在控件可见时本来就会自己滚到底，这段只是兜住不可见时的情况。
        QScrollBar* bar = m_text->verticalScrollBar();
        bar->setValue(bar->maximum());
    }
}

void ReceiveView::enforceBufferLimit()
{
    QTextDocument* doc = m_text->document();
    if (!doc)
        return;

    // characterCount() 把末尾的段落分隔符也算进去，减 1 才是可见字符数
    const qsizetype visible = doc->characterCount() - 1;
    if (visible <= m_limitChars)
        return;

    // 一次删够，不要写 while 循环反复 movePosition —— 文档越大越慢，
    // 满速接收时这里每 40ms 都要跑一次。
    QTextCursor c(doc);
    c.movePosition(QTextCursor::Start);
    c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                   static_cast<int>(qMin<qsizetype>(visible - m_limitChars, visible)));
    c.removeSelectedText();
}

void ReceiveView::setBufferLimit(qsizetype chars)
{
    int idx = indexOfLimit(chars);
    if (idx < 0) {
        // 配置可能来自旧版本或被手工改过。退回默认，绝不能让下拉框停在
        // 「无选中项」—— 那会让 bufferLimit() 与界面显示对不上。
        chars = kDefaultReceiveLimit;
        idx = indexOfLimit(chars);
    }

    m_limitChars = chars;
    if (m_limitCombo->currentIndex() != idx) {
        // QSignalBlocker：否则回填下拉框会触发 currentIndexChanged，
        // 那里又会读 currentData()，在半更新状态下取到旧值
        const QSignalBlocker blocker(m_limitCombo);
        m_limitCombo->setCurrentIndex(idx);
    }
    m_text->setMaximumBlockCount(blockCapFor(m_limitChars));
    enforceBufferLimit();
}

qsizetype ReceiveView::bufferLimit() const
{
    return m_limitChars;
}

void ReceiveView::clearText()
{
    m_text->clear();
}

void ReceiveView::stopFlush()
{
    // 关键前提：QTimer::stop() 在同一栈内就把内部 timer id 清成 INV_TIMER。
    // Windows 上 QEventDispatcherWin32 确实走 timeSetEvent + postEvent(QTimerEvent)，
    // 理论上可能有一次 timeout 已经进了投递队列，但 QTimer::timerEvent() 会用
    // e->timerId() == d->id 把这种"陈旧投递"挡掉。因此本函数返回后 onFlush()
    // 不会再被调用，takePending() 也就不会再碰到即将被 deleteLater 销毁的
    // SerialPortManager。Task 12 的 closeEvent 必须先调它，再 thread.wait()。
    m_timer->stop();
}

QString ReceiveView::fontString() const
{
    return m_text->font().toString();
}

void ReceiveView::setFontString(const QString& s)
{
    if (s.isEmpty())
        return;
    // QFont::fromString() 是非静态成员、返回 bool，失败时返回 false 且不改 *this，
    // 所以拿现有字体当副本、改成功再整体套用
    QFont f = m_text->font();
    if (!f.fromString(s))
        return;
    m_text->setFont(f);
}

QString ReceiveView::plainText(bool stripDirectionMarkers) const
{
    const QString text = m_text->toPlainText();
    return stripDirectionMarkers ? DataFormatter::stripDirectionMarkers(text) : text;
}

void ReceiveView::chooseFont()
{
    bool ok = false;
    const QFont chosen = QFontDialog::getFont(&ok, m_text->font(), this, tr("选择接收区字体"));
    if (ok)
        m_text->setFont(chosen);
}
