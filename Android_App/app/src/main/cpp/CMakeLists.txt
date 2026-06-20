# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html.
# For more examples on how to use CMake, see https://github.com/android/ndk-samples.

# Sets the minimum CMake version required for this project.
cmake_minimum_required(VERSION 3.22.1)

# Declares the project name. The project name can be accessed via ${ PROJECT_NAME},
# Since this is the top level CMakeLists.txt, the project name is also accessible
# with ${CMAKE_PROJECT_NAME} (both CMake variables are in-sync within the top level
# build script scope).
project("labrador-imgui-android")

add_compile_options(-fsigned-char) # saw same symptom as shown here: https://github.com/espotek-org/Labrador/issues/231 ; adding this flag fixes it, and seemingly doesn't break imgui, SDL, etc.
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
find_package(SDL3 REQUIRED)

# add_subdirectory(deps/librador)

file(GLOB SOURCES "*.cpp")
file(GLOB IMGUI_SOURCES "deps/imgui/*.cpp")
file(GLOB IMPLOT_SOURCES "deps/implot/*.cpp")
file(GLOB LIBRADOR_SOURCES "deps/librador/*.cpp")

# now build app's shared lib
add_library(
        labrador-imgui-android
        SHARED
        ${SOURCES}
        ${IMGUI_SOURCES}
        deps/imgui/backends/imgui_impl_sdl3.cpp
        deps/imgui/backends/imgui_impl_opengl3.cpp
        ${IMPLOT_SOURCES}
        ${LIBRADOR_SOURCES}
)

target_include_directories(labrador-imgui-android PRIVATE
        deps/imgui
        deps/imgui/backends
        deps/implot
        deps/librador
        ../../../../../libdfuprog
        deps/librador/librador_deps/libusb
        ${SDL3_INCLUDE_DIR}
        # for SDL_android.h
        deps/SDL/src/core/android
        # for SDL_internal.h
        deps/SDL/src
        # for SDL_build_config.h
        deps/SDL/include/build_config
        # for SDL_iostream_c.h
        deps/SDL/src/io
    )

target_compile_definitions(labrador-imgui-android PRIVATE 
    IMGUI_IMPL_OPENGL_ES3
    PLATFORM_ANDROID)

add_library(libusb SHARED IMPORTED)
set_target_properties(libusb PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_LIST_DIR}/deps/librador/librador_deps/libusb/android/${ANDROID_ABI}/libusb1.0.so)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../../../libdfuprog ${CMAKE_CURRENT_LIST_DIR}/../../../../../libdfuprog/build)

# add lib dependencies
target_link_libraries(labrador-imgui-android
        android
        log
        EGL
        libdfuprog
        libusb
        GLESv3
        SDL3-static
    )


## Creates and names a library, sets it as either STATIC
## or SHARED, and provides the relative paths to its source code.
## You can define multiple libraries, and CMake builds them for you.
## Gradle automatically packages shared libraries with your APK.
##
## In this top level CMakeLists.txt, ${CMAKE_PROJECT_NAME} is used to define
## the target library name; in the sub-module's CMakeLists.txt, ${PROJECT_NAME}
## is preferred for the same purpose.
##
## In order to load a library into your app from Java/Kotlin, you must call
## System.loadLibrary() and pass the name of the library defined here;
## for GameActivity/NativeActivity derived applications, the same library name must be
## used in the AndroidManifest.xml file.
#add_library(${CMAKE_PROJECT_NAME} SHARED
#        # List C/C++ source files with relative paths to this CMakeLists.txt.
#        native-lib.cpp)
#
## Specifies libraries CMake should link to your target library. You
## can link libraries from various origins, such as libraries defined in this
## build script, prebuilt third-party libraries, or Android system libraries.
#target_link_libraries(${CMAKE_PROJECT_NAME}
#        # List libraries link to the target library
#        android
#        log)
