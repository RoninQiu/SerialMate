#include "core/SerialPortManager.h"

#include <QMutexLocker>

SerialPortManager::SerialPortManager(QObject* parent)
    : QObject(parent)
{
}

SerialPortManager::~SerialPortManager()
{
    // m_port 是本对象的子对象，会被 QObject 析构自动 delete。
    // 此处只需确保端口已关闭。
    if (m_port && m_port->isOpen())
        m_port->close();
}

QByteArray SerialPortManager::takePending()
{
    QMutexLocker locker(&m_mutex);
    QByteArray out;
    out.swap(m_recvBuf);
    return out;
}

void SerialPortManager::initialize()
{
    if (m_port)
        return;

    m_port = new QSerialPort(this);
    connect(m_port, &QSerialPort::readyRead,
            this, &SerialPortManager::onReadyRead);
    connect(m_port, &QSerialPort::bytesWritten,
            this, &SerialPortManager::onBytesWritten);
    connect(m_port, &QSerialPort::errorOccurred,
            this, &SerialPortManager::onPortErrorOccurred);
}

void SerialPortManager::openPort(const SerialConfig& cfg)
{
    if (!m_port) {
        emit errorOccurred(tr("串口尚未初始化"));
        return;
    }
    if (m_open)
        closePort();

    m_port->setPortName(cfg.portName);
    m_port->setBaudRate(cfg.baudRate);
    m_port->setDataBits(cfg.dataBits);
    m_port->setParity(cfg.parity);
    m_port->setStopBits(cfg.stopBits);
    m_port->setFlowControl(cfg.flowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        // 此处不再等待 errorOccurred 回调：那一刻 m_open 仍是 false，
        // onPortErrorOccurred 会直接返回，不会重复上报。
        emit errorOccurred(tr("打开串口 %1 失败：%2").arg(cfg.portName, m_port->errorString()));
        return;
    }

    // DTR / RTS 必须在 open() 之后设置，Windows 上顺序反了不生效。
    m_port->setDataTerminalReady(cfg.dtr);
    m_port->setRequestToSend(cfg.rts);

    m_open = true;
    emit opened(cfg.portName);
}

void SerialPortManager::closePort()
{
    if (m_port && m_port->isOpen())
        m_port->close();

    m_sendQueue.clear();

    if (m_open) {
        m_open = false;
        emit closed();
    }
}

void SerialPortManager::writeData(const QByteArray& data)
{
    if (data.isEmpty())
        return;
    m_sendQueue.append(data);
    flushSendQueue();
}

void SerialPortManager::flushSendQueue()
{
    if (m_sendQueue.isEmpty())
        return;

    if (!m_port || !m_port->isOpen()) {
        m_sendQueue.clear();
        return;
    }

    const qint64 n = m_port->write(m_sendQueue);
    if (n < 0) {
        m_sendQueue.clear();
        emit errorOccurred(tr("发送失败：%1").arg(m_port->errorString()));
        return;
    }

    // 部分后端可能只受理一部分，剩余留在队列里等下一次 bytesWritten 续写。
    // Qt 6.5 的 Windows 后端会一次收下全部，因此这里通常是整块清空。
    m_sendQueue.remove(0, int(n));
}

void SerialPortManager::onBytesWritten(qint64 /*n*/)
{
    flushSendQueue();
}

void SerialPortManager::onReadyRead()
{
    const QByteArray chunk = m_port->readAll();
    if (chunk.isEmpty())
        return;

    QMutexLocker locker(&m_mutex);
    m_recvBuf.append(chunk);
}

void SerialPortManager::onPortErrorOccurred(QSerialPort::SerialPortError e)
{
    if (e == QSerialPort::NoError)
        return;

    // 打开阶段的失败由 openPort() 自行上报；此处 m_open 仍为 false，直接拦截，
    // 保证同一次失败只产生一条 errorOccurred。
    if (!m_open)
        return;

    const QString msg = m_port->errorString();
    closePort();          // 先关串口，把后续错误信号一并拦掉
    emit errorOccurred(msg);
}
