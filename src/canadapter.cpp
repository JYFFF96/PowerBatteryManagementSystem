#include "canadapter.h"
#include <QCoreApplication>
#include <QDir>

CanAdapter::CanAdapter(QObject *parent) : QThread(parent)
{
    qRegisterMetaType<QVector<CanFrame>>("QVector<CanFrame>");
}

CanAdapter::~CanAdapter() { close(); }

bool CanAdapter::open(int deviceType, int deviceIndex, int channel, int baudKbps, QString *error)
{
    close();
#ifndef Q_OS_WIN
    if (error) *error = QStringLiteral("ControlCAN.dll 仅支持 Windows");
    return false;
#else
    library_.setFileName(QDir(QCoreApplication::applicationDirPath())
                         .filePath(QStringLiteral("ControlCAN.dll")));
    if (!library_.load()) {
        if (error) *error = library_.errorString();
        return false;
    }
    openFn_ = reinterpret_cast<OpenFn>(library_.resolve("VCI_OpenDevice"));
    closeFn_ = reinterpret_cast<CloseFn>(library_.resolve("VCI_CloseDevice"));
    initFn_ = reinterpret_cast<InitFn>(library_.resolve("VCI_InitCAN"));
    startFn_ = reinterpret_cast<StartFn>(library_.resolve("VCI_StartCAN"));
    receiveFn_ = reinterpret_cast<ReceiveFn>(library_.resolve("VCI_Receive"));
    if (!openFn_ || !closeFn_ || !initFn_ || !startFn_ || !receiveFn_) {
        if (error) *error = QStringLiteral("ControlCAN.dll 缺少 VCI_* 导出函数");
        library_.unload();
        return false;
    }
    deviceType_ = quint32(deviceType);
    deviceIndex_ = quint32(deviceIndex);
    channel_ = quint32(channel);
    if (openFn_(deviceType_, deviceIndex_, 0) != 1) {
        if (error) *error = QStringLiteral("VCI_OpenDevice 失败");
        library_.unload();
        return false;
    }
    InitConfig cfg{};
    cfg.accCode = 0;
    cfg.accMask = 0xFFFFFFFFu;
    cfg.filter = 1;
    if (baudKbps == 250) {
        cfg.timing0 = 0x01;
        cfg.timing1 = 0x1C;
    } else if (baudKbps == 500) {
        cfg.timing0 = 0x00;
        cfg.timing1 = 0x1C;
    } else {
        closeFn_(deviceType_, deviceIndex_);
        library_.unload();
        if (error) *error = QStringLiteral("不支持的CAN波特率");
        return false;
    }
    cfg.mode = 0; // normal receiving mode
    if (initFn_(deviceType_, deviceIndex_, channel_, &cfg) != 1 ||
        startFn_(deviceType_, deviceIndex_, channel_) != 1) {
        closeFn_(deviceType_, deviceIndex_);
        library_.unload();
        if (error) *error = QStringLiteral("CAN 初始化或启动失败，请检查通道与波特率");
        return false;
    }
    opened_ = true;
    stopped_ = false;
    start();
    return true;
#endif
}

void CanAdapter::close()
{
    stopped_ = true;
    if (isRunning()) wait();
#ifdef Q_OS_WIN
    if (opened_ && closeFn_)
        closeFn_(deviceType_, deviceIndex_);
#endif
    opened_ = false;
    if (library_.isLoaded()) library_.unload();
}

void CanAdapter::run()
{
#ifdef Q_OS_WIN
    while (!stopped_) {
        CanObject buffer[256]{};
        const quint32 count = receiveFn_(deviceType_, deviceIndex_, channel_, buffer, 256, 0);
        if (count > 0 && count <= 256) {
            QVector<CanFrame> frames;
            frames.reserve(int(count));
            for (quint32 i = 0; i < count; ++i) {
                const CanObject &v = buffer[i];
                if (v.remoteFlag || v.dataLen != 8 || v.externFlag != 1)
                    continue;
                CanFrame f;
                f.id = v.id;
                f.extended = true;
                f.data = QByteArray(reinterpret_cast<const char *>(v.data), 8);
                frames.push_back(f);
            }
            if (!frames.isEmpty()) emit framesReceived(frames);
        }
        msleep(20);
    }
#endif
}
