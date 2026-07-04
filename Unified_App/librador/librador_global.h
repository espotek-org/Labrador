#ifndef LIBRADOR_GLOBAL_H
#define LIBRADOR_GLOBAL_H

#ifdef FORCE_COMPILATION_FOR_QT5
    #include <QtCore/qglobal.h>
#else
    #if defined(_WIN32) || defined(_WIN64)
        #define Q_DECL_EXPORT __declspec(dllexport)
        #define Q_DECL_IMPORT
    #else
        #define Q_DECL_EXPORT
        #define Q_DECL_IMPORT
    #endif
#endif

#if defined(LIBRADOR_LIBRARY)
#  define LIBRADORSHARED_EXPORT Q_DECL_EXPORT
#else
#  define LIBRADORSHARED_EXPORT Q_DECL_IMPORT
#endif

#endif // LIBRADOR_GLOBAL_H
