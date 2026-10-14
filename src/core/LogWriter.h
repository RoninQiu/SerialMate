#pragma once

#include <QFile>
#include <QString>
#include <QTextStream>

// 接收数据落盘。接收的是已格式化好的文本而非原始字节，
// 以保证保存结果与接收区内容逐字一致（需求验收标准 10、规格 §3.11）。
class LogWriter
{
public:
    LogWriter() = default;
    ~LogWriter();

    bool open(const QString& path);   // 追加模式，UTF-8
    void write(const QString& text);
    void close();
    bool isOpen() const;
    bool hasWriteError() const;       // 磁盘满 / 介质拔出等写入失败

private:
    QFile       m_file;
    QTextStream m_stream;
    bool        m_open        = false;
    bool        m_writeFailed = false;
};
