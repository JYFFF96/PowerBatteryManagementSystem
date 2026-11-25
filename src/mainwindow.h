#pragma once

#include <QMainWindow>
#include "bmsprotocol.h"
#include "canadapter.h"
#include <QVector>

class QLabel;
class QTableWidget;
class QTextEdit;
class QTimer;
class QPushButton;
class QComboBox;
class QSpinBox;

class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void buildUi();
    void applyBmsState();
    void processCanFrames(const QVector<CanFrame> &frames);
    void toggleCan();
    void refreshDemo();
    void exportCsv();
    void appendLog(const QString &message);
    void setTableRow(QTableWidget *table, int row,
                     const QStringList &columns, bool warning = false);

    QVector<QLabel *> metrics_;
    QTableWidget *cellTable_ = nullptr;
    QTableWidget *temperatureTable_ = nullptr;
    QLabel *alarmLabel_ = nullptr;
    QLabel *connectionLabel_ = nullptr;
    QTextEdit *log_ = nullptr;
    QTimer *timer_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    int sampleIndex_ = 0;
    QVector<double> voltages_;
    BmsProtocol protocol_;
    CanAdapter *adapter_ = nullptr;
    QPushButton *canButton_ = nullptr;
    QComboBox *channelBox_ = nullptr;
    QSpinBox *deviceIndexBox_ = nullptr;
    bool liveMode_ = false;
};
