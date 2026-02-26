#ifndef GENERICUSBDRIVER_H
#define GENERICUSBDRIVER_H

#include <QWidget>
#include <QLabel>
#include <QDebug>
#include <QTimer>
#include <QThread>
#include <math.h>
#include <stdint.h>
#include <QMessageBox>

#include "functiongencontrol.h"
#include "xmega.h"
#include "desktop_settings.h"
//#include "buffercontrol.h"
#include "unified_debug_structure.h"

//#define EXPECTED_FIRMWARE_VERSION 0x0007

#ifdef WINDOWS_64_BIT
    #define DEFINED_EXPECTED_VARIANT 1
    #define ISO_PACKET_SIZE 125
    #define NUM_ISO_ENDPOINTS (6)
#else
    #define DEFINED_EXPECTED_VARIANT 2
    #define ISO_PACKET_SIZE 750
    #define NUM_ISO_ENDPOINTS (1)
#endif

#ifdef PLATFORM_WINDOWS
    #define ISO_PACKETS_PER_CTX 17
    #define NUM_FUTURE_CTX 40
#elif defined PLATFORM_RASPBERRY_PI
    #define ISO_PACKETS_PER_CTX 66 // 15fps...
    #define NUM_FUTURE_CTX 4
#elif defined PLATFORM_LINUX
    #define ISO_PACKETS_PER_CTX 17
    #define NUM_FUTURE_CTX 20
#else
    // A real Mac may be capable of higher refresh rates and more parallel contexts, but these settings work on a hackintosh too.
    #define ISO_PACKETS_PER_CTX 33
    #define NUM_FUTURE_CTX 4
#endif

#define ISO_TIMER_PERIOD 1
#define MAX_OVERLAP (NUM_FUTURE_CTX*NUM_ISO_ENDPOINTS + 1)

#define RECOVERY_PERIOD 1000
#define BOARD_VID 0x03eb
#define BOARD_PID 0xba94
#define GOBINDAR_PID 0xa000

#define E_BOARD_IN_BOOTLOADER static_cast<unsigned char>(-65)

//genericUsbDriver handles the parts of the USB stack that are not platform-dependent.
//It exists as a superclass for winUsbDriver (on Windows) or unixUsbDriver (on Linux)

class genericUsbDriver : public QLabel
{
    Q_OBJECT
public:
    //State Vars
    int deviceMode = INIT_DEVICE_MODE;
    double scopeGain = 0.5;
    int dutyTemp = 21;
    bool killOnConnect = false;
    //Generic Vars
    unsigned char *outBuffers[2];
    unsigned int bufferLengths[2];
    bool connected = false;
    bool calibrateOnConnect = false;
    //Generic Functions
    explicit genericUsbDriver(QWidget *parent = 0);
    ~genericUsbDriver();
    virtual char *isoRead(unsigned int *newLength) = 0;
    //void setBufferPtr(bufferControl *newPtr);
    void saveState(int *_out_deviceMode, double *_out_scopeGain, double *_out_currentPsuVoltage, int *_out_digitalPinState);
    void setTxUart(int baudRate_CH1, std::vector<uint8_t> samples, functionGen::ChannelID channelID, functionGen::SingleChannelController* fGenControl);
    virtual void usbSendControl(uint8_t RequestType, uint8_t Request, uint16_t Value, uint16_t Index, uint16_t Length, unsigned char *LDATA) = 0;
    virtual void manualFirmwareRecovery(void) = 0;
    double psu_offset = 0;
protected:
    //State Vars
    unsigned char fGenTriple=0;
    unsigned short gainMask = 2056;
	functionGen::SingleChannelController* fGenPtrData[2] = {NULL, NULL};
    int dutyPsu = 0;
    double currentPsuVoltage;
    int digitalPinState = 0;
    unsigned char firmver = 0;
    unsigned char variant = 0;
    //Generic Vars
    //bufferControl *bufferPtr = NULL;
    QTimer *psuTimer = nullptr;
    unsigned char pipeID[3];
    QTimer *isoTimer = nullptr;
    QTimer *connectTimer = nullptr;
    QTimer *recoveryTimer;
    unsigned char currentWriteBuffer = 0;
    unsigned long timerCount = 0;
    unsigned char inBuffer[256];
    //Generic Functions
    void requestFirmwareVersion(void);
    void requestFirmwareVariant(void);
    void deGobindarise();
    virtual unsigned char usbInit(unsigned long VIDin, unsigned long PIDin) = 0;
    virtual int usbIsoInit(void) = 0;
    virtual int flashFirmware(void) = 0;
    QMessageBox *messageBox;
signals:
    void sendClearBuffer(bool ch3751, bool ch3752, bool ch750);
    void setVisible_CH2(bool visible);
    void gainBuffers(double multiplier);
    void disableWindow(bool enabled);
    void enableMMTimer();
    void checkXY(bool);
    void upTick(void);
    void killMe(void);
    void connectedStatus(bool status);
    void initialConnectComplete(void);
    void signalFirmwareFlash(void);
    void calibrateMe(void);
public slots:
    void setPsu(double voltage);
    void setFunctionGen(functionGen::ChannelID channelID, functionGen::SingleChannelController *fGenControl);
	void sendFunctionGenData(functionGen::ChannelID channelID);
    void setDeviceMode(int mode);
    void newDig(int digState);
    void psuTick(void);
    void setGain(double newGain);
    void avrDebug(void);
    virtual void isoTimerTick(void) = 0;
    virtual void recoveryTick() = 0;
    virtual void shutdownProcedure() = 0;
    void checkConnection();
    void bootloaderJump();
    void kickstartIso();
};


#endif // GENERICUSBDRIVER_H
