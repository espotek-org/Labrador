#ifndef LOGGING_INTERNAL_H
#define LOGGING_INTERNAL_H

#include "logging.h"
//TODO : maybe handle the logger switch for android by setting _librador_global_logger in librador.cpp instead?
#ifdef PLATFORM_ANDROID

#include <android/log.h>
#define LOG_TAG "librador"
#define LIBRADOR_LOG(level, ...) __android_log_print(level, LOG_TAG, __VA_ARGS__)

#else

#ifdef LIBRADOR_ENABLE_LOGGING
	#define LIBRADOR_LOG(level, ...)                                                         \
		do                                                                                   \
		{                                                                                    \
			librador_global_logger(level, __VA_ARGS__);                                      \
		} while (0)
#else
	#define LIBRADOR_LOG(level, ...)                                                         \
		do                                                                                   \
		{                                                                                    \
			/* Logging is disabled */                                                        \
		} while (0)
#endif // LIBRADOR_DISABLE_LOGGING

#endif // PLATFORM_ANDROID

void librador_global_logger(const int level, const char* format, ...);

#endif // LOGGING_INTERNAL_H
