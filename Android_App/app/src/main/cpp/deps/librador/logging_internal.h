#ifndef LOGGING_INTERNAL_H
#define LOGGING_INTERNAL_H

#include "logging.h"
#include <android/log.h>
#define LOG_TAG "librador"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

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

void librador_global_logger(const int level, const char* format, ...);

#endif // LOGGING_INTERNAL_H
