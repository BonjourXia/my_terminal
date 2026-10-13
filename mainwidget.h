#ifndef MAINWIDGET_H              // 头文件守卫：防止同一个头文件被 include 两次（名字随便起，全局唯一即可）
#define MAINWIDGET_H              // 与上面配对：第一次进来会定义这个宏，第二次再进来就被 ifndef 挡住

#include <QWidget>                // 基类 QWidget：所有可见控件的祖宗
#include <QStackedWidget>         // 右侧"卡片盒"：一个索引 = 一个页面，同一时刻只显示其中一张
#include <QHBoxLayout>            // 左列表 | 右堆栈 的横向布局管理器
#include <QListWidget>            // 左侧导航栏（一行一行的条目）

// MainWidget：主窗口类，继承 QWidget（"是一种 QWidget"，所以它天生就能显示、能收事件）
class MainWidget : public QWidget
{
    Q_OBJECT                      // 不能省：moc 工具靠这个宏生成"元对象代码"，信号槽才成立
                                  //（它会被 moc_*.cpp 那步处理，编译前自动生成代码）

public:
    // 构造函数
    //   参数 parent：父窗口指针，默认值 nullptr
    //     —— 填了某个窗口，我就是人家的子窗口，跟着人家一起显示/销毁
    //     —— 不填（nullptr），我就"没有爹"，自己就是一个顶层窗口
    //   默认值写在头文件里，所以 main.cpp 里 MainWidget w; 这一句就够了
    MainWidget(QWidget *parent = nullptr);
    ~MainWidget();                           // 析构函数

private:
    QWidget        *widget;          // 内容容器（导航栏和页面都挂在它上面）
    QHBoxLayout    *hBoxLayout;      // 横向布局：左边列表、右边堆栈
    QListWidget    *listWidget;      // 左侧导航
    QStackedWidget *stackedWidget;   // 右侧卡片盒

    void windows_init();             // 建界面的函数，构造函数里调它
};
#endif
