#include "ui/SendPanel.h"

#include "core/DataFormatter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

SendPanel::SendPanel(QWidget* parent)
    : QGroupBox(tr("发送数据"), parent)
    , m_text(new QPlainTextEdit(this))
    , m_hexCheck(new QCheckBox(tr("HEX发送"), this))
    , m_crlfCheck(new QCheckBox(tr("发送新行"), this))
    , m_timerCheck(new QCheckBox(tr("定时发送"), this))
    , m_intervalSpin(new QSpinBox(this))
    , m_sendButton(new QPushButton(tr("发送"), this))
    , m_hexButton(new QPushButton(tr("发送(HEX)"), this))
    , m_encCombo(new QComboBox(this))
    , m_timer(new QTimer(this))
{
    m_text->setMaximumHeight(80);

    m_intervalSpin->setRange(1, 60000);
    m_intervalSpin->setValue(1000);
    m_intervalSpin->setSuffix(tr(" ms"));

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_sendButton);
    buttons->addWidget(m_hexButton);
    buttons->addWidget(m_hexCheck);
    buttons->addWidget(m_crlfCheck);
    auto* encLabel = new QLabel(tr("编码"), this);
    buttons->addWidget(encLabel);
    buttons->addWidget(m_encCombo);
    buttons->addStretch(1);

    // 发送编码下拉。SSCOM 没有这个选项（隐式走系统 ANSI），所以这里默认 UTF-8。
    for (TextEncoding enc : { TextEncoding::Utf8, TextEncoding::Local, TextEncoding::Latin1 }) {
        m_encCombo->addItem(textEncodingName(enc), textEncodingToInt(enc));
        m_encCombo->setItemData(m_encCombo->count() - 1, textEncodingTooltip(enc),
                                Qt::ToolTipRole);
    }
    m_encCombo->setCurrentIndex(m_encCombo->findData(textEncodingToInt(m_encoding)));

    auto* timing = new QHBoxLayout;
    timing->addWidget(m_timerCheck);
    timing->addWidget(new QLabel(tr("间隔"), this));
    timing->addWidget(m_intervalSpin);
    timing->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_text);
    layout->addLayout(buttons);
    layout->addLayout(timing);

    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &SendPanel::onTick);

    connect(m_sendButton, &QPushButton::clicked, this, &SendPanel::onSendClicked);
    connect(m_hexButton, &QPushButton::clicked, this, &SendPanel::onHexSendClicked);
    connect(m_encCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        setTextEncoding(textEncodingFromInt(m_encCombo->currentData().toInt()));
    });

    connect(m_timerCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            m_timer->start(m_intervalSpin->value());
        else
            m_timer->stop();
    });
    connect(m_intervalSpin, &QSpinBox::valueChanged, this, [this](int ms) {
        if (m_timer->isActive())
            m_timer->start(ms);
    });
}

QString SendPanel::text() const
{
    return m_text->toPlainText();
}

void SendPanel::setText(const QString& t)
{
    m_text->setPlainText(t);
}

bool SendPanel::isSending() const
{
    return m_timer->isActive();
}

void SendPanel::stopSending()
{
    m_timerCheck->setChecked(false);   // 触发 toggled 信号，进而 stop 定时器
}

bool SendPanel::hexMode() const
{
    return m_hexCheck->isChecked();
}

bool SendPanel::appendCrlf() const
{
    return m_crlfCheck->isChecked();
}

int SendPanel::intervalMs() const
{
    return m_intervalSpin->value();
}

void SendPanel::setOptions(bool hex, bool crlf, int intervalMs)
{
    m_hexCheck->setChecked(hex);
    m_crlfCheck->setChecked(crlf);
    // QSpinBox 自带范围夹取：配置被手改成 0 或负数时不会静默变成 0ms 死循环
    m_intervalSpin->setValue(intervalMs);
}

TextEncoding SendPanel::textEncoding() const
{
    return m_encoding;
}

void SendPanel::setTextEncoding(TextEncoding enc)
{
    if (enc == m_encoding)
        return;
    m_encoding = enc;
    const int idx = m_encCombo->findData(textEncodingToInt(enc));
    if (idx >= 0 && m_encCombo->currentIndex() != idx) {
        const QSignalBlocker blocker(m_encCombo);
        m_encCombo->setCurrentIndex(idx);
    }
    emit textEncodingChanged(m_encoding);
}

QByteArray SendPanel::buildPayload(bool quiet)
{
    const QString raw = m_text->toPlainText();

    QByteArray payload;
    if (m_hexCheck->isChecked()) {
        bool ok = false;
        payload = DataFormatter::hexStringToBytes(raw, &ok);
        if (!ok) {
            if (!quiet) {
                QMessageBox::warning(this, tr("HEX 输入无效"),
                                     tr("无法解析为十六进制字节。请检查是否含有非法字符，"
                                        "或半字节数为奇数。"));
            }
            return QByteArray();
        }
    } else {
        // 文本模式按所选编码发送。默认 UTF-8；选「系统ANSI」则与 SSCOM 行为一致
        payload = encodeText(raw, m_encoding);
    }

    if (m_crlfCheck->isChecked())
        payload.append("\r\n");

    return payload;
}

void SendPanel::onSendClicked()
{
    const QByteArray payload = buildPayload();
    if (!payload.isEmpty())
        emit sendRequested(payload);
}

void SendPanel::onHexSendClicked()
{
    // 「发送(HEX)」按钮强制按 HEX 解析，与上方 HEX 复选框状态无关
    const bool was = m_hexCheck->isChecked();
    m_hexCheck->setChecked(true);
    const QByteArray payload = buildPayload();
    m_hexCheck->setChecked(was);

    if (!payload.isEmpty())
        emit sendRequested(payload);
}

void SendPanel::onTick()
{
    // quiet=true：定时路径下绝不弹窗。QMessageBox 是模态的，会开嵌套事件循环，
    // 期间定时器仍然 active，onTick 会无限套娃。
    const QByteArray payload = buildPayload(true);
    if (payload.isEmpty()) {
        // 先停表再提示，顺序反了弹窗期间还会继续触发。
        m_timerCheck->setChecked(false);
        // 空内容不算错误，不必打扰用户；只有 HEX 解析失败才值得说一声。
        // 用 trimmed()：全空白输入在 HEX 模式下是"合法得到 0 字节"（ok=true），
        // 不是解析失败，用 isEmpty() 会给出与事实不符的提示。
        if (!m_text->toPlainText().trimmed().isEmpty() && m_hexCheck->isChecked()) {
            QMessageBox::warning(this, tr("定时发送已停止"),
                                 tr("内容无法解析为十六进制字节，定时发送已停止。"));
        }
        return;
    }
    // 注意：勾了「发送新行」时 payload 恒为至少 2 字节的 CRLF，
    // 输入框为空也不会停表 —— 那正是用户配置的行为（周期性发裸 CRLF）。
    emit sendRequested(payload);
}
