#include "core/LogWriter.h"

LogWriter::~LogWriter()
{
    close();
}

bool LogWriter::open(const QString& path)
{
    close();
    // 新的一次落盘从干净状态开始，否则上一次文件的失败标记会被算到新文件头上。
    // 只能放在 open() 里，不能放进 close()：调用方是先写、再 close、再查 hasWriteError()。
    m_writeFailed = false;
    if (path.isEmpty())
        return false;

    m_file.setFileName(path);
    // 刻意不加 QIODevice::Text：该标志会让 QTextStreamPrivate::flushWriteBuffer() 在 Q_OS_WIN 下
    // 把每个 '\n' 展开成 "\r\n"，且不检查前面是否已有 '\r'（调用方若已拼过 \r\n 就会写成 \r\r\n）；
    // 这段行为又被 #ifdef Q_OS_WIN 包住，加上标志会让同一份代码在 Windows 和 Linux 上落盘字节不同。
    // 此处原样透传调用方给的裸文本（QPlainTextEdit::toPlainText() 那样的 '\n' 分隔文本）。
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append))
        return false;

    m_stream.setDevice(&m_file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    m_stream.setCodec("UTF-8");
#endif
    m_open = true;
    return true;
}

void LogWriter::write(const QString& text)
{
    if (!m_open || text.isEmpty())
        return;
    m_stream << text;
    m_stream.flush();
    // 磁盘满、U 盘拔出等情况 Qt 不会抛异常，只把状态置为 WriteError。
    // 不检查的话调用方会以为保存成功，实际文件是空的或截断的。
    if (m_stream.status() != QTextStream::Ok)
        m_writeFailed = true;
}

bool LogWriter::hasWriteError() const
{
    return m_writeFailed;
}

void LogWriter::close()
{
    if (!m_open)
        return;
    m_stream.flush();
    m_stream.setDevice(nullptr);
    m_file.close();
    m_open = false;
}

bool LogWriter::isOpen() const
{
    return m_open;
}
