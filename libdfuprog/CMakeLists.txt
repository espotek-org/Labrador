cmake_minimum_required(VERSION 3.16)
project(libdfuprog VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_library(libdfuprog STATIC
    config.h
    dfu-programmer/src/arguments.c dfu-programmer/src/arguments.h
    dfu-programmer/src/atmel.c dfu-programmer/src/atmel.h
    dfu-programmer/src/commands.c dfu-programmer/src/commands.h
    dfu-programmer/src/dfu.c dfu-programmer/src/dfu.h
    dfu-programmer/src/dfu-bool.h
    dfu-programmer/src/dfu-device.h
    dfu-programmer/src/intel_hex.c dfu-programmer/src/intel_hex.h
    dfu-programmer/src/stm32.c dfu-programmer/src/stm32.h
    dfu-programmer/src/util.c dfu-programmer/src/util.h
    libdfuprog.c libdfuprog.h
)

target_include_directories(libdfuprog PRIVATE
    dfu-programmer/src
)

if(ANDROID)
    # Android build: libusb headers from the in-tree copy; the app links the
    # prebuilt libusb .so and drives dfuprog with an already-open device handle.
    target_include_directories(libdfuprog PRIVATE
        ../Android_App/app/src/main/cpp/deps/librador/librador_deps/libusb
    )
    target_compile_definitions(libdfuprog PRIVATE
        HAVE_CONFIG_H
        PLATFORM_ANDROID
    )
else()
    # Desktop: system libusb; dfuprog manages its own context and discovery.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DFUPROG_LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
    # LABRADOR_STATIC_LIBUSB is set by the parent Unified_App build for
    # self-contained bundles (macOS .app, Linux AppImage).  Link the archive
    # by absolute path (a plain -lusb-1.0 would prefer a .dylib/.so sitting
    # next to it), then append Libs.private via STATIC_LDFLAGS_OTHER plus any
    # extra -l deps (e.g. objc).
    if(LABRADOR_STATIC_LIBUSB)
        find_library(DFUPROG_LIBUSB_STATIC_ARCHIVE NAMES libusb-1.0.a
                     HINTS ${DFUPROG_LIBUSB_STATIC_LIBRARY_DIRS} ${DFUPROG_LIBUSB_LIBRARY_DIRS})
        if(NOT DFUPROG_LIBUSB_STATIC_ARCHIVE)
            message(FATAL_ERROR "LABRADOR_STATIC_LIBUSB=ON but libusb-1.0.a was not found")
        endif()
        set(DFUPROG_LIBUSB_STATIC_DEPS ${DFUPROG_LIBUSB_STATIC_LIBRARIES})
        list(REMOVE_ITEM DFUPROG_LIBUSB_STATIC_DEPS usb-1.0)
        target_include_directories(libdfuprog PUBLIC ${DFUPROG_LIBUSB_STATIC_INCLUDE_DIRS})
        target_link_libraries(libdfuprog PUBLIC ${DFUPROG_LIBUSB_STATIC_ARCHIVE}
            ${DFUPROG_LIBUSB_STATIC_DEPS} ${DFUPROG_LIBUSB_STATIC_LDFLAGS_OTHER})
    else()
        target_link_libraries(libdfuprog PUBLIC PkgConfig::DFUPROG_LIBUSB)
    endif()
    target_compile_definitions(libdfuprog PRIVATE HAVE_CONFIG_H)
endif()

install(TARGETS libdfuprog
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# dfu-programmer predates C23 (its dfu-bool.h defines enum constants named
# true/false, keywords from C23 on); pin the dialect new GCCs default past.
set_target_properties(libdfuprog PROPERTIES C_STANDARD 17 C_EXTENSIONS ON)
