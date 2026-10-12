#include"mainwidget.h"
#include<QFile>
#include<QApplication>

int main(int argc,char *argv[])
{
    QApplication a(argc,argv);//固定第一步的流程

       QFile file(":/style/mainstyle.qss");
       if(file.exists()){
        file.open(QFile::ReadOnly);
         qApp->setStyleSheet(QLatin1String(file.readAll()));
        file.close();
       }
    MainWidget w;
    w.show();

    return a.exec();


}