#include "dashboardwindow.h"
#include "qcustomplot.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QStyle>
#include <QStringConverter>
#include <QPaintEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>

namespace {
const QColor blue("#5ab5ff"), yellow("#f5be43"), purple("#b977ff"), green("#53c98c"), red("#ed645d");
QLabel *label(const QString &value = {}, const QString &role = {}) {
    auto *w = new QLabel(value);
    if (!role.isEmpty()) w->setObjectName(role);
    return w;
}
QPushButton *button(const QString &title, const QString &role = {}) {
    auto *w = new QPushButton(title);
    if (!role.isEmpty()) w->setObjectName(role);
    w->setCursor(Qt::PointingHandCursor);
    return w;
}
QVBoxLayout *vertical(QWidget *parent, int margin = 12) {
    auto *l = new QVBoxLayout(parent);
    l->setContentsMargins(margin,margin,margin,margin);
    l->setSpacing(10);
    return l;
}
QString n(double x, int digits = 2) { return QString::number(x,'f',digits); }
QString canId(quint32 id) { return QString("0x%1").arg(id,8,16,QChar('0')).toUpper().replace("0X","0x"); }
QString bytes(const QByteArray &d) { return QString::fromLatin1(d.toHex(' ').toUpper()); }
QString classify(quint32 id) {
    const int fid = int((id >> 16) & 255);
    if (fid == 1) return QStringLiteral("FID-01 总压");
    if (fid == 2) return QStringLiteral("FID-02 状态/SOC");
    if (fid == 3) return QStringLiteral("FID-03 单体极值");
    if (fid == 4) return QStringLiteral("FID-04 温度极值");
    if (fid == 5) return QStringLiteral("FID-05 故障");
    if (fid >= 0x10 && fid <= 0x3f) return QStringLiteral("单体电压 FID=%1").arg(fid,0,16).toUpper();
    if (fid >= 0x46 && fid <= 0x4d) return QStringLiteral("温度 FID=%1").arg(fid,0,16).toUpper();
    return QStringLiteral("其他帧");
}
}
// All chart widgets use the same QCustomPlot 2.1.1 implementation as mmWaveRadar.
// The widget is owned and painted exclusively by the GUI thread.
class GraphWidget : public QCustomPlot {
public:
    explicit GraphWidget(bool bars, QWidget *parent = nullptr)
        : QCustomPlot(parent), bars_(bars) {
        setMinimumHeight(135);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setBackground(QColor("#0b1017"));
        setAntialiasedElements(QCP::aeNone);
        plotLayout()->setMargins(QMargins(5, 3, 3, 5));
        axisRect()->setBackground(QColor("#0b1017"));
        for (QCPAxis *axis : {xAxis, yAxis}) {
            axis->setBasePen(QPen(QColor("#334155")));
            axis->setTickPen(QPen(QColor("#334155")));
            axis->setSubTickPen(Qt::NoPen);
            axis->setTickLabelColor(QColor("#8597b0"));
            axis->setLabelColor(QColor("#aabbd1"));
            axis->grid()->setPen(QPen(QColor("#273446")));
            axis->grid()->setSubGridVisible(false);
        }
        xAxis->grid()->setVisible(false);
        yAxis->setNumberFormat("f");
        yAxis->setNumberPrecision(2);
        if (!bars_) {
            auto ticker=QSharedPointer<QCPAxisTickerDateTime>(new QCPAxisTickerDateTime);
            ticker->setDateTimeFormat("HH:mm:ss");
            ticker->setTickCount(5);
            xAxis->setTicker(ticker);
            xAxis->setTickLabelRotation(0);
            xAxis->setRange(QDateTime::currentMSecsSinceEpoch()/1000.0-60,
                            QDateTime::currentMSecsSinceEpoch()/1000.0);
        } else {
            auto ticker=QSharedPointer<QCPAxisTickerText>(new QCPAxisTickerText);
            xAxis->setTicker(ticker);
            for (int i=0;i<3;++i) {
                auto *bar=new QCPBars(xAxis,yAxis);
                bar->setPen(Qt::NoPen);
                bar->setBrush(QColor(i==0?"#5ab5ff":i==1?"#ed645d":"#b977ff"));
                barsSeries_.append(bar);
            }
        }
    }

    void setTimeSeries(const QVector<double> &times,
                       const QVector<QVector<double>> &series,
                       const QVector<QColor> &colors) {
        if (bars_)return;
        if (series.size()!=graphCount() || times.size()<lastCount_) {
            clearGraphs();
            for(int i=0;i<series.size();++i) {
                QCPAxis *axis=yAxis;
                if(series.size()==2 && i==1)axis=yAxis2;
                auto *g=addGraph(xAxis,axis);
                g->setPen(QPen(colors.isEmpty()?QColor("#5ab5ff"):colors[i%colors.size()],1.6));
                g->setAdaptiveSampling(true);
                g->setLineStyle(QCPGraph::lsLine);
            }
            lastCount_=0;
            lastKey_=-1;
        }
        if (series.isEmpty() || times.isEmpty()) {
            xAxis->setRange(QDateTime::currentMSecsSinceEpoch()/1000.0-60,
                            QDateTime::currentMSecsSinceEpoch()/1000.0);
            replot(QCustomPlot::rpQueuedReplot);
            return;
        }
        // In one hour at 2 Hz, at most ~7200 time samples are in memory.
        const double newest=times.last();
        const double cutoff=newest-3600.0;
        for(int k=0;k<series.size();++k) {
            const int maxCount=qMin(times.size(),series[k].size());
            for(int i=0;i<maxCount;++i) {
                if (times[i]<=lastKey_ || times[i]<cutoff)continue;
                const double value=series[k][i];
                if(std::isfinite(value) && value>-500)
                    graph(k)->addData(times[i],value);
            }
            graph(k)->data()->removeBefore(cutoff);
            graph(k)->rescaleValueAxis(false,true);
        }
        lastKey_=newest;lastCount_=times.size();
        double span=qMin(3600.0,qMax(60.0,newest-times.first()));
        xAxis->setRange(newest-span,newest);
        if(series.size()==2) {
            yAxis2->setVisible(true);
            yAxis2->setTickLabelColor(QColor("#f5be43"));
            yAxis2->setBasePen(QPen(QColor("#334155")));
        } else {
            yAxis2->setVisible(false);
        }
        if (graphCount()) {
            if(yAxis->range().size()<0.001)yAxis->scaleRange(1.1);
            else yAxis->scaleRange(1.08);
            if(series.size()==2)yAxis2->scaleRange(1.08);
        }
        replot(QCustomPlot::rpQueuedReplot);
    }

    void setSeries(const QVector<QVector<double>> &series,
                   const QVector<QColor> &colors, const QStringList & = {}) {
        if (!bars_)return;
        const auto values=series.isEmpty()?QVector<double>{}:series.first();
        QVector<double> xs[3],ys[3];
        double minValue=1e100,maxValue=-1e100;
        int minIndex=-1,maxIndex=-1;
        for(int i=0;i<values.size();++i) {
            const double v=values[i];
            if(!std::isfinite(v)||v<-500)continue;
            if(v<minValue){minValue=v;minIndex=i;}
            if(v>maxValue){maxValue=v;maxIndex=i;}
        }
        for(int i=0;i<values.size();++i) {
            const double v=values[i];if(!std::isfinite(v)||v<-500)continue;
            const int index=i==maxIndex?1:i==minIndex?2:0;
            xs[index].append(i+1);ys[index].append(v);
        }
        for(int j=0;j<3;++j) {
            barsSeries_[j]->setData(xs[j],ys[j],true);
            barsSeries_[j]->setWidth(values.size()>48?0.85:0.82);
        }
        xAxis->setRange(0.3,qMax(2,values.size())+0.7);
        const double margin=qMax(0.1,(maxValue-minValue)*0.2);
        if(minIndex>=0)yAxis->setRange(minValue-margin,maxValue+margin);
        else yAxis->setRange(0,1);
        auto ticker=QSharedPointer<QCPAxisTickerText>(new QCPAxisTickerText);
        for(int i=0;i<values.size();++i)if((values.size()<=24 && i%2==0)
              ||(values.size()>24 && i%qMax(1,values.size()/12)==0))
            ticker->addTick(i+1,QString::number(i+1));
        xAxis->setTicker(ticker);
        replot(QCustomPlot::rpQueuedReplot);
    }
private:
    bool bars_;
    QVector<QCPBars*> barsSeries_;
    int lastCount_=0;
    double lastKey_=-1;
};

DashboardWindow::DashboardWindow(QWidget *parent) : QMainWindow(parent) {
    can_=new CanAdapter(this);
    demoTimer_=new QTimer(this);
    uiTimer_=new QTimer(this);
    clock_.start();
    setupUi();
    connect(can_,&CanAdapter::framesReceived,this,[this](const QVector<CanFrame> &frames){consumeFrames(frames);});
    connect(demoTimer_,&QTimer::timeout,this,[this]{demoTick();});
    connect(uiTimer_,&QTimer::timeout,this,[this]{refreshUi();});
    demoTimer_->setInterval(500);
    uiTimer_->start(250);
    data_=BmsState{};
    updateConnectionText();
    refreshUi();
}
DashboardWindow::~DashboardWindow(){ if(can_)can_->close(); }

QWidget *DashboardWindow::panel(const QString &name,QWidget *content) {
    auto *w=new QFrame; w->setObjectName("panel");
    auto *l=vertical(w,12);
    auto *title=label(name,"sectionTitle");
    l->addWidget(title);
    if(content)l->addWidget(content,1);
    return w;
}
QWidget *DashboardWindow::page(QWidget *content) {
    auto *sc=new QScrollArea;
    sc->setWidgetResizable(true);
    sc->setFrameShape(QFrame::NoFrame);
    sc->setWidget(content);
    return sc;
}
QTableWidget *DashboardWindow::table(const QStringList &headers) {
    auto *t=new QTableWidget(0,headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->setVisible(false);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setAlternatingRowColors(true);
    t->setShowGrid(false);
    return t;
}

void DashboardWindow::setupUi() {
    setWindowTitle(QStringLiteral("动力电池及管理系统调试软件 v1.0"));
    resize(1540,900);setMinimumSize(1000,640);
    setStyleSheet(R"(
        * {font-family:"Microsoft YaHei";font-size:12px;color:#cbd8ec;}
        QMainWindow, QWidget#root, QScrollArea, QScrollArea>QWidget>QWidget {background:#0b1017;}
        QFrame#header, QFrame#sidebar, QFrame#pageHeader {background:#121923;border-bottom:1px solid #2a3444;}
        QFrame#sidebar {border-right:1px solid #2a3444;border-bottom:0;}
        QFrame#panel {background:#141b26;border:1px solid #303d50;border-radius:7px;}
        QLabel#appTitle {font-size:15px;font-weight:700;color:#f2f6ff;}
        QLabel#sectionTitle {font-weight:600;color:#dbe7fa;}
        QLabel#muted {font-size:11px;color:#768ba9;}
        QLabel#metricValue {font-size:25px;font-weight:700;color:#f1f5ff;}
        QLabel#metricBlue {font-size:25px;font-weight:700;color:#5ab5ff;}
        QLabel#metricYellow {font-size:25px;font-weight:700;color:#f5be43;}
        QLabel#metricPurple {font-size:25px;font-weight:700;color:#b977ff;}
        QLabel#metricGreen {font-size:25px;font-weight:700;color:#53c98c;}
        QLabel#metricRed {font-size:25px;font-weight:700;color:#ed645d;}
        QPushButton {background:#202a3b;border:1px solid #38485e;border-radius:4px;padding:7px 12px;}
        QPushButton:hover {background:#2c405b;border-color:#5aaef7;}
        QPushButton:disabled {color:#607087;background:#1a222e;}
        QPushButton#primary {background:#347bbd;color:white;border-color:#448ecc;}
        QPushButton#danger {background:#68272a;color:white;border-color:#bf4649;}
        QPushButton#nav {text-align:left;background:transparent;border:0;padding:12px 15px;color:#c5d2e5;}
        QPushButton#nav:hover {background:#1b2b40;}
        QPushButton#nav[active="true"] {background:#1d2d43;border:1px solid #32516d;color:#67baff;}
        QComboBox,QSpinBox,QDoubleSpinBox,QLineEdit {background:#1a2433;border:1px solid #3b4d66;
        border-radius:4px;padding:5px 8px;min-height:19px;selection-background-color:#427ab3;}
        QComboBox::drop-down {border:0;width:18px;}
        QTableWidget {background:#121923;alternate-background-color:#151e2b;border:1px solid #2f3b4e;
        gridline-color:#2d3848;selection-background-color:#284563;}
        QHeaderView::section {background:#1c2533;color:#849bbb;border:0;border-bottom:1px solid #354255;
        padding:8px;text-align:left;}
        QScrollBar:vertical {background:#131c28;width:9px;}
        QScrollBar::handle:vertical {background:#3a4a62;border-radius:4px;}
        QCheckBox {spacing:7px;}
    )");
    auto *root=new QWidget;root->setObjectName("root");setCentralWidget(root);
    auto *outer=new QVBoxLayout(root);outer->setSpacing(0);outer->setContentsMargins(0,0,0,0);
    auto *top=new QFrame;top->setObjectName("header");top->setFixedHeight(53);
    auto *hl=new QHBoxLayout(top);hl->setContentsMargins(15,4,15,4);hl->setSpacing(10);
    hl->addWidget(label(QStringLiteral("● 动力电池及管理系统调试软件"),"appTitle"));
    hl->addWidget(label(QStringLiteral("v1.0 · TKB BMS CAN 协议"),"muted"));
    hl->addStretch();
    hl->addWidget(label(QStringLiteral("适配器"),"muted"));
    adapterBox_=new QComboBox; adapterBox_->addItem(QStringLiteral("USBCAN-II"),4);
    adapterBox_->setMinimumWidth(146);hl->addWidget(adapterBox_);
    hl->addWidget(label(QStringLiteral("设备"),"muted"));
    deviceIndex_=new QComboBox;
    for (int i=0;i<3;++i) deviceIndex_->addItem(QString::number(i),i);
    deviceIndex_->setCurrentIndex(0);
    deviceIndex_->setMinimumWidth(58);hl->addWidget(deviceIndex_);
    hl->addWidget(label(QStringLiteral("通道"),"muted"));
    channelBox_=new QComboBox;
    channelBox_->addItem(QStringLiteral("CAN 1"),0); // UI 1-based, driver 0-based
    channelBox_->addItem(QStringLiteral("CAN 2"),1);
    channelBox_->setCurrentIndex(0);
    channelBox_->setMinimumWidth(100);hl->addWidget(channelBox_);
    hl->addWidget(label(QStringLiteral("波特率"),"muted"));
    baudBox_=new QComboBox;
    baudBox_->addItem(QStringLiteral("250 kbps"),250);
    baudBox_->addItem(QStringLiteral("500 kbps"),500);
    baudBox_->setCurrentIndex(0);
    baudBox_->setToolTip(QStringLiteral("TKB V1.1 默认 250 kbps；500 kbps 供硬件配置测试"));hl->addWidget(baudBox_);
    connectButton_=button(QStringLiteral("● 开始连接"),"primary");hl->addWidget(connectButton_);
    demoButton_=button(QStringLiteral("启动模拟"),"primary");hl->addWidget(demoButton_);
    connection_=label(QStringLiteral("● 已断开"));hl->addWidget(connection_);
    outer->addWidget(top);

    auto *body=new QHBoxLayout;body->setSpacing(0);body->setContentsMargins(0,0,0,0);
    auto *side=new QFrame;side->setObjectName("sidebar");side->setFixedWidth(185);
    auto *sl=vertical(side,7);sl->setContentsMargins(8,15,8,12);
    struct Nav {const char *text; const char *glyph;};
    const Nav navs[]={
        {"实时总览","▦"},{"单体电压","▥"},{"温度监控","◉"},{"故障诊断","⚠"},
        {"波形记录","∿"},{"报文监视","▤"},{"数据记录","↓"},{"CAN 下发","↑"},{"系统设置","⚙"}
    };
    for(int i=0;i<9;++i) {
        if(i==0||i==4||i==7||i==8)
            sl->addWidget(label(i==0?QStringLiteral("监控"):i==4?QStringLiteral("数据"):i==7?QStringLiteral("命令"):QStringLiteral("配置"),"muted"));
        auto *btn=button(QString::fromUtf8(navs[i].glyph)+"  "+QString::fromUtf8(navs[i].text),"nav");
        btn->setProperty("active",i==0);
        sl->addWidget(btn);navButtons_<<btn;
        connect(btn,&QPushButton::clicked,this,[this,i]{switchPage(i);});
    }
    sl->addStretch();
    footerValues_<<label("设备 ID: --","muted")<<label("接收帧: 0","muted")<<label("运行时间: 00:00","muted");
    for(auto *l:footerValues_)sl->addWidget(l);
    body->addWidget(side);

    auto *mainArea=new QWidget;auto *mainLayout=new QVBoxLayout(mainArea);
    mainLayout->setContentsMargins(0,0,0,0);mainLayout->setSpacing(0);
    auto *pageHeader=new QFrame;pageHeader->setObjectName("pageHeader");pageHeader->setFixedHeight(38);
    auto *ph=vertical(pageHeader,5);modeHint_=label(QStringLiteral("动力电池及管理系统调试软件 · 实时总览"),"sectionTitle");
    ph->addWidget(modeHint_);mainLayout->addWidget(pageHeader);
    stack_=new QStackedWidget;
    stack_->addWidget(createOverview());
    stack_->addWidget(createCells());
    stack_->addWidget(createTemperatures());
    stack_->addWidget(createFaults());
    stack_->addWidget(createWaveforms());
    stack_->addWidget(createMonitor());
    stack_->addWidget(createRecording());
    stack_->addWidget(createCommands());
    stack_->addWidget(createSettings());
    mainLayout->addWidget(stack_,1);body->addWidget(mainArea,1);outer->addLayout(body,1);
    connect(connectButton_,&QPushButton::clicked,this,[this]{toggleCan();});
    connect(demoButton_,&QPushButton::clicked,this,[this]{demo_?stopDemo():startDemo();});
}

void DashboardWindow::switchPage(int index) {
    stack_->setCurrentIndex(index);
    const QStringList names={QStringLiteral("实时总览"),QStringLiteral("单体电压"),
    QStringLiteral("温度监控"),QStringLiteral("故障诊断"),QStringLiteral("波形记录"),
    QStringLiteral("报文监视"),QStringLiteral("数据记录"),QStringLiteral("CAN 下发"),QStringLiteral("系统设置")};
    modeHint_->setText(QStringLiteral("动力电池及管理系统调试软件 · ")+names[index]);
    for(int i=0;i<navButtons_.size();++i) {
        navButtons_[i]->setProperty("active",i==index);
        navButtons_[i]->style()->unpolish(navButtons_[i]);
        navButtons_[i]->style()->polish(navButtons_[i]);
    }
}

QWidget *DashboardWindow::createOverview() {
    auto *w=new QWidget;auto *l=vertical(w,14);auto *grid=new QGridLayout;
    const QStringList titles={QStringLiteral("总电压"),QStringLiteral("电流"),
        QStringLiteral("SOC 电量"),QStringLiteral("SOH 健康度"),QStringLiteral("充放电状态"),
        QStringLiteral("单体压差 (ΔV)")};
    const QStringList roles={"metricBlue","metricYellow","metricPurple","metricGreen","metricValue","metricYellow"};
    for(int i=0;i<titles.size();++i) {
        auto *card=new QFrame;card->setObjectName("panel");auto *cl=vertical(card,12);
        cl->addWidget(label(titles[i],"muted"));
        auto *valueRow=new QHBoxLayout;
        valueRow->setContentsMargins(0,0,0,0);
        valueRow->setSpacing(4);
        auto *v=label("--",roles[i]);
        overviewValues_<<v;
        valueRow->addWidget(v,0,Qt::AlignBottom);
        const QStringList units={QStringLiteral("V"),QStringLiteral("A"),QStringLiteral("%"),
                                 QStringLiteral("%"),QString(),QStringLiteral("mV")};
        auto *unit=label(units[i]);
        unit->setStyleSheet("font-size:13px;color:#8496af;");
        overviewUnits_<<unit;
        if(!units[i].isEmpty())
            valueRow->addWidget(unit,0,Qt::AlignBottom);
        valueRow->addStretch(1);
        cl->addLayout(valueRow);
        auto *note=label(QStringLiteral("--"),"muted");
        note->setWordWrap(true);
        overviewNotes_<<note;
        cl->addWidget(note);
        grid->addWidget(card,i/5,i%5);
    }
    l->addLayout(grid);
    auto *charts=new QHBoxLayout;
    charts->setSpacing(12);
    for(const auto &name:{QStringLiteral("总电压 / 电流实时波形"),QStringLiteral("SOC / SOH"),
                           QStringLiteral("最大 / 最小单体电压")}) {
        auto *g=new GraphWidget(false); graphs_<<g;
        charts->addWidget(panel(name,g),1);
    }
    l->addLayout(charts,2);
    auto *bottom=new QHBoxLayout;
    auto *vg=new GraphWidget(true);graphs_<<vg;
    auto *tg=new GraphWidget(true);graphs_<<tg;
    bottom->addWidget(panel(QStringLiteral("单体电压分布 · 24节"),vg),3);
    bottom->addWidget(panel(QStringLiteral("温度分布 · 最多64路"),tg),2);
    l->addLayout(bottom,3);
    return page(w);
}

QWidget *DashboardWindow::createCells() {
    auto *w=new QWidget;auto *l=vertical(w,14);
    auto *row=new QHBoxLayout;
    const QStringList titles={QStringLiteral("最高单体电压"),QStringLiteral("最低单体电压"),
        QStringLiteral("平均单体电压"),QStringLiteral("压差 ΔV")};
    for(int i=0;i<4;++i) {
        auto *card=new QFrame;card->setObjectName("panel");
        auto *cl=vertical(card);cl->addWidget(label(titles[i],"muted"));
        auto *v=label("--",i==0?"metricRed":i==1?"metricPurple":i==3?"metricYellow":"metricBlue");
        cellValues_<<v;cl->addWidget(v);row->addWidget(card,1);
    }
    l->addLayout(row);
    auto *g=new GraphWidget(true);graphs_<<g;
    l->addWidget(panel(QStringLiteral("单体电压分布 · 协议 FID 0x10～0x3F"),g),2);
    cellTable_=table({QStringLiteral("编号"),QStringLiteral("单体电压(V)"),QStringLiteral("偏差(mV)"),QStringLiteral("状态")});
    auto *actions=new QWidget;auto *al=new QHBoxLayout(actions);al->setContentsMargins(0,0,0,0);
    al->addWidget(label(QStringLiteral("仅展示已收到的电压数据，未上报显示 --"),"muted"));al->addStretch();
    auto *exp=button(QStringLiteral("导出 CSV"),"primary");al->addWidget(exp);
    connect(exp,&QPushButton::clicked,this,[this]{exportTable(cellTable_,QStringLiteral("cell_voltages.csv"));});
    l->addWidget(actions);
    l->addWidget(cellTable_,4);
    return page(w);
}

QWidget *DashboardWindow::createTemperatures() {
    auto *w=new QWidget;auto *l=vertical(w,14);
    auto *row=new QHBoxLayout;
    for(const auto &title:{QStringLiteral("最高温度"),QStringLiteral("最低温度"),
                          QStringLiteral("平均温度"),QStringLiteral("温差")}) {
        auto *card=new QFrame;card->setObjectName("panel");auto *cl=vertical(card);
        cl->addWidget(label(title,"muted"));auto *v=label("--","metricBlue");
        temperatureValues_<<v;cl->addWidget(v);row->addWidget(card);
    }
    l->addLayout(row);
    auto *g=new GraphWidget(true);graphs_<<g;
    l->addWidget(panel(QStringLiteral("温度分布 · 协议 FID 0x46～0x4D（厂家待确认）"),g),2);
    temperatureTable_=table({QStringLiteral("编号"),QStringLiteral("温度(°C)"),QStringLiteral("与平均偏差(°C)"),QStringLiteral("状态")});
    l->addWidget(temperatureTable_,4);return page(w);
}

QWidget *DashboardWindow::createFaults() {
    auto *w=new QWidget;auto *l=vertical(w,14);
    auto *row=new QHBoxLayout;
    for(const auto &title:{QStringLiteral("电源/绝缘故障"),QStringLiteral("单体/总压故障"),
                          QStringLiteral("温度故障"),QStringLiteral("过流/SOC 故障")}) {
        auto *card=new QFrame;card->setObjectName("panel");auto *cl=vertical(card);
        cl->addWidget(label(title,"muted"));auto *v=label("--","metricValue");
        faultCounts_<<v;cl->addWidget(v);row->addWidget(card);
    }
    l->addLayout(row);
    l->addWidget(label(QStringLiteral("故障状态矩阵 · TKB V1.1 · 0x02058100 · 等级 0/1/2/3"),"sectionTitle"));
    faultTable_=table({QStringLiteral("故障名称"),QStringLiteral("等级"),QStringLiteral("来源字节"),QStringLiteral("状态")});
    faultNames_={QStringLiteral("电源过压"),QStringLiteral("电源欠压"),QStringLiteral("绝缘检测模块通讯故障"),
        QStringLiteral("绝缘电阻过低"),QStringLiteral("单体过压"),QStringLiteral("单体欠压"),
        QStringLiteral("总压过高"),QStringLiteral("总压过低"),QStringLiteral("高温"),
        QStringLiteral("低温"),QStringLiteral("充电过流"),QStringLiteral("放电过流"),QStringLiteral("SOC过低")};
    faultTable_->setRowCount(faultNames_.size());
    l->addWidget(faultTable_,1);return page(w);
}

QWidget *DashboardWindow::createWaveforms() {
    auto *w=new QWidget;auto *l=vertical(w,14);
    auto *toolbar=new QHBoxLayout;toolbar->addWidget(label(QStringLiteral("实时波形 · 总电压 / 电流 / SOC / 单体极值"),"sectionTitle"));
    toolbar->addStretch();
    auto *pause=button(QStringLiteral("暂停"));auto *clear=button(QStringLiteral("清除曲线"));
    toolbar->addWidget(pause);toolbar->addWidget(clear);l->addLayout(toolbar);
    auto *g=new GraphWidget(false);graphs_<<g;
    l->addWidget(panel(QStringLiteral("实时趋势 · 采样周期 500 ms（演示）/ 实时 CAN 数据"),g),1);
    connect(pause,&QPushButton::clicked,this,[this,pause]{
        waveformPaused_=!waveformPaused_;
        pause->setText(waveformPaused_?QStringLiteral("继续"):QStringLiteral("暂停"));
    });
    connect(clear,&QPushButton::clicked,this,[this]{
        trends_.clear();timestamps_.clear();graphsDirty_=true;updateGraphs();
    });
    return page(w);
}

QWidget *DashboardWindow::createMonitor() {
    auto *w=new QWidget;auto *l=vertical(w,14);
    auto *top=new QHBoxLayout;
    top->addWidget(label(QStringLiteral("RX · BMS→ECU CAN 上报"),"sectionTitle"));
    top->addStretch();
    auto *clear=button(QStringLiteral("清屏"));top->addWidget(clear);
    l->addLayout(top);
    monitorTable_=table({QStringLiteral("时间戳"),QStringLiteral("方向"),QStringLiteral("CAN ID"),
        QStringLiteral("DATA[0..7]"),QStringLiteral("解析")});
    l->addWidget(monitorTable_,1);
    connect(clear,&QPushButton::clicked,this,[this]{monitorTable_->setRowCount(0);pendingMonitorRows_.clear();});
    return page(w);
}

QWidget *DashboardWindow::createRecording() {
    auto *w=new QWidget;auto *l=vertical(w,14);
    auto *row=new QHBoxLayout;
    for(const auto &title:{QStringLiteral("录制时长"),QStringLiteral("已录制帧"),QStringLiteral("文件大小估计"),QStringLiteral("缓冲区")}) {
        auto *card=new QFrame;card->setObjectName("panel");auto *cl=vertical(card);
        cl->addWidget(label(title,"muted"));auto *v=label("--","metricBlue");
        recordingValues_<<v;cl->addWidget(v);row->addWidget(card);
    }
    l->addLayout(row);
    auto *controls=new QHBoxLayout;
    recordState_=label(QStringLiteral("● 已停止 · 点击开始录制收集原始 CAN 报文"),"muted");
    recordButton_=button(QStringLiteral("▶ 开始录制"),"primary");
    stopRecordButton_=button(QStringLiteral("■ 停止录制"),"danger");
    auto *exportBtn=button(QStringLiteral("导出 CSV"));
    controls->addWidget(recordState_);controls->addStretch();
    controls->addWidget(recordButton_);controls->addWidget(stopRecordButton_);controls->addWidget(exportBtn);
    l->addLayout(controls);
    recordTable_=table({QStringLiteral("时间戳"),QStringLiteral("方向"),QStringLiteral("CAN ID"),
        QStringLiteral("DATA[0..7]"),QStringLiteral("解析")});
    l->addWidget(recordTable_,1);
    recordDetail_=label(QStringLiteral("录制数据存于内存，导出 CSV 后可用于离线分析；超出缓冲上限覆盖最早帧。"),"muted");
    l->addWidget(recordDetail_);
    connect(recordButton_,&QPushButton::clicked,this,[this]{
        recording_=true;recordedCount_=0;captured_.clear();recordTable_->setRowCount(0);
        recordState_->setText(QStringLiteral("● 正在录制"));pendingRecordRows_.clear();});
    connect(stopRecordButton_,&QPushButton::clicked,this,[this]{
        recording_=false;recordState_->setText(QStringLiteral("● 已停止"));});
    connect(exportBtn,&QPushButton::clicked,this,[this]{exportFrames();});
    return page(w);
}

QWidget *DashboardWindow::createCommands() {
    auto *w=new QWidget;auto *l=vertical(w,14);
    l->addWidget(label(QStringLiteral("CAN 下发 · TKB 精简版仅提供 BMS→ECU 上报定义"),"sectionTitle"));
    auto *hint=new QLabel(QStringLiteral("当前协议没有给出有效的下发指令、控制条件与安全联锁。为避免误操作，正式 CAN 发送功能保持禁用。可在此查看接收帧结构和协议能力，取得厂家完整指令表后再实现。"));
    hint->setWordWrap(true);l->addWidget(hint);
    commandTable_=table({QStringLiteral("方向"),QStringLiteral("CAN ID / 范围"),QStringLiteral("说明"),QStringLiteral("状态")});
    const QVector<QStringList> rows={
        {QStringLiteral("BMS→ECU"),QStringLiteral("0x02018100 ～ 0x02058100"),QStringLiteral("状态 / 电压极值 / 温度极值 / 故障"),QStringLiteral("只读")},
        {QStringLiteral("BMS→ECU"),QStringLiteral("0x02108100 ～ 0x023F8100"),QStringLiteral("单体电压 1～192"),QStringLiteral("只读")},
        {QStringLiteral("BMS→ECU"),QStringLiteral("0x02468100 ～ 0x024D8100"),QStringLiteral("温度 1～64（范围待确认）"),QStringLiteral("只读")},
        {QStringLiteral("ECU→BMS"),QStringLiteral("未定义"),QStringLiteral("复位 / 查询 / 参数设置 / 指令下发"),QStringLiteral("禁用·待协议")}
    };
    commandTable_->setRowCount(rows.size());
    for(int r=0;r<rows.size();++r)for(int c=0;c<rows[r].size();++c)
        commandTable_->setItem(r,c,new QTableWidgetItem(rows[r][c]));
    l->addWidget(commandTable_,1);return page(w);
}

QWidget *DashboardWindow::createSettings() {
    auto *w=new QWidget;auto *l=vertical(w,14);
    auto *cols=new QHBoxLayout;auto *left=new QVBoxLayout;auto *right=new QVBoxLayout;
    auto *comm=new QWidget;auto *form=new QFormLayout(comm);
    form->addRow(QStringLiteral("适配器"),label(QStringLiteral("周立功 USBCAN-II · ControlCAN.dll")));
    form->addRow(QStringLiteral("帧格式"),label(QStringLiteral("CAN 2.0 扩展帧 · 8字节")));
    form->addRow(QStringLiteral("CAN 波特率"),label(QStringLiteral("250 kbps (固定)")));
    form->addRow(QStringLiteral("字节顺序"),label(QStringLiteral("Motorola / 大端")));
    left->addWidget(panel(QStringLiteral("通讯参数"),comm));

    auto *threshold=new QWidget;auto *tf=new QFormLayout(threshold);
    highVoltage_=new QDoubleSpinBox;highVoltage_->setRange(0,6000);highVoltage_->setDecimals(0);
    lowVoltage_=new QDoubleSpinBox;lowVoltage_->setRange(0,6000);lowVoltage_->setDecimals(0);
    highTemperature_=new QDoubleSpinBox;highTemperature_->setRange(-50,200);highTemperature_->setDecimals(0);
    lowTemperature_=new QDoubleSpinBox;lowTemperature_->setRange(-50,200);lowTemperature_->setDecimals(0);
    highVoltage_->setValue(3650);lowVoltage_->setValue(2750);
    highTemperature_->setValue(55);lowTemperature_->setValue(-30);
    tf->addRow(QStringLiteral("单体高压告警(mV)"),highVoltage_);
    tf->addRow(QStringLiteral("单体低压告警(mV)"),lowVoltage_);
    tf->addRow(QStringLiteral("高温告警(°C)"),highTemperature_);
    tf->addRow(QStringLiteral("低温告警(°C)"),lowTemperature_);
    left->addWidget(panel(QStringLiteral("本地提示阈值（非 BMS 保护参数）"),threshold));

    auto *battery=new QWidget;auto *bf=new QFormLayout(battery);
    cellCount_=new QSpinBox;cellCount_->setRange(1,192);cellCount_->setValue(24);
    tempCount_=new QSpinBox;tempCount_->setRange(1,64);tempCount_->setValue(4);
    bf->addRow(QStringLiteral("模拟单体数量"),cellCount_);
    bf->addRow(QStringLiteral("模拟温感数量"),tempCount_);
    right->addWidget(panel(QStringLiteral("电池参数（仅模拟模式）"),battery));
    auto *sampling=new QWidget;auto *sf=new QFormLayout(sampling);
    sf->addRow(QStringLiteral("状态帧周期"),label(QStringLiteral("400 ms（协议定义）")));
    sf->addRow(QStringLiteral("单体电压帧周期"),label(QStringLiteral("500 ms（协议定义）")));
    sf->addRow(QStringLiteral("温度帧周期"),label(QStringLiteral("600 ms（协议定义）")));
    recordLimit_=new QSpinBox;recordLimit_->setRange(100,100000);recordLimit_->setSingleStep(500);
    recordLimit_->setValue(5000);
    sf->addRow(QStringLiteral("录制缓冲帧数"),recordLimit_);
    right->addWidget(panel(QStringLiteral("采集周期与缓冲"),sampling));
    auto *actions=new QHBoxLayout;auto *restore=button(QStringLiteral("恢复默认"));auto *save=button(QStringLiteral("保存配置"),"primary");
    actions->addStretch();actions->addWidget(restore);actions->addWidget(save);right->addLayout(actions);
    cols->addLayout(left,1);cols->addLayout(right,1);l->addLayout(cols);l->addStretch();

    auto load=[this](bool defaults){
        QSettings s("PowerBatteryManagementSystem","Dashboard");
        highVoltage_->setValue(defaults?3650:s.value("highVoltage",3650).toDouble());
        lowVoltage_->setValue(defaults?2750:s.value("lowVoltage",2750).toDouble());
        highTemperature_->setValue(defaults?55:s.value("highTemperature",55).toDouble());
        lowTemperature_->setValue(defaults?-30:s.value("lowTemperature",-30).toDouble());
        cellCount_->setValue(defaults?24:s.value("cells",24).toInt());
        tempCount_->setValue(defaults?4:s.value("temps",4).toInt());
        recordLimit_->setValue(defaults?5000:s.value("recordLimit",5000).toInt());
        maxFrames_=recordLimit_->value();
    };
    load(false);
    connect(restore,&QPushButton::clicked,this,[load]{load(true);});
    connect(save,&QPushButton::clicked,this,[this]{
        QSettings s("PowerBatteryManagementSystem","Dashboard");
        s.setValue("highVoltage",highVoltage_->value());s.setValue("lowVoltage",lowVoltage_->value());
        s.setValue("highTemperature",highTemperature_->value());s.setValue("lowTemperature",lowTemperature_->value());
        s.setValue("cells",cellCount_->value());s.setValue("temps",tempCount_->value());
        s.setValue("recordLimit",recordLimit_->value());maxFrames_=recordLimit_->value();
        QMessageBox::information(this,QStringLiteral("保存成功"),QStringLiteral("本地参数已保存。设置不下发至 BMS。"));
    });
    return page(w);
}

void DashboardWindow::toggleCan() {
    if(can_->connected()) {
        can_->close();protocol_.reset();data_=BmsState{};lastReceiveMs_=-1;
        adapterBox_->setEnabled(true);channelBox_->setEnabled(true);
        deviceIndex_->setEnabled(true);baudBox_->setEnabled(true);
        updateConnectionText();refreshUi();return;
    }
    stopDemo();
    protocol_.reset();data_=BmsState{};displayDirty_=true;refreshUi();
    QString error;
    if(!can_->open(adapterBox_->currentData().toInt(),deviceIndex_->currentData().toInt(),channelBox_->currentData().toInt(),baudBox_->currentData().toInt(),&error)) {
        QMessageBox::warning(this,QStringLiteral("CAN 连接失败"),error+
            QStringLiteral("\n确认同目录有对应架构的 ControlCAN.dll，并已安装驱动。"));
        updateConnectionText();return;
    }
    protocol_.reset();data_=BmsState{};lastReceiveMs_=-1;displayDirty_=true;
    adapterBox_->setEnabled(false);channelBox_->setEnabled(false);
    deviceIndex_->setEnabled(false);baudBox_->setEnabled(false);
    updateConnectionText();refreshUi();
}
void DashboardWindow::startDemo() {
    if(can_->connected()) return;
    protocol_.reset();data_=BmsState{};lastReceiveMs_=-1;
    demo_=true;demoStep_=0;demoTimer_->start();
    demoButton_->setText(QStringLiteral("停止模拟"));
    updateConnectionText();demoTick();
}
void DashboardWindow::stopDemo() {
    demo_=false;demoTimer_->stop();
    if(demoButton_)demoButton_->setText(QStringLiteral("启动模拟"));
    updateConnectionText();
}
void DashboardWindow::updateConnectionText() {
    if (can_ && can_->connected()) {
        connectButton_->setText(QStringLiteral("● 断开连接"));
        connectButton_->setObjectName("danger");
        connection_->setText(QStringLiteral("● 已连接"));
        connection_->setStyleSheet("color:#55cd90;");
        demoButton_->setEnabled(false);
    } else {
        connectButton_->setText(QStringLiteral("● 开始连接"));
        connectButton_->setObjectName("primary");
        connection_->setText(QStringLiteral("● 已断开"));
        connection_->setStyleSheet("color:#ed645d;");
        demoButton_->setEnabled(true);
    }
    // Qt style sheets need repolish when the button's objectName changes.
    connectButton_->style()->unpolish(connectButton_);
    connectButton_->style()->polish(connectButton_);
}
void DashboardWindow::demoTick() {
    if(!demo_)return;
    ++demoStep_;
    const int count=cellCount_->value(), tempCount=tempCount_->value();
    QVector<int> voltages; int sumMv=0,maxMv=0,minMv=65535,maxIdx=0,minIdx=0;
    for(int i=0;i<count;++i) {
        const int mv=qRound(3200+50*std::sin(demoStep_*0.08+i*0.32)+8*std::cos(i*0.9));
        voltages<<mv;sumMv+=mv;
        if(mv>maxMv){maxMv=mv;maxIdx=i;}
        if(mv<minMv){minMv=mv;minIdx=i;}
    }
    QVector<int> temperatures;
    int maxT=-100,minT=100,maxTi=0,minTi=0;
    for(int i=0;i<tempCount;++i){
        const int t=qRound(27+3*std::sin(demoStep_*0.06+i));
        temperatures<<t;
        if(t>maxT){maxT=t;maxTi=i;}
        if(t<minT){minT=t;minTi=i;}
    }
    auto make=[this](int fid,QByteArray d){
        CanFrame f;f.id=0x02008100u|(quint32(fid)<<16);f.extended=true;f.data=d;
        ++receiveCount_;addFrame(f.id,f.data,QStringLiteral("SIM"));
        protocol_.parse(f);
    };
    auto set16=[](QByteArray &d,int offset,int value){
        d[offset]=char((value>>8)&255);d[offset+1]=char(value&255);
    };
    QByteArray d(8,char(0));
    d[0]=char(count);d[1]=char(tempCount);set16(d,2,qRound(sumMv/100.0));make(0x01,d);
    d.fill(char(0));set16(d,0,qRound(260+40*std::sin(demoStep_*0.07)));
    d[2]=char(2);d[3]=char(qBound(0,79-demoStep_/60,100));d[4]=char(96);make(0x02,d);
    d.fill(char(0));set16(d,0,maxMv);d[2]=char(1);d[3]=char(maxIdx+1);
    set16(d,4,minMv);d[6]=char(1);d[7]=char(minIdx+1);make(0x03,d);
    d.fill(char(0));d[0]=char(maxT+50);d[1]=char(1);d[2]=char(maxTi+1);
    d[3]=char(minT+50);d[4]=char(1);d[5]=char(minTi+1);make(0x04,d);
    d.fill(char(0));make(0x05,d);
    for(int i=0;i<count;i+=4){
        d.fill(char(0));
        for(int j=0;j<4&&i+j<count;++j)set16(d,2*j,voltages[i+j]);
        make(0x10+i/4,d);
    }
    for(int i=0;i<tempCount;i+=8){
        d.fill(char(0));
        for(int j=0;j<8&&i+j<tempCount;++j)d[j]=char(temperatures[i+j]+50);
        make(0x46+i/8,d);
    }
    data_=protocol_.state();
    appendSample();refreshUi();
}
void DashboardWindow::consumeFrames(const QVector<CanFrame> &frames) {
    if(!can_->connected())return;
    bool changed=false;
    for(const auto &frame:frames) {
        ++receiveCount_;
        addFrame(frame.id,frame.data,QStringLiteral("RX"));
        changed=protocol_.parse(frame)||changed;
    }
    if(changed) {
        data_=protocol_.state();
        lastReceiveMs_=clock_.elapsed();
        liveDataDirty_=true;
        displayDirty_=true;
    }
}
// Queued display updates: reception and decoding never insert QTableWidget rows.
void DashboardWindow::addFrame(quint32 id,const QByteArray &raw,const QString &direction) {
    const QStringList record={QDateTime::currentDateTime().toString("HH:mm:ss.zzz"),direction,
        canId(id),bytes(raw),classify(id)};
    pendingMonitorRows_.append(record);
    if(pendingMonitorRows_.size()>1000)
        pendingMonitorRows_.remove(0,pendingMonitorRows_.size()-1000);
    if(recording_) {
        ++recordedCount_;
        captured_.append(record);
        if(captured_.size()>maxFrames_)captured_.remove(0,captured_.size()-maxFrames_);
        pendingRecordRows_.append(record);
        if(pendingRecordRows_.size()>1000)
            pendingRecordRows_.remove(0,pendingRecordRows_.size()-1000);
    }
}

void DashboardWindow::flushFrameRows() {
    // Keep the total widget row count bounded and amortize per-item allocations.
    auto flush=[](QTableWidget *table,QVector<QStringList> &pending,int maxRows) {
        if(!table||pending.isEmpty())return;
        if(!table->isVisible()) {
            // The raw rows remain in the bounded queue until the page is viewed.
            return;
        }
        table->setUpdatesEnabled(false);
        const int batch=qMin(150,pending.size());
        for(int i=0;i<batch;++i) {
            const auto &record=pending[i];
            const int row=table->rowCount();
            table->insertRow(row);
            for(int col=0;col<record.size();++col)
                table->setItem(row,col,new QTableWidgetItem(record[col]));
        }
        pending.remove(0,batch);
        const int excess=table->rowCount()-maxRows;
        for(int i=0;i<excess;++i)table->removeRow(0);
        table->setUpdatesEnabled(true);
        table->scrollToBottom();
    };
    flush(monitorTable_,pendingMonitorRows_,1000);
    flush(recordTable_,pendingRecordRows_,1000);
}

void DashboardWindow::appendSample() {
    if(waveformPaused_)return;
    const double now=QDateTime::currentMSecsSinceEpoch()/1000.0;
    if(!timestamps_.isEmpty() && now<=timestamps_.last())return;
    double maxV=-999,minV=-999;
    if(data_.hasCellExtremes){maxV=data_.maxCellVoltage;minV=data_.minCellVoltage;}
    const QVector<double> sample={
        data_.hasSummary?data_.packVoltage:-999,
        data_.hasStatus?data_.currentRawAmps:-999,
        data_.hasStatus?double(data_.soc):-999,
        data_.hasStatus?double(data_.soh):-999,maxV,minV
    };
    if(trends_.size()!=sample.size())trends_=QVector<QVector<double>>(sample.size());
    timestamps_.append(now);
    for(int i=0;i<sample.size();++i)trends_[i].append(sample[i]);
    // Time-based retention, NOT the old 160-sample limit. Drop all points older
    // than one hour in all 6 series simultaneously, maintaining aligned indices.
    int expired=0;
    while(expired<timestamps_.size() && timestamps_[expired]<now-3600.0)++expired;
    if(expired>0) {
        timestamps_.remove(0,expired);
        for(auto &series:trends_)series.remove(0,expired);
    }
    graphsDirty_=true;
}
void DashboardWindow::updateGraphs() {
    if(!graphsDirty_ || graphs_.size()!=8)return;
    graphsDirty_=false;
    auto history=[this](std::initializer_list<int> indices) {
        QVector<QVector<double>> result;
        for(int i:indices)
            result<<((i<trends_.size())?trends_[i]:QVector<double>{});
        return result;
    };
    graphs_[0]->setTimeSeries(timestamps_,history({0,1}),{blue,yellow});
    graphs_[1]->setTimeSeries(timestamps_,history({2,3}),{purple,green});
    graphs_[2]->setTimeSeries(timestamps_,history({4,5}),{red,blue});
    QVector<double> cells,temp;
    const int nCells=data_.hasSummary?data_.cellCount:(demo_?data_.cellCount:0);
    const int nTemps=data_.hasSummary?data_.temperatureCount:(demo_?data_.temperatureCount:0);
    QVector<double> overviewCells(24,-999.0);
    for(int i=0;i<qMin(24,nCells);++i)overviewCells[i]=data_.cells[i];
    for(int i=0;i<nCells;++i)cells<<data_.cells[i];
    for(int i=0;i<nTemps;++i)temp<<(data_.temperatures[i]==-999?-999.0:double(data_.temperatures[i]));
    graphs_[3]->setSeries({overviewCells},{blue});
    graphs_[4]->setSeries({temp},{green});
    graphs_[5]->setSeries({cells},{blue});
    graphs_[6]->setSeries({temp},{green});
    graphs_[7]->setTimeSeries(timestamps_,history({0,1,2,4,5}),{blue,yellow,purple,green,red});
}
void DashboardWindow::updateFaults() {
    const struct {const char *name;int byte;int shift;} fields[]={
        {"电源过压",0,0},{"电源欠压",0,2},{"绝缘检测模块通讯故障",0,4},
        {"绝缘电阻过低",0,6},{"单体过压",2,2},{"单体欠压",2,4},
        {"总压过高",2,6},{"总压过低",3,0},{"高温",3,4},
        {"低温",3,6},{"充电过流",4,0},{"放电过流",4,2},{"SOC过低",4,4}
    };
    int counts[4]={0,0,0,0};
    for(int i=0;i<13;++i) {
        const QString name=QString::fromUtf8(fields[i].name);
        int level=-1;
        if(data_.hasFaultFrame) {
            level=0;
            for(int j=1;j<=3;++j)
                if(data_.faults.contains(name+QStringLiteral("（%1级）").arg(j)))level=j;
        }
        if(level>0)counts[i<4?0:i<8?1:i<10?2:3]++;
        QStringList row={name,level<0?QStringLiteral("--"):QString::number(level),
            QStringLiteral("BYTE%1 bit[%2:%3]").arg(fields[i].byte+1).arg(fields[i].shift+1).arg(fields[i].shift),
            level<0?QStringLiteral("未收到"):level==0?QStringLiteral("无故障"):QStringLiteral("%1级故障").arg(level)};
        for(int j=0;j<row.size();++j){
            auto *item=faultTable_->item(i,j);
            if(!item){item=new QTableWidgetItem;faultTable_->setItem(i,j,item);}
            item->setText(row[j]);
            item->setForeground(level>0?(level==3?red:yellow):QColor("#a9c4dc"));
        }
    }
    for(int i=0;i<4;++i)faultCounts_[i]->setText(data_.hasFaultFrame?QString::number(counts[i])+QStringLiteral(" 项"):QStringLiteral("--"));
}
void DashboardWindow::refreshUi() {
    updateConnectionText();
    const qint64 nowMs=clock_.elapsed();
    if(can_ && can_->connected() && liveDataDirty_ && nowMs-lastSampleMs_>=500) {
        liveDataDirty_=false;
        lastSampleMs_=nowMs;
        appendSample();
    }
    flushFrameRows();
    if(footerValues_.size()==3){
        footerValues_[0]->setText(QStringLiteral("设备 ID: 0x02018100"));
        footerValues_[1]->setText(QStringLiteral("接收帧: ")+QString::number(receiveCount_));
        const qint64 secs=clock_.elapsed()/1000;
        footerValues_[2]->setText(QStringLiteral("运行时间: %1:%2")
            .arg(secs/60,2,10,QChar('0')).arg(secs%60,2,10,QChar('0')));
    }
    if(overviewValues_.size()!=6 || overviewNotes_.size()!=6 || overviewUnits_.size()!=6)return;
    if(!displayDirty_ && !demo_ && !liveDataDirty_ && !graphsDirty_)return;
    displayDirty_=false;
    const int numCells=data_.hasSummary?data_.cellCount:0;
    const int numTemps=data_.hasSummary?data_.temperatureCount:0;
    double sum=0;int count=0;
    for(int i=0;i<numCells;++i)if(data_.cells[i]>=0) {
        sum+=data_.cells[i];++count;
    }
    const double avg=count?sum/count:0;

    // Reference pack specification is 24 x 3.2 V; not a BMS-reported nominal voltage.
    const QString nominalText=QStringLiteral("标称 76.8V (24×3.2V) · 均值 %1")
        .arg(count?n(avg,2)+QStringLiteral(" V"):QStringLiteral("--"));
    overviewValues_[0]->setText(data_.hasSummary?n(data_.packVoltage,2):QStringLiteral("--"));
    overviewNotes_[0]->setText(nominalText);
    overviewNotes_[0]->setStyleSheet("color:#b977ff;font-size:11px;");

    // Show the unsigned raw value * 0.1 A; charge/discharge direction comes from the separate state field.
    overviewValues_[1]->setText(data_.hasStatus?n(data_.currentRawAmps,2):QStringLiteral("--"));
    const QString direction=data_.chargeState==1?QStringLiteral("充电中"):
        data_.chargeState==2?QStringLiteral("放电中"):
        data_.chargeState==0?QStringLiteral("空闲"):QStringLiteral("状态未知");
    overviewNotes_[1]->setText(data_.hasStatus
        ?direction+QStringLiteral(" · %1 A").arg(n(data_.currentRawAmps,2))
        :QStringLiteral("等待电流/状态报文"));
    overviewNotes_[1]->setStyleSheet(QStringLiteral("color:%1;font-size:11px;")
        .arg(data_.chargeState==1?"#53c98c":data_.chargeState==2?"#f5be43":"#7a91ad"));

    overviewValues_[2]->setText(data_.hasStatus?n(data_.soc,2):QStringLiteral("--"));
    // Neither estimated range nor vehicle energy consumption is in this protocol.
    overviewNotes_[2]->setText(data_.hasStatus
        ?QStringLiteral("SOH %1% · 续航约 -- km（未提供）").arg(data_.soh)
        :QStringLiteral("SOH -- · 续航约 -- km"));
    overviewNotes_[2]->setStyleSheet("color:#b977ff;font-size:11px;");

    overviewValues_[3]->setText(data_.hasStatus?QString::number(data_.soh):QStringLiteral("--"));
    const QString health=!data_.hasStatus?QStringLiteral("--"):
        data_.soh>=95?QStringLiteral("优秀"):
        data_.soh>=85?QStringLiteral("良好"):QStringLiteral("一般");
    overviewNotes_[3]->setText(QStringLiteral("%1 · 循环 -- 次（协议未提供）").arg(health));
    overviewNotes_[3]->setStyleSheet("color:#71839c;font-size:11px;");

    // Charge/discharge indication is based on BYTE3 state, not on unsigned current.
    QString state=QStringLiteral("未知");
    QString color=QStringLiteral("#7a91ad");
    if(data_.hasStatus) {
        if(data_.chargeState==0){state=QStringLiteral("空闲");color="#8797ab";}
        else if(data_.chargeState==1){state=QStringLiteral("充电");color="#53c98c";}
        else if(data_.chargeState==2){state=QStringLiteral("放电");color="#f5be43";}
    }
    overviewValues_[4]->setText(QStringLiteral("● ")+state);
    overviewValues_[4]->setStyleSheet(QStringLiteral("font-size:21px;font-weight:700;color:%1;").arg(color));

    // The fault byte reports 13 known fields with level 1/2/3, not trip counts.
    int severe=0;
    for(const QString &fault:data_.faults) {
        if(fault.contains(QStringLiteral("（3级）")))++severe;
    }
    overviewNotes_[4]->setText(data_.hasFaultFrame
        ?QStringLiteral("三级故障：%1 项 / 当前故障 %2 项").arg(severe).arg(data_.faults.size())
        :QStringLiteral("三级故障：-- / 当前故障 --"));
    overviewNotes_[4]->setStyleSheet("color:#71839c;font-size:11px;");

    overviewValues_[5]->setText(data_.hasCellExtremes
        ?n((data_.maxCellVoltage-data_.minCellVoltage)*1000,2)
        :QStringLiteral("--"));
    overviewNotes_[5]->setText(data_.hasCellExtremes
        ?QStringLiteral("%1 V (最高) - %2 V (最低) · 阈值 %3mV")
            .arg(n(data_.maxCellVoltage,2)).arg(n(data_.minCellVoltage,2))
            .arg(QStringLiteral("50"))
        :QStringLiteral("最高 -- / 最低 -- · 阈值 50mV"));
    overviewNotes_[5]->setStyleSheet("color:#b977ff;font-size:11px;");
    const bool cellExt=data_.hasCellExtremes;
    const QStringList cv={cellExt?n(data_.maxCellVoltage,3)+QStringLiteral(" V"):QStringLiteral("--"),
        cellExt?n(data_.minCellVoltage,3)+QStringLiteral(" V"):QStringLiteral("--"),
        count?n(avg,3)+QStringLiteral(" V"):QStringLiteral("--"),
        cellExt?n((data_.maxCellVoltage-data_.minCellVoltage)*1000,1)+QStringLiteral(" mV"):QStringLiteral("--")};
    for(int i=0;i<cellValues_.size();++i)cellValues_[i]->setText(cv[i]);
    if(cellTable_&&cellTable_->rowCount()!=numCells)cellTable_->setRowCount(numCells);
    for(int i=0;i<numCells;++i){
        double v=data_.cells[i];bool valid=v>=0;
        bool alarm=valid&&(v*1000>highVoltage_->value()||v*1000<lowVoltage_->value());
        QStringList row={QString("#%1").arg(i+1,3,10,QChar('0')),
            valid?n(v,3):QStringLiteral("--"),
            valid&&count?n((v-avg)*1000,1):QStringLiteral("--"),
            !valid?QStringLiteral("未收到"):alarm?QStringLiteral("本地阈值告警"):QStringLiteral("正常")};
        for(int j=0;j<row.size();++j){
            auto *item=cellTable_->item(i,j);if(!item){item=new QTableWidgetItem;cellTable_->setItem(i,j,item);}
            item->setText(row[j]);item->setForeground(alarm?red:QColor("#c5d8ed"));
        }
    }
    double tempSum=0;int tempSamples=0,maxT=-999,minT=999;
    for(int i=0;i<numTemps;++i)if(data_.temperatures[i]!=-999) {
        int t=data_.temperatures[i];tempSum+=t;++tempSamples;maxT=qMax(maxT,t);minT=qMin(minT,t);
    }
    const double tempAvg=tempSamples?tempSum/tempSamples:0;
    const QStringList tv={
        data_.hasTempExtremes?QString::number(data_.maxTemperature)+QStringLiteral(" °C"):QStringLiteral("--"),
        data_.hasTempExtremes?QString::number(data_.minTemperature)+QStringLiteral(" °C"):QStringLiteral("--"),
        tempSamples?n(tempAvg,1)+QStringLiteral(" °C"):QStringLiteral("--"),
        data_.hasTempExtremes?QString::number(data_.maxTemperature-data_.minTemperature)+QStringLiteral(" °C"):QStringLiteral("--")
    };
    for(int i=0;i<temperatureValues_.size();++i)temperatureValues_[i]->setText(tv[i]);
    if(temperatureTable_&&temperatureTable_->rowCount()!=numTemps)temperatureTable_->setRowCount(numTemps);
    for(int i=0;i<numTemps;++i) {
        int t=data_.temperatures[i];bool valid=t!=-999;
        bool alarm=valid&&(t>highTemperature_->value()||t<lowTemperature_->value());
        QStringList row={QString("#%1").arg(i+1,2,10,QChar('0')),
            valid?QString::number(t):QStringLiteral("--"),
            valid?n(t-tempAvg,1):QStringLiteral("--"),
            !valid?QStringLiteral("未收到"):alarm?QStringLiteral("本地阈值告警"):QStringLiteral("正常")};
        for(int j=0;j<row.size();++j) {
            auto *item=temperatureTable_->item(i,j);
            if(!item){item=new QTableWidgetItem;temperatureTable_->setItem(i,j,item);}
            item->setText(row[j]);item->setForeground(alarm?red:QColor("#c5d8ed"));
        }
    }
    updateFaults();updateGraphs();
    if(can_ && can_->connected())liveDataDirty_=false;
    if(recordingValues_.size()==4) {
        recordingValues_[0]->setText(recording_?QStringLiteral("录制中"):QStringLiteral("已停止"));
        recordingValues_[1]->setText(QString::number(recordedCount_)+QStringLiteral(" 帧"));
        recordingValues_[2]->setText(n(captured_.size()*100.0/1024.0,1)+QStringLiteral(" KB"));
        recordingValues_[3]->setText(QString::number(captured_.size())+QStringLiteral(" 帧"));
    }
}
void DashboardWindow::exportTable(QTableWidget *source,const QString &name) {
    const QString path=QFileDialog::getSaveFileName(this,QStringLiteral("导出 CSV"),name,QStringLiteral("CSV (*.csv)"));
    if(path.isEmpty())return;
    QFile f(path);
    if(!f.open(QIODevice::WriteOnly|QIODevice::Text)){
        QMessageBox::warning(this,QStringLiteral("导出失败"),f.errorString());return;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    auto cell=[](QString s){s.replace("\"","\"\"");return QStringLiteral("\"")+s+QStringLiteral("\"");};
    for(int c=0;c<source->columnCount();++c){if(c)out<<',';out<<cell(source->horizontalHeaderItem(c)->text());}
    out<<'\n';
    for(int r=0;r<source->rowCount();++r){
        for(int c=0;c<source->columnCount();++c) {
            if(c)out<<',';
            auto *item=source->item(r,c);
            out<<cell(item?item->text():QString{});
        }
        out<<'\n';
    }
}
void DashboardWindow::exportFrames() {exportTable(recordTable_,QStringLiteral("bms_can_log.csv"));}
