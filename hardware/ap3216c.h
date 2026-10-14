
#ifndef AP3216C_H
#define AP3216C_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <stdint.h>

class Ap3216c : public QObject
{
    Q_OBJECT

public:
    explicit Ap3216c(QObject *parent = 0);
    ~Ap3216c();

    Q_INVOKABLE void setCapture(bool str);

    QString alsData();
    QString psData();
    QString irData();

private:
    QTimer *timer;
    int i2cFd;                  // I2C设备文件描述符
    bool i2cInitialized;        // I2C是否初始化成功

    QString alsdata;
    QString psdata;
    QString irdata;

    void initI2cDevice();       // 初始化I2C和传感器
    int i2cWriteReg(uint8_t reg, uint8_t val);//写寄存器方法
    int i2cReadReg(uint8_t reg, uint8_t *val);//读寄存器方法

    QString readAlsData();
    QString readPsData();
    QString readIrData();

    Q_PROPERTY(QString alsData READ alsData NOTIFY ap3216cDataChanged)//Q_PROPERTY：把"数据"变成 Qt 认识的"属性" ----
    Q_PROPERTY(QString psData READ psData NOTIFY ap3216cDataChanged)
    Q_PROPERTY(QString irData READ irData NOTIFY ap3216cDataChanged)

public slots:
    void timer_timeout();//定时器

signals:
    void ap3216cDataChanged();//信号触发修改

};

#endif // AP3216C_H
