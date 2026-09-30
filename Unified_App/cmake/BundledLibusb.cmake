# Windows: build libusb from its release sources with Labrador's fix for the
# libusb-win32 (libusb0.sys) transfer-length bug; see
# patch_libusb_iocp_length.cmake.  Provides the static target `labrador_libusb`
# that librador and libdfuprog link instead of the system/MSYS2 libusb.
#
# Offline builds: point FETCHCONTENT_SOURCE_DIR_LABRADOR_LIBUSB at an
# extracted libusb-1.0.30 tree (the patch is applied to it in place).

include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW) # extracted files get the extraction time
endif()

set(LABRADOR_LIBUSB_VERSION 1.0.30)
FetchContent_Declare(labrador_libusb
    URL https://github.com/libusb/libusb/releases/download/v${LABRADOR_LIBUSB_VERSION}/libusb-${LABRADOR_LIBUSB_VERSION}.tar.bz2
    URL_HASH SHA256=fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf
    PATCH_COMMAND ${CMAKE_COMMAND} -DLIBUSB_SOURCE_DIR=<SOURCE_DIR>
                  -P ${CMAKE_CURRENT_LIST_DIR}/patch_libusb_iocp_length.cmake
)
# The tarball has no CMakeLists.txt, so this only downloads + patches it;
# the library target is defined below.
FetchContent_MakeAvailable(labrador_libusb)
if(FETCHCONTENT_SOURCE_DIR_LABRADOR_LIBUSB)
    # FetchContent skips PATCH_COMMAND for a user-supplied source dir.
    execute_process(COMMAND ${CMAKE_COMMAND} -DLIBUSB_SOURCE_DIR=${labrador_libusb_SOURCE_DIR}
                            -P ${CMAKE_CURRENT_LIST_DIR}/patch_libusb_iocp_length.cmake
                    COMMAND_ERROR_IS_FATAL ANY)
endif()

set(_libusb_src ${labrador_libusb_SOURCE_DIR}/libusb)
# Same file set as libusb's Makefile.am for OS_WINDOWS (Windows hotplug is
# off by default there too, and the app polls for devices).
add_library(labrador_libusb STATIC
    ${_libusb_src}/core.c
    ${_libusb_src}/descriptor.c
    ${_libusb_src}/hotplug.c
    ${_libusb_src}/io.c
    ${_libusb_src}/strerror.c
    ${_libusb_src}/sync.c
    ${_libusb_src}/os/events_windows.c
    ${_libusb_src}/os/threads_windows.c
    ${_libusb_src}/os/windows_common.c
    ${_libusb_src}/os/windows_usbdk.c
    ${_libusb_src}/os/windows_winusb.c
)
target_include_directories(labrador_libusb
    PUBLIC  ${_libusb_src}
    PRIVATE ${_libusb_src}/os)
if(MSVC)
    target_include_directories(labrador_libusb PRIVATE ${labrador_libusb_SOURCE_DIR}/msvc)
else()
    # libusbi.h does #include <config.h>
    set(_libusb_cfg_dir ${CMAKE_CURRENT_BINARY_DIR}/labrador_libusb_config)
    configure_file(${CMAKE_CURRENT_LIST_DIR}/libusb_mingw_config.h
                   ${_libusb_cfg_dir}/config.h COPYONLY)
    target_include_directories(labrador_libusb PRIVATE ${_libusb_cfg_dir})
    target_compile_options(labrador_libusb PRIVATE -w)
endif()
set_target_properties(labrador_libusb PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
