# Headless command-line tools over librador; add one add_executable per tool.
add_executable(labrador_capture labrador_capture.cpp)
set_target_properties(labrador_capture PROPERTIES OUTPUT_NAME labrador-capture)
target_link_libraries(labrador_capture PRIVATE librador)
