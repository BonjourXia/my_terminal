/******************************************************************
Copyright © Deng Zhimao Co., Ltd. 1990-2021. All rights reserved.
* @projectName   sensor
* @brief         ap3216c.h  (adapted for I2C userspace on 100ask IMX6ULL)
* @author        Deng Zhimao
* @email         1252699831@qq.com
* @net           www.openedv.com
* @date          2020-07-10
*******************************************************************/
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
    int i2cWriteReg(uint8_t reg, uint8_t val);
    int i2cReadReg(uint8_t reg, uint8_t *val);

    QString readAlsData();
    QString readPsData();
    QString readIrData();

    Q_PROPERTY(QString alsData READ alsData NOTIFY ap3216cDataChanged)
    Q_PROPERTY(QString psData READ psData NOTIFY ap3216cDataChanged)
    Q_PROPERTY(QString irData READ irData NOTIFY ap3216cDataChanged)

public slots:
    void timer_timeout();

signals:
    void ap3216cDataChanged();

};

#endif // AP3216C_H
