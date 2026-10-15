#pragma once

#include "core/AppConfigTypes.h"
// LogWriter 是**值**成员，不是指针：只有前置声明是不够的，C++ 要求
// 完整类型才能作为成员。原计划漏了这一行，会直接报
// "'LogWriter' was not declared in this scope"。它不可拷贝也不可移动
// （成员 QTextStream 带 Q_DISABLE_COPY），所以只能按值持有。
#include "core/LogWriter.h"

#include <QMainWindow>

class MultiSendTable;
class QAction;
class QLabel;
class ReceiveView;
class QThread;
class SendPanel;
class SerialConfigPanel;
class SerialPortManager;
class SendQueueManager;

// 菜单栏、状态栏、布局装配，以及跨面板转发的唯一场所。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void buildMenus();
    void buildShortcuts();
    void wireSignals();
    void startSerialThread();
    void applyConfig(const AppConfig& cfg);
    AppConfig collectConfig() const;
    void sendToPort(const QByteArray& data);
    void updateStatusCounters();
    void updateStatusParams();
    void clearCounters();
    void saveReceivedToFile();
    void openSerial();
    void closeSerial();
    void clearReceive();
    void showUsage();
    void importFromSscom();
    void warnIfConfigNotWritable();

    SerialConfigPanel*   m_serialPanel = nullptr;
    ReceiveView*         m_recvView    = nullptr;
    SendPanel*           m_sendPanel   = nullptr;
    MultiSendTable*      m_multiTable  = nullptr;
    QLabel*              m_statusConn  = nullptr;
    QLabel*              m_statusParam = nullptr;
    QLabel*              m_statusCount = nullptr;
    // 串口打开时必须禁用：开着口时重新枚举没有意义，且会换掉下拉框当前项
    QAction*             m_refreshAction = nullptr;
    QAction*             m_openAction    = nullptr;
    QAction*             m_closeAction   = nullptr;
    // 「视图」菜单里的多字符串面板显隐开关，与 m_multiTable 的可见性双向同步
    QAction*             m_multiTableAction = nullptr;

    QThread*             m_thread = nullptr;
    SerialPortManager*   m_serial = nullptr;
    SendQueueManager*    m_queue  = nullptr;
    LogWriter            m_log;

    qint64 m_recvCount = 0;
    qint64 m_sendCount = 0;

    // 串口是否已打开，由 opened() / closed() 两个信号维护。
    // SerialPortManager 跑在工作线程且没有 isOpen() 查询口，跨线程问它不安全，
    // 所以 UI 侧自己记一份 —— 发送路径必须靠它才能保证"没发出去的不计数、不回显"。
    bool    m_portOpen  = false;
};