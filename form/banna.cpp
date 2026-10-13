#include "banna.h"



banna::banna(QWidget *parent):
QWidget(parent)
{
ui_init();
}

void banna::ui_init()
{
    this->resize(824, 600);                                  // 页面尺寸 = 1024−200（左边导航栏占掉 200）
    this->setStyleSheet("QWidget,QFrame{background-color:#1A1A1A;}");   // 深灰底，和第 0 级的黑导航栏一套

       // ---- 图片容器 ----
    widgetImage = new QWidget(this);                         // parent = this：它自己是页面的直接子控件
    widgetImage->setGeometry(0, -10, 824, 471);              // 往上顶 10px（作者故意的，让弧形顶部不被切）
    widgetImage->setStyleSheet("QLabel{border-radius:6px;}"); // 图片圆角


    // ---- 5 张图：注意 parent 是 widgetImage，不是 this ----
    // （parent 决定坐标系原点：下面的 30/180 是相对 widgetImage 的，不是相对页面的）
    labelFirst = new QLabel(widgetImage);                    // 第 1 张：最左、最小
    labelFirst->setGeometry(30, 180, 320, 200);              // x=30 y=180 宽320 高200

    labelSecond = new QLabel(widgetImage);                   // 第 2 张
    labelSecond->setGeometry(80, 150, 400, 250);             // 比第 1 张更大、更靠中间

    labelThird = new QLabel(widgetImage);                    // 第 3 张
    labelThird->setGeometry(160, 130, 480, 300);             // 正中间、最大（x 正好在 824 的中线上）

    labelFourth = new QLabel(widgetImage);                   // 第 4 张
    labelFourth->setGeometry(340, 150, 400, 250);            // 和第 2 张左右对称

    labelFifth = new QLabel(widgetImage);                    // 第 5 张
    labelFifth->setGeometry(480, 180, 320, 200);             // 和第 1 张左右对称

    // ---- 往这 5 个"空画框"里真正填上图片 ----
    // 上面 5 行只摆了框的位置和大小，框里还是空的 —— 没有这一步，屏幕上就是
    // 5 块"什么都没有"的区域（背景色透出来），看着像"没显示图片"。
    // 作者原版把这段单独写成 addAnimationPixmap()，因为做动画时要反复调用；
    // 这里先按最直接的写法展开，等做到轮播动画那一级再抽成函数。
    //
    // 用的办法：给 label 设样式表，和下面给圆点、箭头涂色是同一招
    //   setStyleSheet("border-image:url(:/images/banna_pic/banna/1.jpg);")
    //     函数作用：把一串 qss 规则装到这个 label 上
    //     参数就是那串规则，从里往外读：
    //       url(...)      括号里是图片路径，开头的 :/ 表示去 qrc 资源里找
    //                     （这 5 张 jpg 已被 res.qrc 打进可执行文件，
    //                       所以板子上不用另外放图片）
    //       border-image  把图片拉伸铺满整个 label —— 正好当"图片框"用
    //   路径从哪来：和 label 一一对应，第 1 个 label 配 1.jpg，依次往下
    // （也可以改用 label->setPixmap(QPixmap(...))，效果一样；
    //   跟原版保持一致用样式表，因为圆角、拉伸这些由它统一管）
    const char *imagePaths[5] = {
        ":/images/banna_pic/banna/1.jpg",    // → labelFirst（最左、最小）
        ":/images/banna_pic/banna/2.jpg",    // → labelSecond
        ":/images/banna_pic/banna/3.jpg",    // → labelThird（正中间、最大）
        ":/images/banna_pic/banna/4.jpg",    // → labelFourth
        ":/images/banna_pic/banna/5.jpg",    // → labelFifth（最右、最小）
    };
    QLabel *labels[5] = { labelFirst, labelSecond, labelThird, labelFourth, labelFifth };
    for (int i = 0; i < 5; i++) {
        // arg(...)：把路径填进 %1 的位置 → "border-image:url(:/…/1.jpg);"
        labels[i]->setStyleSheet(QString("border-image:url(%1);").arg(imagePaths[i]));
    }

    // ---- 重叠时的上下层关系：交给函数末尾的 updateZOrder() 统一摆 ----
    // 这 5 个框是互相压着的（比如第 3 张的右半边压着第 4 张的左半边）。
    // 默认规则是"谁后 new 谁在上面"，那第 5 张就会盖住中间那张，很难看。
    // 现在不写死"第 3 张在最上"了 —— 因为滑起来之后，谁在中间是不断变的，
    // 所以改由 updateZOrder() 按"谁里中间近谁在上面"的规则随时重算。

    // ---- 左右箭头（parent 是 this，不是 widgetImage）----
    pushButtonLeft = new QPushButton(this);                  // parent 写错成 widgetImage 会被容器裁掉
    pushButtonLeft->setGeometry(30, 260, 48, 48);            // 左边、竖直居中
    pushButtonLeft->setStyleSheet(                           // 两张图拼成一个按钮
        "background-color:transparent;"                      // 底色透明（只留箭头图案）
        "border-image:url(:/images/banna_pic/other/less_than.png);");   // :/ = 从 qrc 资源里取

    pushButtonRight = new QPushButton(this);                 // 右箭头
    pushButtonRight->setGeometry(750, 260, 48, 48);          // 右边、竖直居中
    pushButtonRight->setStyleSheet(
        "background-color:transparent;"                      // 同上
        "border-image:url(:/images/banna_pic/other/more_than.png);");

    // ---- 底部圆点条 ----
    frameDot = new QFrame(this);                             // 一个空容器，专门用来装 5 个圆点
    frameDot->setGeometry(0, 430, 824, 161);                 // 铺满底部（y 从 430 到 591）
    frameDot->setStyleSheet(                                 // 圆点样式写在这里，5 个点共用
        "QPushButton{border-image:url(:/images/banna_pic/other/normal_gray.png);}"           // 没选中：灰点
        "QPushButton:checked{border-image:url(:/images/banna_pic/other/hover_blue.png);}");  // 选中：蓝点

    // 圆点条的内部布局：左弹簧 + 5 个圆点 + 右弹簧 = 整体居中
    // （原码在 .ui 里放了两个 sizeHint 287 的水平 spacer）
    QHBoxLayout *dotLayout = new QHBoxLayout(frameDot);      // 布局直接挂在 frameDot 上
    dotLayout->setContentsMargins(0, 25, 0, 35);             // 上留 25、下留 35（把点往上顶一点）
    dotLayout->addStretch(287);                              // 左弹簧：撑开 287px

    QPushButton *dots[5];                                    // 临时数组，先攒着再分给 5 个成员变量
    for (int i = 0; i < 5; i++) {                            // 5 个圆点长得一样，循环建最省事
        dots[i] = new QPushButton(frameDot);                 // parent 是 frameDot → 布局才收得住它
        dots[i]->setFixedSize(16, 16);                       // 固定 16×16（对应 .ui 里的 min=max=16）
        dots[i]->setCheckable(true);                         // 能"选中"→ 靠 :checked 换成蓝色图
        dotLayout->addWidget(dots[i]);                       // 加进布局
    }
    pushButtonFirst  = dots[0];                              // 把这 5 个指针挂到成员变量上
    pushButtonSecond = dots[1];                              // （后面槽函数要用它们）
    pushButtonThird  = dots[2];
    pushButtonFourth = dots[3];
    pushButtonFifth  = dots[4];

    // 再把 5 张图、5 个圆点各抄一份到"数组"里：轮播时要挨个遍历它们，
    // 用 labelArray[i] 这种写法比写 5 遍 if 清爽得多
    QLabel *labelList[5] = { labelFirst, labelSecond, labelThird, labelFourth, labelFifth };
    QPushButton *dotList[5] = { pushButtonFirst, pushButtonSecond, pushButtonThird, pushButtonFourth, pushButtonFifth };

    dotGroup = new QButtonGroup(this);                       // 圆点的"互斥组"：组里同时只能有一个被选中
    for (int i = 0; i < 5; i++) {
        labelArray[i] = labelList[i];                        // 数组下标 i 就等于"第几张图"
        dotArray[i]   = dotList[i];
        slotRect[i]   = labelArray[i]->geometry();           // ★ 量下它此刻的坐标，记成"槽位 i"的坐标
        slotOf[i]     = i;                                   // ★ 此刻第 i 张图就待在槽位 i
        dotGroup->addButton(dotArray[i]);                    // 把圆点收进互斥组
    }

    pushButtonThird->setChecked(true);                       // .ui 里只有第 3 个是 checked：开页面就亮
    dotLayout->addStretch(287);                              // 右弹簧：和左边对称

    // ============ 以下都是"轮播"要用的东西（第 1.2 级新增）============

    // ---- ① 给每张图配一条动画 ----
    // new QPropertyAnimation(作用对象, 要动的属性名)：
    //     ① 作用对象 —— labelArray[i]，第 i 张图。来源：上面那个循环刚填好的数组
    //     ② "geometry" —— Qt 把"位置(x,y) + 大小(宽,高)"打包成一个叫 geometry 的属性
    //        动画去改这个属性，就等于同时改坐标和宽高 —— 所以图会"一边滑一边变大/变小"
    //        （因为槽位坐标本来就有大有小：中间的槽位 480×300，两头的只有 320×200）
    //     这里先不给它父对象：等下面 addAnimation() 把它交给动画组，由动画组负责销毁
    animationGroup = new QParallelAnimationGroup(this);      // 父对象 this：它随页面一起销毁
    for (int i = 0; i < 5; i++) {
        animArray[i] = new QPropertyAnimation(labelArray[i], "geometry");
        animArray[i]->setDuration(400);                      // 一次滑完用 400 毫秒
        animArray[i]->setEasingCurve(QEasingCurve::InOutQuad);// 缓动曲线：起手慢、中间快、收尾慢 → 看着"平滑"
        animationGroup->addAnimation(animArray[i]);          // 交给动画组，5 条一起start
    }

    // ---- ② 两个箭头的点击 ----
    // connect(谁, 发什么信号, 谁接, 接到后干什么)：
    //     ① pushButtonLeft / pushButtonRight —— 上面 new 出来的两个箭头按钮
    //     ② clicked() —— QPushButton 自带的信号：被点一下发一次（不带参数）
    //     ③ this —— 让本页面自己接
    //     ④ SLOT(on_..._clicked()) —— 收到后去执行下面定义的槽函数
    connect(pushButtonLeft,  SIGNAL(clicked()), this, SLOT(on_pushButtonLeft_clicked()));
    connect(pushButtonRight, SIGNAL(clicked()), this, SLOT(on_pushButtonRight_clicked()));

    // ---- ③ 自动轮播的节拍器 ----
    // new QTimer(this)：参数 this 是父对象（进对象树，随页面销毁）
    changeImageTimer = new QTimer(this);
    connect(changeImageTimer, SIGNAL(timeout()), this, SLOT(slot_changeImageTimer()));
    changeImageTimer->setInterval(3000);                     // 参数是毫秒：3000 = 每 3 秒响一次
    changeImageTimer->start();                               // 开始计时（少这句它永远不响）

    // ---- ④ 把初始的上下层摆好 ----
    updateZOrder();
}

// ==========================================================================
// 开一轮滑动：让 5 张图一起挪到隔壁槽位，边挪边缩放
//   参数 isNext：true  = "下一张"（点右箭头 / 自动轮播）→ 整排图往左挪一格，右边补进来一张
//                false = "上一张"（点左箭头）        → 整排图往右挪一格，左边补进来一张
//   参数从哪来：两个箭头槽里写死 true/false，节拍器槽里写死 true（轮播就是"一直点右箭头"）
// ==========================================================================
void banna::startSlide(bool isNext)
{
    // 上一轮还没滑完就别再开新的：连点箭头、或者正好撞上定时器，
    // 都会让下面"起点/终点"算错，图就滑乱了（这行是防抖，不是可有可无）
    if (animationGroup->state() == QParallelAnimationGroup::Running)
        return;

    for (int i = 0; i < 5; i++) {
        // ① 先算这张图这次该去哪个槽位
        //    +4 再 %5 就是"槽位号减 1"，写 +4 是为了不出现负数：
        //    例如槽位 0 往左挪 → (0+4)%5 = 4，正好绕到最右边的槽位（5 个槽位是首尾相接的一圈）
        slotOf[i] = isNext ? (slotOf[i] + 4) % 5 : (slotOf[i] + 1) % 5;

        // ② 告诉它的动画：从哪动到哪
        //    setStartValue(QRect) —— 起点：直接问图自己"你现在在哪"，labelArray[i]->geometry()
        //    setEndValue(QRect)   —— 终点：slotRect[新槽位]，就是初始化时量好的那 5 份坐标之一
        //    终点里连宽高都有，所以动画过程中图会同时平移 + 缩放
        animArray[i]->setStartValue(labelArray[i]->geometry());
        animArray[i]->setEndValue(slotRect[slotOf[i]]);
    }

    animationGroup->start();          // 5 条动画同时开跑 → 整排图一起动

    updateZOrder();                   // 立刻按"新位置"重排上下层（滑的过程中不会互相胡乱盖）

    // ③ 更新底部圆点：让"此刻待在正中间槽位(2)的那张图"对应的点变蓝，其余变灰
    //    setChecked(条件) —— 参数是布尔值：true = 选中（互斥组会自动把别的点弹起来）
    for (int i = 0; i < 5; i++)
        dotArray[i]->setChecked(slotOf[i] == 2);
}

// ==========================================================================
// 重排 5 张图的上下层：越靠中间的越浮在上面（没参数）
//   原理：raise() 是"把控件提到最上层"，所以谁最后被 raise，谁就在最上面。
//   于是按"离中间槽位(2) 由远到近"的顺序依次 raise：
//   最远的先 raise（它垫在最底下），最近的最后 raise（它浮到最上）。
// ==========================================================================
void banna::updateZOrder()
{
    int order[5] = {0, 1, 2, 3, 4};                       // order[] 里装的是"第几张图"

    // 两重循环排序：i 每往前走一格，就保证 order[i] 是"剩下这些里离中间最远的那个"
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            // qAbs(x)：取绝对值。这里比的是"谁离槽位 2 更远"
            if (qAbs(slotOf[order[i]] - 2) < qAbs(slotOf[order[j]] - 2)) {
                int t = order[i];                         // 远的换到前面来
                order[i] = order[j];
                order[j] = t;
            }
        }
    }

    for (int i = 0; i < 5; i++)
        labelArray[order[i]]->raise();                    // 从最远的开始一路 raise 上去
}

// ==========================================================================
// 节拍器到点（每 3 秒一次）：自动轮播
//   干的事就一件：当成"有人点了一下右箭头"
// ==========================================================================
void banna::slot_changeImageTimer()
{
    startSlide(true);
}

// 点左箭头：整排图往右挪一格（看更前面的一张）
void banna::on_pushButtonLeft_clicked()
{
    startSlide(false);
}

// 点右箭头：整排图往左挪一格（看更后面的一张）
void banna::on_pushButtonRight_clicked()
{
    startSlide(true);
}