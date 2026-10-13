#include"mainwidget.h"                  // 主窗口类：界面骨架都在它的构造函数里搭
#include<QFile>                         // 读文件用的类
#include<QApplication>                  // Qt GUI 程序的总管家：管事件循环、管全局设置

// main：程序入口
//   两个参数不用你自己填，由"启动这个程序的 shell/系统"传进来：
//     argc = 参数个数（最少是 1：第 0 个永远是程序自己的名字）
//     argv = 参数字符串数组（argv[0] 是 "./my_terminal"，argv[1] 起才是你在命令行敲的东西）
//   Qt 拿到后会先挑走自己认识的那几个（例如 -platform linuxfb），剩下的留给你用
int main(int argc,char *argv[])
{
    // QApplication a(argc, argv)：创建"应用对象"，一个 Qt GUI 程序有且只有一个
    //   ① a          —— 变量名，随便起；惯例叫 a 或 app
    //   ② argc, argv —— 就是 main 传进来的那两个，原样转交给 Qt
    //                    Qt 要用它们做初始化（识别平台参数、样式参数等）
    //                    ——我们没走命令行传参，是靠环境变量 QT_QPA_PLATFORM=linuxfb 达到同样效果
    //   ③ 时机       —— 必须是整个程序里第一个被创建的 Qt 对象，晚了会报错
    //   它内部把事件系统、平台插件都准备好，后面 a.exec() 那个循环就靠它
    QApplication a(argc,argv);//固定第一步的流程

    // QFile file(":/style/mainstyle.qss")：开一个文件对象
    //   参数是文件路径；开头 ":/" 表示走 Qt 资源系统 —— 这个 qss 被 res.qrc 编进了
    //   可执行文件内部，不是板子上的真实路径（所以不用把 qss 推到板子上）
    QFile file(":/style/mainstyle.qss");

    // exists()：文件在不在？这里等价于问"编译时资源真的打进去了吗"
    if(file.exists()){
        // open(QFile::ReadOnly)：打开文件
        //   参数是"打开模式"，QFile::ReadOnly 表示只读（读样式表用不着写权限）
        //   其它常见模式：WriteOnly（只写）/ ReadWrite（读写）/ Append（追加）
        file.open(QFile::ReadOnly);

        // qApp->setStyleSheet(QLatin1String(file.readAll()));
        //   从最里面往外拆，一层一层看：
        //   ① file.readAll()         读整个文件，返回 QByteArray（就是"一堆字节"）
        //   ② QLatin1String(...)     把这堆字节包成 Qt 认识的字符串（零拷贝，只是递个指针过去）
        //                            qss 内容全是英文，不存在编码问题
        //   ③ qApp->setStyleSheet()  把样式表装到"整个应用"上，之后所有窗口/控件
        //                            都按 mainstyle.qss 里的规则换肤
        //   qApp 是个宏：#define qApp (static_cast<QApplication*>(QCoreApplication::instance()))
        //   说白了它就是上面那个 a 的另一个名字（全程序只有一个应用对象，这是取它的快捷方式）
        qApp->setStyleSheet(QLatin1String(file.readAll()));
        file.close();
       }

    // MainWidget w：在栈上造一个主窗口对象（一造出来，构造函数就把界面搭好了）
    //   没写参数 = 用默认值 parent=nullptr = 我没有父窗口 = 自己就是顶层窗口
    MainWidget w;

    // w.show()：把窗口显示出来。调用之前界面其实已经建好了，只是还藏着
    w.show();

    // a.exec()：进入"事件循环"，整个程序的发动机
    //   它就是个死循环：不停等事件（触摸、重绘、定时器…）→ 分发给对应的控件处理，
    //   直到窗口被关闭 / 有人调了退出，才返回。返回值是退出码：0 = 正常退出
    //   这个返回值直接当作 main 的返回值交还给系统
    return a.exec();


}
