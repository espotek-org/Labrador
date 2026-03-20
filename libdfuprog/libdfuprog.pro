QT -= core gui

TARGET = dfuprog
TEMPLATE = lib
CONFIG += staticlib

QMAKE_CFLAGS_WARN_ON -= -Wextra

DEFINES += HAVE_CONFIG_H
android: DEFINES += PLATFORM_ANDROID

INCLUDEPATH += \
	$$PWD/dfu-programmer/src

SOURCES += \
	$$PWD/libdfuprog.c \
	$$PWD/dfu-programmer/src/arguments.c \
	$$PWD/dfu-programmer/src/atmel.c \
	$$PWD/dfu-programmer/src/commands.c \
	$$PWD/dfu-programmer/src/dfu.c \
	$$PWD/dfu-programmer/src/intel_hex.c \
	$$PWD/dfu-programmer/src/stm32.c \
	$$PWD/dfu-programmer/src/util.c
HEADERS += \
	$$PWD/config.h \
	$$PWD/libdfuprog.h \
	$$PWD/dfu-programmer/src/arguments.h \
	$$PWD/dfu-programmer/src/atmel.h \
	$$PWD/dfu-programmer/src/commands.h \
	$$PWD/dfu-programmer/src/dfu.h \
	$$PWD/dfu-programmer/src/dfu-bool.h \
	$$PWD/dfu-programmer/src/dfu-device.h \
	$$PWD/dfu-programmer/src/intel_hex.h \
	$$PWD/dfu-programmer/src/stm32.h \
	$$PWD/dfu-programmer/src/util.h

unix:!android:!macx {
	CONFIG += link_pkgconfig
	PKGCONFIG += libusb-1.0
}
macx {
	INCLUDEPATH += $$system(brew --prefix)/include/libusb-1.0
}
android {
	INCLUDEPATH += ../Desktop_Interface/build_android/libusb-242
}
