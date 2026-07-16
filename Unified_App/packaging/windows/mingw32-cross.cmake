# Cross toolchain: the 32-bit Windows exe is built from Linux with Ubuntu's
# mingw-w64 packages, because MSYS2 dropped its i686 (MINGW32) repo.  The
# -posix compiler variant supplies the winpthread-backed std::thread that
# librador needs; Debian-family mingw-w64 links msvcrt, which every old
# 32-bit machine already has (unlike UCRT).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)
set(CMAKE_C_COMPILER i686-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER i686-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER i686-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/i686-w64-mingw32)
# CI exports LABRADOR_WIN32_ROOT = the prefix holding the cross-built static
# libusb.  It must be a find root: with FIND_ROOT_PATH_MODE_LIBRARY ONLY,
# find_library re-roots every hint and would otherwise miss it.
if(DEFINED ENV{LABRADOR_WIN32_ROOT})
    list(APPEND CMAKE_FIND_ROOT_PATH "$ENV{LABRADOR_WIN32_ROOT}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
