#include "unixusbdriver.h"
#include <QApplication>
#include <QMessageBox>
#include <QStandardPaths>

unixUsbDriver::unixUsbDriver(QWidget *parent) : genericUsbDriver(parent)
{
    qDebug() << "unixUsbDriver created!";
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    for (unsigned char k=0; k<NUM_ISO_ENDPOINTS; k++){
        midBuffer_current[k] = (unsigned char*)calloc(ISO_PACKETS_PER_CTX * ISO_PACKET_SIZE, 1);
        midBuffer_prev[k] = (unsigned char*)calloc(ISO_PACKETS_PER_CTX * ISO_PACKET_SIZE, 1);
    }
}

unixUsbDriver::~unixUsbDriver(void){
    qDebug() << "\n\nunixUsbDriver destructor ran!";
    //unixDriverDeleteMutex.lock();
    if(connected){
		if (workerThread)
			{
			workerThread->deleteLater();
			while(workerThread->isRunning()){
				workerThread->quit();
				qDebug() << "isRunning?" << workerThread->isFinished();
				QThread::msleep(100);
			}
		}
		if (isoHandler)
	        delete(isoHandler);
        //delete(workerThread);
        qDebug() << "THREAD Gone!";

        for (int i=0; i<NUM_FUTURE_CTX; i++){
            for (int k=0; k<NUM_ISO_ENDPOINTS; k++){
				if (isoCtx[k][i])
	                libusb_free_transfer(isoCtx[k][i]);
            }
        }
        qDebug() << "Transfers freed.";
    }

    if(handle != NULL){
    libusb_release_interface(handle, 0);
        qDebug() << "Interface released";
        libusb_close(handle);
        qDebug() << "Device Closed";
    }
    if(ctx != NULL){
        libusb_exit(ctx);
        qDebug() << "Libusb exited";
    }
    //unixDriverDeleteMutex.unlock();
    qDebug() << "unixUsbDriver destructor completed!\n\n";
}

unsigned char unixUsbDriver::usbInit(unsigned long VIDin, unsigned long PIDin){
    qDebug() << "Entering unixUsbDriver::usbInit";

    int error;
    //Should only run once.
    if(ctx == NULL){
        error = libusb_init(&ctx);
        if(error){
            qDebug() << "libusb_init FAILED";
            return 1;
        } else qDebug() << "Libusb context initialised";

        libusb_set_debug(ctx, 3);
    }

    if(handle == NULL){
        handle = libusb_open_device_with_vid_pid(ctx, VIDin, PIDin);
        if(handle==NULL){
            qDebug() << "DEVICE NOT FOUND";
            return 1;
        }
        qDebug() << "Device found!!";
    }

    qDebug() << (libusb_kernel_driver_active(handle, 0) ? "KERNEL DRIVER ACTIVE" : "KERNEL DRIVER INACTIVE");
    if(libusb_kernel_driver_active(handle, 0)){
        libusb_detach_kernel_driver(handle, 0);
    }
    error = libusb_claim_interface(handle, 0);
    if(error){
        qDebug() << "libusb_claim_interface FAILED";
        qDebug() << "ERROR" << error << libusb_error_name(error);
        handle = NULL;
        return 1;
    } else qDebug() << "Interface claimed!";

    return 0;
}

void unixUsbDriver::usbSendControl(uint8_t RequestType, uint8_t Request, uint16_t Value, uint16_t Index, uint16_t Length, unsigned char *LDATA){
    //qDebug("Sending Control packet! 0x%x,\t0x%x,\t%u,\t%u,\t%d,\t%u", RequestType, Request, Value, Index, LDATA, Length);
    unsigned char *controlBuffer;

    if(!connected){
        qDebug() << "Control packet requested before device has connected!";
        return;
    }

    if (LDATA==NULL){
        controlBuffer = inBuffer;
    }
    else controlBuffer = LDATA;

    int error = libusb_control_transfer(handle, RequestType, Request, Value, Index, controlBuffer, Length, 4000);
    if(error<0){
        qDebug() << "Error number:" << error;
        qDebug("unixUsbDriver::usbSendControl FAILED with error %s", libusb_error_name(error));
    } //else qDebug() << "unixUsbDriver::usbSendControl SUCCESS";
    if((error == LIBUSB_ERROR_NO_DEVICE) && (Request!=0xa7)){ //Bootloader Jump won't return; this is expected behaviour.
        qDebug() << "Device not found.  Becoming an hero.";
        connectedStatus(false);
        killMe();
    }
    return;
}

//Callback on iso transfer complete.
static void LIBUSB_CALL isoCallback(struct libusb_transfer * transfer){
    tcBlockMutex.lock();
    //int number = ((tcBlock *)transfer->user_data)->number;
    //bool completed = ((tcBlock *)transfer->user_data)->completed;

    //qDebug() << "CALLBACK" << number;
    //qDebug() << completed;

    if(transfer->status!=LIBUSB_TRANSFER_CANCELLED){
        ((tcBlock *)transfer->user_data)->completed = true;
        ((tcBlock *)transfer->user_data)->timeReceived = QDateTime::currentMSecsSinceEpoch();
    }
    //qDebug() << ((tcBlock *)transfer->user_data)->timeReceived;
    tcBlockMutex.unlock();
    return;
}

int unixUsbDriver::usbIsoInit(void){
    int error;

    for(int n=0;n<NUM_FUTURE_CTX;n++){
        for (unsigned char k=0;k<NUM_ISO_ENDPOINTS;k++){
            isoCtx[k][n] = libusb_alloc_transfer(ISO_PACKETS_PER_CTX);
            transferCompleted[k][n].number = (k * ISO_PACKETS_PER_CTX) + n;
            transferCompleted[k][n].completed = false;
            libusb_fill_iso_transfer(isoCtx[k][n], handle, pipeID[k], dataBuffer[k][n], ISO_PACKET_SIZE*ISO_PACKETS_PER_CTX, ISO_PACKETS_PER_CTX, isoCallback, (void*)&transferCompleted[k][n], 4000);
            libusb_set_iso_packet_lengths(isoCtx[k][n], ISO_PACKET_SIZE);
        }
    }

    for(int n=0;n<NUM_FUTURE_CTX;n++){
        for (unsigned char k=0;k<NUM_ISO_ENDPOINTS;k++){
            error = libusb_submit_transfer(isoCtx[k][n]);
            if(error){
                qDebug() << "libusb_submit_transfer FAILED";
                qDebug() << "ERROR" << libusb_error_name(error);
				return -1;
            } else {
                if(n == 0){
                    qint64 t0;
                    qint64 t = QDateTime::currentMSecsSinceEpoch();
                    if(k==0){
                        t0 = t;
                    }
                    midBufferOffsets[k] = t0 - t;
                    qDebug() << "isoCtx submitted successfully!";
                    qDebug() << "[n, k] = " << n << k;
                    qDebug() << "t = " << t;
                    qDebug() << "Delay = " << 0;//midBufferOffsets[k];
                }
            }
        }
    }

    isoTimer = new QTimer();
    isoTimer->setTimerType(Qt::PreciseTimer);
    isoTimer->start(ISO_TIMER_PERIOD);
    connect(isoTimer, SIGNAL(timeout()), this, SLOT(isoTimerTick()));

    qDebug() << "Setup successful!";

    isoHandler = new worker();
    workerThread = new QThread();

    isoHandler->ctx = ctx;
    isoHandler->moveToThread(workerThread);
    connect(workerThread, SIGNAL(started()), isoHandler, SLOT(handle()));

    workerThread->start();

    qDebug() << "MAIN THREAD ID" << QThread::currentThreadId();
    //QThread::sleep(1);
    qDebug() << "Iso Stack initialised!";
    return 0;
}

void unixUsbDriver::isoTimerTick(void){
    timerCount++;

    char subString[3] = "th";
    if(timerCount%10 == 1) strcpy(subString, "st");
    if(timerCount%10 == 2) strcpy(subString, "nd");
    if(timerCount%10 == 3) strcpy(subString, "rd");
    if((timerCount<20) && (timerCount > 10)) strcpy(subString, "th");

    //qDebug("\n\nThis is the %d%s Tick!", timerCount, subString);

    int n, error, earliest = MAX_OVERLAP;
    qint64 minFrame = 9223372036854775807; //max value for 64 bit signed

    unsigned int packetLength = 0;
    unsigned char* packetPointer;

    tcBlockMutex.lock();
    for (n=0; n<NUM_FUTURE_CTX; n++){
        if(allEndpointsComplete(n)){
            //qDebug("Transfer %d is complete!!", n);
            if(transferCompleted[0][n].timeReceived < minFrame){
                minFrame = transferCompleted[0][n].timeReceived;
                earliest = n;
            }
        }
    }
    if (earliest == MAX_OVERLAP){
        tcBlockMutex.unlock();
        //qDebug() << "Returning early...";
        return;
    }

    //qDebug() << "Processing Ctx" << earliest;


    //Swap the buffers so that we override the oldest one.
    for (unsigned char k = 0; k< NUM_ISO_ENDPOINTS; k++){
        unsigned char* temp = midBuffer_prev[k];
        midBuffer_prev[k] = midBuffer_current[k];
        midBuffer_current[k] = temp;
    }

    //Copy iso data into mid-buffer
    for(int i=0;i<isoCtx[0][earliest]->num_iso_packets;i++){
        for(unsigned char k=0; k<NUM_ISO_ENDPOINTS;k++){
            packetPointer = libusb_get_iso_packet_buffer_simple(isoCtx[k][earliest], i);
            //qDebug() << packetLength;
            memcpy(&(midBuffer_current[k][packetLength]), packetPointer, isoCtx[k][earliest]->iso_packet_desc[i].actual_length);
        }
        packetLength += ISO_PACKET_SIZE;
    }

    packetLength = 0; //I don't really know why I use the same variable twice but hey
    //Read out from mid-buffer to out-buffer
    unsigned char *srcPtr;
    qint64 current_offset;
    for(int i=0;i<isoCtx[0][earliest]->num_iso_packets;i++){
        for(unsigned char k=0; k<NUM_ISO_ENDPOINTS;k++){
            current_offset = midBufferOffsets[k] + i;
            if(current_offset >= 0){
                srcPtr = &midBuffer_current[k][ISO_PACKET_SIZE * current_offset];
            }else{
                srcPtr = &midBuffer_prev[k][ISO_PACKET_SIZE * (ISO_PACKETS_PER_CTX + current_offset)];
            }
            memcpy(&(outBuffers[currentWriteBuffer][packetLength]), srcPtr, ISO_PACKET_SIZE);
            packetLength += ISO_PACKET_SIZE;
        }
    }

    //qDebug() << "Data copy complete!";

    //Control data for isoDriver
    bufferLengths[currentWriteBuffer] = packetLength;
    currentWriteBuffer = !currentWriteBuffer;

    //Setup next transfer
    for(unsigned char k=0; k<NUM_ISO_ENDPOINTS;k++){
        transferCompleted[k][earliest].completed = false;
        if(shutdownMode){
            error = libusb_cancel_transfer(isoCtx[k][earliest]);
            numCancelled++;
            qDebug() << "Cancelled" << earliest << k;
            qDebug() << "Total Cancelled =" << numCancelled;
            if(numCancelled == (NUM_FUTURE_CTX * NUM_ISO_ENDPOINTS)){
                isoHandler->stopTime = true;
            }
        }else{
            error = libusb_submit_transfer(isoCtx[k][earliest]);
        }
        if(error){
            qDebug() << (shutdownMode ? "libusb_cancel_transfer FAILED" : "libusb_submit_transfer FAILED");
            qDebug() << "ERROR" << libusb_error_name(error);
        } //else qDebug() << "isoCtx submitted successfully!";
    }

    tcBlockMutex.unlock();
    //qDebug() << "Calling upTick()";
    upTick();
   return;
}

char *unixUsbDriver::isoRead(unsigned int *newLength){
    //*(newLength) = 0;
    //return (char*) NULL;
    //qDebug() << "unixUsbDriver::isoRead";
    *(newLength) = bufferLengths[!currentWriteBuffer];
    return (char*) outBuffers[(unsigned char) !currentWriteBuffer];
}

void unixUsbDriver::recoveryTick(void){
    //This should not be called in shutdown mode since it cause double deletion!
    if(!shutdownMode){
        avrDebug();
    }
}

bool unixUsbDriver::allEndpointsComplete(int n){
    //Just tells you if transfers have completed on _all_ iso endpoints for a given value of n.
    for (unsigned char k=0;k<NUM_ISO_ENDPOINTS;k++){
        if(!transferCompleted[k][n].completed){
            return false;
        }
    }
    return true;
}

void unixUsbDriver::shutdownProcedure(){
    shutdownMode = true;
    QTimer::singleShot(100, this, SLOT(backupCleanup()));
}

//On physical disconnect, isoTimerTick will not assert stopTime.  Hence this duct-tape function.
void unixUsbDriver::backupCleanup(){
	if (isoHandler)
	    isoHandler->stopTime = true;
}

int unixUsbDriver::flashFirmware(void){
#ifndef PLATFORM_ANDROID
    qDebug() << "\n\n\n\n\n\n\n\nFIRMWARE MISMATCH!!!!  FLASHING....\n\n\n\n\n\n\n";

    signalFirmwareFlash();
    QApplication::processEvents();

    //Go to bootloader mode
    bootloaderJump();
    qDebug() << "BA94 closed";

    //Get location of firmware file
#if defined(PLATFORM_LINUX)
    QString firmware_path = QString::asprintf("firmware/labrafirm_%04x_%02x.hex", EXPECTED_FIRMWARE_VERSION, DEFINED_EXPECTED_VARIANT);
    firmware_path = QStandardPaths::locate(QStandardPaths::AppDataLocation, firmware_path);
#else
    QString firmware_path = QCoreApplication::applicationDirPath();
    firmware_path.append(QString::asprintf("/firmware/labrafirm_%04x_%02x.hex", EXPECTED_FIRMWARE_VERSION, DEFINED_EXPECTED_VARIANT));
#endif
    qDebug() << "FLASHING" << firmware_path;

    //Set up interface to dfuprog
    int exit_code;
    char command[256];

    //Run stage 1, until there's a success
    do {
        QThread::msleep(200);
        snprintf(command, sizeof command, "dfu-programmer atxmega32a4u erase --force");
        exit_code = dfuprog_virtual_cmd(command);
        QApplication::processEvents();
    } while (exit_code);

    //Run stage 2
    snprintf(command, sizeof command, "dfu-programmer atxmega32a4u flash %s", qPrintable(firmware_path));
    exit_code = dfuprog_virtual_cmd(command);
    if (exit_code) {
        return exit_code+200;
    }

    //Run stage 3
    snprintf(command, sizeof command, "dfu-programmer atxmega32a4u launch");
    exit_code = dfuprog_virtual_cmd(command);
    if (exit_code) {
       return exit_code+300;
    }

    //Run stage 4 - double launch to clear the eeprom flag from bootloaderJump.
    do {
        QThread::msleep(200);
        snprintf(command, sizeof command, "dfu-programmer atxmega32a4u launch");
        exit_code = dfuprog_virtual_cmd(command);
        QApplication::processEvents();
    } while (exit_code);

    libusb_release_interface(handle, 0);
    qDebug() << "Interface released";
    libusb_close(handle);
    qDebug() << "Device Closed";
    libusb_exit(ctx);
    qDebug() << "Libusb exited";
    connected = false;
    handle = NULL;
    ctx = NULL;

    return 0;
#endif
}


void unixUsbDriver::manualFirmwareRecovery(void){
#ifndef PLATFORM_ANDROID
    //Get location of firmware file
#if defined(PLATFORM_LINUX)
    QString firmware_path = QString::asprintf("firmware/labrafirm_%04x_%02x.hex", EXPECTED_FIRMWARE_VERSION, DEFINED_EXPECTED_VARIANT);
    firmware_path = QStandardPaths::locate(QStandardPaths::AppDataLocation, firmware_path);
#else
    QString firmware_path = QCoreApplication::applicationDirPath();
    firmware_path.append(QString::asprintf("/firmware/labrafirm_%04x_%02x.hex", EXPECTED_FIRMWARE_VERSION, DEFINED_EXPECTED_VARIANT));
#endif

    //Set up interface to dfuprog
    int exit_code;
    char command[256];

    //Intro
    QMessageBox manualFirmwareMessages;
    manualFirmwareMessages.setText("Welcome to the firmware recovery wizard.\nThis tool will attempt various steps to troubleshoot a board with connection issues.\n\nPress OK to continue.");
    manualFirmwareMessages.exec();

    //Hello, this is IT, can you try turning it off and on again?
    manualFirmwareMessages.setText("Before continuing, please disconnect and reconnect your Labrador board, then wait 10 seconds.\n\nAlso ensure that there are no other instances of the Labrador software running on this machine.");
    manualFirmwareMessages.exec();
    manualFirmwareMessages.setStandardButtons(QMessageBox::Yes|QMessageBox::No);
    manualFirmwareMessages.setText("Did that fix things?");
    int messageBoxReturn = manualFirmwareMessages.exec();
    manualFirmwareMessages.setStandardButtons(QMessageBox::Ok);
    if (messageBoxReturn == QMessageBox::Yes) {
        manualFirmwareMessages.setText("Awesome!  Have fun!");
        manualFirmwareMessages.exec();
        return;
    }

    //Real troubleshooting begins here.....

    //USB Problems.
    if (connected) {
        manualFirmwareMessages.setText("It seems like your board is already connected and configured correctly.\n\nIf your board is not functioning correctly, this indicates that there is an issue with the USB driver.\n\nLet's go through some manual troubleshooting steps.");
        manualFirmwareMessages.exec();
        manualFirmwareMessages.setText("There are two main possibilities:\n\n - Your USB Controller does not support Isochronous mode at USB 2.0 FS\n - Another device is competing with Labrador for bandwidth.");
        manualFirmwareMessages.exec();
        manualFirmwareMessages.setText("If Labrador is connected to a USB 2.0 port, unplug it and connect it to a USB3 port.\n\nIf it's in a USB3 port, try connecting it to a USB 2.0 port.\n\nIf you have other USB devices, such as a keyboard and mouse, ensure they're connected to a port of a different type than Labrador (e.g., if your mouse and keyboard are in USB 2.0 ports, try putting Labrador in a USB 3.0 port).\n\nIf you're not sure which is which, USB3 ports are usually blue on the inside.");
        manualFirmwareMessages.exec();
        manualFirmwareMessages.setText("If you have a spare USB hub, connect Labrador (and only Labrador) to the hub.\n\nThis will usually result in a huge reduction in bandwidth required to communicate with Labrador, since a Hi-Speed hub reads from Labrador at 12MHz, but transmits upstream to the host at 480MHz.\n\n(If the host is connected to Labrador directly, it is slowed down to 12MHz!) ");
        manualFirmwareMessages.exec();
        manualFirmwareMessages.setText("If it's still not working, please disconnect all USB devices from your machine, then one by one, insert Labrador into each USB port on your machine until it starts working.");
        manualFirmwareMessages.exec();
        manualFirmwareMessages.setText("If that doesn't fix it, please open an issue on github.com/espotek-org/labrador, or contact me at admin@espotek.com.");
        manualFirmwareMessages.exec();
        return;
    } else {
        snprintf(command, sizeof command, "dfu-programmer atxmega32a4u launch");
        exit_code = dfuprog_virtual_cmd(command);
        manualFirmwareMessages.setText("No Labrador board could be detected.\n\nIt's possible that you're stuck in booloader mode.\n\nI've attempted to launch the firmware manually.");
        manualFirmwareMessages.exec();
        if (exit_code) {
            manualFirmwareMessages.setText("Command failed.  This usually means that no device is detected.\n\nPlease Ensure that the cable you're using can carry data (for example, by using it to transfer data to your phone).\n\nSome cables are for charging only, and not physically contain data lines.\n\nAlso note that the red light on the Labrador board is a power indicator for the PSU output pins.\nIt will turn on even if no data lines are present.");
            manualFirmwareMessages.exec();
            return;
        }
        //Firmware launch failed, but bootloader preset
        if (!connected) {
            snprintf(command, sizeof command, "dfu-programmer atxmega32a4u erase --force");
            exit_code = dfuprog_virtual_cmd(command);
            snprintf(command, sizeof command, "dfu-programmer atxmega32a4u flash %s", qPrintable(firmware_path));
            exit_code += dfuprog_virtual_cmd(command);
            manualFirmwareMessages.setText("The bootloader is present, but firmware launch failed.  I've attempted to reprogram it.");
            manualFirmwareMessages.exec();

            if (!exit_code) { //Reprogramming was successful, but board is still in bootloader mode.
                snprintf(command, sizeof command, "dfu-programmer atxmega32a4u launch");
                exit_code = dfuprog_virtual_cmd(command);
                manualFirmwareMessages.setText("Reprogramming was successful!  Attempting to launch the board.\n\nIf it does not start working immediately, please wait 10 seconds and then reconnect the board.");
                manualFirmwareMessages.exec();
            } else { //Programming failed.
                manualFirmwareMessages.setText("Automatic Reprogramming failed.\n\nPlease contact me at admin@espotek.com for further support.");
                manualFirmwareMessages.exec();
            }

            return;
        }
    }

#endif
}
