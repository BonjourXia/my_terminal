// ============================================================================
// form/gpslocation.cpp —— 网络定位页面
//
// 【这一页要干的三件事，按发生顺序】
//   ① 问位置：调 ipGeo->requestData() 发 HTTP 请求（数据不在板子上，在网上）
//   ② 拉地图：拿到经纬度后，算出需要哪 9 张地图瓦片，一张一张拉回来
//   ③ 拼装  ：9 张贴成 768x768 的画布 → 缩放到 460x460 → 打上定位点 → 贴给 QLabel
//
// 【★ 本页最核心的一个概念：异步 + 凑齐】
//   第 ② 步的 9 张瓦片是**同时**发出去的（不是发一张等一张，那样太慢），
//   回来顺序**完全不保证**：可能第 7 张先到、也可能第 3 张永远不到。
//   所以不能"发完就当拼好了"，必须：
//     发之前 —— 立一个计数器 tilePending = 9
//     每回来一张 —— tilePending 减 1
//     减到 0 —— 才说明凑齐了，这时候才能拼
//   这个套路在所有"并发拿一堆东西、要等全部到齐"的场景里都通用
//   （页面右上角先用灰底顶着，就是防止你在只到了 3 张的时候就把半成品贴出去）。
//
// 【地图瓦片是什么、为什么是 3x3】
//   见 gpslocation.h 里的说明。简单说：z/x/y 三个数字就能定位地球上任意一块
//   256x256 的地图小方块，我们用 9 块拼出一张"以大致的所在地为中心"的地图。
// ============================================================================

#include "gpslocation.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStringList>
#include <QPainter>         // ★ 画定位点、贴瓦片、写占位文字都靠它
#include <QImage>           // 瓦片字节 → 解码成图
#include <QUrl>
#include <QNetworkRequest>
#include <cmath>            // std::floor / std::log / std::tan / std::cos

// 圆周率。故意不用 <cmath> 里的 M_PI：
// M_PI 不是 C++ 标准的一部分，在严格模式下有些编译器不定义它，自己写一个最保险
#define GPS_PI  3.14159265358979323846

// ---------------------------------------------------------------------------
// 构造函数：造零件 + 建界面
//   ⚠ 顺序不能乱：ui_init() 里要用 ipGeo 接信号，所以 ipGeo 必须先造出来。
//   ⚠ 这里**故意不发网络请求** —— 9 个页面开机全部构造，
//     在这里发请求等于每次开程序都偷偷联网。发请求放在 showEvent() 里（见下）。
// ---------------------------------------------------------------------------
GpsLocation::GpsLocation(QWidget *parent) : QWidget(parent)
{
    // ---- 零件一：数据源（IP 反查城市）。传 this 当爹，页面没了它跟着没 ----
    ipGeo = new IpGeo(this);

    // ---- 零件二：拉地图瓦片的网络总管 ----
    // 为什么不让 ipGeo 顺便把瓦片也拉了：分工。
    //   ipGeo 管"板子在哪"（一份数据，问一次）
    //   这里管"把地图画出来"（九张图，纯界面的事）
    // 混在一起的话，换个定位服务就得连地图代码一起改
    tileManager = new QNetworkAccessManager(this);

    // ---- 瓦片超时保护：15 秒还没凑齐就别干等了 ----
    mapTimeoutTimer = new QTimer(this);
    mapTimeoutTimer->setSingleShot(true);
    connect(mapTimeoutTimer, SIGNAL(timeout()), this, SLOT(mapTimeout()));

    // ---- 地图状态初始化（全先摆成"空"的样子）----
    tilePending = 0;
    tileOkCount = 0;
    centerTileX = 0;
    centerTileY = 0;
    markerX     = 0.0;
    markerY     = 0.0;
    started     = false;
    for (int i = 0; i < 9; i++)
        tileArrived[i] = false;

    ui_init();          // ★ 建界面放最后：上面那些成员都备齐了它才不慌
}

// ---------------------------------------------------------------------------
// 析构函数：什么都不用做
//   ipGeo / tileManager / mapTimeoutTimer 都挂了对象树（构造时传了 this），
//   本页面被销毁时 Qt 会替我们全部回收，这里不用手写 delete。
// ---------------------------------------------------------------------------
GpsLocation::~GpsLocation()
{
}

// ---------------------------------------------------------------------------
// ★ 懒加载：只有用户真点进这一页，才去联网
//   showEvent 是"控件被显示出来"时 Qt 自动调的回调（和 paintEvent 是一类东西）。
//
//   ⚠ 为什么要 started 这个开关：在 QStackedWidget 里切走再切回来，
//     showEvent 会被**再次**触发。不拦一下的话，每切一次就重发一次请求。
// ---------------------------------------------------------------------------
void GpsLocation::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);      // 先让基类做完它该做的，别把事件吃了

    if (started)                    // 已经启动过，直接返回
        return;

    started = true;
    ipGeo->setCapture(true);        // 开自动刷新（以后每 10 分钟问一次）
    ipGeo->requestData();           // 立刻先问一次，别让用户等 10 分钟
}

// ---------------------------------------------------------------------------
// 建界面（= 原来 ui->setupUi(this) 干的活，这一页全手写，没有 .ui 文件）
//   布局思路：一个横向布局，左边一栏装地图，右边一栏装数据。
// ---------------------------------------------------------------------------
void GpsLocation::ui_init()
{
    // 页面尺寸。★ 严格说这里设不设都行 —— 被 addWidget 塞进 QStackedWidget 后，
    // 卡片盒会按自己的大小重新安排页面尺寸，布局管理器再跟着调整内部控件。
    // 写上是和 musicplay/videoplayer 那些页面保持一致的写法，一眼能看出页面多大。
    this->setGeometry(0, 0, 824, 600);

    // 给页面起内部名字。qss 里用 #gpsPage 这个选择器就能选中它
    this->setObjectName("gpsPage");

    // ================= 左边一栏：标题 + 地图 + 状态行 =================
    titleLabel = new QLabel("网络定位");
    titleLabel->setObjectName("gpsTitle");

    mapLabel = new QLabel();
    mapLabel->setObjectName("gpsMap");
    // setFixedSize：宽高都锁死。为什么不用布局自己去挤：
    // 地图是方块，尺寸必须精确，让布局拉扯成非正方形的话瓦片会变形
    mapLabel->setFixedSize(GPS_VIEW, GPS_VIEW);
    mapLabel->setAlignment(Qt::AlignCenter);    // 里面的图和字都居中

    statusLabel = new QLabel("正在获取…");
    statusLabel->setObjectName("gpsStatus");

    // 先贴一张"正在获取"的占位图，免得刚进页面是一片空白
    showMapPlaceholder("正在获取位置…");

    QVBoxLayout *leftBox = new QVBoxLayout();
    leftBox->addWidget(titleLabel);
    leftBox->addWidget(mapLabel);
    leftBox->addWidget(statusLabel);
    leftBox->setSpacing(12);

    // ================= 右边一栏：城市 + 键值对 + 刷新按钮 =================
    cityLabel = new QLabel("——");
    cityLabel->setObjectName("gpsCity");

    addrLabel = new QLabel("等待数据");
    addrLabel->setObjectName("gpsAddr");

    // 五组"键 | 值"用网格布局排成两列：
    //   第 0 列放"纬度"这种小灰字，第 1 列放具体的值
    //   每行一个 QLabel，行号就是网格的 y —— 所以五行正好是 0..4
    QString keys[5] = {"纬度", "经度", "公网 IP", "运营商", "时区"};

    QGridLayout *infoGrid = new QGridLayout();
    infoGrid->setHorizontalSpacing(14);         // 两列之间的横向间距
    infoGrid->setVerticalSpacing(14);           // 行与行之间的纵向间距

    for (int i = 0; i < 5; i++) {
        keyLabel[i] = new QLabel(keys[i]);
        keyLabel[i]->setObjectName("gpsKey");

        valLabel[i] = new QLabel("——");
        valLabel[i]->setObjectName("gpsVal");
        // 运营商那串能到 40 个字符（"CNC Group CHINA169 Chongqing Province Network"），
        // 不折行的话会把右边的布局整个撑宽，把地图挤变形
        valLabel[i]->setWordWrap(true);
        valLabel[i]->setFixedWidth(190);

        // addWidget(控件, 行, 列, 对齐方式)
        // AlignTop：值可能折成两行，让它从顶格开始排，和左边的键对齐
        infoGrid->addWidget(keyLabel[i], i, 0, Qt::AlignLeft | Qt::AlignTop);
        infoGrid->addWidget(valLabel[i], i, 1, Qt::AlignLeft | Qt::AlignTop);
    }
    infoGrid->setColumnStretch(1, 1);           // 第 1 列（值那一列）占满剩余宽度

    refreshBtn = new QPushButton("刷  新");
    refreshBtn->setObjectName("gpsRefresh");
    refreshBtn->setFixedHeight(42);

    QVBoxLayout *rightBox = new QVBoxLayout();
    rightBox->addWidget(cityLabel);
    rightBox->addWidget(addrLabel);
    rightBox->addSpacing(10);
    rightBox->addLayout(infoGrid);      // ★ 注意是 addLayout 不是 addWidget —— 加的是布局不是控件
    rightBox->addStretch();             // 弹簧：把后面的刷新按钮顶到最底部
    rightBox->addWidget(refreshBtn);
    rightBox->setSpacing(8);

    // ================= 左右合起来 =================
    // QHBoxLayout(this)：构造时就把 this 传给布局 —— 等于顺便 setLayout(this)，
    // 所以下面不用再写一句 this->setLayout(rootBox)
    QHBoxLayout *rootBox = new QHBoxLayout(this);
    rootBox->addLayout(leftBox);
    rootBox->addSpacing(20);            // 左右两栏之间留一条固定空隙
    rootBox->addLayout(rightBox);
    rootBox->setContentsMargins(24, 20, 24, 20);   // 页边距：左 上 右 下

    // ================= 接信号 =================
    connect(refreshBtn, SIGNAL(clicked()), this, SLOT(slotRefreshClicked()));
    connect(ipGeo, SIGNAL(ipGeoDataChanged()), this, SLOT(getIpGeoData()));
}

// ---------------------------------------------------------------------------
// ★ 收到 IpGeo 的消息："数据变了"（成功或失败都会来，靠 errorData() 分辨）
// ---------------------------------------------------------------------------
void GpsLocation::getIpGeoData()
{
    refreshBtn->setEnabled(true);       // 有回音了，把刷新按钮解禁

    // ================= 情况一：这次没拿到数据 =================
    if (!ipGeo->errorData().isEmpty()) {
        statusLabel->setText("⚠ " + ipGeo->errorData());

        // 从来没成功过（连时间戳都是空的）—— 说明一次都没定位上，
        // 右边那栏还是初始的"——"，地图也换成一句提示，别让用户对着灰方块猜
        if (ipGeo->updateTimeData().isEmpty())
            showMapPlaceholder("地图不可用\n（拿不到位置）");

        // ★ 注意这里就 return 了，**没有清空已有的数据**。
        //   如果之前成功过，就保留上次的位置和地图，只是多一行错误提示。
        //   宁可显示"旧数据 + 一行警告"，也好过整页变空白 ——
        //   这也是真实导航软件的做法（隧道里 GPS 断了，地图不会消失）
        return;
    }

    // ================= 情况二：正常拿到数据 =================
    cityLabel->setText(ipGeo->cityData());

    // 拼"国家 · 省 · 市"。★ 为什么要判重：
    // 直辖市（重庆/北京/上海/天津）的 regionName 和 city 都是"重庆市"，
    // 直接拼会得到"中国 · 重庆市 · 重庆市"，看着像 bug
    QStringList parts;
    if (!ipGeo->countryData().isEmpty()) parts << ipGeo->countryData();
    if (ipGeo->regionData() != ipGeo->cityData() && !ipGeo->regionData().isEmpty())
        parts << ipGeo->regionData();
    if (!ipGeo->cityData().isEmpty()) parts << ipGeo->cityData();
    addrLabel->setText(parts.join(" · "));

    valLabel[0]->setText(ipGeo->latitudeData()  + "°");
    valLabel[1]->setText(ipGeo->longitudeData() + "°");
    valLabel[2]->setText(ipGeo->ipData());
    valLabel[3]->setText(ipGeo->ispData());
    valLabel[4]->setText(ipGeo->timezoneData());

    statusLabel->setText("上次更新 " + ipGeo->updateTimeData());

    // ★ 最后一步：有了新坐标，重新拉地图。
    //   这一句是**不阻塞**的 —— loadMap 把 9 个请求发出去就立刻返回了，
    //   真正的拼图在 tileFinished() 里慢慢进行，界面不会被卡住
    loadMap(ipGeo->latitudeValue(), ipGeo->longitudeValue());
}

// ---------------------------------------------------------------------------
// 点了"刷新"按钮
// ---------------------------------------------------------------------------
void GpsLocation::slotRefreshClicked()
{
    refreshBtn->setEnabled(false);              // 先禁用，防止手快连点（请求回来会解禁）
    statusLabel->setText("正在获取…");
    showMapPlaceholder("正在获取位置…");
    ipGeo->requestData();                       // 发出去就不管了，结果会走 getIpGeoData()
}

// ===========================================================================
//                          地图部分（本页的重头戏）
// ===========================================================================

// ---------------------------------------------------------------------------
// ① 按经纬度算出要哪 9 张瓦片，全部发出去
//   参数 lat/lon：定位点的经纬度
// ---------------------------------------------------------------------------
void GpsLocation::loadMap(double lat, double lon)
{
    // 画布先填成深灰。等会儿瓦片一张张往上贴，
    // 万一有拉不到的地方，露出来的就是这个底色（而不是花屏）
    tileCanvas = QPixmap(GPS_CANVAS, GPS_CANVAS);
    tileCanvas.fill(QColor(43, 43, 43));

    // ★ 经纬度 → 带小数的瓦片编号（整数部分 = 第几块，小数部分 = 在这块里的位置）
    double txf = lonToTileX(lon, GPS_ZOOM);
    double tyf = latToTileY(lat, GPS_ZOOM);

    // 定位点所在的那一块，就是中心块
    centerTileX = int(std::floor(txf));
    centerTileY = int(std::floor(tyf));

    // ★★ 整个地图部分最关键的两行：定位点在 768x768 画布上的像素位置
    //   推导（以横向为例）：
    //     9 块瓦片贴在画布上，列号从左到右是 centerTileX-1, centerTileX, centerTileX+1
    //     第 0 列（最左）在画布上的起点是 x=0
    //     定位点距离"最左那块"的起点，是 (txf - (centerTileX - 1)) 块瓦片
    //     一块瓦片 256 像素，所以乘 256 就得到像素坐标
    //   因为 txf 的小数部分 ∈ [0,1)，这个结果必然落在 256~512 之间 ——
    //   也就是**永远在中间那块上**，不会跑到边上被裁掉。这正是取 3x3 而不是
    //   只拉 1 块的原因：只拉 1 块的话，定位点可能贴在瓦片的角落里
    markerX = (txf - (centerTileX - 1)) * GPS_TILE_SIZE;
    markerY = (tyf - (centerTileY - 1)) * GPS_TILE_SIZE;

    // 状态复位：计数器立成 9，等 9 张挨个回来减到 0
    for (int i = 0; i < 9; i++)
        tileArrived[i] = false;
    tilePending = GPS_GRID * GPS_GRID;      // 3 * 3 = 9
    tileOkCount = 0;

    // ★ 一次把 9 个请求全发出去（不是发一个等一个）
    //   row/col 是"在 3x3 里的第几行第几列"，和瓦片自己的编号是两回事，别混
    for (int row = 0; row < GPS_GRID; row++) {
        for (int col = 0; col < GPS_GRID; col++) {
            int tx  = centerTileX - 1 + col;    // 实际的瓦片列号
            int ty  = centerTileY - 1 + row;    // 实际的瓦片行号
            int idx = row * GPS_GRID + col;     // 0~8，在画布上的位置编号

            QNetworkRequest request(QUrl(tileUrl(tx, ty, GPS_ZOOM)));
            // 瓦片服务器会看 User-Agent，带上浏览器标识最稳
            request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0");
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);

            QNetworkReply *r = tileManager->get(request);

            // ★★ 关键技巧：9 个请求共用一个槽 tileFinished()，
            //   槽里怎么知道"这次回来的是第几块"？靠这个自定义属性。
            //   setProperty / property 是 QObject 自带的一对"贴标签"函数，
            //   能挂任意 QVariant 上去，用来在异步回调之间传上下文最方便
            r->setProperty("tileIndex", idx);

            connect(r, SIGNAL(finished()), this, SLOT(tileFinished()));
        }
    }

    // 开始 15 秒倒计时：到点还没凑齐就不再等，先显示已经拿到的部分
    mapTimeoutTimer->start(15000);
}

// ---------------------------------------------------------------------------
// ② 一张瓦片回来了（9 张全走这一个槽）
// ---------------------------------------------------------------------------
void GpsLocation::tileFinished()
{
    // ★ sender() = "发出这个信号的那个对象"，返回 QObject*，要自己转类型。
    //   qobject_cast 是 Qt 版的 dynamic_cast，转不了会返回 0（不崩）
    QNetworkReply *r = qobject_cast<QNetworkReply *>(sender());
    if (!r)
        return;

    int idx = r->property("tileIndex").toInt();     // 取回发请求时贴的编号

    if (r->error() == QNetworkReply::NoError) {
        QByteArray bytes = r->readAll();
        QImage img;

        // loadFromData：直接从**内存里**的一坨字节解码成图。
        //   注意和 QImage("xxx.png") 的区别 —— 那是从磁盘文件读，
        //   瓦片是网络流过来的，根本没有文件，所以必须用这个函数
        if (img.loadFromData(bytes)) {
            int row = idx / GPS_GRID;               // 反算出它是 3x3 里的第几行
            int col = idx % GPS_GRID;               // 和第几列

            QPainter p(&tileCanvas);
            p.drawImage(col * GPS_TILE_SIZE, row * GPS_TILE_SIZE, img);
            p.end();        // ⚠ 必须 end()，否则画布一直是"正在被画"的锁定状态

            if (!tileArrived[idx]) {                // 防重复计数
                tileArrived[idx] = true;
                tileOkCount++;                      // 统计成功了几张
            }
        }
        // 解码失败（比如服务器回了一页 HTML 错误页）就什么都不画，
        // 那块地方保持底色的深灰 —— 比画个花屏好
    }

    // 和 ipGeo 里一样：不能在槽里直接 delete 发信号的对象，要 deleteLater()
    r->deleteLater();

    // ★ 到齐了就拼图
    if (tilePending > 0)
        tilePending--;

    if (tilePending == 0) {
        mapTimeoutTimer->stop();        // 齐了，超时保护下班
        showMap();
    }
}

// ---------------------------------------------------------------------------
// 瓦片等太久了（15 秒）
//   ⚠ 这里**不去 abort 那 9 个请求**，理由：
//     它们各有各的 finished()，abort 之后照样会进 tileFinished 把 tilePending 减下去，
//     逻辑会变得绕。简单粗暴的办法是：直接把计数器清零、拿现在有的先拼一张出来。
//     晚到的瓦片还会照常画进画布，只是不再触发重绘 —— 代价是地图可能有几块是灰的，
//     但这比"转圈转到天荒地老"好得多。
// ---------------------------------------------------------------------------
void GpsLocation::mapTimeout()
{
    if (tilePending > 0) {
        tilePending = 0;
        showMap();
    }
}

// ---------------------------------------------------------------------------
// ③ 9 张到齐（或超时）—— 缩放 + 打定位点 + 贴到 QLabel
// ---------------------------------------------------------------------------
void GpsLocation::showMap()
{
    // 一张都没拉到（网络不通 / 服务器挂了）—— 地图区换成一句人话，
    // 右边的文字数据不受影响，照常显示
    if (tileOkCount == 0) {
        showMapPlaceholder("地图加载失败\n（检查板子网络）");
        return;
    }

    // ★ 顺序很重要：**先把瓦片缩放好，再往上画定位点**
    //   反过来（先画点再缩放）的话，定位点会跟着一起被拉小、被插值糊掉，
    //   十字线会变成一团模糊
    QPixmap shown = tileCanvas.scaled(GPS_VIEW, GPS_VIEW,
                                      Qt::KeepAspectRatio,        // 保持宽高比，不变形
                                      Qt::SmoothTransformation);  // 平滑缩放（720→460 用这个才不锯齿）

    // 缩放比例。画布 768 → 显示 460，所以 k ≈ 0.599
    //   用 shown.width() 而不是写死 GPS_VIEW，是因为万一以后改了 KeepAspectRatio
    //   的行为，这里自动跟着对
    double k = double(shown.width()) / GPS_CANVAS;

    drawMarker(shown, markerX * k, markerY * k);    // 定位点按同样比例换算到显示坐标系

    mapLabel->setPixmap(shown);
}

// ---------------------------------------------------------------------------
// 地图区的"兜底画面"：一张纯色底 + 一句居中提示文字
//   什么时候用：正在获取 / 拿不到位置 / 一张瓦片都没拉到
// ---------------------------------------------------------------------------
void GpsLocation::showMapPlaceholder(const QString &text)
{
    QPixmap blank(GPS_VIEW, GPS_VIEW);
    blank.fill(QColor(43, 43, 43));

    QPainter p(&blank);
    p.setPen(QColor(150, 150, 150));
    QFont f = p.font();
    f.setPixelSize(18);
    p.setFont(f);
    // drawText(矩形, 对齐方式, 文字)：在矩形范围内居中画字。
    // 文字里的 \n 会被它认成换行，所以能画多行
    p.drawText(blank.rect(), Qt::AlignCenter, text);
    p.end();

    mapLabel->setPixmap(blank);
}

// ---------------------------------------------------------------------------
// 画定位点（直接画在传进来的画布上，不另做控件）
//
// 【为什么不做成一个 QGraphicsItem / 自定义控件】
//   地图本身就是一个静态的 QPixmap，画完贴在 QLabel 上就不动了 ——
//   既然整张图都是"一次性画好的死图"，定位点跟着一起画进去最省事，
//   不用再开一个控件、不用处理父子层级和重绘顺序。
//   （反过来，如果定位点是会动的 —— 比如做个"位置漂移"动画 ——
//     那就该用 paintEvent 单独画，不然每动一下都要重拼整张图）
//
// 参数：pix 画布（就地修改）；x/y 定位点在**这张画布**上的像素坐标
// ---------------------------------------------------------------------------
void GpsLocation::drawMarker(QPixmap &pix, double x, double y)
{
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);   // 开抗锯齿，圆才不是锯齿边

    // ---- ① 外面两圈半透明光晕：视觉上表示"大概在这一片" ----
    //   IP 定位本来就只有城市级精度（误差几公里），画个大圈比画个精确的小点
    //   更诚实 —— 图形会告诉看的人"别指望这点是准的"
    //   QColor(r, g, b, a) 第四个参数是透明度：0 = 全透明，255 = 不透明
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(48, 140, 198, 60));
    p.drawEllipse(QPointF(x, y), 30, 30);
    p.setBrush(QColor(48, 140, 198, 90));
    p.drawEllipse(QPointF(x, y), 18, 18);

    // ---- ② 十字准星 ----
    //   故意在中间留出一段空白（10~32 像素），不画到圆心 ——
    //   这样中心那个圆点不会和十字线糊在一起
    QPen pen(QColor(255, 255, 255, 230));
    pen.setWidth(2);
    p.setPen(pen);
    p.drawLine(QPointF(x - 32, y), QPointF(x - 10, y));   // 左
    p.drawLine(QPointF(x + 10, y), QPointF(x + 32, y));   // 右
    p.drawLine(QPointF(x, y - 32), QPointF(x, y - 10));   // 上
    p.drawLine(QPointF(x, y + 10), QPointF(x, y + 32));   // 下

    // ---- ③ 中心实心点 ----
    p.setPen(QPen(Qt::white, 2));
    p.setBrush(QColor(48, 140, 198));
    p.drawEllipse(QPointF(x, y), 7, 7);

    p.end();
}

// ===========================================================================
//                        坐标换算（Web 墨卡托投影）
// ===========================================================================
//
// 【为什么不能直接用经纬度当像素坐标】
//   地球是圆的，地图是平的。要把球面摊平，必须选一种"投影"方式。
//   所有在线地图（高德、百度、Google、OSM）用的都是 **Web 墨卡托（EPSG:3857）**，
//   公式全世界统一。不自己算的话，你就没法知道"29.5689°N, 106.5577°E"
//   到底该落在哪块瓦片上。
//
// 【横向的推导（比较好懂）】
//   经度范围是 -180° ~ +180°，一共 360°。
//   z 级时，横向一共切成 2^z 块（z=0 是 1 块，z=1 是 2 块……每升一级翻倍）。
//   所以：瓦片列号 = (经度 + 180) / 360 * 2^z
//   代入重庆： (106.5577 + 180) / 360 * 4096 = 3260.39
//   → 第 3260 块，而且在这块里偏右 0.39 的位置
//
// 【纵向为什么不是简单的线性】
//   墨卡托投影有个特点：**纬度越高，地图被拉得越开**（所以格陵兰看着比非洲还大）。
//   为了让"每块瓦片是一个正方形"，纵向必须按这个拉伸反过来补偿，
//   于是就有了那个 log(tan+sec) 的公式。它不好从零推导，但可以这样理解它：
//   它是一个"把纬度从球面均匀映射到平面"的桥梁，输入 -85.05°~85.05°（两极附近发散，
//   所以地图上下都截到 ±85.05°，这也是为什么所有在线地图都到不了南北极）。
//
//   ⚠ 顺便说明一个精度问题：高德用的是 **GCJ-02（火星坐标）**，
//     而 ip-api 给的经纬度是 **WGS-84（GPS 原始坐标）**，两者差三五百米。
//     城市级显示（一张瓦片约 10 公里）下这点偏移肉眼看不出来，
//     所以本页**不做**坐标系转换。真要做到米级，得补一段 WGS-84 → GCJ-02 的算法。
// ---------------------------------------------------------------------------

// 经度 → 瓦片列号（带小数）
double GpsLocation::lonToTileX(double lon, int zoom)
{
    double n = double(1 << zoom);       // 1 << zoom = 2 的 zoom 次方（移位比 pow 快）
    return (lon + 180.0) / 360.0 * n;
}

// 纬度 → 瓦片行号（带小数）
double GpsLocation::latToTileY(double lat, int zoom)
{
    double rad = lat * GPS_PI / 180.0;  // 三角函数要弧度，先把度换成弧度
    double n   = double(1 << zoom);

    // 这一行就是墨卡托投影的纵向公式，逐段读：
    //   std::tan(rad) + 1.0 / std::cos(rad)   就是 tan(φ) + sec(φ)
    //   std::log(...) / GPS_PI                把它归一化到"半个地球"的尺度
    //   (1.0 - ...) / 2.0                     翻转并压缩到 0~1（因为纬度是往北增大的，
    //                                         而瓦片行号是往下增大的，方向相反）
    //   * n                                   最后缩放到 2^zoom 块的尺度
    return (1.0 - std::log(std::tan(rad) + 1.0 / std::cos(rad)) / GPS_PI) / 2.0 * n;
}

// 瓦片编号 → 完整 URL
//   实测（2026-10-16，从板子上直接 curl）：返回 200，一张 256x256 的 PNG 约 9~26 KB，1.2 秒
//   ⚠ 反过来，OpenStreetMap 的瓦片（tile.openstreetmap.org）从国内网络**拉不到**（超时），
//     所以这里用的是高德
QString GpsLocation::tileUrl(int x, int y, int zoom)
{
    // 高德有 01~04 四台瓦片服务器，内容完全一样（实测四台的 md5 相同）。
    // 分散着用是为了让浏览器/客户端能同时开多个连接，拉得更快。
    // 这里用 (x + y) % 4 做简单的轮换，保证相邻的瓦片走不同服务器
    int s = (x + y) % 4 + 1;

    // ⚠ 字符串里的 & 不能写成裸的（虽然在 C++ 里没问题，但养成习惯）——
    //   这里是 arg() 拼的，不涉及转义
    return QString("https://webrd0%1.is.autonavi.com/appmaptile"
                   "?lang=zh_cn&size=1&scale=1&style=8&x=%2&y=%3&z=%4")
            .arg(s).arg(x).arg(y).arg(zoom);
}
