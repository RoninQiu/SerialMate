#include "ui/MainWindow.h"

#include "core/AppConfigTypes.h"
#include "core/ConfigManager.h"
#include "core/DataFormatter.h"
#include "core/LogWriter.h"
#include "core/SerialPortManager.h"
#include "core/SendQueueManager.h"
#include "core/SscomImporter.h"
#include "ui/MultiSendTable.h"
#include "ui/ReceiveView.h"
#include "ui/SendPanel.h"
#include "ui/SerialConfigPanel.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaType>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QThread>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // SerialConfig 要跨线程以队列连接传给 openPort()，必须先注册元类型，
    // 否则运行时会报 "Cannot queue arguments of type 'SerialConfig'"（规格 §4.3）
    qRegisterMetaType<SerialConfig>("SerialConfig");

    buildUi();
    buildMenus();

    // 顺序：startSerialThread 必须在 wireSignals **之前**。
    // wireSignals 里有 m_recvView->setSerialSource(m_serial)，那一行是立即求值的，
    // 原计划把 wireSignals 排在前面，m_serial 此刻还是 nullptr —— 接收区会
    // 永远拿不到数据，且不报任何错。反过来 m_queue 被 startSerialThread 里的
    // closed lambda 引用是安全的：那几条连接都是 QueuedConnection，只有事件循环
    // 才会投递，而构造函数返回前事件循环不会转。
    startSerialThread();
    wireSignals();

    QString loadErr;
    applyConfig(ConfigManager::loadChecked(&loadErr));
    updateStatusCounters();

    // 配置读坏了必须在启动就讲清楚：等用户配完参数、关程序时才发现存不上，
    // 前面全白做；更糟的是此刻配置已经被换成默认值，退出时还会被原样写回去。
    m_configReadError = !loadErr.isEmpty();
    if (m_configReadError)
        warnAboutUnreadableConfig(loadErr);

    // 绿色软件的代价：配置就写在 exe 旁边。程序被放进 Program Files 而当前
    // 用户无写权限时，参数根本存不下来。必须在启动就讲清楚 —— 等用户配完
    // 一堆参数、关程序时才发现没存上，前面全白做。
    warnIfConfigNotWritable();
}

void MainWindow::warnAboutUnreadableConfig(const QString& reason)
{
    statusBar()->showMessage(tr("⚠ 配置读取异常，本次不保存"), 0);
    QMessageBox::warning(this, tr("配置读取异常"), tr(
        "SerialMate.ini 没能正常读取，已按默认值继续运行。\n\n"
        "原因：%1\n\n"
        "**退出时不会把这份默认值写回该文件** —— 原文件里可能有能手工救回来的"
        "内容，覆盖掉就找不回来了。可以先把它改名备份，再重新启动本程序。")
        .arg(reason));
}

void MainWindow::warnIfConfigNotWritable()
{
    QString err;
    if (ConfigManager::checkWritable(&err))
        return;

    statusBar()->showMessage(tr("⚠ 配置无法保存"), 0);
    QMessageBox::warning(this, tr("配置无法保存"), err + tr(
        "\n\n本软件是绿色软件，配置保存在 exe 同目录的 SerialMate.ini，不写注册表。"
        "\n请把程序移到有写权限的目录（例如用户目录下的 SerialMate）再运行。"));
}

MainWindow::~MainWindow()
{
    // 正常路径下 closeEvent 已处理完并把 m_thread 置空。这里兜住"未经
    // closeEvent 就被销毁"的路径（例如外部直接 delete）。
    // 关键：wait() 超时不能就这么走 —— m_thread 是本对象的子对象，
    // 带着 running 状态进入 ~QThread 会让 Qt qFatal 崩掉整个进程。
    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(3000)) {
            m_thread->terminate();
            m_thread->wait(1000);
            if (m_thread->isRunning())
                m_thread->setParent(nullptr);   // 故意泄漏，换取正常退出
        }
        m_thread = nullptr;
    }
}

void MainWindow::buildUi()
{
    m_serialPanel = new SerialConfigPanel(this);
    m_recvView    = new ReceiveView(this);
    m_sendPanel   = new SendPanel(this);
    m_multiTable  = new MultiSendTable(this);

    auto* serialLayout = new QVBoxLayout;
    serialLayout->setContentsMargins(0, 0, 0, 0);
    serialLayout->addWidget(m_serialPanel);
    serialLayout->addStretch(1);

    QWidget* serialPane = new QWidget(this);
    serialPane->setObjectName(QStringLiteral("serialPane"));
    serialPane->setLayout(serialLayout);
    // 最小宽度比 setSizes 更可靠：setSizes 只是"期望值"，随后 applyConfig() 里的
    // restoreState() 会用存档里的分隔条位置把它盖掉（实测：设 470 被压回 270）。
    // 最小宽度是硬约束，splitter 只能把别处压小来满足它。
    serialPane->setMinimumWidth(210);

    auto* rightLayout = new QVBoxLayout;
    rightLayout->addWidget(m_recvView, 1);
    // 分隔线。Qt 6.12 起 QLine 这个分隔线控件已被移除 —— 它从来只是 QFrame 的
    // HLine 特例，因此不能再 new QLine。顺带避开同名坑：写 <QLine> 会命中
    // QtCore 的**几何**类 QLine，因为 qmainwindow.h → qaction.h → qicon.h →
    // qpixmap.h → qimage.h → qtransform.h → qline.h 是传递包含，而 QtCore 的
    // include 路径排在 QtWidgets 之前。
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    rightLayout->addWidget(separator);
    rightLayout->addWidget(m_sendPanel);

    QWidget* mainPane = new QWidget(this);
    mainPane->setObjectName(QStringLiteral("mainPane"));
    mainPane->setLayout(rightLayout);
    mainPane->setMinimumWidth(380);

    // 多字符串面板独占右栏，可拖宽、可隐藏 —— 对齐 SSCOM 5.13.1 的 Panel2
    // （原版那块也是右侧可调宽容器，宽度存在 ini 的 N1086=418）。
    //
    // objectName 不是可有可无的装饰：QMainWindow::saveState() 只会把**有 objectName**
    // 的子控件的可见性和分隔条位置存进去。没设名字，分隔条宽度与面板显隐就都存不住。
    m_multiTable->setObjectName(QStringLiteral("multiTable"));
    // 六个固定宽列合计 362px（序号40+注释110+使能44+HEX44+间隔68+发送56），
    // 内容列还要至少 ~170px 才够编辑（FR-04-2），加上竖向滚动条约 20px。
    // 低于 540 的话内容列会被压到只剩几十像素、发送按钮直接挤出可视区，
    // 实机截图里就是这么看到的。最小宽度是硬约束，splitter 只能压别处。
    // 改这六个 setColumnWidth 时记得同步改这个数。
    m_multiTable->setMinimumWidth(540);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    splitter->addWidget(serialPane);
    splitter->addWidget(mainPane);
    splitter->addWidget(m_multiTable);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);   // 只有主工作区跟着窗口一起伸缩
    splitter->setStretchFactor(2, 0);
    // 允许把面板拖到 0 宽，等同于手动折叠
    splitter->setChildrenCollapsible(true);
    splitter->setSizes({ 230, 700, 560 });
    // 默认窗口宽度要放得下三栏的最小值之和（210+380+540）再加窗口边框，
    // 否则首次启动时 Qt 会先按最小值撑开再被 restoreGeometry 拉回去，
    // 表现为面板被压扁一瞬。
    resize(1320, 760);

    QWidget* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(splitter);
    setCentralWidget(central);

    m_statusConn  = new QLabel(tr("串口已关闭"), this);
    m_statusParam = new QLabel(QString(), this);
    m_statusCount = new QLabel(QString(), this);

    statusBar()->addWidget(m_statusConn);
    statusBar()->addWidget(m_statusParam);
    statusBar()->addPermanentWidget(m_statusCount);
    // FR-06 要求状态栏上有清空计数的入口。此前只在「操作」菜单里，
    // 挂在状态栏上才符合"状态栏实时显示 + 可清零"的原意。
    auto* reset = new QPushButton(tr("清零计数(&Z)"), this);
    reset->setFlat(true);
    reset->setToolTip(tr("把接收与发送字节数清零（快捷键见「操作」菜单）"));
    connect(reset, &QPushButton::clicked, this, &MainWindow::clearCounters);
    statusBar()->addPermanentWidget(reset);
}

void MainWindow::buildMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(tr("保存接收数据(&S)..."), this, &MainWindow::saveReceivedToFile);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("从 SSCOM 导入配置(&I)..."), this, &MainWindow::importFromSscom);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出(&X)"), this, &QWidget::close);

    QMenu* operMenu = menuBar()->addMenu(tr("操作(&O)"));
    // 不用 &SerialConfigPanel::onRefreshButtonClicked 作接收者：该槽是 private slots，
    // 类外取其地址通不过访问检查。改用公开的 refreshPorts()。
    m_refreshAction = operMenu->addAction(tr("刷新串口(&R)"), this, [this] {
        m_serialPanel->refreshPorts();
    });
    // FR-08 要求操作菜单里有「打开串口 / 关闭串口」。没有"当前是否已打开"
    // 的查询接口，所以两个都放，用 setVisible 跟着 opened/closed 切。
    m_openAction  = operMenu->addAction(tr("打开串口(&O)"), this, [this] {
        QMetaObject::invokeMethod(m_serial, "openPort", Qt::QueuedConnection,
                                  Q_ARG(SerialConfig, m_serialPanel->currentConfig()));
    });
    m_closeAction = operMenu->addAction(tr("关闭串口(&C)"), this, [this] {
        QMetaObject::invokeMethod(m_serial, "closePort", Qt::QueuedConnection);
    });
    m_openAction->setVisible(false);
    operMenu->addSeparator();
    operMenu->addAction(tr("清空接收(&C)"), m_recvView, &ReceiveView::clearText);
    operMenu->addAction(tr("清空发送"), m_sendPanel, [this] { m_sendPanel->setText(QString()); });
    operMenu->addAction(tr("清零计数(&Z)"), this, &MainWindow::clearCounters);

    QMenu* viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    m_multiTableAction = viewMenu->addAction(tr("显示多字符串面板(&M)"));
    m_multiTableAction->setCheckable(true);
    m_multiTableAction->setChecked(true);
    connect(m_multiTableAction, &QAction::toggled, this, [this](bool on) {
        m_multiTable->setVisible(on);
    });
    viewMenu->addSeparator();
    viewMenu->addAction(tr("全部恢复默认布局(&R)"), this, [this] {
        // 恢复不了可见性就等于没恢复，所以显隐状态必须一起还原
        m_multiTable->setVisible(true);
        m_multiTableAction->setChecked(true);
        if (auto* sp = findChild<QSplitter*>(QStringLiteral("mainSplitter")))
            sp->setSizes({ 230, 700, 560 });
    });

    QMenu* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(tr("使用说明(&U)"), this, [this] { showUsage(); });
    helpMenu->addAction(tr("关于(&A)..."), this, [this] {
        QMessageBox::about(this, tr("关于 SerialMate"),
                            tr("SerialMate 串口调试助手\n\n"
                               "复刻 SSCOM 操作习惯的 Qt 6.5 实现。\n"
                               "本软件为独立实现，与 SSCOM 及其作者无关。"));
    });

    buildShortcuts();
}

void MainWindow::buildShortcuts()
{
    // FR-08 指定的四个快捷键
    auto add = [this](const QKeySequence& seq, void (MainWindow::*slot)()) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, slot);
    };
    add(QKeySequence(Qt::CTRL | Qt::Key_O), &MainWindow::openSerial);
    add(QKeySequence(Qt::CTRL | Qt::Key_D), &MainWindow::closeSerial);
    add(QKeySequence(Qt::CTRL | Qt::Key_L), &MainWindow::clearReceive);
    add(QKeySequence(Qt::CTRL | Qt::Key_S), &MainWindow::saveReceivedToFile);
}

void MainWindow::openSerial()
{
    // 串口已经开着时必须直接返回，不能去 trigger() 那个菜单项。
    //
    // 缺陷原委：m_openAction 只在 closed() 里恢复 setVisible(true)，
    // 而 **QAction::trigger() 不看 isVisible()** —— 菜单里点不到的那一项，
    // 快捷键照样触发得到。于是串口开着时按 Ctrl+O：
    //   invokeMethod(openPort) → openPort 里 `if (m_open) closePort();`
    //   → closed() → MainWindow 在里面 stop 循环发送、stopSending 定时发送、
    //     stopLoop、关日志文件；
    //   → 紧接着又把同一个端口开了回来。
    // 端口毫发无损，循环和定时发送却被静默拆掉，界面上"什么都没发生"。
    //
    // 为什么不能只靠 m_openAction->setEnabled(false)：那依赖 QAction::activate
    // 的内部实现细节。这里自己判一次，行为写在明面上。
    //
    // 也挡不住全部：m_portOpen 由 opened()/closed() 两个**队列连接**维护，
    // 端口真正打开到信号投递之间有一个窗口，窗口里按 Ctrl+O 仍会走到
    // openPort —— 那一瞬由 SerialPortManager::openPort 的重入拒绝兜底。
    if (m_portOpen) {
        statusBar()->showMessage(tr("串口已打开，无需重复打开"), 3000);
        return;
    }
    m_openAction->trigger();
}

void MainWindow::closeSerial()
{
    m_closeAction->trigger();
}

void MainWindow::clearReceive()
{
    m_recvView->clearText();
}

void MainWindow::showUsage()
{
    QMessageBox::information(this, tr("使用说明"), tr(
        "<b>基本流程</b><br>"
        "1. 左侧选串口号与参数，点「打开串口」<br>"
        "2. 下方输入要发的东西，点「发送」或勾「发送(HEX)」<br>"
        "3. 收到的数据显示在接收区，右下角可切 HEX / 时间戳<br><br>"
        "<b>多字符串</b><br>"
        "勾「使能」的行参与循环发送；内容列里按回车即可发送该行。<br><br>"
        "<b>快捷键</b><br>"
        "Ctrl+O 打开串口　Ctrl+D 关闭串口<br>"
        "Ctrl+L 清空接收区　Ctrl+S 保存接收数据<br><br>"
        "<b>缓冲上限</b><br>"
        "接收区工具栏右侧的下拉可改缓冲上限，超出后最早的数据会被丢弃。<br><br>"
        "<b>配置</b><br>"
        "本软件是绿色软件：配置保存在 exe 同目录的 <b>SerialMate.ini</b>，"
        "不写注册表，整个目录拷到别的电脑上照常工作。<br>"
        "删掉该文件即可恢复默认设置。"));
}

void MainWindow::startSerialThread()
{
    m_thread = new QThread(this);

    m_serial = new SerialPortManager;   // 刻意不给父对象：moveToThread 对有父对象的对象会失败
    m_serial->moveToThread(m_thread);

    // 必须经 started 触发（AutoConnection → DirectConnection，initialize 同步跑完）；
    // 改用 invokeMethod(..., QueuedConnection) 会让这个性质消失。
    connect(m_thread, &QThread::started, m_serial, &SerialPortManager::initialize);
    // 接收者必须是 manager 本人。写成 m_thread 会 delete 掉 QThread 对象本身，
    // 而 ~QThread 在 running 状态直接 qFatal。
    connect(m_thread, &QThread::finished, m_serial, &QObject::deleteLater);

    connect(m_serial, &SerialPortManager::opened, this,
            [this](const QString& name) {
                // 顺序不可换：setControlsEnabled(false) 会把打开按钮设为可用
                //（它同时是"关闭串口"的入口），而 setOpenState 只改文字、不碰
                // enabled。反了就让"关闭串口"永久灰掉。
                m_serialPanel->setOpenState(true);
                m_serialPanel->setControlsEnabled(false);
                if (m_refreshAction)
                    m_refreshAction->setEnabled(false);
                if (m_openAction)
                    m_openAction->setVisible(false);
                if (m_closeAction)
                    m_closeAction->setVisible(true);
                // 开着口时"打开"不再有意义。setVisible 只是不显示，
                // 而快捷键走的是 trigger()，那条路不看可见性 —— 所以还得禁用。
                if (m_openAction)
                    m_openAction->setEnabled(false);
                m_statusConn->setText(tr("串口已打开：%1").arg(name));
                m_portOpen = true;
                // 串口没开时发送控件是禁用的，这里放开。
                m_sendPanel->setSendEnabled(true);
                m_multiTable->setSendEnabled(true);
                updateStatusParams();
            }, Qt::QueuedConnection);

    connect(m_serial, &SerialPortManager::closed, this, [this] {
        m_serialPanel->setOpenState(false);
        m_serialPanel->setControlsEnabled(true);
        if (m_refreshAction)
            m_refreshAction->setEnabled(true);
        if (m_openAction)
            m_openAction->setVisible(true);
        if (m_closeAction)
            m_closeAction->setVisible(false);
        if (m_openAction)
            m_openAction->setEnabled(true);
        m_statusConn->setText(tr("串口已关闭"));
        m_portOpen = false;
        // 发送控件一并禁用：SSCOM 也是这么做的，且能彻底堵死"串口没开却点了发送"
        // 这条路径 —— 计数与回显的谎话就不会有机会产生。
        m_sendPanel->setSendEnabled(false);
        m_multiTable->setSendEnabled(false);
        // 串口没了却还在"发送中"，用户会一直以为在发数据
        m_queue->stop();
        m_sendPanel->stopSending();
        m_multiTable->stopLoop();
        m_log.close();
    }, Qt::QueuedConnection);

    // 复位只挂在 closed() 上：打开失败路径没有 closed()，
    // 若把 UI 复位挂在 errorOccurred 上会漏复位。
    connect(m_serial, &SerialPortManager::errorOccurred, this, [this](const QString& msg) {
        QMessageBox::warning(this, tr("串口错误"), msg);
    }, Qt::QueuedConnection);

    m_thread->start();
}

void MainWindow::wireSignals()
{
    m_queue = new SendQueueManager(this);
    m_recvView->setSerialSource(m_serial);

    // 启动时串口是关的，发送控件必须先禁掉。
    // QPushButton 默认是 enabled，不显式禁的话用户一进来就能点发送 ——
    // 而 MultiSendTable::m_sendEnabled 的初值是 false，视觉与实际状态会对不上。
    m_portOpen = false;
    m_sendPanel->setSendEnabled(false);
    m_multiTable->setSendEnabled(false);

    // ---- 串口打开 / 关闭 ----
    connect(m_serialPanel, &SerialConfigPanel::openRequested, this,
            [this](const SerialConfig& cfg) {
                QMetaObject::invokeMethod(m_serial, "openPort", Qt::QueuedConnection,
                                          Q_ARG(SerialConfig, cfg));
            });
    connect(m_serialPanel, &SerialConfigPanel::closeRequested, this, [this] {
        QMetaObject::invokeMethod(m_serial, "closePort", Qt::QueuedConnection);
    });

    // ---- 发送：所有来源都汇聚到 sendToPort ----
    connect(m_sendPanel, &SendPanel::sendRequested, this, &MainWindow::sendToPort);
    connect(m_multiTable, &MultiSendTable::sendRequested, this, &MainWindow::sendToPort);
    connect(m_queue, &SendQueueManager::sendRequested, this, &MainWindow::sendToPort);

    // ---- 循环发送 ----
    // ---- 循环发送 ----
    // 编码只有一个入口（SendPanel 的下拉框），改这里同步到另外两个发送来源
    connect(m_sendPanel, &SendPanel::textEncodingChanged, this, [this](TextEncoding enc) {
        m_multiTable->setTextEncoding(enc);
        m_queue->setTextEncoding(enc);
    });
    connect(m_multiTable, &MultiSendTable::loopToggled, this, [this](bool on) {
        m_queue->setItems(m_multiTable->items());
        // 原计划这里写的是 setLoopEnabled(true)：关掉循环时也会把队列置成
        // 循环模式。功能上没炸（m_running 已是 false），但状态是错的，
        // 且日后任何依赖 m_loopOn 的判断都会读到反的值。
        m_queue->setLoopEnabled(on);
        if (on)
            m_queue->start();
        else
            m_queue->stop();
    });
    // ---- 标出「当前正在发送」的那一行 ----
    // 队列每真正发出一条就报一次行号（发 sendRequested 之前），表格据此
    // 上色 + 状态行。行号是队列下标，与表格行号严格 1:1，见 SendQueueManager.h。
    connect(m_queue, &SendQueueManager::rowSent, m_multiTable, &MultiSendTable::setCurrentRow);
    // 停下来必须清高亮：非循环跑完一轮后若把指示留在最后一行，
    // 用户会以为它还在发 —— 停止后残留的指示比没有指示更糟。
    // 串口被拔掉时走的也是这条（那里会先 m_queue->stop()）。
    connect(m_queue, &SendQueueManager::runningChanged, m_multiTable, [this](bool running) {
        if (!running)
            m_multiTable->setCurrentRow(-1);
    });
    // 循环跑着的时候改表，队列里的快照必须跟着换。
    // 少了这条：用户在第 5 行把间隔从 1000 改成 100，表格上白纸黑字写着 100，
    // 实际仍然每 1000ms 发一次，而且没有任何迹象说明它没跟上 ——
    // 唯一能察觉的是"循环怎么不听我的"。
    //
    // 用 updateItems 而不是 setItems：后者会把进度清零从头再来一遍，
    // 用户改个间隔的观感反而是"循环重启了"，比不跟随更糟。
    connect(m_multiTable, &MultiSendTable::itemsEdited, this, [this] {
        m_queue->updateItems(m_multiTable->items());
    });
    // 循环发送的节奏改由每行自己的「间隔」列决定，队列不再接受全局间隔，
    // 所以这里没有 loopIntervalChanged -> setInterval 这条连线了。
    // 「全部间隔」输入框只是「应用到全部」的来源，改它不必通知队列。

    // ---- 接收计数：唯一归属在 MainWindow（规格 §5.7）----
    connect(m_recvView, &ReceiveView::chunkReceived, this, [this](int n) {
        m_recvCount += n;
        updateStatusCounters();
    });

    // ---- 保存接收数据（FR-02-5）----
    connect(m_recvView, &ReceiveView::saveRequested, this, &MainWindow::saveReceivedToFile);

    // ---- 配置面板刷新 ----
    // 只刷新状态栏显示，绝不能把配置回写回去：本机无串口时下拉框里是
    // placeholderText 画的"（无串口）"，回写会把占位符当成端口名存进
    // QSettings，下次启动就再也找不到用户真正的串口了。
    connect(m_serialPanel, &SerialConfigPanel::refreshRequested,
            this, [this] { updateStatusParams(); });
}

void MainWindow::sendToPort(const QByteArray& data)
{
    if (data.isEmpty())
        return;

    if (!m_portOpen) {
        // 没发出去的就不能记账、也不能回显 —— 那两样都会被读成"已经发送了"。
        // 发送按钮此时本就是禁用的（见 opened/closed 里的 setSendEnabled），
        // 走到这里通常是串口刚被拔掉、状态还没同步过来的那一瞬。
        //
        // 用状态栏而不是 QMessageBox：sendRequested 可能来自定时发送，而
        // QMessageBox 会开嵌套事件循环，期间定时器仍 active → onTick 无限套娃。
        statusBar()->showMessage(tr("串口未打开，数据未发送"), 3000);
        return;
    }

    QMetaObject::invokeMethod(m_serial, "writeData", Qt::QueuedConnection,
                              Q_ARG(QByteArray, data));

    m_sendCount += data.size();
    updateStatusCounters();
    m_recvView->appendSent(data);
}

void MainWindow::updateStatusCounters()
{
    m_statusCount->setText(tr("接收: %1 字节   发送: %2 字节")
                               .arg(m_recvCount).arg(m_sendCount));
}

void MainWindow::updateStatusParams()
{
    const SerialConfig cfg = m_serialPanel->currentConfig();
    m_statusParam->setText(tr("%1  %2,%3,%4,%5")
                               .arg(cfg.portName)
                               .arg(cfg.baudRate)
                               .arg(int(cfg.dataBits))
                               .arg(int(cfg.parity))
                               .arg(int(cfg.stopBits)));
}

void MainWindow::clearCounters()
{
    m_recvCount = 0;
    m_sendCount = 0;
    updateStatusCounters();
}

void MainWindow::saveReceivedToFile()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("保存接收数据"), QString(), tr("文本文件 (*.txt);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    // 方向标记是显示层的装饰，不是数据。用户要的到底是"回看当时的画面"，
    // 还是"一份能直接喂给解析工具的干净数据"，只有他知道，所以让他当场选。
    //
    // **试过把勾选框直接塞进文件对话框**（外层 QDialog + 内层 QFileDialog 子控件，
    // 因为 QFileDialog 在 Qt 6 里没有 setOptionWidgets 这类挂附加控件的口子）。
    // 那条路在本环境里验证不了：内层 QFileDialog 实测 isVisible=0、layout()=0x0，
    // 内部控件压根没被创建，于是没法确认点「保存」后外层 exec() 会不会返回。
    // 而一个永不返回的保存对话框表现为程序卡死，比多问一句严重得多。
    // 退回两个对话框：文件框行为与改动前逐字节相同，风险为零。
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("保存接收数据"));
    box.setText(tr("文件里要不要保留行首的方向标记？"));
    auto* strip = new QCheckBox(tr("剥离标记（← 收 / → 发），只保留数据本身"), &box);
    strip->setChecked(false);
    strip->setToolTip(tr("不勾则与接收区看到的一模一样，便于回查当时发了什么。"));
    box.setCheckBox(strip);
    box.setInformativeText(path);
    box.setStandardButtons(QMessageBox::Save | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);
    if (box.exec() != QMessageBox::Save)
        return;

    if (!m_log.open(path)) {
        QMessageBox::warning(this, tr("保存失败"), tr("无法写入文件：%1").arg(path));
        return;
    }
    // 按点击时刻的接收区内容一次性落盘。需求 FR-02-5 与验收标准 10 都要求
    // "保存内容与接收区一致"，一次性写入让这一点天然成立。
    m_log.write(m_recvView->plainText(strip->isChecked()));
    m_log.close();

    // 磁盘满、介质拔出等情况 Qt 不会抛异常，文件会静默变成空的或截断的。
    // 不检查的话用户会以为保存成功。
    if (m_log.hasWriteError()) {
        QMessageBox::warning(this, tr("保存失败"),
                             tr("数据未能完整写入文件，请检查磁盘空间或介质是否已拔出。"));
    }
}

void MainWindow::importFromSscom()
{
    // 默认从 exe 所在目录挑：SSCOM 是绿色软件，很多人会把 sscom5.13.1.exe 和
    // SerialMate.exe 摆在同一个文件夹里迁移，配置文件通常就在旁边。
    const QString startDir = QCoreApplication::applicationDirPath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择 SSCOM 配置文件"), startDir,
        tr("SSCOM 配置文件 (sscom51.ini);;INI 文件 (*.ini);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("导入失败"),
                             tr("无法读取文件：\n%1\n\n%2").arg(path, f.errorString()));
        return;
    }

    // 以**当前界面上的配置**为基底，只覆盖 SSCOM 里确实存在的项，
    // 这样导入过程中用户刚改过、但 SSCOM 没有对应键的设置不会丢。
    AppConfig cfg = collectConfig();
    const SscomImportReport rep = SscomImporter::import(f.readAll(), cfg);
    if (!rep.ok) {
        QMessageBox::warning(this, tr("导入失败"), rep.error);
        return;
    }

    applyConfig(cfg);

    // 立刻落盘。导入是用户把老配置搬过来的关键一步，等到退出时才存的话，
    // 中途崩溃或被强杀就全丢了，而且用户不会知道。
    QString saveErr;
    if (!ConfigManager::saveChecked(cfg, &saveErr))
        QMessageBox::warning(this, tr("导入后保存失败"), saveErr);

    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(tr("导入完成"));
    box.setText(tr("已从 SSCOM 配置导入：\n"
                   "串口　%1　%2\n"
                   "多字符串 %3 条（使能状态需自行勾选）")
                     .arg(cfg.serial.portName)
                     .arg(cfg.serial.baudRate)
                     .arg(rep.itemsImported));
    // 丢弃/降级项目全列出来。迁移工具最忌讳静默丢数据 ——
    // 用户几个月后才发现某项配置没迁过来，代价远大于现在多看一屏。
    box.setInformativeText(tr("有若干项无法原样迁移，点「Show Details」查看。"));
    box.setDetailedText(rep.notes.join(QStringLiteral("\n\n")));
    box.exec();
}

void MainWindow::applyConfig(const AppConfig& cfg)
{
    m_serialPanel->setConfig(cfg.serial);
    updateStatusParams();

    m_recvView->setOptions(cfg.recvHex, cfg.recvTimestamp, cfg.recvWrap, cfg.recvShowSent);
    m_recvView->setBufferLimit(cfg.recvBufferLimit);
    m_recvView->setFontString(cfg.recvFont);

    m_sendPanel->setText(cfg.sendText);
    m_sendPanel->setOptions(cfg.sendHexMode, cfg.sendAppendCrlf, cfg.sendIntervalMs);
    m_sendPanel->setTextEncoding(cfg.sendEncoding);
    // 三个发送来源共用同一个编码：主发送框、多字符串表、循环发送队列
    m_multiTable->setTextEncoding(cfg.sendEncoding);
    m_queue->setTextEncoding(cfg.sendEncoding);
    m_recvView->setTextEncoding(cfg.recvEncoding);

    QList<SendItem> items = cfg.sendItems;
    while (items.size() < kSendItemCount) {
        SendItem blank;
        items.append(blank);
    }
    m_multiTable->setItems(items);
    m_queue->setItems(items);
    // 只恢复循环开关，不自动起发：启动就往外发数据不是任何人期待的行为
    m_queue->setLoopEnabled(cfg.loopEnabled);

    // 窗口尺寸与位置总是恢复：saveGeometry() 的格式与控件树无关，
// 布局怎么改都能用。
if (!cfg.geometry.isEmpty())
        restoreGeometry(cfg.geometry);

// 但 saveState() 的 blob 是贴着当时那套控件树记的，布局一改就对不上，
// 而且不会报错 —— restoreState() 会照旧结构把新控件设成隐藏，还返回 true。
// 所以只在布局版本号匹配时才用，否则整个分隔条结构与面板显隐都退回默认值。
if (!cfg.state.isEmpty() && cfg.layoutVersion == kLayoutVersion) {
    restoreState(cfg.state);
    // restoreState 会把面板设成存档时的可见性，但菜单勾选状态是自己的，
    // 不同步的话用户看到的是「面板没了，菜单却还打着勾」。
    if (m_multiTableAction)
        m_multiTableAction->setChecked(m_multiTable->isVisible());
}
}

AppConfig MainWindow::collectConfig() const
{
    AppConfig cfg;
    cfg.serial         = m_serialPanel->currentConfig();
    cfg.sendItems      = m_multiTable->items();
    cfg.sendText       = m_sendPanel->text();
    cfg.sendHexMode    = m_sendPanel->hexMode();
    cfg.sendAppendCrlf = m_sendPanel->appendCrlf();
    cfg.sendIntervalMs = m_sendPanel->intervalMs();
    cfg.sendEncoding   = m_sendPanel->textEncoding();
    cfg.loopEnabled    = m_multiTable->loopEnabled();
    cfg.loopIntervalMs = m_multiTable->loopIntervalMs();

    // 方向与持久化无关（收发都恒为带标记），所以这里传 None，
    // 只要 hex/timestamp/encoding 三项。
    const DataFormatter::DisplayOptions opts =
        m_recvView->displayOptions(DataFormatter::Direction::None);
    cfg.recvHex       = opts.hexMode;
    cfg.recvTimestamp = opts.timestamp;
    cfg.recvBufferLimit = m_recvView->bufferLimit();
    cfg.recvFont      = m_recvView->fontString();
    cfg.recvEncoding  = opts.encoding;        // displayOptions() 直接带出下拉当前值
    cfg.geometry      = saveGeometry();
    cfg.state         = saveState();

    return cfg;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // 顺序不可调换：
    // 1) 先停所有定时器。否则定时发送可能在保存配置时仍在触发；
    //    接收定时器更关键 —— 它可能在本对象的 SerialPortManager 已被
    //    deleteLater 销毁之后仍触发 takePending()，那是 use-after-free。
    m_queue->stop();
    m_sendPanel->stopSending();
    m_multiTable->stopLoop();
    m_recvView->stopFlush();

    QString saveErr;
    if (m_configReadError) {
        // 见 m_configReadError 的注释：读回来的那份是默认值，写回去等于永久
        // 覆盖掉用户原来那个坏掉的文件。宁可这次会话的布局存不下来。
        statusBar()->showMessage(tr("⚠ 配置读取异常，本次不保存"), 0);
    } else if (!ConfigManager::saveChecked(collectConfig(), &saveErr)) {
        // 走到这里说明启动时的探测漏了（磁盘满、目录权限被改、U 盘拔出）。
        // 照实报出来，别让用户以为参数存下了。
        QMessageBox::warning(this, tr("配置保存失败"), saveErr);
    }
    m_log.close();

    if (m_thread) {
        // 用 Queued 而非 BlockingQueued：阻塞式调用在 worker 卡死时会连带卡死主线程，
        // 关窗就变成"程序关不掉"。这里根本不需要等 closePort 执行完——
        // quit() 之后线程结束时 SerialPortManager 随 QObject 析构自动关闭端口。
        QMetaObject::invokeMethod(m_serial, "closePort", Qt::QueuedConnection);
        m_thread->quit();

        if (!m_thread->wait(3000)) {
            // 工作线程卡死。m_thread 是本对象的子对象，若让它带着 running 状态
            // 进入 ~QThread，Qt 会 qFatal 直接崩掉整个进程 —— 宁可泄漏一个线程。
            m_thread->terminate();
            m_thread->wait(1000);
            if (m_thread->isRunning())
                m_thread->setParent(nullptr);   // 故意泄漏，换取正常退出
        }
        m_thread = nullptr;
    }

    event->accept();
}