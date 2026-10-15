QT += core gui widgets network
CONFIG += c++11

TARGET   = my_terminal
TEMPLATE = app

SOURCES += main.cpp mainwidget.cpp
HEADERS += mainwidget.h
RESOURCES += res.qrc


BOARD = $$(MY_TERMINAL_BOARD)
!isEmpty(BOARD) {
    message("=== 构建目标：ARM 板 ===")
    QT += charts sql multimedia multimediawidgets
    SYSROOT = /home/book/100ask_imx6ull-sdk/Buildroot_2020.02.x/output/host/arm-buildroot-linux-gnueabihf/sysroot
    INCLUDEPATH += $$SYSROOT/usr/include
    LIBS += -L$$SYSROOT/usr/lib \
        -lopencv_core -lopencv_highgui -lopencv_imgproc -lopencv_videoio \
        -lopencv_imgcodecs -lopencv_objdetect -lopencv_video -lopencv_ml \
        -lopencv_calib3d -lopencv_features2d -lopencv_flann -lopencv_photo
} else {
    message("=== 构建目标：x86 主机 ===")
    QT += sql
   QT += charts
    QT += multimedia multimediawidgets
}
# ================= 目录汇入（见 0.4）=================
include(./form/form.pri)
include(./hardware/hardware.pri)

INCLUDEPATH += $$PWD/form
INCLUDEPATH += $$PWD/hardware