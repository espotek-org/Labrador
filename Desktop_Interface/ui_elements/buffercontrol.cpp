#include "buffercontrol.h"

bufferControl::bufferControl(QWidget *parent) : QLabel(parent)
{
    //refreshImage();
}

void bufferControl::refreshImage(void){
    //qDebug() << "Trying to show bitmap " << numBuffers;

    switch(numBuffers){
    case 0:
        bufferImage = QPixmap(":/bitmap/buffer_0.png");
        break;
    case 1:
        bufferImage = QPixmap(":/bitmap/buffer_1.png");
        break;
    case 2:
        bufferImage = QPixmap(":/bitmap/buffer_2.png");
        break;
    }

    if(bufferImage.isNull()) qFatal("Resource not found\n");

    this->setPixmap(bufferImage);
    //qDebug() << this->text();
    this->show();
    return;
}

void bufferControl::scopeIn_CH1(bool state){  //What about DSR!?
    scopeState_CH1 = state;

    if (scopeState_CH2){ //Implicitly state is false
        scopeState_CH2 = false;
        //updateBuffer(0);  -  Causes issues because the uncheck below called scopeIn_CH2 (but only when toggle)!!!
        scopeUncheck(0);
    }

    //Turn off the DSR when CH1 is disabled.
    if(!state && !scopeDsrDisableOverride){
        scopeDsrUncheck(0);
    }

    scopeDsrOut(state);
    scopeOut_CH2(state);

    updateBuffer(state,1); // Do this last to ensure anything accidentally enabled is immediately switched off
    qDebug() << "scopeIn_CH1" << state;
    updateMode();
}

void bufferControl::scopeIn_CH2(bool state){
    scopeState_CH2 = state;

    updateBuffer(state,1);
    qDebug() << "scopeIn_CH2" << state;
    updateMode();
}

void bufferControl::scopeDsrIn(bool state){
    scopeDsrState = state;

    updateBuffer(state,1);
    qDebug() << "scopeDsrIn" << state;
    updateMode();
}

/*void bufferControl::signalGenIn(bool state){
    signalGenState = state;

    updateBuffer(state,1);
    qDebug() << "signalGenIn" << state;
    updateMode();
}*/

void bufferControl::busSnifferIn_CH1(bool state){
    busSnifferState_CH1 = state;

    if (busSnifferState_CH2){ //Implicitly state is false
        busSnifferState_CH2 = false;
        //updateBuffer(0);  -  Causes issues because the uncheck below called scopeIn_CH2 (but only when toggle)!!!
        busSnifferUncheck(0);
    }

    //Signal Gen CH2 doesn't work with bus sniffer.
    signalGenOut(!state);

    busSnifferOut_CH2(state);

    updateBuffer(state,1);
    qDebug() << "busSnifferIn_CH1" << state;
    updateMode();
}

void bufferControl::busSnifferIn_CH2(bool state){
    busSnifferState_CH2 = state;

    updateBuffer(state,1);
    qDebug() << "busSnifferIn_CH2" << state;
    updateMode();
}

void bufferControl::multimeterIn(bool state){
    multimeterState = state;

    updateBuffer(state,2);
    qDebug() << "multimeterIn" << state;
    updateMode();
}



void bufferControl::updateBuffer(bool decrement, int amount){
    if(decrement){
        numBuffers-=amount;
    }
    else{
        numBuffers+=amount;
    }
    refreshImage();

    //Write new state
    switch(numBuffers){
        case 0:
            if(scopeState_CH1 == false) scopeOut_CH1(0);
            if(scopeState_CH2 == false) scopeOut_CH2(0);
            if(scopeDsrState == false) scopeDsrOut(0);
            //if(signalGenState == false) signalGenOut(0);
            if(busSnifferState_CH1 == false) busSnifferOut_CH1(0);
            if(busSnifferState_CH2 == false) busSnifferOut_CH2(0);
            if(multimeterState == false) multimeterOut(0);
            break;
        case 1:
            scopeOut_CH1(1);
            if(scopeState_CH1 == true){
                scopeDsrOut(1);
                scopeOut_CH2(1);
            }
            //signalGenOut(1);
            busSnifferOut_CH1(1);
            if(busSnifferState_CH1 == true){
                busSnifferOut_CH2(1);
            }

            //busSnifferOut_CH2(1);
            multimeterOut(0);
        break;
        case 2:
            scopeOut_CH1(1);
            //scopeOut_CH2(1);
            if(scopeState_CH1 == true) scopeDsrOut(1);
            //signalGenOut(1);
            busSnifferOut_CH1(1);
            //busSnifferOut_CH2(1);
            multimeterOut(1);
            break;
    default:
        qFatal("numBuffers is not equal to 0, 1 or 2");
    }

    if(scopeDsrDisableOverride){
        scopeDsrOut(0);
    }
}

void bufferControl::digIn_CH1(bool state){
    unsigned char mask = 0x01;
    if(state){
        digState |= mask;
    }
    else{
        digState &= (unsigned char) 0xff - mask;
    }
    updateDig(digState);
}

void bufferControl::digIn_CH2(bool state){
    unsigned char mask = 0x02;
    if(state){
        digState |= mask;
    }
    else{
        digState &= (unsigned char) 0xff - mask;
    }    updateDig(digState);
}

void bufferControl::digIn_CH3(bool state){
    unsigned char mask = 0x04;
    if(state){
        digState |= mask;
    }
    else{
        digState &= (unsigned char) 0xff - mask;
    }    updateDig(digState);
}

void bufferControl::digIn_CH4(bool state){
    unsigned char mask = 0x08;
    if(state){
        digState |= mask;
    }
    else{
        digState &= (unsigned char) 0xff - mask;
    }    updateDig(digState);
}

void bufferControl::updateMode(void){
    if(multimeterState){
        modeChange(7);
        qDebug() << "Changed to mode 7";
        return;
    }
    if(scopeDsrState){
        modeChange(6);
        qDebug() << "Changed to mode 6";
        return;
    }

    if (busSnifferState_CH2){
        modeChange(4);
        qDebug() << "Changed to mode 4";
        return;
    }
    if(busSnifferState_CH1 && scopeState_CH1){
        modeChange(1);
        qDebug() << "Changed to mode 1";
        return;
    }
    if (scopeState_CH2){
        modeChange(2);
        qDebug() << "Changed to mode 2";
        return;
    }
    if(busSnifferState_CH1){
        modeChange(3);
        qDebug() << "Changed to mode 3";
        return;
    }
    if(scopeState_CH1){
        modeChange(0);
        qDebug() << "Changed to mode 0";
        return;
    }

    modeChange(5);
    qDebug() << "Changed to mode 5";
    return;

}

void bufferControl::poke(void){
    updateDig(digState);
    updateMode();
    updateBuffer(0,0);
}

