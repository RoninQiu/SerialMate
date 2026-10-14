#pragma once

#include "models.h"

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QSerialPort>
#include <QString>

// 串口生命周期与收发。整个对象（连同其 new 出来的 QSerialPort）运行在工作线程。
//
// 线程归属关键点：QSerialPort 是在 initialize() 槽里 new 的，而该槽由
// QThread::started 触发、由关联线程发出，因此 new 出来的对象天然属于工作线程。
// 若改用值成员 QSerialPort m_port; 配合 moveToThread()，该成员不会随之迁移，
// 会滞留在主线程并导致 readyRead 归属错误（Qt 6.5 QObject 文档明确说明）。
class SerialPortManager : public QObject
{
    Q_OBJECT

public:
    explicit SerialPortManager(QObject* parent = nullptr);
    ~SerialPortManager() override;

    // 线程安全：加锁后把待显示缓冲区整块换出并清空。可从主线程直接调用。
    // 调用方须保证调用期间对象未被销毁：管理器是被 deleteLater 异步删除的，
    // 因此 thread.wait() 返回之前不得再调用本函数。
    QByteArray takePending();

public slots:
    void initialize();                      // 由 QThread::started 触发，在工作线程执行
    void openPort(const SerialConfig& cfg);
    void closePort();
    void writeData(const QByteArray& data);

signals:
    void opened(const QString& portName);
    void closed();
    void errorOccurred(const QString& message);

private slots:
    void onReadyRead();
    void onBytesWritten(qint64 n);
    void onPortErrorOccurred(QSerialPort::SerialPortError e);

private:
    void flushSendQueue();

    QSerialPort* m_port       = nullptr;   // 仅在工作线程内创建与销毁
    bool          m_open       = false;

    QByteArray    m_recvBuf;                // m_mutex 保护，跨线程读写
    QMutex        m_mutex;

    QByteArray    m_sendQueue;              // 仅工作线程访问，无需加锁
};
