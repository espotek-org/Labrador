INCLUDEPATH += $$PWD/ui_elements
INCLUDEPATH += $$PWD/ui_elements/qcp$${QCP_VER}

SOURCES += \
    ui_elements/buffercontrol.cpp \
    ui_elements/cursorenabler.cpp \
    ui_elements/deviceconnecteddisplay.cpp \
    ui_elements/espocombobox.cpp \
    ui_elements/esposlider.cpp \
    ui_elements/espospinbox.cpp \
    ui_elements/noclosemenu.cpp \
    ui_elements/qcp$${QCP_VER}/qcustomplot.cpp \
    ui_elements/siprint.cpp \
    ui_elements/timedtickbox.cpp \
    ui_elements/voltagespinbox.cpp \
    ui_elements/swipeystack.cpp

HEADERS += \
    ui_elements/buffercontrol.h \
    ui_elements/cursorenabler.h \
    ui_elements/deviceconnecteddisplay.h \
    ui_elements/espocombobox.h \
    ui_elements/esposlider.h \
    ui_elements/espospinbox.h \
    ui_elements/noclosemenu.h \
    ui_elements/qcp$${QCP_VER}/qcustomplot.h \
    ui_elements/siprint.h \
    ui_elements/timedtickbox.h \
    ui_elements/voltagespinbox.h \
    ui_elements/swipeystack.h
