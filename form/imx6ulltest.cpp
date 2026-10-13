// ============================================================================
// form/imx6ulltest.cpp —— 板载传感器页面（第 3 级）
//
// 【这一页的数据流】以 AP3216C 那条线为例，从上到下走一遍：
//
//   Ap3216c 里的定时器（500ms 一次）
//        └─ 读 I2C 传感器 → 更新自己肚子里的 alsdata/psdata/irdata
//        └─ emit ap3216cDataChanged()          ← 喊一声"数据变了"
//              └─ 本页的槽 getAp3216cData()     ← 构造函数里 connect 上的
//                    ├─ 把字符串塞进三个数值框（alsEdit/irEdit/psEdit）
//                    └─ 把数字 append 到三条曲线上 → QChart 重画
//
//   ICM20608 那条线一模一样，只是槽换成 getIcm20608Data()、数值框换成 7 个。
//   页面和传感器之间**只靠信号槽**联系：页面不知道传感器怎么读的，传感器也不知道
//   数据会显示在哪儿 —— 这就是为什么把 ap3216c.cpp 换成别的传感器，这一页几乎不用改。
//
// 【和 banna 那一页的对照】banna 全是手写坐标；这一页几乎全是布局管理器，
//   只有 tab3 里那两个"孤零零的容器"保留绝对坐标（原码 .ui 里它们不归任何父布局管）。
// ============================================================================

#include "imx6ulltest.h"

#include <QtCharts>          // 图表全家桶（QChart / QValueAxis / QSplineSeries …）
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSpacerItem>       // 弹簧（布局里的占位符）
#include <QStringList>
#include <QPainter>          // 抗锯齿：setRenderHint(QPainter::Antialiasing)

/* ---------------------------------------------------------------------------
 * 圆角深灰控件样式
 * 原码在 .ui 里给**每一个**按钮和数值框都单独贴了一份一模一样的样式表，
 * 而且头一行 "background:transparent;" 是多余的（下一行的 #3c3c3c 把它覆盖掉了）。
 * 手写界面的好处就在这儿：抽成一个常量，以后想换配色改一处就行。
 * --------------------------------------------------------------------------- */
static const char *STYLE_BOX =
        "background:#3c3c3c;"     // 深灰底
        "color: white;"           // 白字
        "border-radius:20px;";    // 圆角半径 20 像素（圆角按钮/圆角数值框都靠它）

/* ---------------------------------------------------------------------------
 * pickExisting()：在几个候选路径里挑第一个"真实存在"的节点，都不存在就返回空串
 *
 * 为什么要这个函数：原码的 LED/蜂鸣器路径是按**正点原子**那块板子写死的，
 * 100ask 这块板子把同样的东西放在了 /sys/class/leds/ 下面，路径不一样。
 * 与其改死路径，不如把两种都列上让程序自己挑 —— 换板子时不用改代码。
 *   参数 paths：候选路径列表（按优先级排好）
 *   参数 used ：出参，选中的是哪一条（调试时打印出来，方便知道走的哪条路）
 *   返回      ：选中的路径；一个都不存在返回空 QString
 * --------------------------------------------------------------------------- */
static QString pickExisting(const QStringList &paths, QString *used = nullptr)
{
    for (const QString &p : paths) {
        if (QFile::exists(p)) {           // QFile::exists 是静态函数，不用先 new 出对象
            if (used)
                *used = p;
            return p;
        }
    }
    return QString();
}

// ============================================================================
// 构造函数：三步搭好这个页面
//   ui_init()       —— 摆放界面（= 原来 ui->setupUi(this) 干的活）
//   chart_init()    —— 准备图表对象（坐标轴、三条曲线）
//   hardware_init() —— new 出传感器、连信号、认领 LED/蜂鸣器节点
// 顺序不能乱：hardware_init 里的槽函数要用 ui 里的数值框，界面必须先建好。
// ============================================================================
imx6ullTest::imx6ullTest(QWidget *parent) :
    QMainWindow(parent)
{
    ui_init();          // ① 先有界面
    chart_init();       // ② 界面里的 graphicsView 才能装图表
    hardware_init();    // ③ 最后才是传感器（它的信号一响就会去动界面和图表）

    /* 按钮 → 槽 的连接。
       老式 connect 的写法：SIGNAL(...) / SLOT(...) 里写的是**函数签名字符串**，
       运行期拿去元对象表里查名字。所以槽函数必须写在 .h 的 slots: 段里。
       用 SIGNAL/SLOT 宏而不是"函数指针"写法的唯一好处：拼错了是运行期报错，
       编译期不拦 —— 所以拼签名时要格外小心。 */
    connect(btnClear,      SIGNAL(clicked()), this, SLOT(slotBtnClear()));
    connect(btnStartAndStop, SIGNAL(clicked()), this, SLOT(slotBtnStartAndStop()));
    connect(btnStop,       SIGNAL(clicked()), this, SLOT(slotBtnStop()));

    connect(ledBt,         SIGNAL(clicked()), this, SLOT(on_ledBt_clicked()));
    connect(beepBt,        SIGNAL(clicked()), this, SLOT(on_beepBt_clicked()));

    connect(start2060Bt,   SIGNAL(clicked()), this, SLOT(on_start2060Bt_clicked()));
    connect(stop2060Bt,    SIGNAL(clicked()), this, SLOT(on_stop2060Bt_clicked()));

    /* ★ 和原码不一样的一处：原码要手按"启动检测"才开始采数据，
       这里开机就采，进页面直接能看到曲线在动（想还原成原码就删掉下面两行）。
       两个函数就是"启动"按钮的槽，直接调用即可。 */
    slotBtnStartAndStop();        // 打开 AP3216C 的采集
    on_start2060Bt_clicked();     // 打开 ICM20608 的采集

    /* 关于槽名的小知识：原码的"停止检测"按钮对应的槽叫 on_pushButton_clicked()，
       是 .ui 里"转到槽"自动生成的。这种 on_<对象名>_<信号名> 的名字有特殊待遇：
       setupUi() 里会偷偷调一次 QMetaObject::connectSlotsByName(this)，
       自动帮你 connect 一遍 —— 原码里又手写了一遍 connect，于是**一次点击进了两次槽**。
       我们这里没有 setupUi，也不会去调 connectSlotsByName，所以不存在这个问题。 */
}

// 析构函数：什么都不用删
//   界面控件都挂了父对象（new 的时候传了 this 或在布局里），Qt 的对象树会替我们收；
//   m_chart 交给 graphicsView 管，三条曲线和两个坐标轴交给 m_chart 管，
//   两个传感器 new 的时候传了 this —— 全是"有主的"，写 delete 反而会重复释放。
imx6ullTest::~imx6ullTest()
{
}

// ============================================================================
// ui_init()：把 .ui 文件里的东西一行行手写出来
//   .ui 是 XML，uic 工具把它翻译成 ui_imx6ulltest.h 里的 setupUi() —— 说白了
//   就是下面这一大堆 new / addWidget。看懂这一段，你就再也不用 Designer 了。
// ============================================================================
void imx6ullTest::ui_init()
{
    /* ===================== ① 窗口骨架 ===================== */
    resize(824, 600);                                       // 原码 .ui 里的窗口尺寸
    setMinimumSize(800, 600);                               // 拖太小会把布局挤烂，设个下限
    setStyleSheet("background-color: rgb(31, 31, 32);");    // 深灰底

    /* QMainWindow 自己不吃界面：它只有"菜单栏 / 工具栏 / 状态栏 / 中央区"四个格子，
       想往里放东西，必须先用 setCentralWidget 塞一个中央控件进去。
       这是它和 QWidget 最直接的区别（也解释了为什么这一页的基类不是 QWidget）。*/
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    /* 标签页容器。geometry 铺满整窗（原码 .ui 里 tabWidget 就是 824x600 贴着左上角）。
       注意这里 setGeometry 能用是因为**它没有父布局**——一旦被布局管起来，
       setGeometry 会被布局覆盖掉，这是新手最常见的"设了坐标没效果"的原因。 */
    tabWidget = new QTabWidget(centralWidget);
    tabWidget->setGeometry(0, 0, 824, 600);
    tabWidget->setStyleSheet("background:black; color: white;");

    /* ------------------------------------------------------------------
     * 小工具：造一个"数值框 + 名字"的竖排组合，和 .ui 里那 9 处重复代码等价
     *   参数 box      ：出参，把 new 出来的 QTextEdit 交回给成员变量
     *                   （写成引用 *& 是为了能把 new 的地址带出去，
     *                     不用返回值再解包，读起来更顺）
     *   参数 name     ：框下面那行小字（als / gxdata …）
     *   参数 fixSize  ：是否把尺寸锁死成 60x30
     *                   tab2 的三个框是 min、max 都写死；tab3 的七个只写死 max
     *   返回          ：装好的 QVBoxLayout，调用方直接 addLayout 收进上层布局
     * ------------------------------------------------------------------ */
    auto makeValueBox = [](QTextEdit *&box, const QString &name,
                           bool fixSize) -> QVBoxLayout * {
        QVBoxLayout *v = new QVBoxLayout;

        box = new QTextEdit;                       // 显示数值的框
        box->setMaximumSize(60, 30);               // 最宽 60、最高 30 像素
        if (fixSize)
            box->setMinimumSize(60, 30);           // 再锁死下限，就不许再变了
        box->setStyleSheet(STYLE_BOX);

        QLabel *label = new QLabel(name);          // 框下面那行小字
        label->setAlignment(Qt::AlignCenter);      // 居中显示

        v->addWidget(box);                         // 竖排：先框、后字
        v->addWidget(label);
        return v;
    };

    /* ===================== ② 标签页 1：led&&beep ===================== */
    tabLed = new QWidget;
    {
        QGridLayout *grid = new QGridLayout(tabLed);

        /* 用"弹簧"占住第一行和最后一行：弹簧的高度策略是 Expanding，
           也就是"有富余空间就全给我"，于是中间那两行被顶到垂直居中的位置。
           布局里想空一块地方，别用空 QLabel，用 QSpacerItem。 */
        grid->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum,
                                      QSizePolicy::Expanding), 0, 0);

        /* 第二行：两个圆按钮。横向布局里同样是拿弹簧把按钮顶开、
           让它们分布在左右两侧（原码 .ui 里是 弹簧-按钮-弹簧-按钮-弹簧 五段）。 */
        QHBoxLayout *row = new QHBoxLayout;
        ledBt  = new QPushButton("LED");
        beepBt = new QPushButton("BEEP");
        ledBt->setStyleSheet(STYLE_BOX);
        beepBt->setStyleSheet(STYLE_BOX);
        row->addStretch();      // 弹簧
        row->addWidget(ledBt);
        row->addStretch();
        row->addWidget(beepBt);
        row->addStretch();
        grid->addLayout(row, 1, 0);     // 放在第 1 行第 0 列

        /* 第 3 行：大字 "Hello World"（原码 .ui 里这个 QLabel 叫 label_11，
           这里按用途改名成 labelHello，功能不变）*/
        labelHello = new QLabel("Hello World");
        labelHello->setStyleSheet("font-size: 40px; text-align: center;");
        labelHello->setAlignment(Qt::AlignCenter);
        grid->addWidget(labelHello, 3, 0);

        grid->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding,
                                      QSizePolicy::Minimum), 3, 1);
        grid->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum,
                                      QSizePolicy::Expanding), 4, 0);
    }
    /* 标签标题里的 "&&"：Qt 会把单个 & 当成快捷键前缀（&F = Alt+F），
       写两个 && 才会显示成一个普通的 & 符号，所以标签上看到的是 "led&beep"。*/
    tabWidget->addTab(tabLed, "led&&beep");

    /* ===================== ③ 标签页 2：AP3216c ===================== */
    tabAp = new QWidget;
    {
        /* 顶部一行控件 + 下面一张图，权重 1:8 —— 原码 .ui 用
           verticalLayout_4（stretch="1,8"）实现的，就是 addLayout/addWidget
           的第二个参数。权重的作用：整页高度按 1:8 分给这两块。 */
        QVBoxLayout *col = new QVBoxLayout(tabAp);

        QHBoxLayout *row = new QHBoxLayout;
        btnClear = new QPushButton("清除曲线");
        btnStop  = new QPushButton("停止检测");          // 原码 .ui 里叫 pushButton
        btnStartAndStop = new QPushButton("启动检测");
        btnClear->setMinimumHeight(50);                  // 原码给这三个按钮定的最小高度
        btnStop->setMinimumHeight(50);
        btnStartAndStop->setMinimumHeight(50);
        btnClear->setStyleSheet(STYLE_BOX);
        btnStop->setStyleSheet(STYLE_BOX);
        btnStartAndStop->setStyleSheet(STYLE_BOX);

        /* addSpacing(n) = 插一段固定 n 像素的空隙（对应 .ui 里 Fixed 类型的弹簧）；
           addStretch()  = 可伸缩的弹簧（对应 Expanding）。
           .ui 里这段是 清除曲线-30-als-13-ir-13-ps-50-停止检测-40-启动检测-40。 */
        row->addWidget(btnClear);
        row->addSpacing(30);
        row->addLayout(makeValueBox(alsEdit, "als", true));
        row->addSpacing(13);
        row->addLayout(makeValueBox(irEdit,  "ir",  true));
        row->addSpacing(13);
        row->addLayout(makeValueBox(psEdit,  "ps",  true));
        row->addSpacing(50);
        row->addWidget(btnStop);
        row->addSpacing(40);
        row->addWidget(btnStartAndStop);
        row->addSpacing(40);

        /* 曲线画布。⚠ 类型是 QChartView（QtCharts 提供的），
           不是 QGraphicsView —— 它比 QGraphicsView 多一层"图表"的概念。 */
        graphicsView = new QChartView;
        graphicsView->setStyleSheet(STYLE_BOX);
        graphicsView->setRenderHint(QPainter::Antialiasing);   // 抗锯齿，曲线才顺滑

        col->addLayout(row, 1);            // 上面的控件行：权重 1
        col->addWidget(graphicsView, 8);   // 下面的图：权重 8
    }
    tabWidget->addTab(tabAp, "AP3216c");

    /* ===================== ④ 标签页 3：icm2060 ===================== */
    tabIcm = new QWidget;
    {
        /* 这一页原码 .ui 里**没有顶层布局**：两个 layoutWidget 是直接贴在
           tab_3 上、用绝对坐标摆位置的。所以这里也用 setGeometry（tabIcm 没挂布局，
           坐标不会被覆盖），把原码的四个数字照抄过来。 */
        layoutWidget_2 = new QWidget(tabIcm);
        layoutWidget_2->setGeometry(30, 130, 751, 60);

        QHBoxLayout *rowTop = new QHBoxLayout(layoutWidget_2);
        rowTop->setContentsMargins(0, 0, 0, 0);   // 容器只有 60 高，边距全清掉才放得下
        rowTop->addLayout(makeValueBox(gxdataEdit, "gxdata", false));   // 角速度 X
        rowTop->addStretch();
        rowTop->addLayout(makeValueBox(gydataEdit, "gydata", false));   // 角速度 Y
        rowTop->addStretch();
        rowTop->addLayout(makeValueBox(gzdataEdit, "gzdata", false));   // 角速度 Z
        rowTop->addStretch();
        rowTop->addLayout(makeValueBox(axdataEdit, "axdata", false));   // 加速度 X
        rowTop->addStretch();
        rowTop->addLayout(makeValueBox(aydataEdit, "aydata", false));   // 加速度 Y
        rowTop->addStretch();
        rowTop->addLayout(makeValueBox(azdataEdit, "azdata", false));   // 加速度 Z
        rowTop->addStretch();
        rowTop->addLayout(makeValueBox(tempdataEdit, "tempdata", false)); // 温度

        layoutWidget_3 = new QWidget(tabIcm);
        layoutWidget_3->setGeometry(260, 220, 291, 101);

        QHBoxLayout *rowBottom = new QHBoxLayout(layoutWidget_3);
        rowBottom->setContentsMargins(0, 0, 0, 0);
        stop2060Bt  = new QPushButton("关闭传感器");
        start2060Bt = new QPushButton("开启传感器");
        stop2060Bt->setFixedSize(100, 50);      // 原码 min/max 都写 100x50 = 固定尺寸
        start2060Bt->setFixedSize(100, 50);
        stop2060Bt->setStyleSheet(STYLE_BOX);
        start2060Bt->setStyleSheet(STYLE_BOX);
        /* 两个按钮各占一边：中间一个弹簧，容器 291 宽 - 两个按钮 200 = 91 像素的空隙 */
        rowBottom->addWidget(stop2060Bt);
        rowBottom->addStretch();
        rowBottom->addWidget(start2060Bt);
    }
    tabWidget->addTab(tabIcm, "icm2060");

    /* 开机停在第二页（原码 .ui 里 currentIndex = 1）。
       索引从 0 开始：0=led&beep，1=AP3216c，2=icm2060 */
    tabWidget->setCurrentIndex(1);
}

// ============================================================================
// chart_init()：准备图表对象
//   顺序很重要：new 图表 → 造坐标轴和曲线 → addAxis/addSeries 挂到图表上
//   → attachAxis 把曲线和坐标轴配对（必须在 addSeries 之后！）
//   → 最后 setChart 交给画布显示
// ============================================================================
void imx6ullTest::chart_init()
{
    /* ---------- 坐标轴 ---------- */
    m_axisX = new QValueAxis();
    m_axisY = new QValueAxis();

    /* 这套界面是黑底，所以坐标轴的刻度字、轴线、标题全设成白色，
       否则默认的黑色在深色背景上看不见（原码踩过这个坑，都写了）*/
    m_axisX->setLabelsColor(Qt::white);      // 刻度上的数字
    m_axisY->setLabelsColor(Qt::white);
    m_axisX->setLinePenColor(Qt::white);     // 轴线本身
    m_axisY->setLinePenColor(Qt::white);
    m_axisX->setTitleBrush(QBrush(Qt::white));   // 轴标题的字色（QBrush 是"刷子"，比 QColor 多支持渐变/纹理）
    m_axisY->setTitleBrush(QBrush(Qt::white));

    m_axisX->setTitleText("X-label");
    m_axisY->setTitleText("Y-label");

    /* 取值范围。X 轴：0 ~ 40（一次画 40 个点，横坐标就是"第几个点"）；
       （原码在 setMax(AXIS_MAX_Y) 前面还有一句 m_axisY->setMax(0)，
         立刻就被下一句覆盖了，属于写残的死代码，这里不抄。）
       Y 轴：0 ~ axisMaxY，而 axisMaxY 是**自适应**的 —— 原码写死 40，
       可 als 值随随便便就上千、强光下能到 65535，结果三条线全糊在顶端，
       等于白画。改进点就这一处，见下面 getAp3216cData() 里的说明。 */
    m_axisX->setMin(0);
    m_axisX->setMax(AXIS_MAX_X);
    m_axisY->setMin(0);
    m_axisY->setMax(axisMaxY);

    /* ---------- 三条曲线 ---------- */
    /* QSplineSeries = 样条曲线，线是弯的、看着顺滑；
       QLineSeries   = 折线，点对点直连。
       这里三条都是 spline。名字（setName）会显示在图例里。 */
    splineSeries  = new QSplineSeries();     // als：环境光强度
    splineSeries1 = new QSplineSeries();     // ir ：红外强度
    splineSeries2 = new QSplineSeries();     // ps ：接近距离
    splineSeries->setPointsVisible(false);   // 不画数据点，只画线（点上万个会糊成一团）
    splineSeries1->setPointsVisible(false);
    splineSeries2->setPointsVisible(false);
    splineSeries->setName("als数据");
    splineSeries1->setName("ir数据");
    splineSeries2->setName("ps数据");

    /* ---------- 图表本体 ---------- */
    m_chart = new QChart();
    m_chart->setBackgroundBrush(QBrush(Qt::black));     // 图内黑底（和画布样式表配一套）
    m_chart->setTitleBrush(QBrush(Qt::white));
    m_chart->legend()->setBrush(QBrush(Qt::white));     // 图例那块的底
    m_chart->legend()->setLabelColor(Qt::white);        // 图例文字

    /* 挂上去。addAxis 的第二个参数是"贴在哪一边"（左/下/右/上），
       一个图表最多可以有四根不同的坐标轴。 */
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addSeries(splineSeries);
    m_chart->addSeries(splineSeries1);
    m_chart->addSeries(splineSeries2);

    /* 曲线数据变动时做动画（曲线自己长出来/平移过去，而不是硬跳）*/
    m_chart->setAnimationOptions(QChart::SeriesAnimations);

    /* ★ 关键顺序：attachAxis 必须在 addSeries **之后**。
       反过来的话运行时警告 "Axis is not added to the chart"，曲线显示不出来。
       每条曲线要连着 attach 两次：一次 X 轴、一次 Y 轴。 */
    splineSeries->attachAxis(m_axisX);
    splineSeries->attachAxis(m_axisY);
    splineSeries1->attachAxis(m_axisX);
    splineSeries1->attachAxis(m_axisY);
    splineSeries2->attachAxis(m_axisX);
    splineSeries2->attachAxis(m_axisY);

    /* 把图表交给画布（QChartView::setChart 会接管 m_chart 的所有权，
       以后不用自己 delete）*/
    graphicsView->setChart(m_chart);
}

// ============================================================================
// hardware_init()：把两个传感器和其它硬件"认领"进来
//   ⚠ 原码整个函数被 #if __arm__ 包着，因为 Ap3216c 里直接 open("/dev/i2c-0")，
//     在电脑上跑会失败。我们这版的 Ap3216c / Icm20608 都做了容错
//     （打不开就打个 qDebug 然后返回，读数据返回 "I2C Error"），
//     所以在电脑上也能跑、只是数字是错的 —— 那就没必要再包一层条件编译了。
// ============================================================================
void imx6ullTest::hardware_init()
{
    /* ---------- 两个传感器 ---------- */
    /* 参数 this：认页面当爹 —— 页面被销毁时，两个传感器跟着一起销毁 */
    ap3216c  = new Ap3216c(this);
    icm20608 = new Icm20608(this);

    /* 传感器"数据更新了"→ 本页的刷新槽。
       注意这里跨的是**两个类**：信号由传感器发，槽在页面里。
       页面认识传感器（头顶 include 了它的头文件），传感器**不认识**页面，
       它只管 emit —— 这叫"解耦"，也是信号槽最大的价值。 */
    connect(ap3216c,  SIGNAL(ap3216cDataChanged()),  this, SLOT(getAp3216cData()));
    connect(icm20608, SIGNAL(icm20608DataChanged()), this, SLOT(getIcm20608Data()));

    /* ---------- LED ---------- */
    /* 先关掉 LED 的"心跳/触发器"：这块板子上的 sys-led 默认被内核拿去做心跳灯
       （一直在闪），不先把这个关掉，我们写进去的 0/1 根本不生效。
       节点不存在就跳过这条命令，免得在终端里刷一行报错。 */
    if (QFile::exists("/sys/class/leds/sys-led/trigger"))
        system("echo none > /sys/class/leds/sys-led/trigger");

    /* 找亮度节点。原码只写了正点原子那块板子的路径；
       这里把 100ask 的 /sys/class/leds/sys-led/brightness 也列上（优先级更高） */
    QString usedPath;
    QString ledPath = pickExisting(
                QStringList() << "/sys/class/leds/sys-led/brightness"
                              << "/sys/devices/platform/leds/leds/sys-led/brightness",
                &usedPath);
    if (ledPath.isEmpty()) {
        /* 一个都没有 → 按钮上直接写清楚，别让用户点半天没反应 */
        ledBt->setText("未获取到LED设备！");
        qDebug() << "imx6ullTest: LED 节点不存在";
    } else {
        qDebug() << "imx6ullTest: LED 用" << usedPath;
        file.setFileName(ledPath);
        getLedState();          // 读一下当前状态，顺便把按钮文字初始化成"LED点亮/熄灭"
    }

    /* ---------- 蜂鸣器 ---------- */
    /* 和 LED 一模一样的套路，只是换了路径 —— 所以原码也没为它单独建一个类，
       两个 QFile 成员直接放在页面里就够了。（判据：能独立成"类"的东西，
       得有自己的状态和行为，比如 Icm20608 要管定时器和换算；这里只是个文件句柄。）*/
    QString beepPath = pickExisting(
                QStringList() << "/sys/class/leds/beep/brightness"
                              << "/sys/devices/platform/leds/leds/beep/brightness");
    if (beepPath.isEmpty()) {
        beepBt->setText("未获取到BEEP设备！");
        qDebug() << "imx6ullTest: 蜂鸣器节点不存在";
    } else {
        qDebug() << "imx6ullTest: BEEP 用" << beepPath;
        file_beep.setFileName(beepPath);
        getBeepState();
    }
}

/* ===========================================================================
 * 下面是各按钮的槽函数
 * =========================================================================== */

// 「清除曲线」：把三条曲线清空、横轴复位回起点（原码 slotBtnClear）
void imx6ullTest::slotBtnClear()
{
    splineSeries->clear();
    splineSeries1->clear();
    splineSeries2->clear();
    m_axisX->setMin(0);
    m_axisX->setMax(AXIS_MAX_X);
    /* pointCount 是从哪来的：每画一个点自增 1，横坐标就用它 ——
       归零之后新点又从 x=0 开始排，所以必须和 clear() 一起做。 */
    pointCount = 0;

    /* 顺手把 Y 轴也放回初始高度：不然清完图后，坐标轴还停在上次那个很高的量程上，
       新曲线会贴着底边画不出来。这是我们这版加的（原码清图后 Y 轴不复位）。 */
    axisMaxY = 100;
    m_axisY->setMax(axisMaxY);
}

// 「启动检测」：让 AP3216C 开始连续采集
void imx6ullTest::slotBtnStartAndStop()
{
    ap3216c->setCapture(true);        // 参数 true = 开（内部会 timer->start(500)）

    /* 原码这里还有一句 setText("启动检测")，等于没变 —— 它把按钮文字当"上一次的动作"
       来用，结果就是"正在采集时按钮写着启动检测、停下来时写着停止检测"，反着的。
       我们这版干脆不玩文字游戏：两个按钮各管一件事，看曲线动不动就知道在没在采。 */
}

// 「停止检测」：停下采集，曲线冻结在原地
//   注意：停的只是**采集**，已经画出来的曲线不动（想清掉就按"清除曲线"）。
//   原码在这个函数里把 pointCount 归零了，但曲线上的旧点没删 —— 再启动时
//   新点会从横坐标 0 开始、和旧点重在一起，图上会乱。这里保持"继续往后画"。
void imx6ullTest::slotBtnStop()
{
    ap3216c->setCapture(false);       // 参数 false = 停
}

/* ---------- LED ---------- */
// 点一下 LED 按钮：把当前状态取反写回去
void imx6ullTest::setLedState()
{
    bool state = getLedState();       // 先读当前状态（顺便校验文件在不在）

    if (!file.exists())               // 节点不存在（不是这块板子的 LED）就直接返回
        return;

    if (!file.open(QIODevice::ReadWrite))
        qDebug() << file.errorString();

    /* sysfs 里的亮度节点只认字符串 "0" / "1"：1=点亮，0=熄灭。
       ⚠ 这里传的是 QByteArray（写的是**字节**），不是文字。*/
    if (state)
        file.write("0");              // 原来亮着 → 写 0 关掉
    else
        file.write("1");              // 原来灭着 → 写 1 点亮

    file.close();
    getLedState();                    // 读回来，顺便刷新按钮文字
}

// 读 LED 当前状态；返回 true = 亮着。同时把按钮文字改成对应的样子
bool imx6ullTest::getLedState()
{
    if (!file.exists())
        return false;

    if (!file.open(QIODevice::ReadWrite))
        qDebug() << file.errorString();

    /* QTextStream 把 QFile 包一层，就能按"行/文本"读，不用自己处理换行符。
       读 sysfs 一定要 readLine() 而不是 readAll()：文件里就一个 "1\n"，
       readAll 会连换行符一起拿到，后面和 "1" 比较就永远不相等。*/
    QTextStream in(&file);
    QString buf = in.readLine();
    file.close();

    if (buf == "1") {
        ledBt->setText("LED点亮");
        return true;
    } else {
        ledBt->setText("LED熄灭");
        return false;
    }
}

void imx6ullTest::on_ledBt_clicked()
{
    setLedState();                    // 亮变灭、灭变亮
}

/* ---------- 蜂鸣器 ---------- */
void imx6ullTest::setBeepState()
{
    bool state = getBeepState();

    if (!file_beep.exists())
        return;

    if (!file_beep.open(QIODevice::ReadWrite))
        qDebug() << file_beep.errorString();

    if (state)
        file_beep.write("0");         // 开 → 关
    else
        file_beep.write("1");         // 关 → 开

    file_beep.close();
    getBeepState();
}

bool imx6ullTest::getBeepState()
{
    if (!file_beep.exists())
        return false;

    if (!file_beep.open(QIODevice::ReadWrite))
        qDebug() << file_beep.errorString();

    QTextStream in(&file_beep);
    QString buf = in.readLine();
    file_beep.close();

    if (buf == "1") {
        beepBt->setText("BEEP开");
        return true;
    } else {
        beepBt->setText("BEEP关");
        return false;
    }
}

void imx6ullTest::on_beepBt_clicked()
{
    setBeepState();
}

/* ---------- ICM20608 ---------- */
void imx6ullTest::on_start2060Bt_clicked()
{
    icm20608->setCapture(true);              // 开：内部 timer->start(500)
    start2060Bt->setText("已启动检测");        // 这个按钮的原文案会变，是原码的行为，保留
}

void imx6ullTest::on_stop2060Bt_clicked()
{
    icm20608->setCapture(false);             // 停
    start2060Bt->setText("已停止检测");

    /* 停了以后不会再收到信号，数值框会永远停在最后一个数 ——
       看着像"卡住了"。所以这里手动把所有值写成"无"，一眼就知道是停了。 */
    gxdataEdit->setText("无");
    gydataEdit->setText("无");
    gzdataEdit->setText("无");
    axdataEdit->setText("无");
    aydataEdit->setText("无");
    azdataEdit->setText("无");
    tempdataEdit->setText("无");
}

/* ===========================================================================
 * 两个"收到数据"的槽
 * =========================================================================== */

// ICM20608 每 500ms 发一次信号就进这里：把 7 个最新值塞进 7 个数值框
void imx6ullTest::getIcm20608Data()
{
    /* 原码在这里写了 7 组这样的代码：
         static QString gx = icm20608->gxData();
         if (gx != icm20608->gxData()) gx = icm20608->gxData();
       意思是"变了才更新，没变就不动界面"—— 可它把 gx 和它自己比，条件永远为假，
       gx 就是刚读到的值，等于**什么都没做**，纯属写残了。
       （真要做"变了才刷新"，得把上一轮的值存成成员变量比较，static 是行不通的。）
       所以这里直接取、直接设，效果和原码完全一样，但短得多。 */
    gxdataEdit->setText(icm20608->gxData());     // 角速度 X，°/s
    gydataEdit->setText(icm20608->gyData());     // 角速度 Y
    gzdataEdit->setText(icm20608->gzData());     // 角速度 Z

    axdataEdit->setText(icm20608->axData());     // 加速度 X，g
    aydataEdit->setText(icm20608->ayData());     // 加速度 Y
    azdataEdit->setText(icm20608->azData());     // 加速度 Z

    tempdataEdit->setText(icm20608->tempData()); // 温度，℃
}

// AP3216C 每 500ms 发一次信号就进这里：刷新三个数值框 + 往曲线上补一个点
void imx6ullTest::getAp3216cData()
{
    /* 三个值都是 QString，取出来存一份 —— 免得下面又调一次 getter */
    const QString als = ap3216c->alsData();     // 环境光强度 0~65535
    const QString ir  = ap3216c->irData();      // 红外强度
    const QString ps  = ap3216c->psData();      // 接近距离

    alsEdit->setText(als);
    irEdit->setText(ir);
    psEdit->setText(ps);

    /* QString → int。⚠ toInt() 遇到 "I2C Error" 这种非数字会返回 0
       （不是报错、也不是随机值），图上是掉到 0 —— 看到曲线突然贴地，
       先去 qDebug 里看有没有 I2C 报错。 */
    const int alsVal = als.toInt();
    const int irVal  = ir.toInt();
    const int psVal  = ps.toInt();

    /* ---- ★ 改进点：纵轴上限自适应 ----
       原码把 Y 轴上限写死成 AXIS_MAX_Y = 40。可这三个值的量级完全不一样：
       als 几百到上万（强光下 65535）、ir/ps 顶天 1023 —— 写死 40 的结果是
       三条线全都贴着图顶，什么也看不出来。
       这里让上限跟着"三条线的当前最大值"走，并且**只涨不缩**：
       数值一回落就跟着缩的话，坐标轴会一直上下抽动，反而更难看清。 */
    const int peak = qMax(alsVal, qMax(irVal, psVal));   // qMax 是 Qt 的取大值函数
    if (peak * 1.1 > axisMaxY) {                         // 留 10% 的富余，别让峰顶贴边
        axisMaxY = int(peak * 1.1) + 1;
        m_axisY->setMax(axisMaxY);
    }
    /* 遗留问题（下一步可以再改）：三条线共用一个 Y 轴时，只要 als 一上几千，
       ir/ps 那两条就又被压成贴底的直线了。彻底解决要给 ir/ps 各挂一根右侧
       坐标轴（QChart 支持一图多轴），那是下一个改进点。 */

    /* ---- 横轴滚动窗口：只显示最近 AXIS_MAX_X 个点 ---- */
    if (pointCount > AXIS_MAX_X) {
        /* 砍掉最老的那个点（横坐标最小的，永远排在 0 号位）。
           ⚠ 原码只对 als 这一条调了 remove(0)，ir/ps 两条**从来没删过点** ——
           跑久了它们的点越攒越多，内存一直涨、画得也越来越慢。
           这里三条一起删，才是真的"滚动"。*/
        splineSeries->remove(0);
        splineSeries1->remove(0);
        splineSeries2->remove(0);
        m_axisX->setMin(pointCount - AXIS_MAX_X);   // 窗口跟着往右平移
        m_axisX->setMax(pointCount);
    }

    /* 往三条曲线上各补一个点：横坐标 = 第几个点，纵坐标 = 传感器数值。
       append 完 QChart 会自己重画（配合上面设的 SeriesAnimations 动画）。 */
    splineSeries->append(QPointF(pointCount, alsVal));
    splineSeries1->append(QPointF(pointCount, irVal));
    splineSeries2->append(QPointF(pointCount, psVal));

    pointCount++;      // 下一个点的横坐标
}
