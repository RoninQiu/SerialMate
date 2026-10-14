#include "ui/SerialConfigPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QList>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QVBoxLayout>

namespace {
const int kBaudPresets[] = { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1152000 };
}

SerialConfigPanel::SerialConfigPanel(QWidget* parent)
    : QGroupBox(tr("串口配置"), parent)
    , m_portCombo(new QComboBox(this))
    , m_baudCombo(new QComboBox(this))
    , m_dataBitsCombo(new QComboBox(this))
    , m_parityCombo(new QComboBox(this))
    , m_stopCombo(new QComboBox(this))
    , m_flowCombo(new QComboBox(this))
    , m_dtrCheck(new QCheckBox(tr("DTR"), this))
    , m_rtsCheck(new QCheckBox(tr("RTS"), this))
    , m_openButton(new QPushButton(tr("打开串口"), this))
    , m_refreshButton(new QPushButton(tr("刷新"), this))
{
    // 两处必须可编辑：本机没有 COM1 时、或要用非预设波特率时，用户需要手动输入
    m_portCombo->setEditable(true);
    m_portCombo->setPlaceholderText(tr("（无串口）"));
    m_baudCombo->setEditable(true);
    m_portCombo->setMinimumWidth(140);
    m_baudCombo->setMinimumWidth(140);

    for (int baud : kBaudPresets)
        m_baudCombo->addItem(QString::number(baud), baud);

    m_dataBitsCombo->addItem(tr("5"), int(QSerialPort::Data5));
    m_dataBitsCombo->addItem(tr("6"), int(QSerialPort::Data6));
    m_dataBitsCombo->addItem(tr("7"), int(QSerialPort::Data7));
    m_dataBitsCombo->addItem(tr("8"), int(QSerialPort::Data8));

    m_parityCombo->addItem(tr("None"), int(QSerialPort::NoParity));
    m_parityCombo->addItem(tr("Even"), int(QSerialPort::EvenParity));
    m_parityCombo->addItem(tr("Odd"), int(QSerialPort::OddParity));
    m_parityCombo->addItem(tr("Space"), int(QSerialPort::SpaceParity));
    // Mark 校验位 ConfigManager 已支持（FR-01-4 要求"校验位"可配），SSCOM 原版也提供，
    // 缺这一项会让配置层的 MarkParity 分支在运行期永远走不到。
    m_parityCombo->addItem(tr("Mark"), int(QSerialPort::MarkParity));

    m_stopCombo->addItem(tr("1"), int(QSerialPort::OneStop));
    m_stopCombo->addItem(tr("2"), int(QSerialPort::TwoStop));

    m_flowCombo->addItem(tr("None"), int(QSerialPort::NoFlowControl));
    m_flowCombo->addItem(tr("Hardware"), int(QSerialPort::HardwareControl));
    m_flowCombo->addItem(tr("Software"), int(QSerialPort::SoftwareControl));

    auto* form = new QFormLayout;
    form->addRow(tr("串口号"), m_portCombo);
    form->addRow(tr("波特率"), m_baudCombo);
    form->addRow(tr("数据位"), m_dataBitsCombo);
    form->addRow(tr("校验位"), m_parityCombo);
    form->addRow(tr("停止位"), m_stopCombo);
    form->addRow(tr("流控"), m_flowCombo);

    auto* flags = new QHBoxLayout;
    flags->addWidget(m_dtrCheck);
    flags->addWidget(m_rtsCheck);
    flags->addStretch(1);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_openButton);
    buttons->addWidget(m_refreshButton);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addLayout(flags);
    layout->addLayout(buttons);

    populatePorts();
    setConfig(SerialConfig{});

    connect(m_openButton, &QPushButton::clicked, this, &SerialConfigPanel::onOpenButtonClicked);
    connect(m_refreshButton, &QPushButton::clicked, this, &SerialConfigPanel::onRefreshButtonClicked);
    connect(m_flowCombo, &QComboBox::currentIndexChanged, this, [this] {
        updateRtsAvailability();
    });
    updateRtsAvailability();
}

void SerialConfigPanel::updateRtsAvailability()
{
    const QSerialPort::FlowControl flow = static_cast<QSerialPort::FlowControl>(
        m_flowCombo->currentData().toInt());

    // 硬件流控下 RTS 线被流控逻辑占用：QSerialPort::setRequestToSend() 会返回
    // false，而此刻 m_open 仍是 false，错误被 openPort 的守卫吞掉 —— 用户勾了
    // RTS 又选硬件流控，RTS 会被静默忽略、界面零提示。直接禁掉并说清楚原因。
    const bool hardwareFlow =
        (flow == QSerialPort::HardwareControl);
    m_rtsCheck->setEnabled(!hardwareFlow);
    m_rtsCheck->setToolTip(hardwareFlow
        ? tr("硬件流控下 RTS 线由流控逻辑占用，无法单独设置")
        : QString());
}

void SerialConfigPanel::populatePorts()
{
    const QString previous = m_portCombo->currentText();

    m_portCombo->clear();
    const QList<QSerialPortInfo> infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& info : infos)
        m_portCombo->addItem(info.portName());

    if (infos.isEmpty()) {
        // 不能用 addItem 放一个"（无串口）"占位项：它会进 item 列表，
        // 于是 currentText() 把它当成端口名读走，一路经 collectConfig() 写进
        // QSettings，下次启动就再也找不到用户真正的串口了。
        // placeholderText 只在绘制时使用、不进列表：设了它之后空列表的
        // currentIndex 停在 -1，此时 currentText() 返回 previous
        // （previous 为空时才是空串，回落到 currentConfig() 里的 COM1 兜底）。
        // enabled 要带上 m_isOpen，否则拔线后点刷新会把"关闭串口"按钮也一起灰掉。
        // clear() 会清掉行编辑框，所以要把用户原来的口名写回去 —— 否则拔线后
        // 关窗会把口名降级为默认的 COM1，用户下次启动得重新选一遍。
        m_portCombo->setCurrentText(previous);
        m_openButton->setEnabled(m_isOpen);
    } else {
        m_openButton->setEnabled(true);
        const int idx = m_portCombo->findText(previous);
        m_portCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
}

void SerialConfigPanel::applyComboValue(QComboBox* box, int value)
{
    const int index = box->findData(value);
    box->setCurrentIndex(index >= 0 ? index : 0);
}

SerialConfig SerialConfigPanel::currentConfig() const
{
    SerialConfig cfg;
    cfg.portName = m_portCombo->currentText().trimmed();
    if (cfg.portName.isEmpty())
        cfg.portName = QStringLiteral("COM1");

    bool ok = false;
    const int baud = m_baudCombo->currentText().toInt(&ok);
    cfg.baudRate = ok && baud > 0 ? baud : 115200;   // 与 SerialConfig 的默认值一致

    cfg.dataBits    = static_cast<QSerialPort::DataBits>(m_dataBitsCombo->currentData().toInt());
    cfg.parity      = static_cast<QSerialPort::Parity>(m_parityCombo->currentData().toInt());
    cfg.stopBits    = static_cast<QSerialPort::StopBits>(m_stopCombo->currentData().toInt());
    cfg.flowControl = static_cast<QSerialPort::FlowControl>(m_flowCombo->currentData().toInt());
    cfg.dtr         = m_dtrCheck->isChecked();
    cfg.rts         = m_rtsCheck->isChecked();
    return cfg;
}

void SerialConfigPanel::setConfig(const SerialConfig& cfg)
{
    m_portCombo->setCurrentText(cfg.portName);
    m_baudCombo->setCurrentText(QString::number(cfg.baudRate));
    applyComboValue(m_dataBitsCombo, int(cfg.dataBits));
    applyComboValue(m_parityCombo, int(cfg.parity));
    applyComboValue(m_stopCombo, int(cfg.stopBits));
    applyComboValue(m_flowCombo, int(cfg.flowControl));
    m_dtrCheck->setChecked(cfg.dtr);
    m_rtsCheck->setChecked(cfg.rts);
    updateRtsAvailability();
}

void SerialConfigPanel::setControlsEnabled(bool on)
{
    m_portCombo->setEnabled(on);
    m_baudCombo->setEnabled(on);
    m_dataBitsCombo->setEnabled(on);
    m_parityCombo->setEnabled(on);
    m_stopCombo->setEnabled(on);
    m_flowCombo->setEnabled(on);
    m_dtrCheck->setEnabled(on);
    // RTS 还受"硬件流控占用"约束，见 updateRtsAvailability()
    const bool hardwareFlow = static_cast<QSerialPort::FlowControl>(
        m_flowCombo->currentData().toInt()) == QSerialPort::HardwareControl;
    m_rtsCheck->setEnabled(on && !hardwareFlow);
    m_refreshButton->setEnabled(on);
    // 打开按钮必须保持可用 —— 它同时是"关闭串口"的入口，
    // 参数被禁时灰掉它会导致串口打开后再也关不上。
    // 但 m_isOpen 为假且参数又被禁时，打开确实没有意义，所以取 or。
    m_openButton->setEnabled(on || m_isOpen);
    m_openButton->setText(m_isOpen ? tr("关闭串口") : tr("打开串口"));
}

void SerialConfigPanel::setOpenState(bool open)
{
    m_isOpen = open;
    m_openButton->setText(open ? tr("关闭串口") : tr("打开串口"));
    m_refreshButton->setEnabled(!open);
}

void SerialConfigPanel::onOpenButtonClicked()
{
    if (m_isOpen) {
        emit closeRequested();
    } else {
        emit openRequested(currentConfig());
    }
}

void SerialConfigPanel::refreshPorts()
{
    populatePorts();
    emit refreshRequested();
}

void SerialConfigPanel::onRefreshButtonClicked()
{
    refreshPorts();
}
