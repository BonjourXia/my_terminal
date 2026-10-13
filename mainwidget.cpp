#include "mainwidget.h"                     // 自己的头文件（成员变量声明都在里面）
#include <QListWidgetItem>                  // 列表项类型：addItem 前要 new 一个

// ==========================================================================
// 导航栏每一项的"数据模板"：相当于一行两列的小表格
//   title          —— 这一行在导航栏里显示什么文字
//   createInstance —— 一张"配方"：调用它就能造出这个页面（存的其实是函数的地址）
// ==========================================================================
struct WidgetItemData {
    QString title;                              // 这一行在导航栏里显示的文字
    typedef QWidget *(*CreateInstanceFunc)();   // 函数指针类型：无参数、返回 QWidget*
    CreateInstanceFunc createInstance;          // 每一项自带"怎么造出这个页面"的办法
};
// 上面那行 typedef 是 C/C++ 的"函数指针类型"写法，拆开读：
//   QWidget *  (*CreateInstanceFunc)  ()
//   └───┬──┘   └─────────┬────────┘  └┬┘
//    返回类型         名字(这里是新类型名)  参数列表，空 = 不用传参
//   中间的星号和括号贴在一起看：说明存的是"函数的地址"（指针），不是函数本身
// 合起来一句话：CreateInstanceFunc 这种类型，装的是"能造出 QWidget* 的函数"的地址
// （C++ 里函数不能像变量那样直接塞进结构体，只能塞它的地址，用的时候再按地址调它）


// 构造函数：建出整个界面骨架
//   参数 parent 从哪来：main.cpp 里写的是 MainWidget w; —— 没填参数，
//   所以 parent 用的是头文件里写的默认值 nullptr（我没有父窗口，自己就是顶层窗口）
//   冒号后面那个 " : QWidget(parent)" 是 C++ 的初始化列表：
//   在进入函数体之前，先把参数原样交给基类 QWidget 的构造函数去处理
MainWidget::MainWidget(QWidget *parent) : QWidget(parent)
{
    // new QWidget(this)：造一个"内容容器"
    //   参数 this 是父对象：把自己交出去当爹 —— 这叫"进入 Qt 对象树"，
    //   爹被销毁时，树上的所有孩子会被自动 delete（所以析构函数里不用管它）
    widget = new QWidget(this);

    // setGeometry(x, y, w, h)：一次性设定这个窗口"摆在屏幕哪个位置、多大"
    //   四个参数都是像素，含义从前往后依次是：
    //     ① x = 0      → 左上角距屏幕左边的距离
    //     ② y = 0      → 左上角距屏幕上边的距离（x、y 都是 0 = 严丝合缝贴在屏幕左上角）
    //     ③ w = 1024   → 宽
    //     ④ h = 600    → 高
    //   为什么是 1024×600：这块板子的屏幕分辨率就是这个数（写死的，因为程序只跑这一块板）
    this->setGeometry(0, 0, 1024, 600);

    // setObjectName("project")：给这个窗口起一个"内部名字"
    //   参数就是名字，随便起，但必须和 qss 里写的一致
    //   下一步读进来的 style/mainstyle.qss 里写的选择器 QWidget#project，
    //   就是靠这个名字（#后面那串）找上门的
    this->setObjectName("project");

    // resize(this->size())：把内容容器调成和主窗口一样大
    //   参数 this->size() 是个 QSize（宽×高 的打包），来源就在上面——刚设好的 1024×600
    //   这里不重新写数字，直接问主窗口"你多大我就多大"，以后改分辨率只用改一处
    widget->resize(this->size());

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
    // new QHBoxLayout()：横向布局管理器
    //   它自己不可见，只负责"把交给它的控件从左到右排好"
    //   这里故意不传父对象：它现在还是"孤儿"，等最后 setLayout 时才正式上岗（见函数末尾）
    hBoxLayout = new QHBoxLayout();

    // new QStackedWidget()：一摞"卡片"（页面），同一时刻只显示其中一张
    //   切页 = 换当前索引 n，这就是右边区域会跟着变的原因
    stackedWidget = new QStackedWidget();

    // new QListWidget(this)：左侧导航栏
    //   参数 this 是父对象 → 进对象树，随主窗口销毁；
    //   它是"列表控件"，专门管理一行一行的条目（增、删、选中都在它内部完成）
    listWidget = new QListWidget(this);

    listWidget->setStyleSheet("background-color: black;");   // 导航栏底色涂黑；参数是一句 qss 规则

    // 全项目只有这个数组需要改：每完成一个页面，就往里加一行
    //   数组元素类型是上面那个 WidgetItemData，每个元素填两个格子：
    //   { 第1格：显示的文字,  第2格：造页面的函数 }
    WidgetItemData items[] = {
        // 第 2 格那坨 []() -> QWidget * { ... } 是个"匿名函数"（lambda），逐段拆开看：
        //   []            捕获列表：空的 = 不抓任何外部变量。
        //                 正因为它空，才能"退化成"普通函数指针塞进结构体
        //                 （要是写成 [&] 抓了外面的变量，就带上了"环境"，没法存成裸指针）
        //   ()            参数列表：空的 = 调用它不用给参数
        //   -> QWidget *  返回类型：返回一个 QWidget*，也就是"造出来的那个页面"
        //   { return new QWidget; }  函数体：真被调用时，new 一个空白页返回
        {"测试页A", []() -> QWidget * { return new QWidget; } },   // 空占位页
        {"测试页B", []() -> QWidget * { return new QWidget; } },   // 同上
    };

    // 一趟循环同时喂两个容器：第 n 次迭代既加列表项、又加页面 → 两边的序号天然对齐
    //   for (auto &itemData : items)：
    //     items   —— 要遍历的数组（上面刚定义）
    //     itemData—— 每转一圈，自动取到数组里的下一个元素
    //     &       —— 引用，表示"直接用原物"，不复制一份（元素里有 QString，复制没必要）
    //     auto    —— 让编译器自己推元素类型，省得写 WidgetItemData
    for (auto &itemData : items) {
        // new QListWidgetItem(itemData.title)：造一个列表项
        //   参数是这一行要显示的文字，来源：数组里每个元素的第 1 格
        QListWidgetItem *item = new QListWidgetItem(itemData.title);

        item->setTextColor(Qt::white);                                // 白字（5.15 会警告 deprecated，无视）
        item->setTextAlignment(Qt::AlignCenter);                      // 参数是"对齐方式"枚举值：居中

        // setSizeHint(QSize(200, 63))：给这一行一个"建议尺寸"，列表会照着排
        //   QSize(w, h) 两个参数就是宽和高：这条目占 200 像素宽、63 像素高
        item->setSizeHint(QSize(200, 63));

        // addItem(item)：把刚造的列表项交给导航栏
        //   参数就是要塞进去的那个条目；塞进去之后它归列表所有，
        //   列表销毁时会替我们 delete 它（所以这里 new 完不用自己 delete）
        listWidget->addItem(item);

        // itemData.createInstance()：调用结构体第 2 格里存的"配方函数"，造出一个新页面
        //   addWidget(页面)：把页面加进卡片盒，它会返回"这是第几张"（0、1、2…）
        //   返回值这里故意不要 —— 因为"第几个加进去"自动等于"第几页"，不用记
        //   （加进去后页面的父对象变成卡片盒，随卡片盒一起销毁）
        if (itemData.createInstance)
            stackedWidget->addWidget(itemData.createInstance());
    }

    listWidget->setMaximumWidth(200);           // 导航栏最宽 200px（剩下 824 给页面）

    // addWidget(控件)：把控件交给布局，之后它的位置大小就听布局安排（不要再手动 setGeometry）
    hBoxLayout->addWidget(listWidget);          // 左边：导航栏（第 1 个加进去的排最左）
    hBoxLayout->addWidget(stackedWidget);       // 右边：页面堆栈（第 2 个加进去的排在它右边）

    // widget->setLayout(hBoxLayout)：把布局"安装"到内容容器上
    //   参数就是要装的布局；这一句之后：
    //   ① 容器开始按布局的规则摆放里面两个控件（在此之前布局还是个没人认领的孤儿）
    //   ② 布局的"爹"变成容器，位置和大小跟着容器走
    widget->setLayout(hBoxLayout);

    // ★ 全部切换逻辑就在下面这几行
    // connect 的四个参数，按顺序是：(谁发消息, 发什么消息, 谁收消息, 收到后干什么)
    //   ① listWidget
    //        发送者：谁有消息要广播。
    //        来源：上面 new 出来的成员变量（左侧导航栏）
    //   ② SIGNAL(currentRowChanged(int))
    //        信号：广播的内容 = "当前选中的行号变了，新行号是这个 int"。
    //        这个信号是 QListWidget 自带的，不用你写 —— 用户点中某一行，
    //        它内部自己就会发（点哪行→内部把 currentRow 改成那行→顺手发信号）。
    //        SIGNAL(...) 是个宏，作用是把括号里的东西变成字符串 "2currentRowChanged(int)"，
    //        开头的 2 是 Qt 内部编号（2=信号，1=槽）；连接和发射全靠这个字符串对上号
    //   ③ stackedWidget
    //        接收者：谁要听这个消息。
    //        来源：上面 new 出来的成员变量（右侧卡片盒）
    //   ④ SLOT(setCurrentIndex(int))
    //        槽：收到消息后干什么 = "把当前显示页切到第 n 张"。
    //        也是 QStackedWidget 自带的；SLOT(...) 宏同理，变成 "1setCurrentIndex(int)"
    //   为什么这根线接得上：信号带一个 int，槽也收一个 int，而且含义刚好对得上
    //   （列表第 n 行 ↔ 卡片盒第 n 页，因为上面那个循环是成对添加的）
    connect(listWidget, SIGNAL(currentRowChanged(int)),      // 列表"当前行变了"
            stackedWidget, SLOT(setCurrentIndex(int)));       // ★ 全部切换逻辑就在这一行
}
