#pragma once

#include "core/TextCodec.h"

#include <QByteArray>
#include <QGroupBox>
#include <QString>

class QCheckBox;
class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

// 发送数据区（需求 FR-03）。定时发送的定时器留在本类内部，
// 不放进 SendQueueManager —— 理由见规格 §3.10。
class SendPanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit SendPanel(QWidget* parent = nullptr);

    QString text() const;
    void setText(const QString& t);

    void stopSending();

    // 串口关闭时调用。禁掉发送入口，而不是让用户点了以后才知道发不出去 ——
    // 计数与接收区的「→ 发」回显都发生在真正写出之前，失败时它们已经是假的了。
    void setSendEnabled(bool on);

    // 串口关闭时必须调用，否则界面显示"正在发送"而实际空转（规格 §7）
    bool isSending() const;

    // 下面三个 getter 与 setOptions 是一对：MainWindow 靠它们把
    // sendHexMode / sendAppendCrlf / sendIntervalMs 三个配置项落盘再读回。
    // 在此之前这三个字段一直在往 QSettings 写、却永远读不回来 ——
    // 因为本类没有任何暴露这三个控件状态的接口。
    bool hexMode() const;
    bool appendCrlf() const;
    int  intervalMs() const;
    void setOptions(bool hex, bool crlf, int intervalMs);

    // 发送编码。与 MultiSendTable、SendQueueManager 共用同一个值，
    // 由 MainWindow 在 SendPanel 的下拉框变化时同步过去。
    TextEncoding textEncoding() const;
    void setTextEncoding(TextEncoding enc);

signals:
    // 唯一的发送出口。Task 10 实现时整个 signals: 段被漏掉了，
    // 导致 emit sendRequested(...) 全部编译不过 —— 该任务未经独立评审，
    // 纸面冲突扫描只对着计划文本比对，因此没能发现。
    void sendRequested(const QByteArray& data);

    // 编码下拉变化。MainWindow 靠它把编码同步给多字符串表与循环发送队列 ——
    // 三个发送来源共用一个编码设置。
    void textEncodingChanged(TextEncoding enc);

private slots:
    void onSendClicked();
    void onHexSendClicked();
    void onTick();

private:
    // 按 HEX / 文本 / 发送新行 组装待发字节。
    // quiet=true 时非法输入不弹窗，只返回空 —— 定时发送必须走这条，
    // 因为 QMessageBox 会开嵌套事件循环，期间定时器仍在 active，
    // onTick 会套娃，1ms 间隔下能叠出几十个模态框。
    QByteArray buildPayload(bool quiet = false);

    QPlainTextEdit* m_text          = nullptr;
    QCheckBox*      m_hexCheck      = nullptr;
    QCheckBox*      m_crlfCheck     = nullptr;
    QCheckBox*      m_timerCheck    = nullptr;
    QSpinBox*       m_intervalSpin  = nullptr;
    QPushButton*    m_sendButton    = nullptr;
    QPushButton*    m_hexButton     = nullptr;
    QComboBox*      m_encCombo     = nullptr;
    QTimer*         m_timer         = nullptr;

    TextEncoding    m_encoding      = TextEncoding::Utf8;
};
