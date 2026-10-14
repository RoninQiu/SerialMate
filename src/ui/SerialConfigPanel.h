#pragma once

#include "models.h"

#include <QGroupBox>

class QCheckBox;
class QComboBox;
class QPushButton;

// 串口参数配置区（需求 FR-01）。
class SerialConfigPanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit SerialConfigPanel(QWidget* parent = nullptr);

    SerialConfig currentConfig() const;
    void setConfig(const SerialConfig& cfg);

    // 打开串口后禁用全部参数控件，防止误改（需求 §5.3）
    void setControlsEnabled(bool on);
    void setOpenState(bool open);

    // 重新枚举串口。面板上的"刷新"按钮与菜单栏的"刷新串口"共用此入口 ——
    // populatePorts() 是私有的，早先把菜单项接成 setConfig(currentConfig())
    // 是条纯空动作，什么也不刷新。顺带发出 refreshRequested 让状态栏跟上。
    void refreshPorts();

signals:
    void openRequested(const SerialConfig& cfg);
    void closeRequested();
    void refreshRequested();

private slots:
    void onOpenButtonClicked();
    void onRefreshButtonClicked();

private:
    void populatePorts();
    void applyComboValue(QComboBox* box, int value);
    // 硬件流控下 RTS 无法单独设置，需同步禁用复选框（否则用户以为设上了）
    void updateRtsAvailability();

    QComboBox*   m_portCombo     = nullptr;
    QComboBox*   m_baudCombo     = nullptr;
    QComboBox*   m_dataBitsCombo = nullptr;
    QComboBox*   m_parityCombo   = nullptr;
    QComboBox*   m_stopCombo     = nullptr;
    QComboBox*   m_flowCombo     = nullptr;
    QCheckBox*   m_dtrCheck      = nullptr;
    QCheckBox*   m_rtsCheck      = nullptr;
    QPushButton* m_openButton    = nullptr;
    QPushButton* m_refreshButton = nullptr;
    bool         m_isOpen        = false;
};
