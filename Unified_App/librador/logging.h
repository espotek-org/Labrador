#ifndef LOGGING_H
#define LOGGING_H

#ifdef PLATFORM_ANDROID
// sync to android_LogPriority
enum {
    LOG_NONE = 0,
    LOG_ERROR = 6,
    LOG_WARNING = 5,
    LOG_DEBUG = 3,
};
#else
enum {
    LOG_NONE = 0,
    LOG_ERROR,
    LOG_WARNING,
    LOG_DEBUG,
};
#endif //PLATFORM_ANDROID



#endif // LOGGING_H
