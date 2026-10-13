// ============================================================================
// form/imx6ulltest.h —— 板载传感器页面（第 3 级的主角页面）
//
// 【这一页干什么】三个标签页：
//   tab1「led&&beep」  两个圆按钮，点一下开关 LED / 蜂鸣器（本板没有这两个节点，见 .cpp 说明）
//   tab2「AP3216c」    三条实时曲线（als/ir/ps 光照/红外/距离）+ 数值框
//   tab3「icm2060」    六轴数据（三轴角速度 + 三轴加速度 + 温度）7 个数值框
//
// 【和 banna 那一页最大的不同】这里是 QMainWindow 而不是 QWidget：
//   因为它用了 QTabWidget（带标签的容器），QMainWindow 自带 centralWidget 这一层，
//   标签页要挂在它上面。（.ui 里的 centralwidget 就是这一层，代码里要自己 new）
//   QMainWindow 本身也是 QWidget 的子类，所以 mainwidget.cpp 里
//   return new imx6ullTest; 照样能塞进 items[]，类型不用改。
//
// 【布局风格也和第 1 级相反】banna 全程手写坐标 setGeometry；这一页几乎全靠布局管理器
//   （QGridLayout / QVBoxLayout / QHBoxLayout），只有 tab3 里那两个 layoutWidget
//   保留绝对坐标 —— 原码 .ui 里它们就是"孤立的容器"，不归任何父布局管。
// ============================================================================

#ifndef IMX6ULLTEST_H
#define IMX6ULLTEST_H

#include <QMainWindow>      // 基类（不是 QWidget！因为要用 QTabWidget 自带的中央区）
#include <QTabWidget>       // 三个标签页的容器
#include <QPushButton>      // 各种按钮
#include <QLabel>           // 数值框下面那排小名字（als / ir / gxdata …）
#include <QTextEdit>        // 显示数值的框（原码用的就是它，不是 QLabel）
#include <QTimer>           // 原码留着一个没用上的定时器，见 .cpp 构造函数里的说明
#include <QFile>            // LED / 蜂鸣器：直接用 QFile 读写 sysfs 节点
#include <QChartView>       // 图表的"画布"控件
#include <QValueAxis>       // 数值坐标轴
#include <QLineSeries>      // 折线（本页没用上，但头文件里留着，方便以后换折线）
#include <QSplineSeries>    // 平滑曲线 —— 三条曲线用的是它

#include "ap3216c.h"        // 零件：光照/距离传感器
#include "icm20608.h"       // 零件：六轴传感器

// 图表的类（QChart、QValueAxis…）都在 QtCharts 这个命名空间里，
// 不写这句就得处处写 QtCharts::QChart。原码用的就是这个宏。
// ⚠ 严格说"在头文件里写 using namespace"是不好的习惯（会污染所有 include 它的文件），
//   正规做法是用 QtCharts:: 前缀。这里跟原码保持一致，知道这回事就行。
QT_CHARTS_USE_NAMESPACE

class imx6ullTest : public QMainWindow
{
    Q_OBJECT

public:
    explicit imx6ullTest(QWidget *parent = nullptr);
    ~imx6ullTest();

private:
    // ---- 界面控件：原来都在 imx6ulltest.ui 里，现在全手写（见 .cpp 的 ui_init）----
    QWidget     *centralWidget;      // QMainWindow 的中央区（.ui 里自动生成的那层）
    QTabWidget  *tabWidget;          // 三个标签页

    QWidget     *tabLed;             // tab1 的页面容器
    QPushButton *ledBt;              //   「LED」圆按钮（本板没有节点 → 显示"未获取到LED设备！"）
    QPushButton *beepBt;             //   「BEEP」圆按钮
    QLabel      *labelHello;         //   「Hello World」大字（原码 .ui 里叫 label_11）

    QWidget     *tabAp;              // tab2 的页面容器
    QPushButton *btnClear;           //   「清除曲线」
    QPushButton *btnStop;            //   「停止检测」（原码 .ui 里叫 pushButton）
    QPushButton *btnStartAndStop;    //   「启动检测」
    QTextEdit   *alsEdit;            //   三个数值框（原码就这么叫，保留原名）
    QTextEdit   *irEdit;
    QTextEdit   *psEdit;
    QChartView  *graphicsView;       //   ★ 三条曲线画在这块画布上（不是 QGraphicsView！）

    QWidget     *tabIcm;             // tab3 的页面容器
    QWidget     *layoutWidget_2;     //   上排 7 个数值框的容器（绝对坐标，原码如此）
    QWidget     *layoutWidget_3;     //   下排两个按钮的容器（绝对坐标）
    QTextEdit   *gxdataEdit;         //   七轴/温度数值框（角速度 3 个 + 加速度 3 个 + 温度 1 个）
    QTextEdit   *gydataEdit;
    QTextEdit   *gzdataEdit;
    QTextEdit   *axdataEdit;
    QTextEdit   *aydataEdit;
    QTextEdit   *azdataEdit;
    QTextEdit   *tempdataEdit;
    QPushButton *stop2060Bt;         //   「关闭传感器」
    QPushButton *start2060Bt;        //   「开启传感器」

    // ---- 图表对象 ----
    QChart      *m_chart;            // 图表本体（坐标轴、曲线都挂在它身上）
    QValueAxis  *m_axisX;            // 横轴：点数
    QValueAxis  *m_axisY;            // 纵轴：数值大小
    QSplineSeries *splineSeries;     // als 曲线（光照强度）
    QSplineSeries *splineSeries1;    // ir  曲线（红外）
    QSplineSeries *splineSeries2;    // ps  曲线（接近/距离）
    const int    AXIS_MAX_X = 40;    // 横轴一次显示多少个点（原码的 40）
    int          pointCount = 0;     // 已经画了多少个点（横坐标就取它）
    int          axisMaxY = 100;     // ★ 纵轴上限（自适应，见 .cpp getAp3216cData 的说明）

    // ---- 两个传感器零件 ----
    Ap3216c *ap3216c;                // 光照/距离（只有板子上才有）
    Icm20608 *icm20608;              // 六轴

    // ---- LED / 蜂鸣器的 sysfs 节点文件 ----
    QFile file;                      // 原码把 QFile 直接放在页面类里，没单独抽成类
    QFile file_beep;                 //（判据：它不是一个独立的类 → 不产生 hardware/ 里的文件）

    void ui_init();                  // 建界面（= 原来 ui->setupUi(this) 干的活）
    void chart_init();               // 建图表：坐标轴 + 三条曲线
    void hardware_init();            // 建传感器、接信号、认领 LED/蜂鸣器节点

    void setLedState();              // 切换 LED 亮灭（写 sysfs）
    bool getLedState();              // 读 LED 当前状态，顺便改按钮文字
    void setBeepState();             // 蜂鸣器同上
    bool getBeepState();

private slots:
    void slotBtnClear();             // 「清除曲线」：清空三条曲线、坐标轴复位
    void slotBtnStartAndStop();      // 「启动检测」：让 AP3216C 开始连续采集
    void slotBtnStop();              // 「停止检测」：停下（曲线冻结在原地）
    void on_ledBt_clicked();         // 点 LED 按钮
    void on_beepBt_clicked();        // 点 BEEP 按钮
    void on_start2060Bt_clicked();   // 「开启传感器」：让 ICM20608 开始采集
    void on_stop2060Bt_clicked();    // 「关闭传感器」：停 + 把 7 个数值框改成"无"
    void getAp3216cData();           // ★ 收到传感器数据更新信号 → 刷新数值框 + 画曲线
    void getIcm20608Data();          // ★ 收到六轴数据更新信号 → 刷新 7 个数值框
};

#endif // IMX6ULLTEST_H
