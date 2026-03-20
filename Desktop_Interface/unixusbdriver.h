#ifndef unixUsbDriver_H
#define unixUsbDriver_H

#include <QWidget>
#include <QThread>
#include <QMutex>
#include <QDateTime>
#include <atomic>

#include "genericusbdriver.h"
#include "libusb.h"
extern "C"
{
    #include "libdfuprog.h"
}

#define MAX_ALLOWABLE_CUMULATIVE_FRAME_ERROR 50

//tcBlock is fed to the callback in the libusb user data section.
typedef struct tcBlock{
    int number;
    bool completed;
    qint64 timeReceived;
} tcBlock;

class unixUsbDriver;

typedef struct isoTransferUserData{
    tcBlock *transferBlock = nullptr;
    unixUsbDriver *owner = nullptr;
    unsigned char endpoint = 0;
    int context = 0;
} isoTransferUserData;

//Oddly, libusb requires you to make a blocking libusb_handle_events() call in order to execute the callbacks for an asynchronous transfer.
//Since the call is blocking, this worker must exist in a separate, low priority thread!
class worker : public QObject
{
    Q_OBJECT

public:
    worker(){};
    ~worker(){};
    libusb_context *ctx;
    std::atomic_bool stopTime{false};
    std::atomic_int *pendingTransfers = nullptr;
public slots:
    void handle(){
        qDebug() << "SUB THREAD ID" << QThread::currentThreadId();
        while(true){
            if(ctx && libusb_event_handling_ok(ctx)){
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 100000;
                libusb_handle_events_timeout(ctx, &tv);
            }
            if(stopTime.load()){
                if((pendingTransfers == nullptr) || (pendingTransfers->load() <= 0)){
                    break;
                }
            }
        }
        qDebug() << "Cleanup complete";
    }
};

//This is the actual unixUsbDriver
//It handles the Mac/Linux specific parts of USB communication, through libusb.
//See genericUsbDriver for the non-platform-specific parts.
class unixUsbDriver : public genericUsbDriver
{
    Q_OBJECT
public:
    explicit unixUsbDriver(QWidget *parent = 0);
    ~unixUsbDriver();
    void usbSendControl(uint8_t RequestType, uint8_t Request, uint16_t Value, uint16_t Index, uint16_t Length, unsigned char *LDATA);
    char *isoRead(unsigned int *newLength);
    void manualFirmwareRecovery(void);
    void noteShutdownTransferCallback(unsigned char endpoint, int context);
protected:
    //USB Vars
    libusb_context *ctx = NULL;
    libusb_device_handle *handle = NULL;
    //USBIso Vars
    unsigned char *midBuffer_current[NUM_ISO_ENDPOINTS];
    unsigned char *midBuffer_prev[NUM_ISO_ENDPOINTS];
    qint64 midBufferOffsets[NUM_ISO_ENDPOINTS];
    libusb_transfer *isoCtx[NUM_ISO_ENDPOINTS][NUM_FUTURE_CTX] = { };
    tcBlock transferCompleted[NUM_ISO_ENDPOINTS][NUM_FUTURE_CTX];
    isoTransferUserData transferUserData[NUM_ISO_ENDPOINTS][NUM_FUTURE_CTX];
    unsigned char dataBuffer[NUM_ISO_ENDPOINTS][NUM_FUTURE_CTX][ISO_PACKET_SIZE*ISO_PACKETS_PER_CTX];
    worker *isoHandler = nullptr;
    QThread *workerThread = nullptr;
    int cumulativeFramePhaseErrors = 0;
    QMutex shutdownStateMutex;
    bool cancelPending[NUM_ISO_ENDPOINTS][NUM_FUTURE_CTX] = { };
    std::atomic_int shutdownCallbacksPending{0};
    bool shutdownInitiated = false;
    bool shutdownSignalSent = false;
    //Generic Functions
    virtual unsigned char usbInit(unsigned long VIDin, unsigned long PIDin);
    int usbIsoInit(void);
    virtual int flashFirmware(void);
    bool allEndpointsComplete(int n);
    int cancelIsoTransfers(void);
    bool shutdownMode = false;
signals:
    void shutdownComplete(void); //This is emitted when the shutdown procedure is complete and the driver is safe to delete.
public slots:
    void isoTimerTick(void);
    void recoveryTick(void);
    void shutdownProcedure(void);
    void backupCleanup(void);
};

#endif // unixUsbDriver_H
