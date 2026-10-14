#pragma once

#include "core/AppConfigTypes.h"   // kDefaultReceiveLimit
#include "core/DataFormatter.h"

#include <QGroupBox>
#include <QString>

class DirectionHighlighter;
class QCheckBox;
class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class SerialPortManager;

// 接收显示区（需求 FR-02）。
class ReceiveView : public QGroupBox
{
    Q_OBJECT

public:
    explicit ReceiveView(QWidget* parent = nullptr);

    // 存在意义只有一个：析构里要先摘掉 m_highlighter。
    // QSyntaxHighlighter 是 document 的 format-change filter，而 document 由
    // QPlainTextEdit 析构时删除；让它活到 document 之后，removeFormatChangeFilter
    // 会打在野指针上。
    ~ReceiveView() override;

    void setSerialSource(SerialPortManager* source);

    // FR-02-8「显示发送数据」勾选后，发送内容经此进入接收区
    void appendSent(const QByteArray& data);

    void clearText();
    void chooseFont();
    bool isPaused() const;

    // FR-02-5「保存接收数据」的数据来源：按点击时刻的接收区内容落盘。
    // stripDirectionMarkers 为 true 时去掉行首的 ← / → —— 标记是显示层的装饰，
    // 要干净数据时用它。默认 false，即与接收区看到的完全一致。
    QString plainText(bool stripDirectionMarkers = false) const;

    // 停止 40ms 批量抽取。关窗时必须先调用它：否则定时器可能在
    // SerialPortManager 已被 deleteLater 销毁之后仍触发 takePending()。
    void stopFlush();

    // 接收区缓冲上限，单位 UTF-16 码元数（对串口数据约等于字节）。
    // 预设值取自 SSCOM 5.13.1：200K/500K/1M/2M/5M/10M/20M，默认 1M。
    // 传入不在预设内的值时退回默认，而不是让下拉框停在"无选中项"。
    void    setBufferLimit(qsizetype chars);
    qsizetype bufferLimit() const;

    // dir 只影响 formatChunk 拼在行首的方向标记，不影响 HEX/时间戳/编码。
    DataFormatter::DisplayOptions displayOptions(DataFormatter::Direction dir) const;

    QString fontString() const;
    void setFontString(const QString& s);

    // 由 MainWindow 在加载配置时一次性套用
    void setOptions(bool hex, bool timestamp, bool wrap, bool showSent);
    // 单独设置接收解码编码（编码下拉与那几个复选框彼此独立）
    void setTextEncoding(TextEncoding enc);

signals:
    void saveRequested();
    void chunkReceived(int byteCount);

private slots:
    void onFlush();

private:
    void appendText(const QString& text);
    void enforceBufferLimit();
    bool isScrolledToBottom() const;

    QPlainTextEdit* m_text          = nullptr;
    DirectionHighlighter* m_highlighter = nullptr;
    QCheckBox*      m_hexCheck      = nullptr;
    QCheckBox*      m_tsCheck       = nullptr;
    QCheckBox*      m_wrapCheck     = nullptr;
    QCheckBox*      m_sentCheck     = nullptr;
    QCheckBox*      m_pauseCheck    = nullptr;
    QPushButton*    m_clearButton   = nullptr;
    QPushButton*    m_saveButton    = nullptr;
    QPushButton*    m_fontButton    = nullptr;
    QComboBox*      m_limitCombo    = nullptr;
    QComboBox*      m_encCombo      = nullptr;
    QTimer*         m_timer         = nullptr;

    qsizetype       m_limitChars    = kDefaultReceiveLimit;

    SerialPortManager* m_source      = nullptr;
};
