#pragma once
#include "bmsprotocol.h"
#include "canadapter.h"
#include <QMainWindow>
#include <QElapsedTimer>
#include <QVector>

class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QScrollArea;
class GraphWidget;

class DashboardWindow : public QMainWindow {
public:
    explicit DashboardWindow(QWidget *parent = nullptr);
    ~DashboardWindow() override;
private:
    QWidget *createOverview();
    QWidget *createCells();
    QWidget *createTemperatures();
    QWidget *createFaults();
    QWidget *createWaveforms();
    QWidget *createMonitor();
    QWidget *createRecording();
    QWidget *createCommands();
    QWidget *createSettings();
    QWidget *page(QWidget *content);
    QWidget *panel(const QString &name, QWidget *content);
    QTableWidget *table(const QStringList &headers);
    void setupUi();
    void switchPage(int index);
    void toggleCan();
    void startDemo();
    void stopDemo();
    void demoTick();
    void consumeFrames(const QVector<CanFrame> &frames);
    void refreshUi();
    void addFrame(quint32 id, const QByteArray &data, const QString &direction);
    void appendSample();
    void updateGraphs();
    void exportTable(QTableWidget *source, const QString &name);
    void exportFrames();
    void updateConnectionText();
    void updateFaults();

    BmsProtocol protocol_;
    BmsState data_;
    CanAdapter *can_ = nullptr;
    QTimer *demoTimer_ = nullptr;
    QTimer *uiTimer_ = nullptr;
    QElapsedTimer clock_;
    bool demo_ = false;
    bool recording_ = false;
    bool waveformPaused_ = false;
    int demoStep_ = 0;
    quint64 receiveCount_ = 0;
    int recordedCount_ = 0;
    qint64 lastReceiveMs_ = -1;
    int maxFrames_ = 2000;
    QVector<QStringList> captured_;
    QVector<QStringList> pendingMonitorRows_;
    QVector<QStringList> pendingRecordRows_;
    bool displayDirty_ = true;
    void flushFrameRows();
    QVector<QVector<double>> trends_;
    QVector<double> timestamps_;
    bool graphsDirty_ = true;
    bool liveDataDirty_ = false;
    qint64 lastSampleMs_ = -500;
    QVector<GraphWidget *> graphs_;
    QVector<QPushButton *> navButtons_;
    QVector<QLabel *> overviewValues_;
    QVector<QLabel *> overviewUnits_;
    QVector<QLabel *> overviewNotes_;
    QVector<QLabel *> cellValues_;
    QVector<QLabel *> faultCounts_;
    QVector<QLabel *> recordingValues_;
    QVector<QLabel *> temperatureValues_;
    QVector<QLabel *> footerValues_;
    QVector<QString> faultNames_;
    QStackedWidget *stack_ = nullptr;
    QLabel *connection_ = nullptr;
    QLabel *recordState_ = nullptr;
    QLabel *modeHint_ = nullptr;
    QLabel *recordDetail_ = nullptr;
    QPushButton *connectButton_ = nullptr;
    QPushButton *demoButton_ = nullptr;
    QPushButton *recordButton_ = nullptr;
    QPushButton *stopRecordButton_ = nullptr;
    QComboBox *adapterBox_ = nullptr;
    QComboBox *channelBox_ = nullptr;
    QComboBox *baudBox_ = nullptr;
    QComboBox *deviceIndex_ = nullptr;
    QSpinBox *cellCount_ = nullptr;
    QSpinBox *tempCount_ = nullptr;
    QSpinBox *recordLimit_ = nullptr;
    QDoubleSpinBox *highVoltage_ = nullptr;
    QDoubleSpinBox *lowVoltage_ = nullptr;
    QDoubleSpinBox *highTemperature_ = nullptr;
    QDoubleSpinBox *lowTemperature_ = nullptr;
    QTableWidget *cellTable_ = nullptr;
    QTableWidget *temperatureTable_ = nullptr;
    QTableWidget *monitorTable_ = nullptr;
    QTableWidget *recordTable_ = nullptr;
    QTableWidget *faultTable_ = nullptr;
    QTableWidget *commandTable_ = nullptr;
};
