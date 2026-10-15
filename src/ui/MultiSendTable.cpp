#include "ui/MultiSendTable.h"

#include "core/AppConfigTypes.h"
#include "core/DataFormatter.h"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
enum Column { ColIndex = 0, ColName, ColEnabled, ColHex, ColInterval,
              ColContent, ColSend, ColumnCount };

// 改表到「队列快照跟着换」之间的等待。取 300ms 是为了盖住一次连续操作：
// 「应用到全部」要写 99×2 格、间隔列一次提交写两格、内容列敲一串字符
// 每敲一下都会触发。取更短的话用户改一串字符的过程中队列会被换十几次；
// 取更长的话改完再等一秒多才生效，用户会以为没跟上。
constexpr int kEditDebounceMs = 300;
}

// ---- IntervalDelegate ----

IntervalDelegate::IntervalDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QWidget* IntervalDelegate::createEditor(QWidget* parent,
                                        const QStyleOptionViewItem& option,
                                        const QModelIndex& index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    auto* spin = new QSpinBox(parent);
    spin->setFrame(false);
    spin->setRange(kMinItemIntervalMs, kMaxItemIntervalMs);
    spin->setSingleStep(100);
    spin->setSuffix(tr(" ms"));
    // 表格里编辑数字时方向键常被表格的上下移动抢走，
    // 不设的话用户按上下想改数字，实际是在换行。
    spin->setFocusPolicy(Qt::StrongFocus);
    return spin;
}

void IntervalDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto* spin = qobject_cast<QSpinBox*>(editor);
    if (!spin)
        return;
    spin->setValue(index.data(Qt::UserRole).toInt());
}

void IntervalDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                    const QModelIndex& index) const
{
    auto* spin = qobject_cast<QSpinBox*>(editor);
    if (!spin || !model)
        return;
    const int ms = qBound(kMinItemIntervalMs, spin->value(), kMaxItemIntervalMs);
    // UserRole 是真值来源；显示文本另存一份，见类注释
    model->setData(index, ms, Qt::UserRole);
    model->setData(index, QString::number(ms), Qt::DisplayRole);
}

void IntervalDelegate::updateEditorGeometry(
    QWidget* editor, const QStyleOptionViewItem& option,
    const QModelIndex& index) const
{
    Q_UNUSED(index);
    QStyledItemDelegate::updateEditorGeometry(editor, option, index);
}

// ---- ContentDelegate ----

ContentDelegate::ContentDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

bool ContentDelegate::eventFilter(QObject* obj, QEvent* ev)
{
    if (ev->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(ev);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            // obj 是编辑器 QLineEdit。同一时刻只可能有一个编辑器，
            // 且它必定建在当前单元��上，所以直接取当前行即可。
            auto* view = qobject_cast<QAbstractItemView*>(parent());
            auto* editor = qobject_cast<QWidget*>(obj);
            const int row = (view && editor) ? view->currentIndex().row() : -1;
            if (row >= 0 && editor) {
                // 先把编辑器内容写回模型，再排队发信号：
                // 不这么做的话 sendRow 读到的还是编辑前的旧文本。
                commitData(editor);
                QMetaObject::invokeMethod(
                    this, [this, row] { emit enterAccepted(row); }, Qt::QueuedConnection);
            }
        }
    }
    // 不拦截，交给 QStyledItemDelegate 走它自己的提交+关闭流程
    return QStyledItemDelegate::eventFilter(obj, ev);
}

MultiSendTable::MultiSendTable(QWidget* parent)
    : QGroupBox(tr("多字符串发送"), parent)
    , m_table(new QTableWidget(kSendItemCount, ColumnCount, this))
    , m_loopCheck(new QCheckBox(tr("循环发送"), this))
    , m_intervalSpin(new QSpinBox(this))
    , m_editDebounce(new QTimer(this))
{
    // 去抖定时器常驻，只在 itemChanged 时被 restart。放在构造里而不是
    // 第一次编辑时惰性建，省掉一个空指针分支 —— 它是本类唯一的成员定时器。
    m_editDebounce->setSingleShot(true);
    m_editDebounce->setInterval(kEditDebounceMs);
    connect(m_editDebounce, &QTimer::timeout, this, &MultiSendTable::onEditDebounced);
    // 注意：下面建 99×7 个单元格时**还没有**接 itemChanged，要等构造跑完再接。
    // 接早了的话，循环里的 setFlags / setCheckState / setData 都会发 itemChanged，
    // 去抖定时器在**构造期**就上了膛，等面板建好 300ms 后凭空冒一个
    // itemsEdited 出去 —— 那不是任何人的编辑，而且它会在 MainWindow
    // 还没连好线的时候就去动队列快照。
    m_table->setHorizontalHeaderLabels(
        { tr("序号"), tr("注释"), tr("使能"), tr("HEX"), tr("间隔"),
          tr("内容"), tr("发送") });
    m_table->verticalHeader()->setVisible(false);
    // 必须是 Interactive 而不是 ResizeToContents：ResizeToContents 会在每次
    // 内容变化后按内容重算列宽，把上面的 setColumnWidth 顶掉 —— 实测「注释」
    // 列会一路撑到 155px，把「内容」列挤成 130px 只剩 "00 5F 0C 00 00 00 ..."。
    // 显式宽度 + 内容列拉伸是唯一可控的排法。
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(ColContent, QHeaderView::Stretch);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setMinimumSectionSize(36);
    // 固定窄列，把剩余空间全部让给内容列。行数从 32 涨到 99、又多了一列注释
    // 和一列间隔，右栏若不放大的话内容列会被挤到没法用（FR-04-2 要求每条
    // 都有可编辑的内容列）。分隔条可以拖，改宽后记得拖回来。
    m_table->setColumnWidth(ColIndex, 40);
    m_table->setColumnWidth(ColName, 110);
    m_table->setColumnWidth(ColEnabled, 44);
    m_table->setColumnWidth(ColHex, 44);
    m_table->setColumnWidth(ColInterval, 68);
    m_table->setColumnWidth(ColSend, 56);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::SelectedClicked
                             | QAbstractItemView::EditKeyPressed);
    // FR-04-6 只对内容列生效；只装内容列的代理，不影响勾选列与按钮列
    m_delegate = new ContentDelegate(m_table);
    m_table->setItemDelegateForColumn(ColContent, m_delegate);
    connect(m_delegate, &ContentDelegate::enterAccepted, this, [this](int row) {
        sendRow(row);
    });
    m_intervalDel = new IntervalDelegate(m_table);
    m_table->setItemDelegateForColumn(ColInterval, m_intervalDel);

    for (int row = 0; row < kSendItemCount; ++row) {
        QTableWidgetItem* indexItem =
            new QTableWidgetItem(QStringLiteral("%1").arg(row + 1, 2, 10, QLatin1Char('0')));
        indexItem->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, ColIndex, indexItem);

        QTableWidgetItem* nameItem = new QTableWidgetItem;
        m_table->setItem(row, ColName, nameItem);

        QTableWidgetItem* enabledItem = new QTableWidgetItem;
        enabledItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        enabledItem->setCheckState(Qt::Unchecked);
        m_table->setItem(row, ColEnabled, enabledItem);

        QTableWidgetItem* hexItem = new QTableWidgetItem;
        hexItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        hexItem->setCheckState(Qt::Unchecked);
        m_table->setItem(row, ColHex, hexItem);

        m_table->setItem(row, ColContent, new QTableWidgetItem);

        QTableWidgetItem* intervalItem = new QTableWidgetItem(
            QString::number(kDefaultItemIntervalMs));
        intervalItem->setData(Qt::UserRole, kDefaultItemIntervalMs);
        // 必须带 ItemIsEditable：QAbstractItemView::edit() 在向 delegate 要编辑器
        // **之前**先查这个标志，与用哪个 delegate 无关。缺了它，IntervalDelegate
        // 那 40 行整段成为死代码（Qt 自己会打一行
        // "qt.widgets.abstractitemview: edit: editing failed"，但界面上什么都不发生），
        // 逐条间隔在程序内根本设不了 —— 唯一入口只剩底部「应用到全部」，
        // 而它把 99 行全设成同一个值。
        //
        // 原先这里只给 ItemIsEnabled|ItemIsSelectable，理由写的是"不允许就地敲键盘，
        // 否则会显示 500、实际 UserRole 还是 1000"。这个担心对 delegate 来说不成立：
        // 本列已用 setItemDelegateForColumn 绑了 IntervalDelegate，提交必经
        // setModelData()，它同时写 UserRole（真值）与 DisplayRole（显示文本）。
        // 真正会错位的是没有 delegate 的裸单元格，而这里不是那种情况。
        intervalItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        m_table->setItem(row, ColInterval, intervalItem);

        QPushButton* button = new QPushButton(tr("发送"), m_table);
        button->setFixedWidth(60);
        connect(button, &QPushButton::clicked, this, [this, row] { sendRow(row); });
        m_table->setCellWidget(row, ColSend, button);
    }

    m_intervalSpin->setRange(kMinItemIntervalMs, kMaxItemIntervalMs);
    m_intervalSpin->setValue(kDefaultItemIntervalMs);
    m_intervalSpin->setSingleStep(100);
    m_intervalSpin->setSuffix(tr(" ms"));

    auto* controls = new QHBoxLayout;
    controls->addWidget(m_loopCheck);
    // 全局间隔不再直接驱动发送节奏（那是每行自己的事了），
    // 退化成「一键把全部行设成同一个值」，免得 99 行要逐个敲。
    m_applyAllBtn = new QPushButton(tr("应用到全部"), this);
    m_applyAllBtn->setToolTip(tr("把上面的间隔值写进每一行。\n"
                                 "发送时按每行自己的间隔停 —— 这条发完后停多久，"
                                 "由这一行的「间隔」决定。"));
    connect(m_applyAllBtn, &QPushButton::clicked, this, &MultiSendTable::applyIntervalToAll);
    controls->addWidget(m_intervalSpin);
    controls->addWidget(m_applyAllBtn);
    controls->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_table, 1);
    layout->addLayout(controls);

    connect(m_loopCheck, &QCheckBox::toggled, this, &MultiSendTable::onLoopToggled);
    connect(m_intervalSpin, &QSpinBox::valueChanged, this, &MultiSendTable::onIntervalChanged);

    // 构造跑完了才接 —— 理由见上面那处注释。
    // itemChanged 覆盖了用户能改的全部 5 列（内容/注释/使能/HEX/间隔）：
    // 文本列走 setText/setData，勾选列走 setCheckState，间隔列由
    // IntervalDelegate 写 UserRole 与 DisplayRole —— 三条路都收在
    // QTableWidget::setData 里，统一发这一个信号。
    connect(m_table, &QTableWidget::itemChanged, this, &MultiSendTable::scheduleItemsEdited);
}

QList<SendItem> MultiSendTable::items() const
{
    QList<SendItem> out;
    out.reserve(kSendItemCount);
    for (int row = 0; row < kSendItemCount; ++row) {
        SendItem it;
        it.enabled = m_table->item(row, ColEnabled)->checkState() == Qt::Checked;
        it.hexMode = m_table->item(row, ColHex)->checkState() == Qt::Checked;
        it.name    = m_table->item(row, ColName)->text();
        it.intervalMs = m_table->item(row, ColInterval)->data(Qt::UserRole).toInt();
        it.content = m_table->item(row, ColContent)->text();
        out.append(it);
    }
    return out;
}

void MultiSendTable::setItems(const QList<SendItem>& items)
{
    // 堵住 QTableWidget 的信号：填表不是"用户改了表"。
    // 配置加载时这里一次写 99 行 × 5 格，不挡的话每格都起一次去抖定时器，
    // 启动 ~100ms 后照样发一个 itemsEdited() 出去 —— 那是个凭空捏造的
    // "编辑事件"，而且它会在 MainWindow 还没连好线的时候就去换队列快照。
    const QSignalBlocker blocker(m_table);
    for (int row = 0; row < kSendItemCount && row < items.size(); ++row) {
        const SendItem& it = items.at(row);
        m_table->item(row, ColEnabled)->setCheckState(it.enabled ? Qt::Checked : Qt::Unchecked);
        m_table->item(row, ColHex)->setCheckState(it.hexMode ? Qt::Checked : Qt::Unchecked);
        m_table->item(row, ColName)->setText(it.name);
        m_table->item(row, ColContent)->setText(it.content);
        // 夹一次再写：配置可能被手改坏，也可能是从 SSCOM 导入的越界值。
        const int ms = qBound(kMinItemIntervalMs, it.intervalMs, kMaxItemIntervalMs);
        m_table->item(row, ColInterval)->setData(Qt::UserRole, ms);
        m_table->item(row, ColInterval)->setText(QString::number(ms));
    }
}

bool MultiSendTable::loopEnabled() const
{
    return m_loopCheck->isChecked();
}

int MultiSendTable::loopIntervalMs() const
{
    return m_intervalSpin->value();
}

void MultiSendTable::applyIntervalToAll()
{
    const int ms = m_intervalSpin->value();
    for (int row = 0; row < kSendItemCount; ++row) {
        m_table->item(row, ColInterval)->setData(Qt::UserRole, ms);
        m_table->item(row, ColInterval)->setText(QString::number(ms));
    }
    // 上面逐格 setData 已经发过 99×2 次 itemChanged，去抖定时器自然被
    // restart 成只差最后一次。这里再显式起一次，是把"用户点了这个按钮"
    // 这件事本身也纳入去抖 —— 万一将来加了不写单元格的批量路径，
    // 不会因为漏掉这一行而静默不跟随。
    scheduleItemsEdited();
}

void MultiSendTable::scheduleItemsEdited()
{
    // restart 而非 start：连续编辑只会在最后一次编辑后触发一次。
    m_editDebounce->start();
}

void MultiSendTable::onEditDebounced()
{
    emit itemsEdited();
}

void MultiSendTable::stopLoop()
{
    m_loopCheck->setChecked(false);
}

void MultiSendTable::setSendEnabled(bool on)
{
    m_sendEnabled = on;
    m_loopCheck->setEnabled(on);
    // 每行的「发送」按钮是构造时 setCellWidget 挂进去的、没有单独存指针，
    // 这里按列取回来逐个设。只在开关串口时发生一次，99 行的开销可以忽略。
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (auto* btn = qobject_cast<QPushButton*>(m_table->cellWidget(row, ColSend)))
            btn->setEnabled(on);
    }
}

void MultiSendTable::setTextEncoding(TextEncoding enc)
{
    m_encoding = enc;
}

void MultiSendTable::onLoopToggled(bool on)
{
    emit loopToggled(on);
}

void MultiSendTable::onIntervalChanged(int ms)
{
    emit loopIntervalChanged(ms);
}

void MultiSendTable::sendRow(int row)
{
    if (row < 0 || row >= kSendItemCount)
        return;

    // 串口没开时按钮已禁用，这里再挡一道：按钮的 enabled 状态与串口状态之间
    // 隔着一次队列连接，拔线那一瞬仍可能点得动。
    if (!m_sendEnabled)
        return;

    const QString raw = m_table->item(row, ColContent)->text();
    const bool hex = m_table->item(row, ColHex)->checkState() == Qt::Checked;

    QByteArray payload;
    if (hex) {
        bool ok = false;
        payload = DataFormatter::hexStringToBytes(raw, &ok);
        if (!ok) {
            QMessageBox::warning(this, tr("HEX 输入无效"),
                                 tr("第 %1 行的内容无法解析为十六进制字节。").arg(row + 1));
            return;
        }
    } else {
        payload = encodeText(raw, m_encoding);
    }

    if (!payload.isEmpty())
        emit sendRequested(payload);
}
