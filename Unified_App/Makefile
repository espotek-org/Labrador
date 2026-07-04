# Convenience wrapper around CMake presets. Real build config lives in
# CMakeLists.txt / CMakePresets.json.
#
#   make macos    - build on macOS
#   make linux    - build on desktop Linux
#   make pi       - build on Raspberry Pi (GLES 2.0, Pi 3 and newer)
#   make windows  - build on Windows (run from a shell with MSVC or MinGW + ninja)
#   make android  - build the Android APK (requires Android SDK/NDK)
#   make clean    - remove all build directories

.PHONY: macos linux pi windows android clean

macos linux pi windows:
	cmake --preset $@
	cmake --build --preset $@

android:
	cd android && ./gradlew assembleDebug

clean:
	rm -rf build
