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

    pushButtonThird->setChecked(true);                       // .ui 里只有第 3 个是 checked：开页面就亮
    dotLayout->addStretch(287);                              // 右弹簧：和左边对称

}