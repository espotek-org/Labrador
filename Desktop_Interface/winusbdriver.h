#ifndef WINUSBDRIVER_H
#define WINUSBDRIVER_H

//libusbK includes
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include "libusbk.h"

#include "genericusbdriver.h"

//winUsbDriver handles the Windows-specific parts of USB communication, through libusbK.
//See genericUsbDriver for the non-platform-specific parts.
class winUsbDriver : public genericUsbDriver
{
    Q_OBJECT
public:
    //Generic Functions
    explicit winUsbDriver(QWidget *parent = 0);
    ~winUsbDriver();
    void usbSendControl(uint8_t RequestType, uint8_t Request, uint16_t Value, uint16_t Index, uint16_t Length, unsigned char *LDATA);
    char *isoRead(unsigned int *newLength);
    void manualFirmwareRecovery(void);
private:
    //USB Vars
    KUSB_HANDLE handle = NULL;
    //USBIso Vars
    PKISO_CONTEXT isoCtx[NUM_ISO_ENDPOINTS][NUM_FUTURE_CTX];
    KOVL_HANDLE ovlkHandle[NUM_ISO_ENDPOINTS][NUM_FUTURE_CTX];
    KOVL_POOL_HANDLE ovlPool;
    unsigned char dataBuffer[NUM_ISO_ENDPOINTS][NUM_FUTURE_CTX][ISO_PACKET_SIZE*ISO_PACKETS_PER_CTX];
    //Generic Functions
    unsigned char usbInit(unsigned long VIDin, unsigned long PIDin);
    int usbIsoInit(void);
    int flashFirmware(void);
    bool allEndpointsComplete(int n);
    bool shutdownRequested = false;
    bool shutdownCompleteSent = false;
    bool transferPending[NUM_ISO_ENDPOINTS][NUM_FUTURE_CTX] = {};
    QElapsedTimer shutdownElapsed;

signals:
    void shutdownComplete();


public slots:
    void isoTimerTick(void);
    void recoveryTick();
    void shutdownProcedure(void);
};

#endif // WINUSBDRIVER_H
