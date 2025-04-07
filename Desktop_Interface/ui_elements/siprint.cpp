#include "siprint.h"
#include <cmath>

siprint::siprint(const char *unitsInit, double valInit)
: value(valInit)
{
    strncpy(units, unitsInit, 6);
}

char* siprint::printVal(){
    bool negative = (value < 0);
    double abs_value = std::fabs(value);

    char* tempStringPtr = printString;
    if (negative)
    {
        printString[0] = '-';
        tempStringPtr++;
    }

    if (abs_value >= 1000000000000000000)
    {
        sprintf(tempStringPtr, "Inf %s", units);
    }
    else if (abs_value >= 1000000)
    {
        sprintf(tempStringPtr, "%.2fM%s", abs_value/1000000, units);
    }
    else if (abs_value >= 1000)
    {
        sprintf(tempStringPtr, "%.2fk%s", abs_value/1000, units);
    }
    else if (abs_value >= 1)
    {
        sprintf(tempStringPtr, "%.2f%s", abs_value, units);
    }
    else if (abs_value >= 0.001)
    {
        sprintf(tempStringPtr, "%.2fm%s", abs_value*1000, units);
    }
    else if (abs_value >= 0.000001)
    {
        sprintf(tempStringPtr, "%.2fu%s", abs_value*1000000, units);
    }
    else if (abs_value >= 0.000000001)
    {
        sprintf(tempStringPtr, "%.2fn%s", abs_value*1000000000, units);
    }
    else if (abs_value >= 0.000000000001)
    {
        sprintf(tempStringPtr, "%.2fp%s", abs_value*1000000000000, units);
    }
    else
    {
        sprintf(tempStringPtr, "%.2f%s", abs_value, units);
    }

    return printString;
}
