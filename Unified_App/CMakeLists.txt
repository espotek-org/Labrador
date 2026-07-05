cmake_minimum_required(VERSION 3.22)
project(Labrador VERSION 2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Renderer floor: desktop = GL3, Android = GLES3, Raspberry Pi = GLES2 (Pi 3 and up)
option(LABRADOR_GLES2 "Target OpenGL ES 2.0 (Raspberry Pi builds)" OFF)

# Labrador hardware sends signed chars; some ARM ABIs default to unsigned char
# (espotek-org/Labrador#231)
if(NOT MSVC)
    add_compile_options(-fsigned-char)
endif()

# exprtk's template instantiations overflow the default PE/COFF section limit
# ("too many sections" from as.exe), so MinGW needs big-obj support
if(MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif()

# --- SDL3 (Brent's fork, adds SDL_PinchFingerEvent) ---
# Desktop: static. Android: shared, because SDLActivity System.loadLibrary()s it.
if(ANDROID)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/SDL EXCLUDE_FROM_ALL)
if(ANDROID)
    set(SDL_LIB SDL3::SDL3-shared)
else()
    set(SDL_LIB SDL3::SDL3-static)
endif()

# --- Dear ImGui (Brent's fork: custom_imgui touch widgets, pinch plumbing) + ImPlot ---
file(GLOB IMGUI_SOURCES CONFIGURE_DEPENDS deps/imgui/*.cpp)
file(GLOB IMPLOT_SOURCES CONFIGURE_DEPENDS deps/implot/*.cpp)
add_library(imgui STATIC
    ${IMGUI_SOURCES}
    deps/imgui/backends/imgui_impl_sdl3.cpp
    deps/imgui/backends/imgui_impl_opengl3.cpp
    deps/imgui/misc/cpp/imgui_stdlib.cpp
    ${IMPLOT_SOURCES}
)
target_include_directories(imgui PUBLIC deps/imgui deps/imgui/backends deps/implot)
target_link_libraries(imgui PUBLIC ${SDL_LIB})
if(ANDROID)
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)
elseif(LABRADOR_GLES2)
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES2)
endif()

# --- Dear ImGui Test Engine (QA builds: cmake -DLABRADOR_QA=ON) ---
# In-process UI test framework + the app's --qa headless runner (src/qa/).
# Off by default. deps/imgui_test_engine/LICENSE.txt: free for open source.
option(LABRADOR_QA "Build with Dear ImGui Test Engine and the --qa test runner" OFF)
if(LABRADOR_QA)
    file(GLOB IMGUI_TE_SOURCES CONFIGURE_DEPENDS deps/imgui_test_engine/*.cpp)
    target_sources(imgui PRIVATE ${IMGUI_TE_SOURCES})
    target_include_directories(imgui PUBLIC deps/imgui_test_engine)
    target_compile_definitions(imgui PUBLIC
        IMGUI_ENABLE_TEST_ENGINE                         # imgui.cpp item hooks
        IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
        LABRADOR_QA)
endif()

# --- librador (canonical merged copy) + libdfuprog (firmware flashing) ---
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../libdfuprog
                 ${CMAKE_CURRENT_BINARY_DIR}/libdfuprog EXCLUDE_FROM_ALL)
file(GLOB LIBRADOR_SOURCES CONFIGURE_DEPENDS librador/*.cpp)
add_library(librador STATIC ${LIBRADOR_SOURCES})
target_include_directories(librador PUBLIC
    librador
    ${CMAKE_CURRENT_SOURCE_DIR}/../libdfuprog
)
target_link_libraries(librador PUBLIC ${SDL_LIB} libdfuprog)
target_compile_definitions(librador PUBLIC LIBRADOR_ENABLE_LOGGING)
if(ANDROID)
    target_compile_definitions(librador PUBLIC PLATFORM_ANDROID)
    # Prebuilt per-ABI libusb (Android build of libusb with fd-wrap support)
    add_library(libusb_android SHARED IMPORTED)
    set_target_properties(libusb_android PROPERTIES IMPORTED_LOCATION
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/libusb-android/android/${ANDROID_ABI}/libusb1.0.so)
    target_include_directories(librador PUBLIC deps/libusb-android)
    target_link_libraries(librador PUBLIC libusb_android android log)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
    target_link_libraries(librador PUBLIC PkgConfig::LIBUSB)
endif()

# --- Application ---
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS src/*.cpp)
if(ANDROID)
    add_library(labrador SHARED ${APP_SOURCES})
else()
    add_executable(labrador ${APP_SOURCES})
endif()
target_include_directories(labrador PRIVATE src deps)
target_link_libraries(labrador PRIVATE imgui librador)

if(APPLE AND NOT ANDROID)
    find_package(OpenGL REQUIRED)
    target_link_libraries(labrador PRIVATE OpenGL::GL)
elseif(ANDROID)
    target_link_libraries(labrador PRIVATE android log EGL GLESv3)
elseif(LABRADOR_GLES2)
    target_link_libraries(labrador PRIVATE GLESv2)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(labrador PRIVATE OpenGL::GL)
endif()

# Dev builds load assets from next to the binary; packaging replaces this
if(NOT ANDROID)
    add_custom_command(TARGET labrador POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:labrador>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/firmware
            $<TARGET_FILE_DIR:labrador>/assets/firmware
    )
endif()
