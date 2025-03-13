#include "siprint.h"
#include <cmath>

siprint::siprint(const char *unitsInit, double valInit)
: value(valInit)
{
    strncpy(units, unitsInit, 6);
}

char* siprint::printVal(){
    std::string suffix;
    bool negative = (value < 0);

    char* tempStringPtr = printString;
    if (negative)
    {
        printString[0] = '-';
        tempStringPtr++;
    }

    if (std::fabs(value) >= 1000000000000000000)
    {
        sprintf(tempStringPtr, "Inf %s", units);
    }
    else if (std::fabs(value) >= 1000000)
    {
        sprintf(tempStringPtr, "%.2fM%s", std::fabs(value)/1000000, units);
    }
    else if (std::fabs(value) >= 1000)
    {
        sprintf(tempStringPtr, "%.2fk%s",  std::fabs(value)/1000, units);
    }
    else if (std::fabs(value) >= 1)
    {
        sprintf(tempStringPtr, "%.2f%s",  std::fabs(value), units);
    }
    else if (std::fabs(value) >= 0.001)
    {
        sprintf(tempStringPtr, "%.2fm%s", std::fabs(value)*1000, units);
    }
    else if (std::fabs(value) >= 0.000001)
    {
        sprintf(tempStringPtr, "%.2fu%s",  std::fabs(value)*1000000, units);
    }
    else if (std::fabs(value) >= 0.000000001)
    {
        sprintf(tempStringPtr, "%.2fn%s",  std::fabs(value)*1000000000, units);
    }
    else if (std::fabs(value) >= 0.000000000001)
    {
        sprintf(tempStringPtr, "%.2fp%s",  std::fabs(value)*1000000000000, units);
    }
    else if (std::fabs(value) >= 1)
    {
        sprintf(tempStringPtr, "%.2f%s",  std::fabs(value), units);
    }

    return printString;
}
