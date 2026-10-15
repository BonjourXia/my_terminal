// ============================================================================
// hardware/icm20608.cpp —— ICM20608 六轴传感器（三轴加速度 + 三轴陀螺仪 + 温度）
//
// 【它的角色】"零件"（hardware/），不在左侧导航栏里，是被页面 #include 进去用的。
//            第 4 级里用它的页面是 form/imx6ulltest.cpp（画三条曲线那一页）。
//
// 【它怎么读到数据】这版**不碰 I2C**。板子上这颗芯片已经被内核里的 inv_mpu6050
//   驱动接管了，驱动把每个数据都以"一个小文本文件"的形式摆出来，位置在
//      /sys/bus/iio/devices/iio:device1/
//   所以这个类的全部工作就是：用 QFile 打开那些文件、读一行文本、转成数字。
//   这叫 sysfs，是"内核已经帮你把驱动写好了"的路子。
//   （对照一下：如果内核里**没有**这颗芯片的驱动，用户态就只能自己
//     open("/dev/i2c-0") + ioctl 操作寄存器：自己算寄存器地址、自己拼字节、
//     自己判断数据有效位 —— 同目录的 ap3216c.cpp 走的就是那条路。
//    ⚠ 顺带说破一件事：这颗 ICM20608 在板上其实挂在 **SPI** 上
//     （设备树 arch/arm/boot/dts/100ask_imx6ull-14x14.dts 里，ecspi3 节点下的
//      icm20608@0，片选 0，8MHz），内核也只编了 SPI 版驱动
//     （CONFIG_INV_MPU6050_SPI=m，而 CONFIG_INV_MPU6050_I2C 根本没开）。
//     但从这里往下的代码里**找不出一个 i2c 或 spi 字样** —— 总线是哪种，
//     被内核驱动和 IIO 这一层整个吃掉了，这正是不碰硬件的好处。）
//
// 【数据怎么流到界面上】
//   定时器每 500ms 响一次 → icm20608ReadData() 读 7 个 raw 文件 → 乘 scale 换算成
//   物理量 → 存进 7 个 QString 成员 → emit icm20608DataChanged() 发信号 →
//   页面在槽函数里调 axData() 之类把新值取走，贴到曲线上。
//   （这就是"零件负责读数、页面负责显示"的分工）
// ============================================================================

#include "icm20608.h"     // 自己的头文件：类声明、成员变量、信号、槽都在里面
#include <stdio.h>        // ↓ 这 6 个系统头是本文件**用不到**的
#include <string.h>       //   （它们是原版 I2C 写法留下的行李：那时候要
#include <sys/types.h>    //     open()/read()/close() 设备节点，才需要这几个头。
#include <sys/stat.h>     //     现在全交给 Qt 的 QFile 了，删掉它们也能编过。）
#include <fcntl.h>        //   留着不影响运行，但记住一条：用不到就别再 include
#include <unistd.h>       //   ——那是给自己增加"看不出来的依赖"。
#include <QDebug>         // qDebug()：往终端/串口打调试信息，和 printf 一个用途
#include <QDir>           // QDir：列目录用的，下面"按名字找设备"要它

// ---------------------------------------------------------------------------
// 构造函数：只做三件"准备工作"，**不读数据**（读数据是定时器的事）
//   参数 QObject *parent：Qt 对象树的规矩 —— 传给谁，将来就由谁负责 delete 我。
//       从哪来：页面里写 new Icm20608(this)，把页面自己当 parent 传进来；
//       也可以传 0 / 不传，那就没人替你 delete，得自己手动管理（容易忘 → 泄漏）。
//       QObject(parent) 这句是把 parent 原样交给基类，基类拿它去登记对象树。
// ---------------------------------------------------------------------------
Icm20608::Icm20608(QObject *parent) : QObject (parent)
{
    // new QTimer()：造一个定时器。⚠ 这里**没给父对象**（原码就是这样），
    //   所以本对象析构时它不会被自动回收 —— 严格说是内存泄漏。
    //   想修：写成 new QTimer(this)，让它挂在对象树上跟着一起死。
    //   （这个定时器本身不跑，只是先造好；真要跑要调 setCapture(true)）
    timer_2060 = new QTimer();

    /* 查找ICM20608的IIO设备路径 */
    // iioPath 是 QString 成员，存形如 "/sys/bus/iio/devices/iio:device1" 的路径前缀
    // 为什么"找"而不是写死：万一开机顺序变了、内核给它编成 device2 呢？
    //   按名字找，换板子/换内核都不用改代码。
    iioPath = findIioDevice();
    if (iioPath.isEmpty()) {
        // isEmpty()：QString 的成员函数，无参数，判断"是不是空串"。
        // findIioDevice() 没找到时返回的就是空串 QString()。
        // 走这一行的场景：电脑上跑（x86 没有这颗芯片）——这就是手册 4.2 说的
        // "让对象照常存在、只是打不开"，所以本文件一个 #if __arm__ 都不需要。
        qDebug() << "ICM20608: IIO device not found!";
    } else {
        qDebug() << "ICM20608: found at" << iioPath;
        /* 读取scale因子 */
        // 换算用的两个系数，见下面的实现。放在这里读一次就够（它们不会变）
        readScaleFactors();
    }

    // connect(谁, 发什么信号, 谁接, 收到后干什么) —— 4 个参数逐个说：
    //   ① timer_2060        谁发信号（上面 new 的那个定时器）
    //   ② SIGNAL(timeout()) 发什么信号：QTimer 自带的，**计时到点**就发一次（不带参数）
    //   ③ this              谁来接（本对象）
    //   ④ SLOT(timer_timeout()) 收到后执行哪个函数
    // 注意：SLOT 里的函数必须在头文件的 slots: 段里声明过，否则 connect 会**运行期**失败
    //   （编译能过、运行时不报错、就是不响应 —— 这类问题最费时间）
    connect(timer_2060, SIGNAL(timeout()), this, SLOT(timer_timeout()));
}

// 析构函数：本类没有 new 出来的资源需要手动释放（定时器那边见构造函数的 ⚠），
// 所以是个空壳。留着它是好习惯：以后加了指针成员，就在这里 delete。
Icm20608::~Icm20608()
{

}

// ---------------------------------------------------------------------------
// 按名字找 IIO 设备，返回它的目录路径；找不到返回空串 QString()
//   （无参数。返回形如 "/sys/bus/iio/devices/iio:device1"，还要调用方自己
//    删掉"末尾斜杠"、自己判空 —— 所以构造里必须 if (isEmpty())）
// ---------------------------------------------------------------------------
QString Icm20608::findIioDevice()
{
    /* 遍历IIO设备，查找名为icm20608的设备 */
    // 第 ① 参数 "/sys/bus/iio/devices/"：要列哪个目录。这是内核给所有 IIO 设备
    //   约定的总目录，里面是 iio:device0、iio:device1… 每个对应一颗芯片。
    //   （本板：iio:device1 就是 ICM20608 —— 可用 cat .../name 亲眼确认）
    QDir iioDir("/sys/bus/iio/devices/");

    // entryList() 作用：把目录里的条目列成一个字符串列表
    //   参数是"过滤条件"，可以按位或 | 叠好几个：
    //     QDir::Dirs             → 只要子目录，不要普通文件
    //     QDir::NoDotAndDotDot   → 排除 "." 和 ".." 这两个特殊目录项
    //   少了第一个参数会把 name、uevent 这些文件也列进来；少了第二个会多出 "."、".."。
    QStringList devices = iioDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    // 这是 C++11 的范围 for："把 devices 里每个元素依次取出来，叫它 dev"
    //   const QString &dev：const=不修改它，&=不拷贝（引用），比按值快
    for (const QString &dev : devices) {
        // 每个设备目录下都固定有一个名为 name 的文件，里面是这颗芯片的名字。
        // + 号是 QString 的字符串拼接（不是加法）
        QString namePath = "/sys/bus/iio/devices/" + dev + "/name";

        QFile nameFile(namePath);       // QFile 是 Qt 对"文件描述符"的封装
        // open() 作用：打开文件；参数 QIODevice::ReadOnly = 只读方式
        //   返回值是 bool：true=成功。**必须判**：目录里可能有我们无权读的文件，
        //   也可能设备根本没注册成功 —— 不判就会在没打开的文件上读，拿到空串
        if (nameFile.open(QIODevice::ReadOnly)) {
            // readAll()：一次把文件内容全读出来（sysfs 里就一行文本，很小）
            // trimmed()：去掉首尾的空白字符 —— 关键是那个末尾换行符 '\n'。
            //   少了它，"icm20608\n" != "icm20608"，永远匹配不上！
            QString name = nameFile.readAll().trimmed();
            nameFile.close();           // 用完就关。sysfs 文件句柄也是有限的资源

            if (name == "icm20608") {   // QString 支持直接用 == 比较内容（不用 strcmp）
                return "/sys/bus/iio/devices/" + dev;   // 命中：把这个设备的目录返回回去
            }
        }
    }
    return QString();   // 循环走完还没找到 → 返回一个**空串**（不是 nullptr）
                        //   调用方用 isEmpty() 判它，见构造函数
}

// ---------------------------------------------------------------------------
// 读两个换算系数（scale）：raw 值 × scale = 物理量
//   （无参数。读进来存进 accelScale / gyroScale 两个 float 成员，返回 void）
// ---------------------------------------------------------------------------
void Icm20608::readScaleFactors()
{
    /* 读取加速度计scale (m/s² per LSB) */
    // 文件名 in_accel_scale 是内核定的命名规范：in=输入量 accel=加速度 scale=换算系数
    // 本板实测值 = 0.000598，意思是"raw 每 1 个 LSB 代表 0.000598 m/s²"。
    // 反过来算：1 / 0.000598 ≈ 1672 LSB 每 m/s² → 就是 ±2g 量程的 16384 LSB/g ✓
    accelScale = readSysfsFloat(iioPath + "/in_accel_scale");

    /* 读取陀螺仪scale (rad/s per LSB) */
    // 注意陀螺仪的单位是 rad/s（弧度/秒），不是度/秒，所以后面换算还要乘 180/π
    // 本板实测值 = 0.001064724 → 1/0.001064724 ≈ 939 LSB 每 rad/s
    //   → 折成 16.4 LSB 每 (°/s)，正是 ±2000 °/s 量程 ✓
    gyroScale = readSysfsFloat(iioPath + "/in_anglvel_scale");

    // 兜底：万一哪个文件读失败（readSysfsFloat 打不开会返回 0.0），
    // 就用写死的默认值顶上，免得后面整串数据都变成 0。
    // 这两个默认值就是上面注释里算出来的数值，和本板实测完全一致。
    if (accelScale <= 0.0f) accelScale = 0.000598f;
    if (gyroScale  <= 0.0f) gyroScale  = 0.001064724f;
}

// ---------------------------------------------------------------------------
// 读一个 sysfs 文件，把里面的文本转成 float（读不到就返回 0.0）
//   参数 const QString &path：要读哪个文件的完整路径。
//       从哪来：调用方用 iioPath 拼出来，例如
//               iioPath + "/in_accel_scale"
//       const=不改它；&=不拷贝整个字符串
// ---------------------------------------------------------------------------
float Icm20608::readSysfsFloat(const QString &path)
{
    QFile file(path);                       // 按路径造一个文件对象（此时还没打开）
    if (file.open(QIODevice::ReadOnly)) {   // 只读打开；成功才往下走
        QString val = file.readAll().trimmed();   // 全部读出 → 去空白/换行
        file.close();                       // 读完了就关
        return val.toFloat();               // QString → float。toFloat() 无参数
                                            //   转不了（比如文件里是乱码）会返回 0.0
    }
    return 0.0f;                            // 打不开就返回 0 —— 防御性写法：
                                            //   宁可有条数据是 0，也别让程序崩
}

// ---------------------------------------------------------------------------
// 同上，只是这次把文本转成 int（读不到返回 0）
//   参数 const QString &path：要读哪个文件，例如 iioPath + "/in_accel_x_raw"
//   为什么要有这个版本：raw 文件里存的是**整数**；虽然 toFloat() 也能转，
//   但用 toInt() 语义更清楚（这 7 个 raw 值本来就是整数）
// ---------------------------------------------------------------------------
int Icm20608::readSysfsInt(const QString &path)
{
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        QString val = file.readAll().trimmed();
        file.close();
        return val.toInt();
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 【核心】读一次全部数据并换算，最后发信号通知界面（无参数、无返回值）
//   谁调它：定时器到点 → timer_timeout() → 调它。也被页面手动调过（刷新一下）
// ---------------------------------------------------------------------------
void Icm20608::icm20608ReadData()
{
    if (iioPath.isEmpty()) {
        return;     // 没找到设备（比如在电脑上跑）→ 直接返回，一个文件都别去读。
                    // 这就是"对象存在、但什么也不做"的写法，界面那边不用加 #if
    }

    /* 从IIO sysfs读取原始数据 */
    // raw = "原始值"，是芯片寄存器里那个没换算过的整数。命名规范：
    //   in_anglvel_x/y/z_raw  角速度（anglvel = angular velocity 角速度）
    //   in_accel_x/y/z_raw    加速度
    //   in_temp_raw           温度
    // 为什么用 float 变量接 int：下面要拿它乘小数，float 写起来省事
    //   （其实用 int 也完全可以，乘 scale 时会自动提升为浮点）
    float gyro_x_raw = readSysfsInt(iioPath + "/in_anglvel_x_raw");
    float gyro_y_raw = readSysfsInt(iioPath + "/in_anglvel_y_raw");
    float gyro_z_raw = readSysfsInt(iioPath + "/in_anglvel_z_raw");
    float accel_x_raw = readSysfsInt(iioPath + "/in_accel_x_raw");
    float accel_y_raw = readSysfsInt(iioPath + "/in_accel_y_raw");
    float accel_z_raw = readSysfsInt(iioPath + "/in_accel_z_raw");
    float temp_raw = readSysfsInt(iioPath + "/in_temp_raw");

    /* 转换为物理单位 */
    // 陀螺仪: rad/s → °/s (乘以 180/PI)
    // 57.29578 = 180 ÷ 3.1415926 —— 弧度转角度的固定系数，背下来就行
    const float radToDeg = 57.29578f;
    // 三步走：raw（整数） × scale（rad/s 每 LSB） = rad/s，再 × 57.29578 = °/s
    // 举个实例：raw = -429 时 -429 × 0.001064724 × 57.29578 ≈ -26.2 °/s
    float gyro_x_act = gyro_x_raw * gyroScale * radToDeg;
    float gyro_y_act = gyro_y_raw * gyroScale * radToDeg;
    float gyro_z_act = gyro_z_raw * gyroScale * radToDeg;

    // 加速度计: m/s² → g (除以 9.81)
    // 0.10197 就是 1 ÷ 9.80665 —— 把"米每二次方秒"换成"几个重力加速度"
    const float ms2ToG = 0.10197f;  // 1/9.81
    // 板子平放时，z 轴应该读到 ±1 g 左右（重力全压在这个轴上），x/y 接近 0
    //   —— 这就是"倾斜板子数字会变"的原理，验收时就看它
    float accel_x_act = accel_x_raw * accelScale * ms2ToG;
    float accel_y_act = accel_y_raw * accelScale * ms2ToG;
    float accel_z_act = accel_z_raw * accelScale * ms2ToG;

    // 温度: ICM20608 公式 °C = raw/326.8 + 25
    // 326.8 是芯片手册给的灵敏度（每 1℃ 变化 326.8 个 LSB），+25 是零点偏移
    //   （raw=0 时对应 25℃）。本板刚才实测 raw=4880 → 4880/326.8+25 = 39.9℃
    //   📖 顺带一提：内核在同一个目录里还摆了 in_temp_scale=0.002941 和
    //      in_temp_offset=12421，那套是按 MPU6050 的公式(raw/340+36.53)配的，
    //      用它算出来是 50.9℃。本芯片是 ICM20608，按它自己的手册就是 326.8/25。
    //      两套公式只差一个固定偏移，画曲线看"变化趋势"完全不受影响。
    float temp_act = temp_raw / 326.8f + 25.0f;

    // QString::number(数值, 格式, 精度)：把数字变成字符串
    //   ① 数值：上面算出来的浮点值
    //   ② 'f' ：定点小数格式（不用科学计数法；'g' 会自动切换、'e' 是指数）
    //   ③ 2   ：小数点后保留 2 位
    // 为什么存成 QString 不存 float：原码把界面当"文本"用（直接显示、直接发给
    //   MQTT），所以在这里一次性转好；页面拿到就能 setText()，不用再转一遍。
    gxdata = QString::number(gyro_x_act, 'f', 2);
    gydata = QString::number(gyro_y_act, 'f', 2);
    gzdata = QString::number(gyro_z_act, 'f', 2);
    axdata = QString::number(accel_x_act, 'f', 2);
    aydata = QString::number(accel_y_act, 'f', 2);
    azdata = QString::number(accel_z_act, 'f', 2);
    tempdata = QString::number(temp_act, 'f', 2);

    // emit 是"发信号"的意思。这个信号在头文件里声明过：
    //   signals: void icm20608DataChanged();
    // 作用：本对象自己不知道谁在看数据，只管喊一声"数据更新了！"，
    //   所有 connect 过这个信号的槽函数都会被调用（可能是 1 个，也可能是 3 个）。
    // 界面收到后调 axData() 之类把新值取走 —— 这就是"零件不碰界面"的分工。
    // 头文件里那 7 个 Q_PROPERTY 的 NOTIFY 也写的它，所以 QML 那边会自动刷新。
    emit icm20608DataChanged();
}

// ============================================================================
// 下面 7 个 getter 都长一个样：把换算好的字符串交出去（无参数，返回 QString）
//   谁调它们：页面的槽函数（收到 icm20608DataChanged 信号后）
//   函数名规矩：成员变量叫 axdata，取值的函数叫 axData() —— 大小写差一个字母，
//   这是 Qt 的惯例（头文件里 7 个 Q_PROPERTY 也是靠这个"名 → 函数"的映射）
// ============================================================================
QString Icm20608::gxData()
{
    return  gxdata;      // 三轴角速度，单位 °/s
}

QString Icm20608::gyData()
{
    return  gydata;
}

QString Icm20608::gzData()
{
    return  gzdata;
}

QString Icm20608::axData()
{
    return  axdata;      // 三轴加速度，单位 g（1 = 一个重力加速度）
}

QString Icm20608::ayData()
{
    return  aydata;
}

QString Icm20608::azData()
{
    return  azdata;
}

QString Icm20608::tempData()
{
    return  tempdata;    // 温度，单位 ℃
}

// ---------------------------------------------------------------------------
// 定时器到点的槽函数（无参数、无返回值）
//   谁调它：构造函数里那根 connect —— 定时器每 500ms 发一次 timeout 就进这里。
//   注意：它自己**不判断**该不该读，只负责"到点了就去做"。
// ---------------------------------------------------------------------------
void Icm20608::timer_timeout()
{
    icm20608ReadData();     // 真正干活的是它（读 + 换算 + 发信号）
}

// ---------------------------------------------------------------------------
// 开/关连续采集
//   参数 bool str：true = 开始采集，false = 停止采集。
//       从哪来：页面里调（例如进这一页时 setCapture(true)、离开时 setCapture(false)），
//       或远程通过 Q_INVOKABLE 调（头文件里它带了 Q_INVOKABLE，QML 里可以写
//       icm20608.setCapture(true)，QMetaObject::invokeMethod 也能按名字调）。
//   ⚠ 参数名 str 是原码留下的（str 是 string 的缩写，这里却是 bool，看着别扭）；
//     自己写的话叫 enabled 或 start 更贴切。
// ---------------------------------------------------------------------------
void Icm20608::setCapture(bool str)
{
    if (str)
        timer_2060->start(500);     // start(毫秒)：启动定时器，每 500 毫秒发一次 timeout。
                                    //   参数是 int 毫秒 —— 500 = 每秒读 2 次。
                                    //   想更平滑就调小（如 100），但 sysfs 读 7 个文件也要时间，
                                    //   太快没有意义（芯片自己的采样率就有限）
    else
        timer_2060->stop();         // stop()：无参数，停掉。注意 stop 只是"不再触发"，
                                    //   已经存好的数据还在，界面上的数字不会清零
}
