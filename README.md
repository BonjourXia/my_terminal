# my_terminal

100ask_IMX6ULL 开发板上的一个 Qt5 触摸屏程序，屏幕 1024x600。左边一列导航，右边切换页面，把轮播图、音乐、视频、传感器、网络定位和闹钟都塞进了一个程序里。

代码是照着正点原子的 smart_terminal 重写的一遍，界面全部手写，没用 Qt Designer。原例程只有轮播图、播放器和传感器这几页，网络定位页是新加的，闹钟页改自正点原子的 SQLite 例程。注释写得比较啰嗦，当时主要是在边写边学。

板子：100ask IMX6ULL，Linux 4.9.88 + Buildroot 2020.02，Qt 5。

## 能用的页面

- 轮播图：5 张图每 3 秒自动切换，左右箭头和底部圆点都能点，切换的时候整排图一起滑动缩放。
- 音乐播放器：自动扫描程序目录下的 myMusic/，支持上一首、播放/暂停、下一首、点歌单切歌和拖动进度。
- 视频播放器：扫描 myVideo/，播放/暂停、下一个、音量加减、音量条、进度条、全屏。
- 板载传感器：AP3216C 的光照/红外/距离三个数值和三条实时曲线；ICM20608 的三轴角速度、三轴加速度和温度。AP3216C 是直接读 I2C 寄存器，ICM20608 走 /sys 的 IIO 节点。
- 网络定位：先向 ip-api 查公网 IP 和城市，再按经纬度拼 3x3 的高德地图瓦片，中间画一个十字定位点。没网或者瓦片拉不回来的时候会显示提示，不是白屏。
- 数据库闹钟：SQLite 存闹钟，可以添加、修改、删除、开关。

## 编译

电脑上直接编（看看界面可以，传感器和定位页没数据）：

```bash
mkdir -p build-host && cd build-host
qmake ../my_terminal.pro
make -j$(nproc)
```

交叉编译到板子：

```bash
mkdir -p build-arm && cd build-arm
MY_TERMINAL_BOARD=1 /path/to/100ask_imx6ull-sdk/Buildroot_2020.02.x/output/host/bin/qmake ../my_terminal.pro
make -j$(nproc)
```

my_terminal.pro 里的 SYSROOT 是我电脑上的 SDK 路径，换机器要改成自己的。

## 跑起来

程序从自己所在目录找 myMusic/ 和 myVideo/，这两个文件夹要和可执行文件放在一起：

```bash
adb push build-arm/my_terminal /root/my_terminal/
adb push myMusic myVideo /root/my_terminal/
```

板子上没有桌面，用 linuxfb 直接往帧缓冲画，触摸走 tslib：

```bash
adb shell "cd /root/my_terminal && \
  QT_QPA_PLATFORM=linuxfb QT_QPA_FB_NO_LIBINPUT=1 QT_QPA_FB_TSLIB=1 \
  TSLIB_TSDEVICE=/dev/input/event1 TSLIB_CONFFILE=/etc/ts.conf \
  TSLIB_PLUGINDIR=/usr/lib/ts TSLIB_CALIBFILE=/etc/pointercal \
  XDG_RUNTIME_DIR=/tmp/runtime-root ./my_terminal"
```

QT_QPA_FB_TSLIB 和 QT_QPA_FB_NO_LIBINPUT 不能漏，漏了就是界面正常、点屏幕没反应。

网络定位页要板子先联网。wifi.sh 是我在板子上开 WiFi 用的脚本，热点名和密码在里面，换成自己的就行。板子的 WiFi 不会开机自动连，每次重启都要重新跑。

## 已知问题

- 板子没有 VPU，视频是 CPU 软解，1080p 会卡得比较明显。
- 板子没有 RTC，开机时间会停在 1970 年，闹钟页要先手动把系统时间设对。
- 这块板没有 LED 和蜂鸣器的 sysfs 节点，传感器页那两个按钮点了只会提示没找到设备。
- 音乐页底部的收藏、模式、菜单、音量按钮目前只有图标，没接功能。
- 闹钟只是存进数据库和显示，到点不会真的响。

## 目录

```
form/       六个页面
hardware/   硬件和网络的数据层：AP3216C(I2C)、ICM20608(sysfs)、ip-api(HTTP)
images/     界面用到的图片
style/      qss 样式
myMusic/    示例音乐
myVideo/    示例视频
v4l2learn/  单独的 V4L2 虚拟摄像头驱动练习，和 Qt 程序没有关系
```

v4l2learn/ 是后来自己学 V4L2 时写的，一个虚拟摄像头驱动加一个测试程序，编译和上面的 Qt 程序互不影响。
