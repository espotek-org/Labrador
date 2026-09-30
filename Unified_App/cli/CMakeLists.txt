# Headless command-line tools over librador. To add a command: implement Command,
# register it in main.cpp, and list its source here.
add_executable(labrador_cli
    main.cpp
    CaptureCommand.cpp
    DeviceSession.cpp
    OptionParser.cpp)
set_target_properties(labrador_cli PROPERTIES OUTPUT_NAME labrador-cli)
target_link_libraries(labrador_cli PRIVATE librador)
