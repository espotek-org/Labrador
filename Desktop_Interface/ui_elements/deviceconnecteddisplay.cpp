#include "deviceconnecteddisplay.h"
#include <QApplication>

deviceConnectedDisplay::deviceConnectedDisplay(QWidget *parent) : QLabel(parent)
{
    setText("Device Not Connected!");
    setStyleSheet("QLabel { color:red; }");
}

void deviceConnectedDisplay::connectedStatusChanged(bool status){
    qDebug() << "deviceConnectedDisplay::connectedStatusChanged running!";
    if(status){
        setText("Device Connected");
        setStyleSheet("");
    }
    else{
        setText("Device Not Connected!");
        setStyleSheet("QLabel { color:red; }");
    }
#ifdef PLATFORM_ANDROID
    this->setVisible(!status);
#endif
}

void deviceConnectedDisplay::flashingFirmware(void){
    qDebug() << "deviceConnectedDisplay::flashingFirmware";
    setText("Flashing Device Firmware");
    setStyleSheet("QLabel { color:green; }");
}
