#ifndef MAINWIDGET_H              // 头文件守卫：防止同一个头文件被 include 两次
#define MAINWIDGET_H              // 与上面配对

#include <QWidget>                // 基类 QWidget
#include <QStackedWidget>         // 右侧"卡片盒"：一个索引 = 一个页面
#include <QHBoxLayout>            // 左列表 | 右堆栈 的横向布局
#include <QListWidget>            // 左侧导航栏

class MainWidget : public QWidget
{
    Q_OBJECT                      // 不能省：moc 靠它生成元对象代码，信号槽才成立

public:
    MainWidget(QWidget *parent = nullptr);   // 构造函数；parent 默认空 = 顶层窗口
    ~MainWidget();                           // 析构函数

private:
    QWidget        *widget;          // 内容容器（导航栏和页面都挂在它上面）
    QHBoxLayout    *hBoxLayout;      // 横向布局：左边列表、右边堆栈
    QListWidget    *listWidget;      // 左侧导航
    QStackedWidget *stackedWidget;   // 右侧卡片盒

    void windows_init();             // 建界面的函数，构造函数里调它
};
#endif