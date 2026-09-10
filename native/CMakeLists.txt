cmake_minimum_required(VERSION 3.24)
project(ThetaNative VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(TE_ADD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/juce/CMakeLists.txt")
    message(FATAL_ERROR "Run python native/scripts/fetch-dependencies.py first")
endif()
add_subdirectory(.deps/juce EXCLUDE_FROM_ALL)
add_subdirectory(.deps/tracktion EXCLUDE_FROM_ALL)

juce_add_gui_app(ThetaNative
    PRODUCT_NAME "Theta"
    BUNDLE_ID "com.theta.native"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Record audio into your songs"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)
juce_add_binary_data(ThetaNativeAssets
    SOURCES assets/HandClap-01_09.flac)
target_sources(ThetaNative PRIVATE
    src/Main.cpp src/Session.cpp src/StepGrid.cpp src/Arrangement.cpp src/ProjectFiles.cpp src/Playhead.cpp
    src/BrowserPanel.cpp src/DeviceRack.cpp src/DrumDevice.cpp src/UtilityDevice.cpp src/ThetaSpaceDevice.cpp src/ThetaBloomDevice.cpp src/ThetaArpDevice.cpp
    src/SelfTest.cpp src/PatternTest.cpp src/ArrangementTest.cpp)
target_compile_definitions(ThetaNative PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=0 JUCE_PLUGINHOST_AU=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)
target_link_libraries(ThetaNative PRIVATE
    ThetaNativeAssets
    tracktion::tracktion_engine tracktion::tracktion_graph tracktion::tracktion_core
    juce::juce_audio_utils juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(ThetaNative PRIVATE /bigobj)
endif()
enable_testing()
add_test(NAME native_device_correctness COMMAND ThetaNative --self-test)
set_tests_properties(native_device_correctness PROPERTIES TIMEOUT 30)
add_test(NAME native_pattern_workflow COMMAND ThetaNative --pattern-test)
set_tests_properties(native_pattern_workflow PROPERTIES TIMEOUT 30)
add_test(NAME native_arrangement_workflow COMMAND ThetaNative --arrangement-test)
set_tests_properties(native_arrangement_workflow PROPERTIES TIMEOUT 30)
add_test(NAME native_startup_lifecycle COMMAND ThetaNative --startup-test)
set_tests_properties(native_startup_lifecycle PROPERTIES TIMEOUT 30)
