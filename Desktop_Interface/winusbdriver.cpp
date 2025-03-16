#include "winusbdriver.h"
#include <QApplication>
#include <qprocess.h>
#include <QMessageBox>

#define SLEEP_DIVIDER 16

winUsbDriver::winUsbDriver(QWidget *parent) : genericUsbDriver(parent)
{
}

winUsbDriver::~winUsbDriver(void){

    //Like any decent destructor, this just frees resources

    qDebug() << "\n\nwinUsbDriver destructor ran!";
    for (unsigned char k=0; k<NUM_ISO_ENDPOINTS; k++){
        for(int n=0;n<NUM_FUTURE_CTX;n++){
            IsoK_Free(isoCtx[k][n]);
        }
        for(int i=0;i<MAX_OVERLAP;i++){
            OvlK_Release(ovlkHandle[k][i]);
        }
        UsbK_FlushPipe(handle, pipeID[k]);
        UsbK_AbortPipe(handle, pipeID[k]);
    }
    OvlK_Free(ovlPool);
    free(outBuffers[0]);
    free(outBuffers[1]);
    UsbK_Free(handle);
}

unsigned char winUsbDriver::usbInit(unsigned long VIDin, unsigned long PIDin){

    //This function gets you a USB handle, as well as any other data needed to send a control transfer over USB.
    //You should be able to call usbSendControl() immediately after this function has returned success!!

    unsigned char success;
    KLST_DEVINFO_HANDLE deviceInfo = NULL;
    UINT deviceCount = 0;
    DWORD ec = ERROR_SUCCESS;
    KLST_HANDLE deviceList = NULL;

    //List libusbk devices connected
    if (!LstK_Init(&deviceList, (KLST_FLAG) 0))	{
        qDebug("Error initializing device list");
        return 1;
    } //else qDebug() << "Device List initialised!";

    /*
    LstK_Count(deviceList, &deviceCount);
    if (!deviceCount) {
        qDebug("Device list empty");
        LstK_Free(deviceList);	// If LstK_Init returns TRUE, the list must be freed.
        return 0;
    } //else qDebug() << "Device Count initialised!";
*/

    //Look for Labrador!
    LstK_FindByVidPid(deviceList, VIDin, PIDin, &deviceInfo);
    LstK_Free(deviceList);
    if (deviceInfo == NULL){
        qDebug("Could not find device VID = %04X, PID = %04X", VIDin, PIDin);
        return 2;
    }

    //Open Labrador!!
    success = UsbK_Init(&handle, deviceInfo);
    if (!success){
        ec = GetLastError();
        qDebug("UsbK_Init failed. ErrorCode: %08Xh", ec);
        return 3;
    } else qDebug("Device opened successfully!");

    if (handle == NULL){
        return 4; //Unkown error, only exists on 32 bit???
    }

    return 0;
}

void winUsbDriver::usbSendControl(uint8_t RequestType, uint8_t Request, uint16_t Value, uint16_t Index, uint16_t Length, unsigned char *LDATA){

    //This function sends a control transfer over USB.
    //LDATA is a pointer to the buffer that is going to be sent to the device.  If no buffer is to be sent, LDATA should be NULL (and any implementation should be able to handle this case!!).
    //The Linux implementation of this is trivially simple.  Have a look!

    //////////////////////////////////////////////////////////////////////////////////////////
    //IF YOU'RE SEEING AN ERROR, CHECK THAT REQUESTTYPE AND REQUEST ARE FORMATTED AS HEX
    //////////////////////////////////////////////////////////////////////////////////////////

    WINUSB_SETUP_PACKET setupPacket;
    unsigned char controlSuccess;
    UINT bytesTransferred = 0;
    unsigned char *controlBuffer;
    DWORD errorCode = ERROR_SUCCESS;

    if(!connected){
        qDebug() << "Not connected.  Ignoring Control Request!!!";
    }
    //Error checking
    if (handle==NULL){
        qDebug("Null handle error in usbSendControl");
        return;
    }

    //Fill the setup packet
    setupPacket.RequestType = RequestType;
    setupPacket.Request = Request;
    setupPacket.Value = Value;
    setupPacket.Index = Index;
    setupPacket.Length = Length;

    //Check for case of null LDATA
    if (LDATA==NULL){
        controlBuffer = inBuffer;
    }
    else controlBuffer = LDATA;

    //Send the packet
    controlSuccess = UsbK_ControlTransfer(handle, setupPacket, controlBuffer, setupPacket.Length, &bytesTransferred, NULL);
    if (controlSuccess) {
        qDebug("%d BYTES TRANSFERRED", bytesTransferred);
    }
    else{
        errorCode = GetLastError();
        qDebug() << "UsbK_ControlTransfer failed with error code" << errorCode;
        if(errorCode == 170){ //Device not connected?? (According to test)
            killMe();
        }
    }
}

int  winUsbDriver::usbIsoInit(void){
    //Iso is slightly less easy than plain old USB.
    //You must set up NUM_FUTURE_CTX iso transfers, with each transfer containing ISO_PACKETS_PER_CTX isochronous packets.
    //These transactions are numbered by n = 0,1,2,3...NUM_FUTURE_CTX-1.  Transfer n should read data into dataBuffer[n].

    //Do note that current implementations don't support changing FPS at runtime.  Some changes will need to be made to enable this (perhaps taking NUM_FUTURE_CTX and ISO_PACKETS_PER_CTX as inputs that the user can change??)


    int n;
    bool success;
    DWORD errorCode = ERROR_SUCCESS;

    //Setting up overlapped I/O.  It's easier than threading on Windows.
    success = OvlK_Init(&ovlPool, handle, MAX_OVERLAP, (KOVL_POOL_FLAG) 0);
    if(!success){
        errorCode = GetLastError();
        qDebug() << "OvlK_Init failed with error code" << errorCode;
        return -1;
    }
    for (unsigned char k=0;k<NUM_ISO_ENDPOINTS;k++){
        success = UsbK_ResetPipe(handle, pipeID[k]);
        if(!success){
            errorCode = GetLastError();
            qDebug() << "UsbK_ResetPipe failed with error code" << errorCode;
            return -2;
        }
    }

    //Filling the transfer contexts
    for(n=0;n<NUM_FUTURE_CTX;n++){
        for (unsigned char k=0;k<NUM_ISO_ENDPOINTS;k++){
            //if(n%2){
            //    QThread::msleep(1); //Wait for next tick after sending two frames.  Still possibility of failure during 100% CPU condition, but risk greatly mitigated.
            //}
            success = IsoK_Init(&isoCtx[k][n], ISO_PACKETS_PER_CTX, 0);
            if(!success){
                errorCode = GetLastError();
                qDebug() << "IsoK_Init failed with error code" << errorCode;
                qDebug() << "n =" << n;
                return -3;
            }

            success = IsoK_SetPackets(isoCtx[k][n], ISO_PACKET_SIZE);
            if(!success){
                errorCode = GetLastError();
                qDebug() << "IsoK_SetPackets failed with error code" << errorCode;
                qDebug() << "n =" << n;
                return -4;
            }

            success = OvlK_Acquire(&ovlkHandle[k][n], ovlPool);
            if(!success){
                errorCode = GetLastError();
                qDebug() << "OvlK_Acquire failed with error code" << errorCode;
                qDebug() << "n =" << n;
                return -5;
            }

            //Sending the transfer requests
            success = UsbK_IsoReadPipe(handle, pipeID[k], dataBuffer[k][n], sizeof(dataBuffer[k][n]), (LPOVERLAPPED) ovlkHandle[k][n], isoCtx[k][n]);
            //qDebug() << "sizeof(dataBuffer[k][n]) = " << sizeof(dataBuffer[k][n]);
        }
    }

    //Setting up isoTimer.  This will call isoTimerTick every ISO_TIMER_PERIOD milliseconds.
    isoTimer = new QTimer();
    isoTimer->setTimerType(Qt::PreciseTimer);
    isoTimer->start(ISO_TIMER_PERIOD);
    connect(isoTimer, SIGNAL(timeout()), this, SLOT(isoTimerTick()));

    qDebug() << "Setup successful!";
    return 0;
}

void winUsbDriver::isoTimerTick(void){
    //This function is called every ISO_TIMER_PERIOD milliseconds, after usbIsoInit() has run.
    //It should check if a transfer is complete, then copy the ___earliest___ transfer into the appropriate outBuffer, as well as set appropriate bufferLengths.
    //Once this is complete, it should resubmit the transfer that it read the data from.
    //Finally, it should signal upTick() so that isoDriver knows it can draw a new frame.

    timerCount++;
    char subString[3] = "th";
    if(timerCount%10 == 1) strcpy(subString, "st");
    if(timerCount%10 == 2) strcpy(subString, "nd");
    if(timerCount%10 == 3) strcpy(subString, "rd");
    if((timerCount<20) && (timerCount > 10)) strcpy(subString, "th");

    //qDebug("\n\nThis is the %d%s Tick!", timerCount, subString);

    bool success;
    DWORD errorCode = ERROR_SUCCESS;
    int n, earliest = MAX_OVERLAP;
    unsigned int minFrame = 4294967295;
    unsigned int dataBufferOffset;
    unsigned int packetLength = 0;

    //Getting earliest transfer number.
    for (n=0; n<NUM_FUTURE_CTX; n++){
        if(allEndpointsComplete(n)){
            if(isoCtx[0][n]->StartFrame < minFrame){
                minFrame = isoCtx[0][n]->StartFrame;
                earliest = n;
            }
        }
    }

    //qDebug() << n << "is the earliest!";

    if (earliest == MAX_OVERLAP){
        return;
    }

    //Copy the tranfer data into buffer
    for(int i=0;i<isoCtx[0][earliest]->NumberOfPackets;i++){
        for(unsigned char k=0; k<NUM_ISO_ENDPOINTS;k++){
            dataBufferOffset = isoCtx[k][earliest]->IsoPackets[i].Offset;
            memcpy(&(outBuffers[currentWriteBuffer][packetLength]), &dataBuffer[k][earliest][dataBufferOffset], isoCtx[k][earliest]->IsoPackets[i].Length);
            packetLength += isoCtx[k][earliest]->IsoPackets[i].Length;
        }
    }

    //Get the data for isoRead() ready and swap buffers
    bufferLengths[currentWriteBuffer] = packetLength;
    currentWriteBuffer = !currentWriteBuffer;


    //Check for incorrect setup and kill if that were the case.
    UINT ep0frame = isoCtx[0][earliest]->StartFrame;
    UINT epkframe = isoCtx[NUM_ISO_ENDPOINTS-1][earliest]->StartFrame;
    UINT framePhaseError = epkframe - ep0frame;
    if(framePhaseError){
        #ifndef WINDOWS_32_BIT
        qDebug("Frame phase error of %d", framePhaseError);
        killMe();
        #endif
    }


    UINT oldStart;
    //Setup transfer for resubmission
    for(unsigned char k=0; k<NUM_ISO_ENDPOINTS; k++){
        //Apparently reusing before resubmitting is a bad idea???

        /*oldStart = isoCtx[k][earliest]->StartFrame;
        success = IsoK_ReUse(isoCtx[k][earliest]);
        if(!success){
            errorCode = GetLastError();
            qDebug() << "IsoK_Init failed with error code" << errorCode;
            qDebug() << "n =" << n;
            return;
        }
        isoCtx[k][earliest]->StartFrame = 0;

        success = OvlK_ReUse(ovlkHandle[k][earliest]);
        if(!success){
            errorCode = GetLastError();
            qDebug() << "OvlK_ReUse failed with error code" << errorCode;
            qDebug() << "n =" << n;
            return;
        }*/
        //Resubmit the transfer
        success = UsbK_IsoReadPipe(handle, pipeID[k], dataBuffer[k][earliest], sizeof(dataBuffer[k][earliest]), (LPOVERLAPPED) ovlkHandle[k][earliest], isoCtx[k][earliest]);
    }
    //qDebug() << "Resubmitted Ctx #"<< earliest;
    //Signal to isoDriver that it can draw a new frame.
    upTick();
    return;
}

char *winUsbDriver::isoRead(unsigned int *newLength){
    //This will be called almost immediately after the upTick() signal is sent.  Make sure bufferLengths[] abd outBuffers[] are ready!
    *(newLength) = bufferLengths[!currentWriteBuffer];
    return (char*) outBuffers[(unsigned char) !currentWriteBuffer];
}

bool winUsbDriver::allEndpointsComplete(int n){
    //Just tells you if transfers have completed on _all_ iso endpoints for a given value of n.
    for (unsigned char k=0;k<NUM_ISO_ENDPOINTS;k++){
        if(!OvlK_IsComplete(ovlkHandle[k][n])){
            return false;
        }
    }
    return true;
}

void winUsbDriver::recoveryTick(){
    //Blind AVR Debug (Doesn't Print)
    usbSendControl(0xc0, 0xa0, 0, 0, sizeof(unified_debug), NULL);
    return;
}

void winUsbDriver::shutdownProcedure(){
    return;
}

int winUsbDriver::flashFirmware(void){
    char fname[64];
    qDebug() << "\n\n\n\n\n\n\n\nFIRMWARE MISMATCH!!!!  FLASHING....\n\n\n\n\n\n\n";
    sprintf(fname, "labrafirm_%04x_%02x.hex", EXPECTED_FIRMWARE_VERSION, DEFINED_EXPECTED_VARIANT);
    qDebug() << "FLASHING " << fname;

    signalFirmwareFlash();
    QApplication::processEvents();


    //Go to bootloader mode
    bootloaderJump();

    //Set up interface to dfuprog
    QString dfuprog_location = QCoreApplication::applicationDirPath();
    dfuprog_location.append("/firmware/dfu-programmer");
    QString file_location = QCoreApplication::applicationDirPath();
    file_location.append("/firmware/");
    file_location.append(fname);
    QProcess dfu_exe;
    QStringList args_stage1;
    args_stage1 << "atxmega32a4u" << "erase" << "--force";
    QStringList args_stage2;
    args_stage2 << "atxmega32a4u" << "flash" << file_location;
    QStringList args_stage3;
    args_stage3 << "atxmega32a4u" << "launch";
    QStringList args_stage4;
    args_stage4 << "atxmega32a4u" << "launch";

    bool atxmega32a4u_connected = false;
    //Run stage 1, until there's a success
    while(!atxmega32a4u_connected){
        QThread::msleep(200);
        dfu_exe.start(dfuprog_location, args_stage1);
        dfu_exe.waitForFinished(-1);
        qDebug() << "stdio_stage1" << dfu_exe.readAllStandardOutput();
        qDebug() << "sterr_stage1" << dfu_exe.readAllStandardError();
        qDebug() << "EXIT_CODE stage1" << dfu_exe.exitCode();
        atxmega32a4u_connected = !dfu_exe.exitCode();
        /*if(dfu_exe.exitCode()){
            return dfu_exe.exitCode()+100;
        }*/
        QApplication::processEvents();
    }


    //Run stage 2
    dfu_exe.start(dfuprog_location, args_stage2);
    dfu_exe.waitForFinished(-1);
    qDebug() << "stdio_stage2" << dfu_exe.readAllStandardOutput();
    qDebug() << "sterr_stage2" << dfu_exe.readAllStandardError();
    qDebug() << "EXIT_CODE stage2" << dfu_exe.exitCode();
    if(dfu_exe.exitCode()){
        return dfu_exe.exitCode()+200;
    }

    //Run stage 3
    dfu_exe.start(dfuprog_location, args_stage3);
    dfu_exe.waitForFinished(-1);
    qDebug() << "stdio_stage3" << dfu_exe.readAllStandardOutput();
    qDebug() << "sterr_stage3" << dfu_exe.readAllStandardError();
    qDebug() << "EXIT_CODE stage3" << dfu_exe.exitCode();
    if(dfu_exe.exitCode()){
        return dfu_exe.exitCode()+300;
    }

    //Run stage 4 - double launch to clear the eeprom flag from bootloaderJump.
    //Connect back to labrador
    while(atxmega32a4u_connected){
        QThread::msleep(200);
        dfu_exe.start(dfuprog_location, args_stage4);
        dfu_exe.waitForFinished(-1);
        qDebug() << "stdio_stage4" << dfu_exe.readAllStandardOutput();
        qDebug() << "sterr_stage4" << dfu_exe.readAllStandardError();
        qDebug() << "EXIT_CODE stage4" << dfu_exe.exitCode();
        atxmega32a4u_connected = (dfu_exe.exitCode()!=0);
        QApplication::processEvents();
    }


    return 0;
}

void winUsbDriver::manualFirmwareRecovery(void){
    //Get location of firmware file
    char fname[128];
    sprintf(fname, "/firmware/labrafirm_%04x_%02x.hex", EXPECTED_FIRMWARE_VERSION, DEFINED_EXPECTED_VARIANT);

    QString file_location = QCoreApplication::applicationDirPath();
    file_location.append(fname);

    //Set up interface to dfuprog
    QString dfuprog_location = QCoreApplication::applicationDirPath();
    dfuprog_location.append("/firmware/dfu-programmer");
    QProcess dfu_exe;

    QStringList leaveBootloaderCommand;
    leaveBootloaderCommand << "atxmega32a4u" << "launch";
    int exit_code;
    QStringList eraseCommand;
    eraseCommand << "atxmega32a4u" << "erase" << "--force";
    QStringList flashCommand;
    flashCommand << "atxmega32a4u" << "flash" << file_location;

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
    if(connected){
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
        qDebug() << "Attempting to leave bootloader!";
        dfu_exe.start(dfuprog_location, leaveBootloaderCommand);
        dfu_exe.waitForFinished(-1);
        exit_code = dfu_exe.exitCode();
        manualFirmwareMessages.setText("No Labrador board could be detected.\n\nIt's possible that you're stuck in booloader mode.\n\nI've attempted to launch the firmware manually.");
        manualFirmwareMessages.exec();
        if(exit_code){
            qDebug("Exit code = %d", exit_code);
            manualFirmwareMessages.setText("Command failed.  This usually means that no device is detected.\n\nPlease Ensure that the cable you're using can carry data (for example, by using it to transfer data to your phone).\n\nSome cables are for charging only, and not physically contain data lines.\n\nAlso note that the red light on the Labrador board is a power indicator for the PSU output pins.\nIt will turn on even if no data lines are present.");
            manualFirmwareMessages.exec();
            return;
        }
        //Firmware launch failed, but bootloader preset
        if(!connected){
            qDebug() << "Attempting to erase!";
            dfu_exe.start(dfuprog_location, eraseCommand);
            dfu_exe.waitForFinished(-1);
            exit_code = dfu_exe.exitCode();

            qDebug("Exit code for erase = %d", exit_code);

            qDebug("Attempting to flash file %s!", file_location.toLocal8Bit().data());
            dfu_exe.start(dfuprog_location, flashCommand);
            dfu_exe.waitForFinished(-1);
            exit_code += dfu_exe.exitCode();

            qDebug("Exit code for flash = %d", exit_code);

            manualFirmwareMessages.setText("The bootloader is present, but firmware launch failed.  I've attempted to reprogram it.");
            manualFirmwareMessages.exec();

            if(!exit_code){            //Reprogramming was successful, but board is still in bootloader mode.
                dfu_exe.start(dfuprog_location, leaveBootloaderCommand);
                dfu_exe.waitForFinished(-1);
                exit_code = dfu_exe.exitCode();
                manualFirmwareMessages.setText("Reprogramming was successful!  Attempting to launch the board.\n\nIf it does not start working immediately, please wait 10 seconds and then reconnect the board.");
                manualFirmwareMessages.exec();
            } else { //Programming failed.
                manualFirmwareMessages.setText("Automatic Reprogramming failed.\n\nPlease try again, making sure you read every message carefully and slowly before pushing 'OK'.\nWindows can take several seconds to detect USB events, so this is sometimes necessary.\n\nIf it's still not programming properly, please contact me at admin@espotek.com for further support.");
                manualFirmwareMessages.exec();
            }

            return;
        }
    }
}
