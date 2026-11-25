#include "dashboardwindow.h"
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
class GraphWidget : public QWidget {
public:
    explicit GraphWidget(bool bars, QWidget *parent = nullptr) : QWidget(parent), bars_(bars) {
        setMinimumHeight(135);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    }
    void setSeries(const QVector<QVector<double>> &series, const QVector<QColor> &colors,
                   const QStringList &names = {}) {
        values_ = series; colors_ = colors; names_ = names; update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        QRectF plot = rect().adjusted(28,13,-12,-22);
        p.fillRect(plot,QColor("#0b1017"));
        p.setPen(QPen(QColor("#273446"),1));
        for (int i=0;i<=4;++i) {
            const double y=plot.top()+plot.height()*i/4.0;
            p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));
        }
        if (values_.isEmpty() || values_.first().isEmpty()) return;
        if (bars_) {
            const auto &v=values_.first();
            double lo=0, hi=1;
            for (double x:v) if (std::isfinite(x) && x>-500) { lo=qMin(lo,x); hi=qMax(hi,x); }
            const double step=plot.width()/qMax(1,v.size());
            for (int i=0;i<v.size();++i) {
                if (!std::isfinite(v[i]) || v[i]<-500) continue;
                const double h=qBound(0.0,(v[i]-lo)/(hi-lo+0.00001)*plot.height(),plot.height());
                QColor color=colors_.isEmpty()?blue:colors_.at(i%colors_.size());
                p.fillRect(QRectF(plot.left()+i*step+1, plot.bottom()-h, qMax(1.0,step-2),h),color);
            }
        } else {
            double lo=1e100,hi=-1e100;
            for(const auto &series:values_) for(double x:series)
                if(std::isfinite(x) && x>-500) {lo=qMin(lo,x);hi=qMax(hi,x);}
            if(lo>hi) return;
            if(qAbs(hi-lo)<0.01){hi+=0.5;lo-=0.5;}
            const int count=qMax(2,values_.first().size());
            for(int k=0;k<values_.size();++k) {
                const auto &s=values_[k];
                p.setPen(QPen(colors_.isEmpty()?blue:colors_[k%colors_.size()],2));
                QPainterPath path; bool first=true;
                for(int i=0;i<s.size();++i) {
                    if(!std::isfinite(s[i])||s[i]<-500){first=true;continue;}
                    QPointF pt(plot.left()+plot.width()*i/(count-1.0),
                               plot.bottom()-(s[i]-lo)/(hi-lo)*plot.height());
                    if(first){path.moveTo(pt);first=false;}else path.lineTo(pt);
                }
                p.drawPath(path);
            }
        }
    }
private:
    bool bars_;
    QVector<QVector<double>> values_;
    QVector<QColor> colors_;
    QStringList names_;
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
    adapterBox_=new QComboBox; adapterBox_->addItem(QStringLiteral("周立功 USBCAN-II"),4);
    adapterBox_->setMinimumWidth(146);hl->addWidget(adapterBox_);
    hl->addWidget(label(QStringLiteral("设备"),"muted"));
    deviceIndex_=new QSpinBox;deviceIndex_->setRange(0,15);deviceIndex_->setFixedWidth(54);hl->addWidget(deviceIndex_);
    hl->addWidget(label(QStringLiteral("通道"),"muted"));
    channelBox_=new QComboBox;channelBox_->addItems({QStringLiteral("CAN 0"),QStringLiteral("CAN 1")});
    channelBox_->setMinimumWidth(100);hl->addWidget(channelBox_);
    hl->addWidget(label(QStringLiteral("波特率"),"muted"));
    baudBox_=new QComboBox;baudBox_->addItem(QStringLiteral("250 kbps"));
    baudBox_->setToolTip(QStringLiteral("TKB V1.1 协议规定为 250 kbps"));hl->addWidget(baudBox_);
    connectButton_=button(QStringLiteral("连接 CAN"),"danger");hl->addWidget(connectButton_);
    demoButton_=button(QStringLiteral("启动模拟"),"primary");hl->addWidget(demoButton_);
    connection_=label(QStringLiteral("● 未连接"));hl->addWidget(connection_);
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
    const QStringList titles={QStringLiteral("总电压"),QStringLiteral("电流 (原始值)"),
        QStringLiteral("SOC 电量"),QStringLiteral("SOH 健康度"),QStringLiteral("充放电状态"),
        QStringLiteral("单体压差 ΔV")};
    const QStringList roles={"metricBlue","metricYellow","metricPurple","metricGreen","metricValue","metricYellow"};
    for(int i=0;i<titles.size();++i) {
        auto *card=new QFrame;card->setObjectName("panel");auto *cl=vertical(card,12);
        cl->addWidget(label(titles[i],"muted"));auto *v=label("--",roles[i]);overviewValues_<<v;
        cl->addWidget(v);cl->addWidget(label(QStringLiteral("TKB BMS · CAN 数据"),"muted"));
        grid->addWidget(card,i/5,i%5);
    }
    l->addLayout(grid);
    auto *charts=new QHBoxLayout;
    for(const auto &name:{QStringLiteral("总电压 / 电流实时波形"),QStringLiteral("SOC / SOH"),
                           QStringLiteral("最大 / 最小单体电压")}) {
        auto *g=new GraphWidget(false); graphs_<<g;
        charts->addWidget(panel(name,g),1);
    }
    l->addLayout(charts,2);
    auto *bottom=new QHBoxLayout;
    auto *vg=new GraphWidget(true);graphs_<<vg;
    auto *tg=new GraphWidget(true);graphs_<<tg;
    bottom->addWidget(panel(QStringLiteral("单体电压分布 · 最多192节"),vg),3);
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
    auto *toolbar=new QHBoxLayout;toolbar->addWidget(label(QStringLiteral("实时波形 · 总电压 / 电流原始值 / SOC / 单体极值"),"sectionTitle"));
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
        trends_.clear();updateGraphs();
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
    connect(clear,&QPushButton::clicked,this,[this]{monitorTable_->setRowCount(0);});
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
        recordState_->setText(QStringLiteral("● 正在录制"));});
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
