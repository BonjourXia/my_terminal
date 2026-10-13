#ifndef BANNA_H
#define BANNA_H

#include <QWidget>                  // 基类
#include <QLabel>                   // 5 张图 = 5 个 QLabel
#include <QPushButton>              // 左右箭头 + 底部 5 个圆点
#include <QFrame>                   // 底部的圆点条 frameDot
#include <QButtonGroup>             // 5 个圆点互斥高亮（同一时刻只有一个 checked）
#include <QTimer>                   // 3 秒自动轮播靠它
#include <QPropertyAnimation>       // 单个 label 的"滑动"动画
#include <QParallelAnimationGroup>  // 把 5 条动画捆成一组，一起 start
#include <QHBoxLayout>              // 圆点条内部"居中"用的布局

class banna :public QWidget
{
    Q_OBJECT

public:
    explicit banna(QWidget *parent = nullptr); // explicit 防隐式转换；parent 默认空 = 顶层窗口

private:
    // ---- 界面：原来在 banna.ui 里，现在全是成员变量（见 0.5）----
    QWidget     *widgetImage;          // 装 5 张图的容器（parent 是 this）
    QLabel      *labelFirst;           // 第 1 张图：最左、最小
    QLabel      *labelSecond;          // 第 2 张
    QLabel      *labelThird;           // 第 3 张：正中间、最大
    QLabel      *labelFourth;          // 第 4 张
    QLabel      *labelFifth;           // 第 5 张：最右、最小
    QPushButton *pushButtonLeft;       // 左箭头
    QPushButton *pushButtonRight;      // 右箭头
    QFrame      *frameDot;             // 底部圆点条
    QPushButton *pushButtonFirst;      // 下面 5 个圆点：点它跳到对应的图
    QPushButton *pushButtonSecond;     //     （这 5 个的 parent 都是 frameDot）
    QPushButton *pushButtonThird;      //     第 3 个：页面一打开默认就是亮的
    QPushButton *pushButtonFourth;
    QPushButton *pushButtonFifth;

    void ui_init();                    // 建控件、摆坐标（= 原来 ui->setupUi(this) 干的活）
};

#endif


















