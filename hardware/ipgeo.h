// ============================================================================
// hardware/ipgeo.h —— 网络定位零件（本项目自创，原 smart_terminal 里没有这一页）
//
// 【这个类干什么】拿板子的"公网出口 IP"去问一个公开的定位服务，
//   让它根据这个 IP 反查"这台设备大概在哪个城市"，把结果存成几个字符串。
//
// 【和 ap3216c / icm20608 最大的不同 —— 三种"取数据"的路子】
//   AP3216C  —— 内核里没驱动，只能自己 open("/dev/i2c-0") 发 I2C 报文（自己开车）
//   ICM20608 —— 内核里有驱动，去 /sys 读文本文件（坐公交）
//   IpGeo    —— 根本不碰板子上的硬件，走的是**网络**：发一个 HTTP 请求出去，
//                等服务器回一段 JSON 文本，再把里面的字段抠出来（打电话问别人）
//   所以这个文件里一个系统头文件都不需要（没有 fcntl.h / unistd.h / ioctl.h），
//   全是 Qt 的网络类。
//
// 【数据流（和传感器零件完全同构）】
//   QTimer 每 10 分钟响一次，或者页面上点"刷新"
//   → requestData()        发一个 HTTP GET 出去
//   → 网络层异步回调 replyFinished()   ← 注意不是紧接着执行的，见下面的坑
//   → parseJson()          把 JSON 拆成几个字段，存进成员变量
//   → emit ipGeoDataChanged()
//   → 页面（form/gpslocation.cpp）的槽收到后刷新界面
//   分工还是那句：本类只负责"拿到数据"，一行界面代码都没有。
//
// 【★ 新手最容易踩的坑：网络是"异步"的】
//   manager->get(req) 这一句**立刻就返回了**，此时服务器还没回话、数据还没到。
//   真正的结果要等一会儿，Qt 会回头来调你用 connect 接上的那个槽。
//   所以**绝对不能**写成：
//       QByteArray data = manager->get(req)->readAll();   // ✗ 拿到的一定是空的
//   记住一句话：网络请求是"发出去就不管了，回来了再叫我"。
//
// 【★ 第二个坑：这个类在没网的时候也必须活着】
//   板子的 WiFi 不是开机自动连的（见 上板流程），所以"现在没网"是常态而不是异常。
//   没网时这里不会崩，只会把 errMsg 填上原因，然后照样 emit 信号让页面去显示提示。
// ============================================================================

#ifndef IPGEO_H                // 头文件守卫，和 ap3216c.h 是同一套写法
#define IPGEO_H

#include <QObject>                  // 基类：要发信号就必须继承它
#include <QTimer>                   // 定时器（自动刷新 + 超时保护）
#include <QString>                  // 存解析出来的文字
#include <QUrl>                     // 请求地址
#include <QNetworkAccessManager>    // ★ 网络总管：一个对象管所有请求
#include <QNetworkReply>            // ★ 一次请求的"回执"，读数据、查错误都靠它
#include <QNetworkRequest>          // ★ 一次请求的"信封"：装地址、装请求头

class IpGeo : public QObject
{
    Q_OBJECT                       // 不能省：信号槽靠 moc 生成的元对象代码

public:
    explicit IpGeo(QObject *parent = 0);   // explicit 防隐式转换（对齐 ap3216c.h 的写法）
    ~IpGeo();

    // 开/关自动刷新。写法对齐 ap3216c 的 setCapture(bool)：
    //   传 true  → 启动定时器，以后每 10 分钟自动问一次
    //   传 false → 停掉定时器，不自动问了（页面点"刷新"仍然能手动问）
    Q_INVOKABLE void setCapture(bool str);

    // ---- 读出数据的"窗口"：每个字段一个 getter ----
    // 都返回 QString（不是 double），因为拿出去基本就是往 QLabel 上贴，省一次转换。
    QString countryData();      // 国家，例如 "中国"
    QString regionData();       // 省 / 直辖市，例如 "重庆市"
    QString cityData();         // 城市，例如 "重庆市"
    QString ispData();          // 运营商，例如 "CNC Group CHINA169 ..."
    QString timezoneData();     // 时区，例如 "Asia/Shanghai"
    QString ipData();           // 公网出口 IP
    QString latitudeData();     // 纬度（字符串版，给界面显示用）
    QString longitudeData();    // 经度（字符串版，给界面显示用）
    QString updateTimeData();   // 上次成功拿到数据的时刻，例如 "12:30:05"
    QString errorData();        // ★ 出错原因；**空字符串 = 一切正常**（重要约定！）

    // 给地图算瓦片坐标用的"数值版"经纬度。
    // 为什么不复用上面两个：QString 转 double 每次都得判空、判转换失败，
    // 不如在这里直接开一个口子，把原始 double 给出去。
    double latitudeValue();
    double longitudeValue();

    // Q_PROPERTY：把数据登记成 Qt 认识的"属性"（QML 里可以 writeable 地绑定）。
    // 本项目是纯 C++ widget，用不到 QML，但保留它和 ap3216c.h 的结构保持一致，
    // 也顺便说明"属性 = 能读 + 变了会发通知"这个 Qt 惯例。
    Q_PROPERTY(QString countryData    READ countryData    NOTIFY ipGeoDataChanged)
    Q_PROPERTY(QString regionData     READ regionData     NOTIFY ipGeoDataChanged)
    Q_PROPERTY(QString cityData       READ cityData       NOTIFY ipGeoDataChanged)
    Q_PROPERTY(QString ispData        READ ispData        NOTIFY ipGeoDataChanged)
    Q_PROPERTY(QString timezoneData   READ timezoneData   NOTIFY ipGeoDataChanged)
    Q_PROPERTY(QString ipData         READ ipData         NOTIFY ipGeoDataChanged)
    Q_PROPERTY(QString latitudeData   READ latitudeData   NOTIFY ipGeoDataChanged)
    Q_PROPERTY(QString longitudeData  READ longitudeData  NOTIFY ipGeoDataChanged)
    Q_PROPERTY(QString updateTimeData READ updateTimeData NOTIFY ipGeoDataChanged)
    Q_PROPERTY(QString errorData      READ errorData      NOTIFY ipGeoDataChanged)

private:
    // 把服务器回的那段 JSON 拆开，把字段填进成员变量。
    // 参数 bytes 就是响应体的原始字节（QByteArray）。
    void parseJson(const QByteArray &bytes);

    // 统一处理"这次没拿到数据"：把原因填进 errMsg。
    // ⚠ 这里**不发信号** —— 发信号统一放在 replyFinished() 末尾，
    //   保证一次请求只通知页面一次（否则超时那条路上会发两遍）。
    // 参数 reason 是要显示给用户看的中文原因。
    void fail(const QString &reason);

    QNetworkAccessManager *manager;     // 网络总管。一个程序里有一个就够，所有请求都用它发
    QNetworkReply         *reply;       // 本次请求的回执（同时只能有一个，所以用 busy 挡住）
    QTimer                *timer;       // 自动刷新定时器（10 分钟一次）
    QTimer                *timeoutTimer; // ★ 超时保护：Qt 5.12 的 QNetworkRequest 没有内置超时，
                                         //   不加这个，断网时请求要挂到 TCP 自己超时（可能两分钟）
                                         //   页面就一直卡在"正在获取…"
    QUrl                   url;         // 请求地址（构造函数里填好，就那一条）
    bool                   busy;        // 是否已有一个请求在路上 —— 防止手快连点"刷新"发出去一堆

    // ---- 解析出来的数据（全私有，外面只能通过上面的 getter 读）----
    QString country;        // 国家
    QString region;         // 省/直辖市
    QString city;           // 城市
    QString isp;            // 运营商
    QString timezone;       // 时区
    QString ipAddr;         // 公网 IP
    QString updateTime;     // 上次成功更新的时刻
    QString errMsg;         // 错误原因（空 = 正常）
    double  latitude;       // 纬度（数值，给地图用）
    double  longitude;      // 经度（数值，给地图用）

public slots:
    void requestData();     // 发一次请求。定时器、刷新按钮、构造函数都调它
    void timer_timeout();   // 自动刷新定时器到点（命名对齐 ap3216c 的 timer_timeout）
    void replyFinished();   // ★ 服务器回话了 / 或者失败了。Qt 自动调，不用我们手动触发
    void replyTimeout();    // 等太久了，主动把请求掐断

signals:
    void ipGeoDataChanged();  // 数据变了（成功或失败都会发，页面按 errorData() 判断是哪种）
};

#endif // IPGEO_H
