#-------------------------------------------------
#
# Project created by QtCreator 2016-03-30T13:27:52
#
#-------------------------------------------------


############################################################################
######CLEAN->RUN QMAKE->BUILD after changing anything on this page!!!######
##########################################################################

QT += core gui

CONFIG += c++14

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

TARGET = Labrador
TEMPLATE = app

GIT_HASH_SHORT=$$system(git rev-parse --short HEAD)
!isEmpty(GIT_HASH_SHORT) {
    DEFINES += "GIT_HASH_SHORT=$${GIT_HASH_SHORT}"
}

QCP_VER = 1
DEFINES += "QCP_VER=$${QCP_VER}"
equals(QCP_VER,"2"){
    DEFINES += QCUSTOMPLOT_USE_OPENGL
    LIBS += -lOpenGL32
    message("Using QCP2 with OpenGL support")
}

include(ui_elements.pri)

MOC_DIR = $$PWD/moc

SOURCES += main.cpp\
        mainwindow.cpp \
    functiongencontrol.cpp \
    isodriver.cpp \
    isobuffer.cpp \
    desktop_settings.cpp \
    scoperangeenterdialog.cpp \
    genericusbdriver.cpp \
    isobufferbuffer.cpp \
    uartstyledecoder.cpp \
    daqform.cpp \
    daqloadprompt.cpp \
    isobuffer_file.cpp \
    i2cdecoder.cpp \
    asyncdft.cpp

HEADERS  += mainwindow.h \
    functiongencontrol.h \
    xmega.h \
    isodriver.h \
    isobuffer.h \
    desktop_settings.h \
    scoperangeenterdialog.h \
    genericusbdriver.h \
    isobufferbuffer.h \
    q_debugstream.h \
    unified_debug_structure.h \
    uartstyledecoder.h \
    daqform.h \
    daqloadprompt.h \
    isobuffer_file.h \
    i2cdecoder.h \
    asyncdft.h

android:{
FORMS    += ui_files_mobile/mainwindow.ui \
    ui_files_mobile/scoperangeenterdialog.ui \
    ui_files_desktop/daqform.ui \
    ui_files_desktop/daqloadprompt.ui
}

!android:{
FORMS    += ui_files_desktop/mainwindow.ui \
    ui_files_desktop/scoperangeenterdialog.ui \
    ui_files_desktop/daqform.ui \
    ui_files_desktop/daqloadprompt.ui
}


RESOURCES += \
    resources.qrc

DESTDIR = bin

RC_ICONS = appicon.ico

INCLUDEPATH += $$PWD/ui_elements
DEPENDPATH += $$PWD/ui_elements


###########################################################
################    WINDOWS BUILD ONLY    ################
#########################################################

win32{
    DEFINES += PLATFORM_WINDOWS
    SOURCES += winusbdriver.cpp
    HEADERS += winusbdriver.h

    #libusbk include
    contains(QT_ARCH, i386) {
        message("Building for Windows (x86)")
        CONFIG(release, debug|release): LIBS += -L$$PWD/build_win/libusbk/bin/lib/x86/ -llibusbK
        else:CONFIG(debug, debug|release): LIBS += -L$$PWD/build_win/libusbk/bin/lib/x86/ -llibusbK
        DEFINES += "WINDOWS_32_BIT"
        INCLUDEPATH += $$PWD/build_win/fftw/x86
        LIBS += -L$$PWD/build_win/fftw/x86 -llibfftw3-3
    } else {
        message("Building for Windows (x64)")
        CONFIG(release, debug|release): LIBS += -L$$PWD/build_win/libusbk/bin/lib/amd64/ -llibusbK
        else:CONFIG(debug, debug|release): LIBS += -L$$PWD/build_win/libusbk/bin/lib/amd64/ -llibusbK
        INCLUDEPATH += $$PWD/build_win/fftw/x64
        LIBS += -L$$PWD/build_win/fftw/x64 -llibfftw3-3

        DEFINES += "WINDOWS_64_BIT"
    }
    INCLUDEPATH += $$PWD/build_win/libusbk/includes
    DEPENDPATH += $$PWD/build/win/libusbk/includes
}

#############################################################
################    GNU/LINUX BUILD ONLY    ################
###########################################################

unix:!android:!macx{
    DEFINES += PLATFORM_LINUX

    isEmpty(PREFIX): PREFIX = /usr/local
    CONFIG += link_pkgconfig
    PKGCONFIG += libusb-1.0  ##make sure you have the libusb-1.0-0-dev package!
    PKGCONFIG += fftw3       ##make sure you have the libfftw3-dev package!
    PKGCONFIG += eigen3      ##make sure you have the libeigen3-dev package!
    contains(QT_ARCH, arm) {
        message("Building for Raspberry Pi")
        #libdfuprog include
        LIBS += -L$$PWD/build_linux/libdfuprog/lib/arm -ldfuprog-0.9
        INCLUDEPATH += $$PWD/build_linux/libdfuprog/include
        DEPENDPATH += $$PWD/build_linux/libdfuprog/include
        QMAKE_CFLAGS += -fsigned-char
        QMAKE_CXXFLAGS += -fsigned-char
        DEFINES += "PLATFORM_RASPBERRY_PI"
        #All ARM-Linux GCC treats char as unsigned by default???
        lib_deploy.files = $$PWD/build_linux/libdfuprog/lib/arm/libdfuprog-0.9.so
        lib_deploy.path = $$PREFIX/lib

    } else:contains(QT_ARCH, i386) {
        message("Building for Linux (x86)")
        #libdfuprog include
        LIBS += -L$$PWD/build_linux/libdfuprog/lib/x86 -ldfuprog-0.9
        INCLUDEPATH += $$PWD/build_linux/libdfuprog/include
        DEPENDPATH += $$PWD/build_linux/libdfuprog/include
        lib_deploy.files = $$PWD/build_linux/libdfuprog/lib/x86/libdfuprog-0.9.so
        lib_deploy.path = $$PREFIX/lib

    } else {
        message("Building for Linux (x64)")
        #libdfuprog include
        LIBS += -L$$PWD/build_linux/libdfuprog/lib/x64 -ldfuprog-0.9
        INCLUDEPATH += $$PWD/build_linux/libdfuprog/include
        DEPENDPATH += $$PWD/build_linux/libdfuprog/include
        lib_deploy.files = $$PWD/build_linux/libdfuprog/lib/x64/libdfuprog-0.9.so
        lib_deploy.path = $$PREFIX/lib
    }

    target.path = $$PREFIX/bin

    firmware.path = $$PREFIX/share/EspoTek/Labrador/firmware
    firmware.files += $$files(bin/firmware/labrafirm*)

    waveforms.path = $$PREFIX/share/EspoTek/Labrador/waveforms
    waveforms.files += $$files(bin/waveforms/*)

    udev.path = /lib/udev/rules.d
    udev.files = rules.d/69-labrador.rules

    icon48.path = $$PREFIX/share/icons/hicolor/48x48/apps/
    icon48.files += resources/icon48/espotek-labrador.png

    icon256.path = $$PREFIX/share/icons/hicolor/256x256/apps/
    icon256.files += resources/icon256/espotek-labrador.png

    desktop.path = $$PREFIX/share/applications
    desktop.files += resources/espotek-labrador.desktop

    symlink.path = $$PREFIX/bin
    symlink.extra = ln -sf Labrador $(INSTALL_ROOT)$$PREFIX/bin/labrador

    udevextra.path = /lib/udev/rules.d
    udevextra.extra = test -n $$shell_quote($(INSTALL_ROOT)) || { udevadm control --reload-rules && udevadm trigger --subsystem-match=usb ; }

    INSTALLS += target
    INSTALLS += lib_deploy
    INSTALLS += firmware
    INSTALLS += waveforms
    INSTALLS += udev
    INSTALLS += icon48
    INSTALLS += icon256
    INSTALLS += desktop
    INSTALLS += symlink
    INSTALLS += udevextra
}



#############################################################
################    MAC OSX BUILD ONLY    ##################
###########################################################

macx:DEFINES += PLATFORM_MAC

#libusb dylib include
macx:LIBS += -L$$PWD/build_mac/libusb/lib -lusb-1.0
macx:INCLUDEPATH += $$PWD/build_mac/libusb/include/libusb-1.0
macx:DEPENDPATH += $$PWD/build_mac/libusb/include/libusb-1.0

#libdfuprog dylib include
macx:LIBS += -L$$PWD/build_mac/libdfuprog/lib -ldfuprog-0.9
macx:INCLUDEPATH += $$PWD/build_mac/libdfuprog/include
macx:DEPENDPATH += $$PWD/build_mac/libdfuprog/include

macx: INCLUDEPATH += $$system(brew --prefix)/include
macx: INCLUDEPATH += $$system(brew --prefix)/include/eigen3
macx: LIBS += -L$$system(brew --prefix)/lib

macx:QMAKE_LFLAGS += "-undefined dynamic_lookup"

QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.10




#############################################################
########   SHARED UNIX-LIKE BUILDS (MAC + LINUX)   #########
###########################################################

unix:SOURCES += unixusbdriver.cpp
unix:HEADERS += unixusbdriver.h

# For multithreading on Unix fftw
unix:!macx: LIBS += -fopenmp
macx: LIBS += -lomp
unix: LIBS += -lfftw3_omp

#############################################################
########       SHARED ANDROID/LINUX GCC FLAGS      #########
###########################################################

unix:!macx: QMAKE_CXXFLAGS_RELEASE -= -O0
unix:!macx: QMAKE_CXXFLAGS_RELEASE -= -O1
unix:!macx: QMAKE_CXXFLAGS_RELEASE -= -O2
unix:!macx: QMAKE_CXXFLAGS_RELEASE -= -O3

android: QMAKE_CXXFLAGS_RELEASE -= -O0
android: QMAKE_CXXFLAGS_RELEASE -= -O1
android: QMAKE_CXXFLAGS_RELEASE -= -O2
android: QMAKE_CXXFLAGS_RELEASE -= -O3
android: QMAKE_CXXFLAGS_RELEASE -= -Os


android: QMAKE_CFLAGS_RELEASE -= -O0
android: QMAKE_CFLAGS_RELEASE -= -O1
android: QMAKE_CFLAGS_RELEASE -= -O2
android: QMAKE_CFLAGS_RELEASE -= -O3
android: QMAKE_CFLAGS_RELEASE -= -Os


#############################################################
#################    ANDROID BUILD ONLY    #################
###########################################################

android:{

    QMAKE_CFLAGS += -fsigned-char
    QMAKE_CXXFLAGS += -fsigned-char
    #Android treats char as unsigned by default (why???)

    # Building .so files fails with -Wl,--no-undefined
    QMAKE_LFLAGS_APP     -= -Wl,--no-undefined
    QMAKE_LFLAGS_SHLIB   -= -Wl,--no-undefined
    QMAKE_LFLAGS_PLUGIN  -= -Wl,--no-undefined
    QMAKE_LFLAGS_NOUNDEF -= -Wl,--no-undefined

    QT += androidextras
    CONFIG += mobility
    MOBILITY =

    DEFINES += PLATFORM_ANDROID
    SOURCES += androidusbdriver.cpp
    HEADERS += androidusbdriver.h
    INCLUDEPATH += $$PWD/build_android/libusb-242
    DEPENDPATH += $$PWD/build_android/libusb-242

    ANDROID_PACKAGE_SOURCE_DIR  = $$PWD/build_android/package_source
    assets_deploy.files=$$files($$PWD/build_android/package_source/assets/*)
    assets_deploy.path=/assets
    INSTALLS += asssets_deploy

    #libdfuprog include
    LIBS += -L$$PWD/build_android/libdfuprog/lib -ldfuprog-0.9
    INCLUDEPATH += $$PWD/build_android/libdfuprog/include
    DEPENDPATH += $$PWD/build_android/libdfuprog/include
    ANDROID_EXTRA_LIBS += $${PWD}/build_android/libdfuprog/lib/libdfuprog-0.9.so

    #liblog include
    LIBS += -L$$PWD/build_android/liblog/lib -llog
    ANDROID_EXTRA_LIBS += $${PWD}/build_android/liblog/lib/liblog.so



    DISTFILES += \
        build_android/package_source/AndroidManifest.xml \
        build_android/package_source/gradle/wrapper/gradle-wrapper.jar \
        build_android/package_source/gradlew \
        build_android/package_source/res/values/libs.xml \
        build_android/package_source/build.gradle \
        build_android/package_source/gradle/wrapper/gradle-wrapper.properties \
        build_android/package_source/gradlew.bat \
        build_android/package_source/AndroidManifest.xml \
        build_android/package_source/res/values/libs.xml \
        build_android/package_source/build.gradle \
        build_android/package_source/src/androidInterface.java

    # Doing the following inside one equals() failed. qmake bug?  https://forum.qt.io/topic/113836/dynamic-libs-on-android-with-qt5-14-2/4
    for(abi, ANDROID_ABIS): message("qmake building for Android ($${abi}) platform")
    for(abi, ANDROID_ABIS): LIBS += -L$${PWD}/build_android/libusb-242/android/$${abi} -lusb1.0
    for(abi, ANDROID_ABIS): ANDROID_EXTRA_LIBS += $${PWD}/build_android/libusb-242/android/$${abi}/libusb1.0.so
}

DISTFILES += \
    build_android/package_source/AndroidManifest.xml \
    build_android/package_source/gradle/wrapper/gradle-wrapper.jar \
    build_android/package_source/gradlew \
    build_android/package_source/res/values/libs.xml \
    build_android/package_source/build.gradle \
    build_android/package_source/gradle/wrapper/gradle-wrapper.properties \
    build_android/package_source/gradlew.bat \
    build_android/package_source/AndroidManifest.xml \
    build_android/package_source/gradle/wrapper/gradle-wrapper.jar \
    build_android/package_source/gradlew \
    build_android/package_source/res/values/libs.xml \
    build_android/package_source/build.gradle \
    build_android/package_source/gradle/wrapper/gradle-wrapper.properties \
    build_android/package_source/gradlew.bat \
    build_android/package_source/res/xml/device_filter.xml
