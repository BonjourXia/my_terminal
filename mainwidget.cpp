#include "mainwidget.h"                     // 自己的头文件（成员变量声明都在里面）
#include <QListWidgetItem>                  // 列表项类型：addItem 前要 new 一个

// 导航栏每一项的"数据模板"：显示的文字 + 一个能造出对应页面的函数指针
struct WidgetItemData {
    QString title;                              // 这一行在导航栏里显示的文字
    typedef QWidget *(*CreateInstanceFunc)();   // 函数指针类型：无参数、返回 QWidget*
    CreateInstanceFunc createInstance;          // 每一项自带"怎么造出这个页面"的办法
};

// 构造函数：建出整个界面骨架
MainWidget::MainWidget(QWidget *parent) : QWidget(parent)
{
    widget = new QWidget(this);                 // 内容容器；this 作父对象 → 入对象树，随窗口自动销毁
    this->setGeometry(0, 0, 1024, 600);         // 主窗口位置和尺寸（板子屏幕分辨率）
    this->setObjectName("project");             // ← qss 里的 #project 靠它命中
    widget->resize(this->size());               // 容器跟主窗口一样大
    this->windows_init();                       // 建导航栏 + 页面堆栈
}

// 析构函数：什么都不用做
MainWidget::~MainWidget()
{
    // 只 delete 非 QObject 的东西；widget/listWidget/stackedWidget 由对象树自动回收
}

// 建界面：左边列表、右边页面堆栈，然后把两者连起来
void MainWidget::windows_init()
{
    hBoxLayout    = new QHBoxLayout();          // 横向布局；先当孤儿，setLayout 时才入树
    stackedWidget = new QStackedWidget();       // 右侧卡片盒：第 n 个 widget = 第 n 页
    listWidget    = new QListWidget(this);      // 左侧导航栏

    listWidget->setStyleSheet("background-color: black;");   // 导航栏底色涂黑

    // 全项目只有这个数组需要改：每完成一个页面，就往里加一行
    WidgetItemData items[] = {
        {"测试页A", []() -> QWidget * { return new QWidget; } },   // 空占位页；[] 不能捕获 → 才能当函数指针
        {"测试页B", []() -> QWidget * { return new QWidget; } },   // 同上
    };

    // 一趟循环同时喂两个容器：第 n 次迭代既加列表项、又加页面
    for (auto &itemData : items) {
        QListWidgetItem *item = new QListWidgetItem(itemData.title);   // 造一个列表项
        item->setTextColor(Qt::white);                                // 白字（5.15 会警告 deprecated，无视）
        item->setTextAlignment(Qt::AlignCenter);                      // 文字居中
        item->setSizeHint(QSize(200, 63));                            // 每一项 200 宽 × 63 高
        listWidget->addItem(item);                                    // 塞进导航栏

        if (itemData.createInstance)                                  // 有造页面的函数才加
            stackedWidget->addWidget(itemData.createInstance());      // 造一个页面塞进堆栈
    }

    listWidget->setMaximumWidth(200);           // 导航栏最宽 200px（剩下 824 给页面）
    hBoxLayout->addWidget(listWidget);          // 左边：导航栏
    hBoxLayout->addWidget(stackedWidget);       // 右边：页面堆栈
    widget->setLayout(hBoxLayout);              // 布局挂到容器上（这一句之后控件才听布局的）

    connect(listWidget, SIGNAL(currentRowChanged(int)),      // 列表"当前行变了"
            stackedWidget, SLOT(setCurrentIndex(int)));       // ★ 全部切换逻辑就在这一行
}