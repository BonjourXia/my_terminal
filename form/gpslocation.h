// ============================================================================
// form/gpslocation.h —— 网络定位页面（本项目自创，原 smart_terminal 里没有这一页）
//
// 【这一页干什么】左边一张地图，用十字准星标出"板子大概在哪"；
//   右边一栏文字：城市 / 经纬度 / 公网 IP / 运营商 / 时区。
//
// 【和别的页面最大的不同：数据不在板子上，在网上】
//   第 1 级 banna 的图片在 res.qrc 里（编译进程序，永远都在）
//   第 3 级传感器的数据在 /dev/i2c-0、/sys 里（插上就有）
//   这一页的数据要**发 HTTP 请求去问互联网**，所以它有三种状态：
//     ① 正常：显示地图 + 数据
//     ② 没网 / 请求失败：显示一行错误提示（★ 板子的 WiFi 不是开机自动连的，这是常态）
//     ③ 正在获取：显示"正在获取…"
//   页面必须把三种都处理掉，不能只在"有网"的时候才对。
//
// 【页面结构（824 x 600）】
//   ┌──────────────────────────────────────────────┐
//   │ 网络定位              ┌────────────────────┐ │
//   │ ┌──────────────────┐  │ 重庆市       (大字) │ │
//   │ │                  │  │ 中国 · 重庆市       │ │
//   │ │   3x3 高德瓦片    │  ├────────────────────┤ │
//   │ │   拼成的地图      │  │ 纬度   29.5689°    │ │
//   │ │                  │  │ 经度   106.5577°   │ │
//   │ │       ✛ 定位点    │  │ 公网IP 123.147...  │ │
//   │ │                  │  │ 运营商 CNC Group…  │ │
//   │ │                  │  │ 时区   Asia/Shanghai│ │
//   │ └──────────────────┘  ├────────────────────┤ │
//   │ 上次更新 12:30:05     │ [     刷  新      ] │ │
//   └──────────────────────────────────────────────┘
//
// 【★ 为什么是 QWidget 而不是 QMainWindow】
//   musicplay / videoplayer / imx6ulltest 都是 QMainWindow（它们要么继承原码，
//   要么要用 QTabWidget 自带的 centralWidget 那一层）。
//   这一页是全新的，不需要那些，QWidget 就够了 —— 而且它同样是 QWidget 的子类，
//   mainwidget.cpp 的 items[] 里照样塞得进去。
// ============================================================================

#ifndef GPSLOCATION_H
#define GPSLOCATION_H

#include <QWidget>          // 基类
#include <QLabel>           // 地图、文字、键值对全靠它
#include <QPushButton>      // "刷新"按钮
#include <QGridLayout>      // 右边那栏"键 | 值"两列用网格布局排
#include <QShowEvent>       // showEvent() 的参数类型
#include <QPixmap>          // 拼好的地图画布
#include <QTimer>           // 瓦片超时保护
#include <QNetworkAccessManager>    // 拉地图瓦片的网络总管
#include <QNetworkReply>

#include "ipgeo.h"          // ★ 数据源零件（IP 反查城市），页面只管用，不管怎么来的

// ---------------------------------------------------------------------------
// 地图相关的几个常量。写成常量是为了"改一处、处处生效"：
//   想换缩放级别就改 MAP_ZOOM，想换地图大小就改 MAP_VIEW，下面代码一行都不用动。
//
// 【瓦片(tile)是什么】在线地图都不是"一张大图"，而是切成很多张 256x256 的小方块，
//   按 z/x/y 三级编号存在服务器上。要给谁显示地图，就按坐标算出要哪几块，拉下来拼。
//     z = 缩放级别（0 是全世界一张图，每 +1 就把地图放大一倍、瓦片数翻两番）
//     x = 从左往右数第几列
//     y = 从上往下数第几行
// ---------------------------------------------------------------------------
#define GPS_TILE_SIZE   256     // 一张瓦片的边长（高德和几乎所有地图都是 256，写死）
#define GPS_GRID        3       // 拼 3x3 = 9 张。为什么是 3：
                                //   定位点在正中心那张上，四周各一圈保证它不会贴到边上
#define GPS_CANVAS      (GPS_TILE_SIZE * GPS_GRID)   // 拼出来的画布边长 = 768
#define GPS_ZOOM        12      // 缩放级别。z=12 时一张瓦片约 10 公里宽，
                                //   3x3 一共约 30 公里 —— 正好铺满一个城市
#define GPS_VIEW        460     // 地图显示尺寸（画布 768 会等比缩到 460）

class GpsLocation : public QWidget
{
    Q_OBJECT

public:
    explicit GpsLocation(QWidget *parent = nullptr);
    ~GpsLocation();

protected:
    // ★ showEvent：控件"被显示出来"时 Qt 自动调它（对应还有 hideEvent）。
    //   这里用它来做**懒加载**：只有用户真点进这一页，才去联网。
    //   为什么不放在构造函数里：mainwidget.cpp 开头就把 9 个页面全部 new 出来了，
    //   构造函数里发请求 = 每次开程序都偷偷发一次网络包（哪怕你从没点进这一页），
    //   在单核的板子上还会拖慢启动。
    //   override 是 C++11 关键字：告诉编译器"我是在重写基类的虚函数"，
    //   万一签名写错了（比如少个 const）它会直接报错，而不是悄悄变成一个新函数。
    void showEvent(QShowEvent *event) override;

private:
    // ---- 界面控件（全手写，没有 .ui —— 见 从零复现手册 的"不用 Qt Designer"约定）----
    QLabel      *titleLabel;        // 左上角"网络定位"
    QLabel      *mapLabel;          // ★ 地图：拼好的瓦片 + 定位点都贴在这上面
    QLabel      *statusLabel;       // 地图下面那行灰字："正在获取…" / "上次更新 …" / 错误提示
    QLabel      *cityLabel;         // 右侧大字城市名
    QLabel      *addrLabel;         // 右侧小字："中国 · 重庆市"
    QLabel      *keyLabel[5];       // 右侧"键"列：纬度 / 经度 / 公网IP / 运营商 / 时区
    QLabel      *valLabel[5];       // 右侧"值"列：和上面一行一对应
    QPushButton *refreshBtn;        // "刷新"按钮

    // ---- 零件 ----
    IpGeo                 *ipGeo;       // 数据源：IP 反查城市
    QNetworkAccessManager *tileManager; // ★ 另一套网络用法：拉地图瓦片（和 ipGeo 各管各的）

    // ---- 地图拼接状态 ----
    QPixmap tileCanvas;         // 768x768 的画布，9 张瓦片往上面贴
    bool    tileArrived[9];     // 每张瓦片到位没（true/false）
    int     tilePending;        // 还有几张没回来 —— 减到 0 就说明凑齐了，可以拼了
    int     tileOkCount;        // 9 张里成功了几张（用来判断"是不是全挂了"）
    int     centerTileX;        // 中心瓦片的列号
    int     centerTileY;        // 中心瓦片的行号
    double  markerX;            // 定位点在 768x768 画布上的像素横坐标
    double  markerY;            // 定位点在 768x768 画布上的像素纵坐标
    QTimer *mapTimeoutTimer;    // 瓦片超时保护
    bool    started;            // 懒加载标记：showEvent 只该真正干活一次

    // ---- 界面搭建 ----
    void ui_init();             // 建控件、装布局、接信号（= 原来 setupUi 干的活，全手写）

    // ---- 地图三件事 ----
    void loadMap(double lat, double lon);   // ① 按经纬度算出要哪 9 张瓦片，发出去
    void showMap();                         // ② 9 张都回来了：缩放 + 打定位点 + 贴到 label
    void showMapPlaceholder(const QString &text);  // ③ 瓦片全拉不到时的兜底画面

    // ---- 打定位点（画在画布上，不单独做控件）----
    // 参数 pix 是画布（会被就地修改），x/y 是定位点在该画布上的像素坐标
    void drawMarker(QPixmap &pix, double x, double y);

    // ---- 坐标换算（Web 墨卡托投影，见 .cpp 里的详细推导）----
    // 声明成 static：它们不用访问任何成员变量，纯输入输出，
    // 这样调用时不用先有对象，语义上也更清楚
    static double  lonToTileX(double lon, int zoom);   // 经度 → 瓦片列号（带小数）
    static double  latToTileY(double lat, int zoom);   // 纬度 → 瓦片行号（带小数）
    static QString tileUrl(int x, int y, int zoom);    // 三兄弟 → 一个完整 URL

private slots:
    void slotRefreshClicked();  // 点了"刷新"按钮
    void getIpGeoData();        // ★ 收到 IpGeo 发来的"数据变了"信号
    void tileFinished();        // ★ 一张瓦片回来了（9 张全靠这一个槽接）
    void mapTimeout();          // 瓦片等太久，别再干等了
};

#endif // GPSLOCATION_H
