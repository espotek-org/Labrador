#include "siprint.h"
#include <cmath>

siprint::siprint(const char *unitsInit, double valInit)
: value(valInit)
{
    units[0] = '\0';
    strncat(units, unitsInit, sizeof units - 1);
}

char* siprint::printVal(){
    const char *sign = (value < 0.0) ? "-" : "";
    double abs_value = std::fabs(value);

    if (abs_value >= 1000000000000000000.0) {
        snprintf(printString, sizeof printString, "%sInf %s", sign, units);
    } else if (abs_value >= 1000000.0) {
        snprintf(printString, sizeof printString, "%s%.4gM%s", sign, abs_value/1000000, units);
    } else if (abs_value >= 1000.0) {
        snprintf(printString, sizeof printString, "%s%.4gk%s", sign, abs_value/1000, units);
    } else if (abs_value >= 1.0) {
        snprintf(printString, sizeof printString, "%s%.4g%s", sign, abs_value, units);
    } else if (abs_value >= 0.001) {
        snprintf(printString, sizeof printString, "%s%.4gm%s", sign, abs_value*1000, units);
    } else if (abs_value >= 0.000001) {
        snprintf(printString, sizeof printString, "%s%.4gu%s", sign, abs_value*1000000, units);
    } else if (abs_value >= 0.000000001) {
        snprintf(printString, sizeof printString, "%s%.4gn%s", sign, abs_value*1000000000, units);
    } else if (abs_value >= 0.000000000001) {
        snprintf(printString, sizeof printString, "%s%.4gp%s", sign, abs_value*1000000000000, units);
    } else {
        snprintf(printString, sizeof printString, "%s%.4g%s", sign, abs_value, units);
    }

    return printString;
}
