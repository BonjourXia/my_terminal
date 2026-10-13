/******************************************************************
Copyright © Deng Zhimao Co., Ltd. 1990-2021. All rights reserved.
* @projectName   sensor
* @brief         ap3216c.cpp  (adapted for I2C userspace on 100ask IMX6ULL)
* @author        Deng Zhimao
* @email         1252699831@qq.com
* @net           www.openedv.com
* @date          2020-07-10
*******************************************************************/
#include "ap3216c.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <QDebug>

#define AP3216C_ADDR        0x1E
#define AP3216C_SYSCONFIG   0x00
#define AP3216C_IRDATALOW   0x0A
#define AP3216C_IRDATAHIGH  0x0B
#define AP3216C_ALSDATALOW  0x0C
#define AP3216C_ALSDATAHIGH 0x0D
#define AP3216C_PSDATALOW   0x0E
#define AP3216C_PSDATAHIGH  0x0F

Ap3216c::Ap3216c(QObject *parent) : QObject (parent)
{
    timer = new QTimer();
    i2cFd = -1;
    i2cInitialized = false;

    /* 打开I2C设备并初始化传感器 */
    initI2cDevice();

    connect(timer, SIGNAL(timeout()), this, SLOT(timer_timeout()));
}

Ap3216c::~Ap3216c()
{
    if (i2cFd >= 0)
        close(i2cFd);
}

void Ap3216c::initI2cDevice()
{
    /* 打开I2C-0总线 */
    i2cFd = open("/dev/i2c-0", O_RDWR);
    if (i2cFd < 0) {
        qDebug() << "AP3216C: Cannot open /dev/i2c-0";
        return;
    }

    /* 设置I2C从设备地址 */
    if (ioctl(i2cFd, I2C_SLAVE, AP3216C_ADDR) < 0) {
        qDebug() << "AP3216C: Failed to set I2C slave address";
        close(i2cFd);
        i2cFd = -1;
        return;
    }

    /* 复位/初始化传感器 */
    if (i2cWriteReg(AP3216C_SYSCONFIG, 0x04) < 0) {
        qDebug() << "AP3216C: Init failed (reset)";
        return;
    }

    /* 等待10ms以上 */
    usleep(15000);

    /* 启动ALS+PS+IR全模式转换 */
    if (i2cWriteReg(AP3216C_SYSCONFIG, 0x03) < 0) {
        qDebug() << "AP3216C: Init failed (start)";
        return;
    }

    i2cInitialized = true;
    qDebug() << "AP3216C: I2C initialized on /dev/i2c-0 addr 0x1E";
}

int Ap3216c::i2cWriteReg(uint8_t reg, uint8_t val)
{
    if (i2cFd < 0) return -1;
    uint8_t buf[2] = {reg, val};
    if (write(i2cFd, buf, 2) != 2)
        return -1;
    return 0;
}

int Ap3216c::i2cReadReg(uint8_t reg, uint8_t *val)
{
    if (i2cFd < 0) return -1;
    /* 写入寄存器地址 */
    if (write(i2cFd, &reg, 1) != 1)
        return -1;
    /* 读取寄存器值 */
    if (read(i2cFd, val, 1) != 1)
        return -1;
    return 0;
}

void Ap3216c::timer_timeout()
{
    if (!i2cInitialized) {
        return;
    }

    alsdata = readAlsData();
    psdata = readPsData();
    irdata = readIrData();
    emit ap3216cDataChanged();
}

QString Ap3216c::readIrData()
{
    uint8_t irLow = 0, irHigh = 0;

    if (i2cReadReg(AP3216C_IRDATALOW, &irLow) < 0)
        return "I2C Error";
    if (i2cReadReg(AP3216C_IRDATAHIGH, &irHigh) < 0)
        return "I2C Error";

    /* 数据无效标志位检查 */
    if (irLow & 0x80)
        return "0";

    uint16_t ir = ((uint16_t)irHigh << 2) | (irLow & 0x03);
    return QString::number(ir);
}

QString Ap3216c::readPsData()
{
    uint8_t psLow = 0, psHigh = 0;

    if (i2cReadReg(AP3216C_PSDATALOW, &psLow) < 0)
        return "I2C Error";
    if (i2cReadReg(AP3216C_PSDATAHIGH, &psHigh) < 0)
        return "I2C Error";

    /* 数据无效标志位检查 */
    if (psLow & 0x40)
        return "0";

    uint16_t ps = ((uint16_t)(psHigh & 0x3F) << 4) | (psLow & 0x0F);
    return QString::number(ps);
}

QString Ap3216c::readAlsData()
{
    uint8_t alsLow = 0, alsHigh = 0;

    if (i2cReadReg(AP3216C_ALSDATALOW, &alsLow) < 0)
        return "I2C Error";
    if (i2cReadReg(AP3216C_ALSDATAHIGH, &alsHigh) < 0)
        return "I2C Error";

    uint16_t als = ((uint16_t)alsHigh << 8) | alsLow;
    return QString::number(als);
}

QString Ap3216c::alsData()
{
    return alsdata;
}

QString Ap3216c::irData()
{
    return irdata;
}

QString Ap3216c::psData()
{
    return psdata;
}

void Ap3216c::setCapture(bool str)
{
    if(str)
        timer->start(500);
    else
        timer->stop();
}
