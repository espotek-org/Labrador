#include "genericusbdriver.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QPalette>

#include <vector>
#include <algorithm>

class GobindarDialog : public QWidget
{
public:
    GobindarDialog();
private:
    QVBoxLayout m_layout;
    QLabel m_largeText;
    QLabel m_smallText;
    QLabel m_image;
};

GobindarDialog::GobindarDialog()
: QWidget()
{
    setWindowFlags(Qt::Window);

    QPalette palette;
    palette.setColor(QPalette::Background, Qt::white);
    setPalette(palette);

    QFont largeFont;
    largeFont.setPointSize(18);
    
    QFont smallFont;
    smallFont.setPointSize(12);

    m_largeText.setText("Sorry to Interrupt!");
    m_largeText.setFont(largeFont);
    m_largeText.setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    m_smallText.setText("\nYour board is misconfigured!\nPlease enable Bootloader mode to repair the issue.\n\nTo do this, connect Digital Out 1 to GND (as shown below), then reconnect the board to your computer.\n");
    m_smallText.setFont(smallFont);
    m_smallText.setAlignment(Qt::AlignTop);

    QPixmap pixmap(":/bitmap/diagram.png");
    m_image.setPixmap(pixmap);
    m_image.setAlignment(Qt::AlignTop);

    m_layout.addWidget(&m_largeText);
    m_layout.addWidget(&m_smallText);
    m_layout.addWidget(&m_image);
    setLayout(&m_layout);

    setGeometry(0, 0, 800, 600);
}

genericUsbDriver::genericUsbDriver(QWidget *parent) : QLabel(parent)
{
    connectedStatus(false);
    qDebug() << "Making USB Driver invisible!!";
    this->hide();

    //Double buffers are used to send the transfers to isoDriver.  outBuffers and bufferLengths store the actual data from each transfer as well as length.  They are read by isoDriver when it calls isoRead().
    outBuffers[0] = (unsigned char *) calloc(ISO_PACKET_SIZE*ISO_PACKETS_PER_CTX*NUM_ISO_ENDPOINTS + 8, 1);
    outBuffers[1] = (unsigned char *) calloc(ISO_PACKET_SIZE*ISO_PACKETS_PER_CTX*NUM_ISO_ENDPOINTS + 8, 1);
    bufferLengths[0] = 0;
    bufferLengths[1] = 0;

    for(int k=0; k<NUM_ISO_ENDPOINTS; k++){
        //qDebug() << k;
        //qDebug() << NUM_ISO_ENDPOINTS;
        pipeID[k] = 0x81+k;
        qDebug() << "pipeID" << k << "=" << pipeID[k];
    }

    connectTimer = new QTimer();
    connectTimer->setTimerType(Qt::PreciseTimer);
    connectTimer->start(USB_RECONNECT_PERIOD);
    connect(connectTimer, SIGNAL(timeout()), this, SLOT(checkConnection()));
    qDebug()<< "Generic Usb Driver setup complete";
	messageBox = new QMessageBox();
}

genericUsbDriver::~genericUsbDriver(void){
    qDebug() << "genericUsbDriver dectructor entering";
    if(connected){
		if (psuTimer)
		{
			psuTimer->stop();
			delete(psuTimer);
		}
		
		if (recoveryTimer)
		{
			recoveryTimer->stop();
			delete(recoveryTimer);
		}

		if (isoTimer)
		{
			isoTimer->stop();
			delete(isoTimer);
		}
    }
    qDebug() << "genericUsbDriver dectructor completed";
}


void genericUsbDriver::setPsu(double voltage){

    qDebug() << "New voltage =" << voltage;
    currentPsuVoltage = voltage;

    //if(deviceMode > 6) qFatal("setPsu is not configured for mode 7!!!");
    double vinp = (voltage - psu_offset)/11;
    double vinn = 0;
    //double vref = 1.65;
    double gainPsu = 1;

    dutyPsu = (int) ((vinp - vinn)/vref * gainPsu * PSU_ADC_TOP);

    qDebug() << "Going to send value " << dutyPsu;
}

void genericUsbDriver::setFunctionGen(functionGen::ChannelID channelID, functionGen::SingleChannelController* fGenControl)
{
        ////////////////////////////
       ////NO RESIZING (YET)!!!////
      ////////////////////////////

        //////////////////////////////////////
       //// CH1 is AUX!!  CH2 is "MAIN"!!////
      //////////////////////////////////////

    //For recalling on crash.
	fGenPtrData[(int)channelID] = fGenControl;

	sendFunctionGenData(channelID);
}

void genericUsbDriver::sendFunctionGenData(functionGen::ChannelID channelID)
{
    //Reading in data
	functionGen::ChannelData channelData = fGenPtrData[(int)channelID]->getData();

    //Triple mode
    if ((channelData.amplitude + channelData.offset) > FGEN_LIMIT)
	{
        channelData.amplitude /= 3.0;
        channelData.offset /= 3.0;
        fGenTriple |= static_cast<uint8_t>(!static_cast<uint8_t>(channelID) + 1);
    }
    else
	{
		fGenTriple &= static_cast<uint8_t>(254 - !static_cast<uint8_t>(channelID));
	}

    //Waveform scaling in V
    channelData.amplitude = (channelData.amplitude * 255) / FGEN_LIMIT;
    channelData.offset = (channelData.offset * 255) / FGEN_LIMIT;
    if (channelData.offset < FGEN_OFFSET)
	{
        if (channelData.amplitude > 5)
            channelData.amplitude -= FGEN_OFFSET;
        else
            channelData.amplitude = 0;
        channelData.offset = FGEN_OFFSET;
    }

#ifdef INVERT_TRIPLE
    unsigned char fGenTemp = 0;
    fGenTemp |= (fGenTriple & 0x01)<<1;
    fGenTemp |= (fGenTriple & 0x02)>>1;
    usbSendControl(0x40, 0xa4, fGenTemp, 0, 0, NULL);
#else
    usbSendControl(0x40, 0xa4, fGenTriple, 0, 0, NULL);
#endif

    // Apply duty cycle to Square waveform
    if(channelData.waveform == "Square")
    {
        int length = channelData.samples.size();
        int dutyCycle = static_cast<int>((channelData.dutyCycle*length)/100);
        for (int i = 0; i < length; ++i)
        {
            if(i < dutyCycle)
                channelData.samples[i] = 255;
            else
                channelData.samples[i] = 0;
        }
    }

	auto applyAmplitudeAndOffset = [&](unsigned char sample) -> unsigned char
	{
		return sample / 255.0 * channelData.amplitude + channelData.offset;
	};

	std::transform(channelData.samples.begin(), channelData.samples.end(),
	               channelData.samples.begin(), // transform in place
	               applyAmplitudeAndOffset);

    //Need to increase size of wave if its freq too high, or too low!
	{
		int shift = 0;
		int newLength = channelData.samples.size();

		while ((newLength >> shift) * channelData.freq > DAC_SPS)
			shift++;

		if (shift != 0)
		{
			channelData.divisibility -= shift;
			newLength >>= shift;

			for (int i = 0; i < newLength; ++i)
				channelData.samples[i] = channelData.samples[i << shift];

			channelData.samples.resize(newLength);
			channelData.samples.shrink_to_fit();

			if (channelData.divisibility <= 0)
				qDebug("genericUsbDriver::setFunctionGen: channel divisibility <= 0 after T-stretching");
		}
	}

    // Timer Setup
    int validClockDivs[7] = {1, 2, 4, 8, 64, 256, 1024};
	auto period = [&](int division) -> int
	{
		return CLOCK_FREQ / (division * channelData.samples.size() * channelData.freq) - 0.5;
	};

	int* clkSettingIt = std::find_if(std::begin(validClockDivs), std::end(validClockDivs),
	                                 [&](int division) -> bool { return period(division) < 65535; });

    int timerPeriod = period(*clkSettingIt);

	// +1 to change from [0:n) to [1:n]
    int clkSetting = std::distance(std::begin(validClockDivs), clkSettingIt) + 1;

    if(deviceMode == 5)
        qDebug("DEVICE IS IN MODE 5");

	
    if (channelID == functionGen::ChannelID::CH2)
    {
		usbSendControl(0x40, 0xa1, timerPeriod, clkSetting, channelData.samples.size(), channelData.samples.data());
    }
    else
    {
		if(channelData.repeat_forever)
			usbSendControl(0x40, 0xa2, timerPeriod, clkSetting, channelData.samples.size(), channelData.samples.data());
		else
			usbSendControl(0x40, 0xb2, timerPeriod, clkSetting, channelData.samples.size(), channelData.samples.data());
    }

    this->sigGenFreqUpdated(channelID, clkSetting, timerPeriod, channelData.samples.size());
    return;

}

void genericUsbDriver::newDig(int digState){
    qDebug() << "newDig";
    digitalPinState = digState;
    usbSendControl(0x40, 0xa6, digState, 0, 0, NULL);
}

/*
void genericUsbDriver::setBufferPtr(bufferControl *newPtr){
    bufferPtr = newPtr;
}
*/

void genericUsbDriver::setDeviceMode(int mode){
    int oldMode = deviceMode;
    deviceMode = mode;
    usbSendControl(0x40, 0xa5, (mode == 5 ? 0 : mode), gainMask, 0, NULL);

    if (fGenPtrData[(int)functionGen::ChannelID::CH1] != NULL)
		sendFunctionGenData(functionGen::ChannelID::CH1);

	if (fGenPtrData[(int)functionGen::ChannelID::CH2] != NULL)
		sendFunctionGenData(functionGen::ChannelID::CH2);

    //switch on new deviceMode!!
    switch(deviceMode){
        case 0:
            if(oldMode > 2) sendClearBuffer(1,0,0);
            setVisible_CH2(0);
            checkXY(0);
            break;
        case 1:
            if(oldMode > 2) sendClearBuffer(1,0,0);
            sendClearBuffer(0,1,0);
            setVisible_CH2(1);
            checkXY(0);
            break;
        case 2:
            if(oldMode > 2) sendClearBuffer(1,0,0);
            sendClearBuffer(0,1,0);
            setVisible_CH2(1);
            break;
        case 3:
            if(oldMode != 4) sendClearBuffer(1,0,0);
            sendClearBuffer(0,1,0);
            setVisible_CH2(0);
            checkXY(0);
            break;
        case 4:
            if(oldMode != 3) sendClearBuffer(1,0,0);
            sendClearBuffer(0,1,0);
            setVisible_CH2(1);
            checkXY(0);
            break;
        case 5:
            setVisible_CH2(0);
            checkXY(0);
            break;
        case 6:
            sendClearBuffer(0,0,1);
            setVisible_CH2(0);
            checkXY(0);
            break;
        case 7:
            sendClearBuffer(1,0,0);
            enableMMTimer();
            checkXY(0);
            break;
        default:
            qFatal("Error in genericUsbDriver::setDeviceMode.  Invalid device mode.");
    }

}

void genericUsbDriver::psuTick(){
    if(dutyTemp == dutyPsu) return;

    if (dutyTemp > dutyPsu){
        if((dutyTemp - dutyPsu)> PSU_STEP){
            dutyTemp -= PSU_STEP;
        } else dutyTemp = dutyPsu;
    }

    if (dutyTemp < dutyPsu){
        if((dutyPsu - dutyTemp)> PSU_STEP){
            dutyTemp += PSU_STEP;
        } else dutyTemp = dutyPsu;
    }

    if ((dutyTemp>106) || (dutyTemp<21)){
        qDebug("PSU DUTY CYCLE of dutyTemp = %d OUT OF RANGE (could underflow on SOF)!!!  ABORTING!!!", dutyTemp);
    }
    usbSendControl(0x40, 0xa3, dutyTemp, 0, 0, NULL);
}

void genericUsbDriver::setGain(double newGain){
    if (newGain == scopeGain) return; //No update!
    gainBuffers(scopeGain/newGain);
    scopeGain = newGain;
    //See XMEGA_AU Manual, page 359.  ADC.CTRL.GAIN.
        if(newGain==0.5) gainMask = 0x07;
        else if (newGain == 1) gainMask = 0x00;
        else if (newGain == 2) gainMask = 0x01;
        else if (newGain == 4) gainMask = 0x02;
        else if (newGain == 8) gainMask = 0x03;
        else if (newGain == 16) gainMask = 0x04;
        else if (newGain == 32) gainMask = 0x05;
        else if (newGain == 64) gainMask = 0x06;
        else qFatal("genericUsbDriver::setGain attempted to set invalid gain value");
    gainMask = gainMask << 2;
    gainMask |= (gainMask << 8);
    /*
     * This bit had to be removed because Android doesn't like log2()
     * if (newGain == 0.5){
        gainMask = 7<<2 | 7<<10;
    }
    else gainMask = (unsigned short)(log2(newGain))<<2 | (unsigned short)(log2(newGain))<<10;
    */
    qDebug("newGain = %f", newGain);
    qDebug("gainMask = %x", gainMask);
    usbSendControl(0x40, 0xa5, deviceMode, gainMask, 0, NULL);
}

void genericUsbDriver::avrDebug(void){
    usbSendControl(0xc0, 0xa0, 0, 0, sizeof(unified_debug), NULL);

    qDebug() << "unified debug is of size" << sizeof(unified_debug);
    /*
#ifndef PLATFORM_ANDROID
    unified_debug *udsPtr = (unified_debug *) inBuffer;
    uint16_t trfcnt0 = (udsPtr->trfcntH0 << 8) + udsPtr->trfcntL0;
    uint16_t trfcnt1 = (udsPtr->trfcntH1 << 8) + udsPtr->trfcntL1;
    uint16_t medianTrfcnt = (udsPtr->medianTrfcntH << 8) + udsPtr->medianTrfcntL;
    uint16_t outOfRange = (udsPtr->outOfRangeH << 8) + udsPtr->outOfRangeL;
    uint16_t counter = (udsPtr->counterH << 8) + udsPtr->counterL;
    uint16_t dma_ch0_cnt = (udsPtr->dma_ch0_cntH << 8) + udsPtr->dma_ch0_cntL;
    uint16_t dma_ch1_cnt = (udsPtr->dma_ch1_cntH << 8) + udsPtr->dma_ch1_cntL;

    qDebug("%s", udsPtr->header);
    qDebug() << "trfcnt0 =" << trfcnt0;
    qDebug() << "trfcnt1 =" << trfcnt1;
    qDebug() << "medianTrfcnt =" << medianTrfcnt;
    qDebug() << "outOfRange = " << outOfRange;
    qDebug() << "counter = " << counter;
    qDebug() << "calValNeg = " << udsPtr->calValNeg;
    qDebug() << "calValPos = " << udsPtr->calValPos;    qDebug() << "CALA = " << udsPtr->CALA;
    qDebug() << "CALB = " << udsPtr->CALB;
    qDebug() << "dma_ch0_cnt = " << dma_ch0_cnt;
    qDebug() << "dma_ch1_cnt = " << dma_ch1_cnt;
#endif
*/
}

void genericUsbDriver::kickstartIso(void){
    qDebug() << "Attempting to kickstart iso...";
    usbSendControl(0x40, 0xaa, 0, 0, 0, NULL);
}

void genericUsbDriver::requestFirmwareVersion(void){
    usbSendControl(0xc0, 0xa8, 0, 0, 2, NULL);
    firmver = *((unsigned short *) inBuffer);
}

void genericUsbDriver::requestFirmwareVariant(void){
    usbSendControl(0xc0, 0xa9, 0, 0, 1, NULL);
    variant = *((unsigned char *) inBuffer);
}

void genericUsbDriver::deGobindarise()
{
    GobindarDialog gobindarDialog;
    gobindarDialog.show();
    QApplication::processEvents();
    flashFirmware();
    gobindarDialog.close();
}

void genericUsbDriver::saveState(int *_out_deviceMode, double *_out_scopeGain, double *_out_currentPsuVoltage, int *_out_digitalPinState){
    *(_out_deviceMode) = deviceMode;
    *(_out_scopeGain) = scopeGain;
    *(_out_currentPsuVoltage) = currentPsuVoltage;
    *(_out_digitalPinState) = digitalPinState;
    return;
}

void genericUsbDriver::checkConnection(){
    //This will connect to the board, then wait one more period before actually starting the stack.

    unsigned char initReturnValue;

    if(!connected){
        connectedStatus(false);
        qDebug() << "CHECKING CONNECTION!";
        initReturnValue = usbInit(BOARD_VID, BOARD_PID);
        connected = !(initReturnValue);

        if(E_BOARD_IN_BOOTLOADER == initReturnValue) {
            qDebug() << "Board found in bootloader mode!!";
            int flashRet = flashFirmware();
            qDebug("flashRet: %d", flashRet);
            connected = false;
            connectTimer->start();
            return;
        }

        if (! connected) {

            bool isGobindar = !(usbInit(BOARD_VID, GOBINDAR_PID));
            if (isGobindar)
            {
                deGobindarise();
            }
        }

        return;
    }
    connectTimer->stop();

    requestFirmwareVersion();
    qDebug("BOARD IS RUNNING FIRMWARE VERSION 0x%04hx", firmver);
    qDebug("EXPECTING FIRMWARE VERSION 0x%04hx", EXPECTED_FIRMWARE_VERSION);
    requestFirmwareVariant();
    qDebug("FIRMWARE VARIANT = 0x%02hx", variant);
    qDebug("EXPECTED VARIANT = 0x%02hx", DEFINED_EXPECTED_VARIANT);

    if((firmver != EXPECTED_FIRMWARE_VERSION) || (variant != DEFINED_EXPECTED_VARIANT)){
        qDebug() << "Unexpected Firmware!!";
        int flashRet = flashFirmware();
        qDebug("flashRet: %d", flashRet);
        connected = false;
        connectTimer->start();
        return;
    }

    qDebug() << "Connecting now!";

    //This is the actual setup code.
    connectTimer->stop();
    delete(connectTimer);

    connectedStatus(true);


    setDeviceMode(deviceMode);
    newDig(digitalPinState);

    int ret = usbIsoInit();
	if (ret != 0)
	{
        messageBox->setText("A USB connection was established, but isochronous communications could not be initialised.<br>This is usually due to bandwidth limitations on the current USB host and can be fixed by moving to a different port.<br>Please see <a href = 'https://github.com/espotek-org/Labrador/wiki/Troubleshooting-Guide#usb-connection-issues-other-platforms'>https://github.com/espotek-org/Labrador/wiki/Troubleshooting-Guide#usb-connection-issues-other-platforms</a>");
        messageBox->exec();
	}

    psuTimer = new QTimer();
    psuTimer->setTimerType(Qt::PreciseTimer);
    psuTimer->start(PSU_PERIOD);
    connect(psuTimer, SIGNAL(timeout()), this, SLOT(psuTick()));

    if(killOnConnect) usbSendControl(0x40, 0xa7, 0, 0, 0, NULL);

    recoveryTimer = new QTimer();
    recoveryTimer->setTimerType(Qt::PreciseTimer);
    recoveryTimer->start(RECOVERY_PERIOD);
    connect(recoveryTimer, SIGNAL(timeout()), this, SLOT(recoveryTick()));
    initialConnectComplete();

    if(!killOnConnect && calibrateOnConnect){
        calibrateMe();
    }
}

void genericUsbDriver::bootloaderJump(){
    usbSendControl(0x40, 0xa7, 1, 0, 0, NULL);
}

