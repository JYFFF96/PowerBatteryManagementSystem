#pragma once
#include "bmsprotocol.h"
#include <QLibrary>
#include <QThread>
#include <atomic>

class CanAdapter : public QThread {
    Q_OBJECT
public:
    explicit CanAdapter(QObject *parent = nullptr);
    ~CanAdapter() override;
    bool open(int deviceType, int deviceIndex, int channel, int baudKbps, QString *error);
    void close();
    bool connected() const { return opened_; }
signals:
    void framesReceived(const QVector<CanFrame> &frames);
protected:
    void run() override;
private:
#ifdef Q_OS_WIN
    struct CanObject {
        quint32 id, timestamp;
        quint8 timeFlag, sendType, remoteFlag, externFlag, dataLen;
        quint8 data[8], reserved[3];
    };
    struct InitConfig {
        quint32 accCode, accMask, reserved;
        quint8 filter, timing0, timing1, mode;
    };
    using OpenFn = quint32 (__stdcall *)(quint32,quint32,quint32);
    using CloseFn = quint32 (__stdcall *)(quint32,quint32);
    using InitFn = quint32 (__stdcall *)(quint32,quint32,quint32,InitConfig *);
    using StartFn = quint32 (__stdcall *)(quint32,quint32,quint32);
    using ReceiveFn = quint32 (__stdcall *)(quint32,quint32,quint32,CanObject *,quint32,int);
    OpenFn openFn_ = nullptr;
    CloseFn closeFn_ = nullptr;
    InitFn initFn_ = nullptr;
    StartFn startFn_ = nullptr;
    ReceiveFn receiveFn_ = nullptr;
#endif
    QLibrary library_;
    std::atomic_bool stopped_{true};
    bool opened_ = false;
    quint32 deviceType_ = 4, deviceIndex_ = 0, channel_ = 0;
};
