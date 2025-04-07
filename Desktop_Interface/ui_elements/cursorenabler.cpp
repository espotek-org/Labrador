#include "cursorenabler.h"

cursorEnabler::cursorEnabler(QWidget *parent) : QLabel(parent)
{
    this->setVisible(0);
#ifdef PLATFORM_ANDROID
    this->m_turnedOn = false;
#endif
}

void cursorEnabler::setTurnedOn(bool enabled){
    m_turnedOn = enabled;
#ifdef PLATFORM_ANDROID
    this->m_turnedOn = false;
#endif
}

void cursorEnabler::clickDetected(QMouseEvent* event){
    if(m_turnedOn){
        if (event->button() == Qt::LeftButton)
        {
            tickHori(1);
        }
        else if (event->button() == Qt::RightButton)
        {
            tickVert(1);
        }
    }
    passOnSignal(event);
}
