// ============================================================================
// hardware/ipgeo.cpp —— 网络定位零件（IP 反查城市）
//
// 【一句话原理】把自己的公网出口 IP 报给一个公开的定位服务，
//   它查自己的 IP 归属地数据库，回一段 JSON，我们从里面抠出城市和经纬度。
//
// 【为什么用 ip-api.com】
//   ① 匿名免费，**不用注册 key**（很多定位 API 都要你先去申请密钥）
//   ② 加 ?lang=zh-CN 之后，国家/省市字段直接回中文，省得自己做翻译表
//   ③ 实测从这块板子上能通（HTTP，不需要 HTTPS，省掉证书那一摊事）
//   限制：免费版每分钟最多 45 次 —— 我们 10 分钟才问一次，安全得很。
//
// 【★ 整个文件的核心是"异步"两个字】
//   见 replyFinished() 上面的说明。一句话：请求发出去就撒手，回来了 Qt 会叫我们。
// ============================================================================

#include "ipgeo.h"          // 自己的头文件
#include <QJsonDocument>    // ★ 把一坨字节"解析"成 JSON 文档
#include <QJsonObject>      // ★ JSON 里的对象（就是一对对 "键": 值）
#include <QJsonParseError>  // ★ 解析失败时的错误详情
#include <QJsonValue>       // JSON 里一个值（可能是字符串/数字/布尔…）
#include <QTime>            // 取"现在几点"给 updateTime 用
#include <QDebug>           // qDebug() 打日志

// ---------------------------------------------------------------------------
// 请求地址 —— 整个文件里唯一一条"对外说话"的线路
//   拆开看这个 URL：
//     http://ip-api.com/json/   服务地址，/json/ 表示"请用 JSON 格式回我"
//     ?lang=zh-CN               查询参数：要中文结果（country=中国 而不是 China）
//   用 #define 顶出来是惯例：地址写一处，改的时候只改这里。
// ---------------------------------------------------------------------------
#define IPGEO_URL   "http://ip-api.com/json/?lang=zh-CN"

// 自动刷新间隔：10 分钟。IP 归属地这种东西几乎不会变，
// 问太勤既浪费流量又可能触发对方的频率限制。
#define IPGEO_REFRESH_MS   (10 * 60 * 1000)

// 单次请求的超时时间：10 秒。断网时 TCP 自己要等两分钟才认输，
// 不自己掐的话页面会一直停在"正在获取…"。
#define IPGEO_TIMEOUT_MS   10000

// ---------------------------------------------------------------------------
// 构造函数：只做"准备工作"，不发请求
//   和 ap3216c 的构造函数是一个套路（造定时器、接信号、初始化成员），
//   区别是 ap3216c 在这里就打开 I2C 设备了，而这里**故意不发网络请求** ——
//   因为 mainwidget.cpp 里 9 个页面是开机时全部构造好的，
//   构造函数里发请求 = 每次开程序都会偷偷发一次网络包（哪怕你根本没点进这一页）。
//   真正"什么时候发"交给页面决定，见 form/gpslocation.cpp 的 showEvent()。
// ---------------------------------------------------------------------------
IpGeo::IpGeo(QObject *parent) : QObject(parent)
{
    // 网络总管。★ 这次老老实实写了 this 当爹 —— 对象树会自动回收它，
    // 不像 ap3216c.cpp 里那个漏掉的 timer（那处是原码的毛病，这里不学）
    manager = new QNetworkAccessManager(this);

    // 自动刷新定时器（默认**不启动**，等页面调 setCapture(true) 才跑）
    timer = new QTimer(this);
    timer->setInterval(IPGEO_REFRESH_MS);
    connect(timer, SIGNAL(timeout()), this, SLOT(timer_timeout()));

    // ⌛ 超时保护定时器。setSingleShot(true) = "只响一次就停"，
    // 否则它会每 10 秒响一次，把还在路上、其实没问题的请求也掐了
    timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(IPGEO_TIMEOUT_MS);
    connect(timeoutTimer, SIGNAL(timeout()), this, SLOT(replyTimeout()));

    url = QUrl(IPGEO_URL);

    // 还没开始，所以：没有请求在路上、没数据、没错误
    reply = nullptr;
    busy = false;
    latitude = 0.0;
    longitude = 0.0;
    errMsg.clear();
    updateTime.clear();
}

// ---------------------------------------------------------------------------
// 析构函数：什么都不用做
//   manager / timer / timeoutTimer 都挂了对象树（构造时传了 this），
//   本对象一死，Qt 会替我们把它们一起回收。这就是"进对象树"的好处。
//   reply 是网络总管生出来的，请求结束时会 deleteLater()，也不归我们管。
// ---------------------------------------------------------------------------
IpGeo::~IpGeo()
{
}

// ---------------------------------------------------------------------------
// 开 / 关自动刷新（对齐 ap3216c::setCapture 的写法）
//   参数 str：true = 开始每 10 分钟自动问一次；false = 停下来
//   ★ timer->start() 不传参数时，用的是构造函数里 setInterval 设的 10 分钟；
//     传参数（start(5000)）则是"这次用 5 秒"，会覆盖掉原来的设置。
// ---------------------------------------------------------------------------
void IpGeo::setCapture(bool str)
{
    if (str)
        timer->start();
    else
        timer->stop();
}

// ---------------------------------------------------------------------------
// ★★ 发一次请求 —— 注意它"发完就返回"，不等结果 ★★
//   谁调它：页面构造函数 / 刷新按钮 / 自动刷新定时器 / 超时后备
// ---------------------------------------------------------------------------
void IpGeo::requestData()
{
    // 已经有一个请求在路上了，就别再发 —— 防止手快连点"刷新"发出去一堆。
    // 这一条也顺便挡住了超时重试和定时器撞车的情况。
    if (busy)
        return;

    // QNetworkRequest 是"信封"：把地址装进去，再补两样东西
    QNetworkRequest request(url);

    // ① 报上名号。有些服务器（尤其国内的一些服务）会拒绝没有 User-Agent 的请求，
    //    带上一个就成了"我是正经程序"，不写也能通，但写了更保险
    request.setHeader(QNetworkRequest::UserAgentHeader, "my_terminal/1.0");

    // ② 重定向策略。服务器哪天换了域名、回了 301/302，Qt 默认**不跟**，
    //    表现就是"明明能通却报错"。NoLessSafeRedirectPolicy =
    //    "只要不是降级到不安全协议，就自动跟着跳"
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    // ★ get() 立即返回一个 reply 对象 —— 此刻数据还没到！
    //   它的作用是"将来结果的落脚点"，一会儿有数据了会往它里面灌
    reply = manager->get(request);

    // ★ 这根线是整个异步机制的枢纽：
    //   reply 完成（成功或失败都算）→ 回头调本对象的 replyFinished()
    //   信号 finished() 由 QNetworkReply 在网络线程收完数据后发出，
    //   因为是同一个线程，槽会在事件循环里排队执行（所以不会打断当前函数）
    connect(reply, SIGNAL(finished()), this, SLOT(replyFinished()));

    busy = true;                  // 挂上"正在路上"的牌子
    timeoutTimer->start();        // 开始倒计时，10 秒不回话就掐
}

// ---------------------------------------------------------------------------
// 自动刷新定时器到点（每 10 分钟一次）
// ---------------------------------------------------------------------------
void IpGeo::timer_timeout()
{
    requestData();                // 定时器只负责"到点喊一声"，真干活的是 requestData
}

// ---------------------------------------------------------------------------
// ★ 青果成熟了：请求结束（成功或失败都会进来）
//   谁调它：上面 connect 的那根线。我们不主动调它。
//   这里要做四件事：停表 → 收摊 → 分情况处理 → 通知页面
// ---------------------------------------------------------------------------
void IpGeo::replyFinished()
{
    timeoutTimer->stop();         // ① 请求有结果了，超时保护下班

    if (!reply)                    // 防御：万一是超时掐断后残留的一次调用
        return;

    busy = false;                 // ② 摘下"正在路上"的牌子，下次可以再发了

    // ③ 分两种情况：出错 / 成功
    if (reply->error() != QNetworkReply::NoError) {
        // ---- 出错了。error() 是个枚举，见下面 reply->errorString() 的翻译 ----
        // 把常见的几种翻译成中文，顺便给出排查方向
        QString reason;
        switch (reply->error()) {
        case QNetworkReply::HostNotFoundError:
            // 域名都解析不了 —— 板子上最常见的状况就是"WiFi 根本没连"
            reason = "域名解析失败：板子可能没联网（WiFi 需要手动连，见上板流程）";
            break;
        case QNetworkReply::TimeoutError:
        case QNetworkReply::OperationCanceledError:
            reason = "请求超时：网络太慢或者对方没响应";
            break;
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::UnknownNetworkError:
            reason = "网络不通：检查板子的网线和 WiFi";
            break;
        case QNetworkReply::ConnectionRefusedError:
            reason = "对方拒绝连接：服务地址可能变了";
            break;
        default:
            // 剩下的直接显示 Qt 给的英文原文（总比什么都不显示强）
            reason = "请求失败：" + reply->errorString();
            break;
        }
        fail(reason);
    } else {
        // ---- 成功了。readAll() 把响应体（服务器回的内容）全读出来 ----
        // ★ 注意：readAll() 只能读一次，读完就空了。所以要么立刻解析，
        //   要么先存进一个 QByteArray。这里是立刻交给 parseJson 处理。
        parseJson(reply->readAll());
    }

    // ④ 收摊：deleteLater() 不是"立刻删"，而是"等回到事件循环、现在这段代码跑完了再删"。
    //    为什么不能直接 delete reply：我们现在还在它发出的 finished() 信号的处理函数里，
    //    信号是它发的、我们自己也是它连着的对象，直接删掉等于把脚下的梯子抽了（会崩）。
    reply->deleteLater();
    reply = nullptr;

    // ⑤ 通知页面：不管成功还是失败都发信号。
    //    页面靠 errorData() 是不是空字符串来判断该显示数据还是显示错误提示
    emit ipGeoDataChanged();
}

// ---------------------------------------------------------------------------
// 超时保护：10 秒还没回话，主动把请求掐了
//   abort() 会立刻让回复结束，并**导致 finished() 信号发出来**
//   —— 所以接下来会正常走进 replyFinished()，在那里读到 OperationCanceledError
// ---------------------------------------------------------------------------
void IpGeo::replyTimeout()
{
    if (reply && reply->isRunning()) {
        qDebug() << "IpGeo: 请求超时，主动掐断";
        reply->abort();           // 掐断 → 触发 finished() → 进 replyFinished()
    }
}

// ---------------------------------------------------------------------------
// 把服务器回的 JSON 拆开，字段填进成员变量
//   参数 bytes：响应体的原始字节
//
//   回来的 JSON 长这样（实测）：
//   {
//     "status": "success",            是否成功（失败时是 "fail"）
//     "country": "中国",
//     "regionName": "重庆市",          省/直辖市
//     "city": "重庆市",
//     "lat": 29.5689,                 纬度
//     "lon": 106.5577,                经度
//     "isp": "CNC Group CHINA169 ...",运营商
//     "timezone": "Asia/Shanghai",
//     "query": "123.147.252.93"       查到的公网 IP
//   }
// ---------------------------------------------------------------------------
void IpGeo::parseJson(const QByteArray &bytes)
{
    // ① 解析。第二个参数是"错误出口"：解析失败的原因会写进这里
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);

    // ★ 一定要判 perr —— 服务器出故障时可能回一页 HTML 错误页，
    //   那不是 JSON，不判就会拿到一个空文档，后面全都读到空值，很难查
    if (perr.error != QJsonParseError::NoError) {
        fail("返回内容不是合法 JSON：" + perr.errorString());
        return;
    }

    // ② JSON 的顶层可以是对象（{}）也可以是数组（[]），我们要的是对象
    if (!doc.isObject()) {
        fail("返回内容不是 JSON 对象");
        return;
    }

    QJsonObject obj = doc.object();

    // ③ 先看业务层成功没。注意：HTTP 200 不代表业务成功 ——
    //    这个服务的约定是"用 status 字段说明业务结果"，得单独判一次
    if (obj.value("status").toString() != "success") {
        QString msg = obj.value("message").toString();
        fail(msg.isEmpty() ? "定位服务返回了失败状态" : ("定位服务返回失败：" + msg));
        return;
    }

    // ④ 逐个字段取值
    //    value("键") 拿到一个 QJsonValue，再用 toString()/toDouble() 转成要的类型。
    //    键写错不会崩，只会拿到空字符串 / 0 —— 所以名字必须和上面 JSON 里一模一样
    country   = obj.value("country").toString();
    region    = obj.value("regionName").toString();
    city      = obj.value("city").toString();
    isp       = obj.value("isp").toString();
    timezone  = obj.value("timezone").toString();
    ipAddr    = obj.value("query").toString();
    latitude  = obj.value("lat").toDouble();
    longitude = obj.value("lon").toDouble();

    // ⑤ 拿到 IP 但查不到城市 —— 这种情况会发生在用了一些匿名代理/机房出口的时候，
    //    数值上经纬度是 0，直接拿去算地图瓦片会得到一张南极附近的地图，所以挡一下
    if (city.isEmpty() && region.isEmpty()) {
        fail("定位服务没给出城市信息（这个 IP 可能查不到归属地）");
        return;
    }

    // ⑥ 一切正常：把错误信息清空。（★ 这个"清空"很关键 ——
    //    页面就是靠 errMsg 是不是空来判断该显示数据还是显示错误的）
    errMsg.clear();
    updateTime = QTime::currentTime().toString("HH:mm:ss");

    qDebug() << "IpGeo: 定位成功" << country << region << city
             << latitude << longitude << ipAddr;
}

// ---------------------------------------------------------------------------
// 统一处理"这次没拿到数据"
//   只填 errMsg，**不在这里 emit** —— 发信号统一放在 replyFinished() 末尾。
//   为什么：一次请求只该通知页面一次。如果这里也发，超时那条路上就会发两次，
//   页面白刷一遍，以后加动画之类的就会看到明显抖动。
//
//   ★ 注意这里**不清空旧的定位数据**：宁可显示"上次的位置 + 一行错误提示"，
//     也好过一下子全变成空白 —— 这也是真实导航软件的做法。
// ---------------------------------------------------------------------------
void IpGeo::fail(const QString &reason)
{
    errMsg = reason;
    qDebug() << "IpGeo:" << reason;
}

// ===========================================================================
// 下面全是 getter：把私有成员读出来给外面用，一行一个，没什么好讲的
//   Q_PROPERTY 里写的 READ countryData 指的就是这些函数
// ===========================================================================

QString IpGeo::countryData()    { return country; }
QString IpGeo::regionData()     { return region; }
QString IpGeo::cityData()       { return city; }
QString IpGeo::ispData()        { return isp; }
QString IpGeo::timezoneData()   { return timezone; }
QString IpGeo::ipData()         { return ipAddr; }
QString IpGeo::updateTimeData() { return updateTime; }
QString IpGeo::errorData()      { return errMsg; }   // 空字符串 = 一切正常

// 经纬度有两个版本：
//   带 Data 的返回字符串，直接贴 QLabel（保留 4 位小数，1 位小数 ≈ 11 米，够城市级用了）
//   带 Value 的返回原始 double，给地图算瓦片用
QString IpGeo::latitudeData()   { return QString::number(latitude,  'f', 4); }
QString IpGeo::longitudeData()  { return QString::number(longitude, 'f', 4); }
double  IpGeo::latitudeValue()  { return latitude; }
double  IpGeo::longitudeValue() { return longitude; }
