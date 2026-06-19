#include "cursorenabler.h"

cursorEnabler::cursorEnabler(QWidget *parent) : QLabel(parent)
{
    this->setVisible(0);
}

void cursorEnabler::setTurnedOn(bool enabled){
    m_turnedOn = enabled;
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
