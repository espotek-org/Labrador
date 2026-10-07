# Windows: build libusb from the copy kept in-tree under deps/libusb — the
# libusb 1.0.30 release sources with Labrador's fix for the libusb-win32
# (libusb0.sys) bootloader driver already applied (deps/libusb/README.md
# has the provenance, the patch and how to update it).  Provides the static
# target `labrador_libusb` that librador and libdfuprog link instead of the
# system/MSYS2 libusb.  Nothing is downloaded at configure time.

set(_libusb_root ${CMAKE_CURRENT_LIST_DIR}/../deps/libusb)
set(_libusb_src ${_libusb_root}/libusb)

# Same file set as libusb's Makefile.am for OS_WINDOWS (Windows hotplug is
# off by default there too, and the app polls for devices).  The vendored
# tree holds exactly these files plus the headers they include.
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
    # libusb's own hand-written config.h for MSVC (copied with the sources).
    target_include_directories(labrador_libusb PRIVATE ${_libusb_root}/msvc)
else()
    # libusbi.h does #include <config.h>
    set(_libusb_cfg_dir ${CMAKE_CURRENT_BINARY_DIR}/labrador_libusb_config)
    configure_file(${CMAKE_CURRENT_LIST_DIR}/libusb_mingw_config.h
                   ${_libusb_cfg_dir}/config.h COPYONLY)
    target_include_directories(labrador_libusb PRIVATE ${_libusb_cfg_dir})
    target_compile_options(labrador_libusb PRIVATE -w)
endif()
set_target_properties(labrador_libusb PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
