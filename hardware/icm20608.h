// ============================================================================
// hardware/icm20608.h —— Icm20608 这个类的"说明书"
//
// 【头文件干什么用】头文件里只放**声明**（长什么样：有哪些成员、哪些函数、参数什么类型），
//   函数的具体实现全在 icm20608.cpp 里。编译器编 .cpp 时第一句 #include 就是它，
//   靠它才知道"Icm20608 是什么、能怎么用"。
//
// 【这个 .h 为什么特殊】里面有 Q_OBJECT 宏 —— 带 Q_OBJECT 的类必须再经过 Qt 的
//   moc（元对象编译器）处理一遍，生成一个 moc_icm20608.cpp，信号/槽才能工作。
//   所以这个文件必须登记进 hardware/hardware.pri 的 HEADERS += 段，
//   qmake 才会知道"要给这个头文件跑 moc"。
//   漏登记的症状：链接时报 undefined reference to `Icm20608::staticMetaObject'
//   （编译能过、链接失败 —— 看到这个报错先回 .pri 里数行数）
// ============================================================================

#ifndef ICM20608_H        // ┐ 这三行叫"包含卫士"（include guard），防的是**重复包含**：
#define ICM20608_H        // │   第一次被别人 include 时，ICM20608_H 还没定义 → 条件为真，
                          // │   整份内容被读一遍，同时把 ICM20608_H 定义下来；
                          // │   第二次再 include 时它已经定义过了 → #ifndef 为假，
                          // │   整段直接跳过，类就不会被声明两遍（否则报"重复定义"）。
                          // │   名字随便取，习惯用"文件名大写 + 下划线"。
                          // └ 注意 #define 和 #ifndef 用的是**同一个名字**

#include <QObject>   // 基类。继承 QObject 才能有信号槽、对象树、父对象替你销毁
#include <QTimer>    // 定时器类型 —— 下面只用了一个 QTimer * 指针成员。
                     //   严格说这里写 class QTimer;（前向声明）就够，能少编译一点东西；
                     //   原码直接 include 了，也能用，只是稍慢，没必要改
#include <QString>   // 7 个数据成员都是 QString（Qt 的字符串类）
#include <QFile>     // ⚠ 这个 .h 里其实**没用到** QFile（只有 .cpp 里用）。
                     //   它属于"可以挪到 .cpp 的多余 include"——不影响运行、也不报错，
                     //   但记住这个感觉：include 只写"本文件真的用到的类型"

// class 类名 : public 基类 —— 公有继承 QObject。
//   类名首字母大写是 Qt 的惯例（函数/变量小写开头），一眼能分出"类型"和"变量"。
//   ⚠ 右花括号后面那个分号不能省 —— 类声明也是一条语句，漏了会报一堆莫名其妙的错
class Icm20608 : public QObject
{
    Q_OBJECT        // ⚠ 这个宏必须是类里的**第一行**。
                    //   它不是普通 C++ 代码，是写给 moc 看的标记：moc 看到它，就会
                    //   为这个类生成"元对象"（一张表：类名、信号表、槽表、属性表）。
                    //   connect 的 SIGNAL()/SLOT() 字符串查找、Q_PROPERTY、qDebug 打印
                    //   对象名，全都靠这张表。
                    //   忘了写它的症状：connect 运行期报 "no such signal/slot"，
                    //   Q_PROPERTY 全部失效 —— 但**编译一样能过**，很难查

public:             // ── 公开段：外面（页面）能直接调用的东西 ──

    // 构造函数（和类同名、没有返回值）。
    //   explicit：禁止"隐式转换"。没有它，某些写法（如把别的指针直接赋给本类变量）
    //     会悄悄编译通过，看着一脸懵；加上它，这类写法直接报错
    //   参数 QObject *parent：对象树的"父亲"。传了谁，谁就负责在销毁时把我一起 delete；
    //     传 0 / 不传（= 0 是默认值，Qt 老风格里的空指针，等价于 nullptr）就得自己管内存
    //   从哪来：页面里 new Icm20608(this) —— 把页面自己当父亲传进来
    explicit Icm20608(QObject *parent = 0);

    ~Icm20608();    // 析构函数（~类名）：对象被销毁时自动调用。实现写在 .cpp 里

    // 开 / 关连续采集。
    //   Q_INVOKABLE：普通 public 函数只能被 C++ 代码直接调；加上这个宏，函数就进了
    //     元对象表，可以**按名字**调用 —— QML 里写 icm20608.setCapture(true)，
    //     或 C++ 里 QMetaObject::invokeMethod(obj, "setCapture", Q_ARG(bool, true))
    //   参数 bool str：true = 开始采集，false = 停止。
    //     （名字是原码留下的，str 让人以为是字符串，其实是个开关；自己写叫 enabled 更好）
    Q_INVOKABLE void setCapture(bool str);

private:            // ── 私有段：只有本类自己的函数能用，页面看不见 ──

    QTimer *timer_2060;      // 采集定时器。⚠ 只是个指针，谁 new 的、谁负责删，
                             //   见 .cpp 构造函数里的说明。
                             //   名字里的 2060 是原码笔误（芯片是 20608），照抄不影响运行
    QString iioPath;         // 设备路径前缀，形如 /sys/bus/iio/devices/iio:device1
                             //   （.cpp 里所有读数文件都是 "iioPath + /文件名" 拼出来的）
    float accelScale;        // 加速度换算系数，单位 m/s² 每 LSB —— 板上实测 0.000598
    float gyroScale;         // 陀螺仪换算系数，单位 rad/s 每 LSB —— 板上实测 0.001064724

    // 下面 7 个装"最新一次读到的值"。为什么用 QString 不用 float：
    //   .cpp 里换算完就直接格式化成两位小数的文本，界面/MQTT 拿去就能用，不用再转
    QString gxdata;          // 角速度 X，单位 °/s（度每秒）
    QString gydata;          // 角速度 Y，°/s
    QString gzdata;          // 角速度 Z，°/s
    QString axdata;          // 加速度 X，单位 g（1 = 一个重力加速度）
    QString aydata;          // 加速度 Y，g
    QString azdata;          // 加速度 Z，g
    QString tempdata;        // 温度，℃

    // 下面 4 个是"内部工具函数"，外面用不着 → 放 private。
    // 只声明、不写实现；实现都在 .cpp（返回什么、参数什么含义见 .cpp 里同名函数）
    QString findIioDevice();                    // 找设备目录，找不到返回空串
    void readScaleFactors();                    // 读上面那 2 个 scale 系数
    float readSysfsFloat(const QString &path);  // 参数：要读的文件完整路径
    int readSysfsInt(const QString &path);      // 同上，读整数（7 个 raw 值用它）

public:             // ── 又切回公开段 ──（C++ 里 public/private 可以来回切，
                    //    原码这样排版是为了把"取值函数"和"数据成员"分开摆）

    // 7 个"取值函数"：无参数，返回最新值的字符串。页面收到数据更新的信号后调它们。
    // 命名规矩：数据成员叫 axdata（小写 d），取值函数叫 axData()（大写 D）——
    //   就差这一字母，下面的 Q_PROPERTY 正是靠这个对应关系找到读取函数的
    QString gxData();
    QString gyData();
    QString gzData();
    QString axData();
    QString ayData();
    QString azData();
    QString tempData();

    // 读一次全部数据并换算（无参数、无返回值）。谁调：定时器到点、或页面主动刷新
    void icm20608ReadData();

    // ---- 7 个 Q_PROPERTY：把"数据"变成 Qt 认识的"属性" ----
    // 语法：Q_PROPERTY(类型 属性名 READ 读取函数 NOTIFY 通知信号)
    //   ① 类型     QString —— 属性的数据类型，必须和 READ 那个函数的返回值一致
    //   ② 属性名   gxData  —— 外面用的名字（QML 里绑定的就是它；
    //                 C++ 里也能 obj->property("gxData") 按名字取）
    //   ③ READ     gxData  —— 读这个属性时去调谁，必须是上面声明过的函数
    //   ④ NOTIFY   icm20608DataChanged —— 值变了发哪个信号，
    //                 Qt 收到后就自动通知所有绑定了这个属性的界面去刷新
    //   只写 READ 不写 WRITE = **只读属性**，外面只能看不能改（这里正合适）
    //   为什么 7 个都 NOTIFY 同一个信号：一次读数 7 个值同时更新，共用一条信号最省事；
    //   代价是"其中一个变了，7 个绑定都会重刷"——在这个数据量下完全无所谓
    Q_PROPERTY(QString gxData READ gxData NOTIFY icm20608DataChanged)
    Q_PROPERTY(QString gyData READ gyData NOTIFY icm20608DataChanged)
    Q_PROPERTY(QString gzData READ gzData NOTIFY icm20608DataChanged)
    Q_PROPERTY(QString axData READ axData NOTIFY icm20608DataChanged)
    Q_PROPERTY(QString ayData READ ayData NOTIFY icm20608DataChanged)
    Q_PROPERTY(QString azData READ azData NOTIFY icm20608DataChanged)
    Q_PROPERTY(QString tempData READ tempData NOTIFY icm20608DataChanged)

public slots:       // ── 槽函数专用段 ──
                    // ⚠ 必须**显式写出 slots:**（不能只写 public:）。
                    //   因为老式 connect 用的是字符串：
                    //     connect(timer, SIGNAL(timeout()), this, SLOT(timer_timeout()))
                    //   它是拿 "timer_timeout()" 这个名字去元对象表里查，
                    //   函数不在 slots 段里就查不到 → 运行时不响应、还不报错
    void timer_timeout();   // 定时器到点就进这里（实现见 .cpp）。无参数、无返回值

                    // （下面空两行是原码的排版，无所谓，别照抄成"漏了内容"）
signals:            // ── 信号专用段 ──
                    // 信号的特点：**只声明，绝对不写实现**。
                    //   函数体由 moc 自动生成（就是"把所有连到它的槽挨个调一遍"），
                    //   自己写了反而编不过
    void icm20608DataChanged();  // 无参数、无返回值 —— 它就喊一声"数据更新了"，
                                 //   想知道具体是多少，接收方自己调 axData() 之类去取。
                                 //   和上面 7 个 Q_PROPERTY 的 NOTIFY 是**同一个信号**
};

#endif // ICM20608_H       // 和开头 #ifndef 配对，把包含卫士合上
