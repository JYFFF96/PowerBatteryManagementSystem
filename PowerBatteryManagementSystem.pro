QT += core gui widgets printsupport

TEMPLATE = app
TARGET = PowerBatteryManagementSystem
CONFIG += c++17
CONFIG -= app_bundle

SOURCES += \
    src/main.cpp \
    src/dashboardwindow.cpp \
    src/bmsprotocol.cpp \
    src/canadapter.cpp \
    thirdparty/qcustomplot/qcustomplot.cpp

HEADERS += \
    src/dashboardwindow.h \
    src/bmsprotocol.h \
    src/canadapter.h \
    thirdparty/qcustomplot/qcustomplot.h

INCLUDEPATH += src thirdparty/qcustomplot

win32 {
    CONFIG(debug, debug|release) {
        DESTDIR = $$OUT_PWD/debug
    } else {
        DESTDIR = $$OUT_PWD/release
    }

    # Use your licensed 64-bit ControlCAN.dll from mmWaveRadar/libs/.
    # Copy it next to the generated executable; no import .lib is required
    # because src/canadapter.cpp loads VCI_* symbols using QLibrary.
    DLL_PATH = $$PWD/libs/ControlCAN.dll
    exists($$DLL_PATH) {
        QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$DLL_PATH) $$shell_path($$DESTDIR) $$escape_expand(\\n)
    }
}
