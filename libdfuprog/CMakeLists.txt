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
    target_link_libraries(libdfuprog PUBLIC PkgConfig::DFUPROG_LIBUSB)
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
