#include "mainwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QSpinBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace {
QLabel *makeMetric(const QString &name, QGridLayout *grid, int index,
                   QVector<QLabel *> &metrics)
{
    auto *panel = new QGroupBox(name);
    auto *layout = new QVBoxLayout(panel);
    auto *value = new QLabel(QStringLiteral("--"));
    value->setObjectName(QStringLiteral("metricValue"));
    value->setAlignment(Qt::AlignCenter);
    layout->addWidget(value);
    grid->addWidget(panel, index / 4, index % 4);
    metrics.append(value);
    return value;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    buildUi();
    adapter_ = new CanAdapter(this);
    connect(adapter_, &CanAdapter::framesReceived, this,
            [this](const QVector<CanFrame> &frames) { processCanFrames(frames); });
    connect(canButton_, &QPushButton::clicked, this, [this] { toggleCan(); });
    timer_ = new QTimer(this);
    timer_->setInterval(1000);

    connect(timer_, &QTimer::timeout, this, [this] { refreshDemo(); });
    connect(startButton_, &QPushButton::clicked, this, [this] {
        if (liveMode_) return;
        timer_->start();
        startButton_->setEnabled(false);
        stopButton_->setEnabled(true);
        connectionLabel_->setText(QStringLiteral("● 模拟运行中"));
        appendLog(QStringLiteral("已启动模拟 BMS，采样周期 1000 ms"));
    });
    connect(stopButton_, &QPushButton::clicked, this, [this] {
        timer_->stop();
        startButton_->setEnabled(true);
        stopButton_->setEnabled(false);
        connectionLabel_->setText(QStringLiteral("● 模拟已暂停"));
        appendLog(QStringLiteral("模拟采样已暂停"));
    });

    refreshDemo();
    appendLog(QStringLiteral("应用已启动，当前为模拟模式，未接入真实设备"));
}

void MainWindow::buildUi()
{
    resize(1300, 850);
    setMinimumSize(1000, 650);
    setWindowTitle(QStringLiteral("动力电池及 BMS 管理系统调试软件 V0.1"));

    setStyleSheet(R"(
        QMainWindow, QWidget { background:#f4f7fb; color:#1f2937; font-size:13px; }
        QGroupBox { background:#ffffff; border:1px solid #e1e8f0;
                    border-radius:9px; margin-top:15px; padding:12px; font-weight:600; }
        QGroupBox::title { subcontrol-origin:margin; left:14px; padding:0 6px; }
        QLabel#mainTitle { font-size:23px; font-weight:700; }
        QLabel#metricValue { font-size:23px; font-weight:700; color:#1677ff; }
        QPushButton { background:#1677ff; color:white; padding:8px 17px;
                      border:0; border-radius:6px; }
        QPushButton:hover { background:#4096ff; }
        QPushButton:disabled { background:#a3b3c8; }
        QTableWidget, QTextEdit { background:#ffffff; border:1px solid #e1e8f0;
                                  border-radius:5px; gridline-color:#eff2f6; }
        QHeaderView::section { background:#eaf1fa; padding:7px; border:0;
                               border-bottom:1px solid #d9e4f2; }
    )");

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(12);
    setCentralWidget(central);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("动力电池及 BMS 管理系统"));
    title->setObjectName(QStringLiteral("mainTitle"));
    connectionLabel_ = new QLabel(QStringLiteral("● 模拟就绪"));
    connectionLabel_->setStyleSheet(QStringLiteral("color:#389e0d; font-weight:600;"));
    startButton_ = new QPushButton(QStringLiteral("启动模拟"));
    stopButton_ = new QPushButton(QStringLiteral("暂停模拟"));
    stopButton_->setEnabled(false);
    auto *exportButton = new QPushButton(QStringLiteral("导出 CSV"));
    header->addWidget(title);
    header->addSpacing(15);
    header->addWidget(connectionLabel_);
    header->addStretch();
    channelBox_ = new QComboBox;
    channelBox_->addItems({QStringLiteral("CAN0"), QStringLiteral("CAN1")});
    deviceIndexBox_ = new QSpinBox;
    deviceIndexBox_->setRange(0, 15);
    deviceIndexBox_->setToolTip(QStringLiteral("CAN设备索引，通常为0"));
    canButton_ = new QPushButton(QStringLiteral("连接 CAN"));
    header->addWidget(new QLabel(QStringLiteral("设备索引")));
    header->addWidget(deviceIndexBox_);
    header->addWidget(channelBox_);
    header->addWidget(canButton_);
    header->addWidget(startButton_);
    header->addWidget(stopButton_);
    header->addWidget(exportButton);
    layout->addLayout(header);

    connect(exportButton, &QPushButton::clicked, this, [this] { exportCsv(); });

    auto *overview = new QGroupBox(QStringLiteral("电池运行状态（模拟数据）"));
    auto *overviewGrid = new QGridLayout(overview);
    const QStringList metricNames = {
        QStringLiteral("SOC 电量"), QStringLiteral("SOH 健康度"),
        QStringLiteral("电池包总电压"), QStringLiteral("电池包总电流"),
        QStringLiteral("电池包功率"), QStringLiteral("最高温度"),
        QStringLiteral("最低单体电压"), QStringLiteral("最大单体压差")
    };
    for (int i = 0; i < metricNames.size(); ++i)
        makeMetric(metricNames[i], overviewGrid, i, metrics_);
    layout->addWidget(overview);

    auto *splitter = new QSplitter(Qt::Horizontal);
    auto *cellGroup = new QGroupBox(QStringLiteral("单体电池电压监控（最多 192 节）"));
    auto *cellLayout = new QVBoxLayout(cellGroup);
    cellTable_ = new QTableWidget(192, 3);
    cellTable_->setHorizontalHeaderLabels({
        QStringLiteral("单体编号"), QStringLiteral("电压 (V)"), QStringLiteral("状态")
    });
    cellTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    cellTable_->verticalHeader()->setVisible(false);
    cellTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cellTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    cellLayout->addWidget(cellTable_);

    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    auto *temperatureGroup = new QGroupBox(QStringLiteral("温度监控（最多 64 路）"));
    auto *temperatureLayout = new QVBoxLayout(temperatureGroup);
    temperatureTable_ = new QTableWidget(64, 3);
    temperatureTable_->setHorizontalHeaderLabels({
        QStringLiteral("探头"), QStringLiteral("温度 (°C)"), QStringLiteral("状态")
    });
    temperatureTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    temperatureTable_->verticalHeader()->setVisible(false);
    temperatureTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    temperatureLayout->addWidget(temperatureTable_);

    auto *alarmGroup = new QGroupBox(QStringLiteral("故障告警"));
    auto *alarmLayout = new QVBoxLayout(alarmGroup);
    alarmLabel_ = new QLabel(QStringLiteral("当前无故障（模拟）"));
    alarmLabel_->setWordWrap(true);
    alarmLayout->addWidget(alarmLabel_);
    rightLayout->addWidget(temperatureGroup, 3);
    rightLayout->addWidget(alarmGroup, 1);

    splitter->addWidget(cellGroup);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    auto *logGroup = new QGroupBox(QStringLiteral("运行日志"));
    auto *logLayout = new QVBoxLayout(logGroup);
    log_ = new QTextEdit;
    log_->setReadOnly(true);
    logLayout->addWidget(log_);
    logGroup->setMaximumHeight(170);
    layout->addWidget(logGroup);
    statusBar()->showMessage(QStringLiteral("仅供界面与流程演示 | 无真实 BMS 连接"));
}

void MainWindow::setTableRow(QTableWidget *table, int row,
                             const QStringList &columns, bool warning)
{
    for (int col = 0; col < columns.size(); ++col) {
        auto *item = table->item(row, col);
        if (!item) {
            item = new QTableWidgetItem;
            table->setItem(row, col, item);
        }
        item->setText(columns.at(col));
        item->setTextAlignment(Qt::AlignCenter);
        item->setBackground(warning ? QColor("#fff1f0") : QColor("#ffffff"));
        item->setForeground(warning ? QColor("#cf1322") : QColor("#1f2937"));
    }
}

void MainWindow::refreshDemo()
{
    if (liveMode_) return;
    ++sampleIndex_;
    const double soc = std::max(0.0, 78.6 - sampleIndex_ * 0.005);
    const double current = -24.8 + std::sin(sampleIndex_ * 0.15) * 3.0;
    double minCell = 100.0, maxCell = 0.0, maxTemp = -100.0;
    voltages_.clear();

    cellTable_->setRowCount(24);
    temperatureTable_->setRowCount(6);
    for (int i = 0; i < 24; ++i) {
        const double v = 3.61 + std::sin(sampleIndex_ * 0.09 + i * 0.7) * 0.025;
        voltages_.append(v);
        minCell = std::min(minCell, v);
        maxCell = std::max(maxCell, v);
        const bool bad = v < 3.0 || v > 4.2;
        setTableRow(cellTable_, i, {
            QStringLiteral("Cell %1").arg(i + 1, 2, 10, QChar('0')),
            QString::number(v, 'f', 3),
            bad ? QStringLiteral("异常") : QStringLiteral("正常")
        }, bad);
    }

    // 24-series virtual battery model: pack voltage is consistent with cell sum.
    double packVoltage = 0.0;
    for (const double v : voltages_)
        packVoltage += v;

    for (int i = 0; i < 6; ++i) {
        const double t = 30 + std::sin(sampleIndex_ * 0.05 + i) * 2.5;
        maxTemp = std::max(maxTemp, t);
        const bool bad = t > 60.0;
        setTableRow(temperatureTable_, i, {
            QStringLiteral("T%1").arg(i + 1),
            QString::number(t, 'f', 1),
            bad ? QStringLiteral("过温") : QStringLiteral("正常")
        }, bad);
    }

    const QStringList metrics = {
        QString::number(soc, 'f', 1) + QStringLiteral(" %"),
        QStringLiteral("96.2 %"),
        QString::number(packVoltage, 'f', 2) + QStringLiteral(" V"),
        QString::number(current, 'f', 1) + QStringLiteral(" A"),
        QString::number(packVoltage * current / 1000.0, 'f', 2) + QStringLiteral(" kW"),
        QString::number(maxTemp, 'f', 1) + QStringLiteral(" °C"),
        QString::number(minCell, 'f', 3) + QStringLiteral(" V"),
        QString::number((maxCell - minCell) * 1000.0, 'f', 1) + QStringLiteral(" mV")
    };
    for (int i = 0; i < metrics_.size(); ++i)
        metrics_[i]->setText(metrics.at(i));

    const bool warning = maxTemp > 60 || minCell < 3.0 || maxCell > 4.2;
    alarmLabel_->setText(warning
        ? QStringLiteral("检测到模拟数据异常")
        : QStringLiteral("当前无故障（模拟）"));
    alarmLabel_->setStyleSheet(warning
        ? QStringLiteral("color:#cf1322; font-weight:600;")
        : QStringLiteral("color:#389e0d; font-weight:600;"));

    if (timer_->isActive() && sampleIndex_ % 10 == 0)
        appendLog(QStringLiteral("BMS 模拟数据采样正常"));
}

void MainWindow::appendLog(const QString &message)
{
    log_->append(QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd HH:mm:ss  ")) + message);
}

void MainWindow::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出单体电压"),
        QStringLiteral("bms_cells.csv"),
        QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }
    QTextStream stream(&file);
    stream << "Cell,Voltage(V),Status\n";
    for (int i = 0; i < cellTable_->rowCount(); ++i) {
        for (int j = 0; j < 3; ++j) {
            if (j) stream << ',';
            stream << cellTable_->item(i, j)->text();
        }
        stream << '\n';
    }
    file.close();
    appendLog(QStringLiteral("已导出单体电压 CSV：") + path);
}

void MainWindow::toggleCan()
{
    if (adapter_->connected()) {
        adapter_->close();
        liveMode_ = false;
        canButton_->setText(QStringLiteral("连接 CAN"));
        channelBox_->setEnabled(true);
        deviceIndexBox_->setEnabled(true);
        connectionLabel_->setText(QStringLiteral("● CAN 已断开"));
        startButton_->setEnabled(true);
        appendLog(QStringLiteral("已断开 CAN 设备，可启动模拟模式"));
        return;
    }
    timer_->stop();
    protocol_.reset();
    QString error;
    if (!adapter_->open(4, deviceIndexBox_->value(), channelBox_->currentIndex(), &error)) {
        QMessageBox::warning(this, QStringLiteral("CAN 连接失败"),
                             error + QStringLiteral("\n请将 mmWaveRadar/libs/ControlCAN.dll 复制到本程序 EXE 同目录，检查驱动及设备。"));
        startButton_->setEnabled(true);
        stopButton_->setEnabled(false);
        return;
    }
    liveMode_ = true;
    startButton_->setEnabled(false);
    stopButton_->setEnabled(false);
    canButton_->setText(QStringLiteral("断开 CAN"));
    channelBox_->setEnabled(false);
    deviceIndexBox_->setEnabled(false);
    connectionLabel_->setText(QStringLiteral("● CAN 已连接 | 250 kbps"));
    appendLog(QStringLiteral("CAN 设备打开成功，等待 TKB BMS 扩展帧"));
}

void MainWindow::processCanFrames(const QVector<CanFrame> &frames)
{
    if (!liveMode_) return;
    int accepted = 0;
    for (const CanFrame &frame : frames)
        accepted += protocol_.parse(frame) ? 1 : 0;
    if (accepted)
        applyBmsState();
}

void MainWindow::applyBmsState()
{
    const auto &s = protocol_.state();
    const auto show = [this](int index, const QString &value) {
        metrics_[index]->setText(value);
    };
    show(0, s.hasStatus ? QString::number(s.soc) + QStringLiteral(" %") : QStringLiteral("--"));
    show(1, s.hasStatus ? QString::number(s.soh) + QStringLiteral(" %") : QStringLiteral("--"));
    show(2, s.hasSummary ? QString::number(s.packVoltage, 'f', 1) + QStringLiteral(" V") : QStringLiteral("--"));
    // Current signedness is not specified in the abridged vendor protocol.
    show(3, s.hasStatus ? QString::number(s.currentRawAmps, 'f', 1) + QStringLiteral(" A (raw)") : QStringLiteral("--"));
    show(4, QStringLiteral("待确认电流符号"));
    show(5, s.hasTempExtremes ? QString::number(s.maxTemperature) + QStringLiteral(" °C") : QStringLiteral("--"));
    show(6, s.hasCellExtremes ? QString::number(s.minCellVoltage, 'f', 3) + QStringLiteral(" V") : QStringLiteral("--"));
    show(7, s.hasCellExtremes ? QString::number((s.maxCellVoltage - s.minCellVoltage) * 1000, 'f', 0) + QStringLiteral(" mV") : QStringLiteral("--"));

    const int cellCount = s.hasSummary ? s.cellCount : 192;
    const int tempCount = s.hasSummary ? s.temperatureCount : 64;
    cellTable_->setRowCount(cellCount);
    temperatureTable_->setRowCount(tempCount);
    for (int i = 0; i < cellCount; ++i) {
        const double v = s.cells.at(i);
        setTableRow(cellTable_, i, {
            QStringLiteral("Cell %1").arg(i + 1, 3, 10, QChar('0')),
            v < 0 ? QStringLiteral("--") : QString::number(v, 'f', 3),
            v < 0 ? QStringLiteral("未收到") : QStringLiteral("已接收")
        });
    }
    for (int i = 0; i < tempCount; ++i) {
        const int t = s.temperatures.at(i);
        setTableRow(temperatureTable_, i, {
            QStringLiteral("T%1").arg(i + 1),
            t == -999 ? QStringLiteral("--") : QString::number(t),
            t == -999 ? QStringLiteral("未收到") : QStringLiteral("已接收")
        });
    }
    if (!s.hasFaultFrame)
        alarmLabel_->setText(QStringLiteral("等待故障状态帧 0x02058100"));
    else
        alarmLabel_->setText(s.faults.isEmpty() ? QStringLiteral("无已报告故障") : s.faults.join(QStringLiteral("；")));
    alarmLabel_->setStyleSheet(s.faults.isEmpty()
        ? QStringLiteral("color:#389e0d;") : QStringLiteral("color:#cf1322;font-weight:bold;"));
    statusBar()->showMessage(QStringLiteral("真实 CAN 接收模式 | 当前为只读监控 | 故障阈值和电流符号待厂商确认"));
}
