#include "pinchcatcher.h"

pinchCatcher::pinchCatcher(QObject *parent) : QObject(parent)
{
    grabGesture(Qt::PinchGesture);
}
