#include "bmsprotocol.h"

quint16 BmsProtocol::be16(const QByteArray &d, int at)
{
    return (quint16(quint8(d.at(at))) << 8) | quint8(d.at(at + 1));
}

bool BmsProtocol::parse(const CanFrame &f)
{
    if (!f.extended || f.data.size() != 8 || (f.id & 0xFF00FFFFu) != 0x02008100u)
        return false;
    const auto b = [&](int i) { return int(quint8(f.data.at(i))); };
    const int fid = int((f.id >> 16) & 0xFF);
    switch (fid) {
    case 0x01:
        state_.cellCount = qBound(0, b(0), 192);
        state_.temperatureCount = qBound(0, b(1), 64);
        state_.packVoltage = be16(f.data, 2) * 0.1;
        state_.hasSummary = true;
        break;
    case 0x02:
        // The abridged document does not define the signed representation;
        // keep the raw unsigned magnitude until the vendor confirms it.
        state_.currentRawAmps = be16(f.data, 0) * 0.1;
        state_.chargeState = b(2);
        state_.soc = b(3);
        state_.soh = b(4);
        state_.severeFaultCode = b(5);
        state_.hasStatus = true;
        break;
    case 0x03:
        state_.maxCellVoltage = be16(f.data, 0) / 1000.0;
        state_.maxCellModule = b(2);
        state_.maxCellIndex = b(3);
        state_.minCellVoltage = be16(f.data, 4) / 1000.0;
        state_.minCellModule = b(6);
        state_.minCellIndex = b(7);
        state_.hasCellExtremes = true;
        break;
    case 0x04:
        state_.maxTemperature = b(0) - 50;
        state_.maxTempModule = b(1);
        state_.maxTempIndex = b(2);
        state_.minTemperature = b(3) - 50;
        state_.minTempModule = b(4);
        state_.minTempIndex = b(5);
        state_.hasTempExtremes = true;
        break;
    case 0x05: {
        struct FaultField { int byte; int shift; const char *name; };
        static const FaultField fields[] = {
            {0,0,"电源过压"}, {0,2,"电源欠压"}, {0,4,"绝缘检测模块通讯故障"},
            {0,6,"绝缘电阻过低"}, {2,2,"单体过压"}, {2,4,"单体欠压"},
            {2,6,"总压过高"}, {3,0,"总压过低"}, {3,4,"高温"},
            {3,6,"低温"}, {4,0,"充电过流"}, {4,2,"放电过流"},
            {4,4,"SOC过低"}
        };
        state_.faults.clear();
        for (const auto &field : fields) {
            const int level = (b(field.byte) >> field.shift) & 0x03;
            if (level)
                state_.faults << QStringLiteral("%1（%2级）")
                    .arg(QString::fromUtf8(field.name)).arg(level);
        }
        state_.hasFaultFrame = true;
        break;
    }
    default:
        if (fid >= 0x10 && fid <= 0x3f) {
            const int start = (fid - 0x10) * 4;
            for (int i = 0; i < 4; ++i)
                state_.cells[start + i] = be16(f.data, i * 2) / 1000.0;
        } else if (fid >= 0x46 && fid <= 0x4d) {
            const int start = (fid - 0x46) * 8;
            for (int i = 0; i < 8; ++i)
                state_.temperatures[start + i] = b(i) - 50;
        } else {
            return false;
        }
        break;
    }
    return true;
}
