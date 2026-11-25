#pragma once
#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

struct CanFrame {
    quint32 id = 0;
    QByteArray data;
    bool extended = false;
};
Q_DECLARE_METATYPE(CanFrame)
Q_DECLARE_METATYPE(QVector<CanFrame>)

struct BmsState {
    int cellCount = 0, temperatureCount = 0;
    double packVoltage = 0, currentRawAmps = 0;
    int chargeState = -1, soc = -1, soh = -1, severeFaultCode = 0;
    double maxCellVoltage = 0, minCellVoltage = 0;
    int maxCellModule = 0, maxCellIndex = 0, minCellModule = 0, minCellIndex = 0;
    int maxTemperature = 0, minTemperature = 0;
    int maxTempModule = 0, maxTempIndex = 0, minTempModule = 0, minTempIndex = 0;
    QVector<double> cells = QVector<double>(192, -1.0);
    QVector<int> temperatures = QVector<int>(64, -999);
    QStringList faults;
    bool hasSummary = false, hasStatus = false, hasCellExtremes = false;
    bool hasTempExtremes = false, hasFaultFrame = false;
};

class BmsProtocol {
public:
    // Returns true only when a documented TKB BMS frame was processed.
    bool parse(const CanFrame &frame);
    void reset() { state_ = BmsState{}; }
    const BmsState &state() const { return state_; }
private:
    BmsState state_;
    static quint16 be16(const QByteArray &data, int offset);
};
