//Known issues:
//Memory leak
//Must be called by main window or labels can disappear.

#include "esposlider.h"

#define MIN(a,b) (((a)<(b))?(a):(b))


espoSlider::espoSlider(QWidget *parent) : QSlider(parent)
{
    windowPointer = parent;
    labelMargin = 3;
}

bool espoSlider::setTickLabel(QString label, int position)
{
    if (position > maxTick()){
            //qDebug() << "Tried to label tick at position " << position << "but ticks range from 0 to " << maxTick();
            return true;
    }
    QLabel * working = addressBook.at(position);
    working->setText(label);
    working->setGeometry(QRect(0, 0, 100, 12));
    //qDebug() << "Set Geometry of QLabel" << working;
    working->show();

    return false;
}

void espoSlider::resizeEvent(QResizeEvent *event){
    this->debug_var++;
    //qDebug() << "move/resize event" << debug_var;
    rearrange();
}

void espoSlider::moveEvent(QMoveEvent *event){
    this->debug_var++;
    //qDebug() << "move/resize event" << debug_var;
    rearrange();
}


void espoSlider::setTickInterval(int ti){
    addressBook.resize(maxTick(ti) + 1, nullptr); //Leaky, but not significantly.  Old qlabels never deleted.
    for (auto &it : addressBook){
        if (!it)
            it = new QLabel(windowPointer);
    }
    QSlider::setTickInterval(ti);
    return;
}

int espoSlider::maxTick(){
    if (this->tickInterval() == 0){
        return 0;
    }
    return (this->maximum() - this-> minimum() + 1) / (this->tickInterval());
}

int espoSlider::maxTick(int ti){
    if (ti == 0){
        return 0;
    }
    return (this->maximum() - this-> minimum() + 1) / ti;
}

void espoSlider::rearrange(){
    QLabel *working;
    int k = 7;
    int c = 5;

    //int left = this->geometry().left();
    int right = this->geometry().right();
    int top = this->geometry().top();
    int bottom = this->geometry().bottom();
    int height = bottom - top;
    int internalHeight = height-c-k;
    int margin = (width()/3) - 6;

    //qDebug() << "left = " << left << "right = " << right << "top = " << top << "bottom = " << bottom << "height = " << height << "internalHeight = " << internalHeight << "margin = " << margin;
    //qDebug() << "maxTick() =" << maxTick();
    if (addressBook.size() > 1)
        for (int i=addressBook.size()-1; i>0; i--){
            working = addressBook.at(i);
            working->setGeometry(QRect(right-margin+labelMargin, top + c + (internalHeight * i * this->tickInterval()) / (this->maximum() - this->minimum()) - 5, 100, k*2));
        }
    working = addressBook.at(0);
    working->setGeometry(QRect(right-margin+labelMargin, top + c - 5, 100, k*2));
}

void espoSlider::selfMoved(int newval)
{
    QString newstring;
    newstring.setNum((double) newval/20, 'f', 2);
    //qDebug() << newstring;
    voltageChanged(((double) newval) / 20);
    lcdOut(newstring);
    return;
}

void espoSlider::poke(void){
    //qDebug() << "Refreshing to voltage" <<  ((double) (this->value())) / 20;
    voltageChanged(((double) (this->value())) / 20);
}
