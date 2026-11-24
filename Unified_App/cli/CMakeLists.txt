# Headless command-line tools over librador. To add a command: implement Command,
# register it in main.cpp, and list its source here. Measurement code shared with
# the app's --auto-qa lives in ../measure (labrador_measure).
add_executable(labrador_cli
    main.cpp
    CaptureCommand.cpp
    InfoCommand.cpp
    MeasureCommand.cpp
    OptionParser.cpp
    ScopeOptions.cpp
    SiggenCommand.cpp
    Stimulus.cpp
    StreamTestCommand.cpp
    UpdateFirmwareCommand.cpp)
set_target_properties(labrador_cli PROPERTIES OUTPUT_NAME labrador-cli)
target_link_libraries(labrador_cli PRIVATE labrador_measure)

# update-firmware flashes the hex the Labrador app bundles; dev builds get the
# same assets/firmware layout next to the binary (packaging replaces this).
add_custom_command(TARGET labrador_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/../firmware
        $<TARGET_FILE_DIR:labrador_cli>/assets/firmware)
