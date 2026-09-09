cmake_minimum_required(VERSION 3.24)
project(ThedaNative VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(TE_ADD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/juce/CMakeLists.txt")
    message(FATAL_ERROR "Run python native/scripts/fetch-dependencies.py first")
endif()
add_subdirectory(.deps/juce EXCLUDE_FROM_ALL)
add_subdirectory(.deps/tracktion EXCLUDE_FROM_ALL)

juce_add_gui_app(ThedaNative
    PRODUCT_NAME "Theda Native"
    BUNDLE_ID "com.theda.native"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Record audio into your songs"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)
target_sources(ThedaNative PRIVATE
    src/Main.cpp src/Session.cpp src/StepGrid.cpp src/ProjectFiles.cpp
    src/UtilityDevice.cpp src/SelfTest.cpp src/PatternTest.cpp)
target_compile_definitions(ThedaNative PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=0 JUCE_PLUGINHOST_AU=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)
target_link_libraries(ThedaNative PRIVATE
    tracktion::tracktion_engine tracktion::tracktion_graph tracktion::tracktion_core
    juce::juce_audio_utils juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(ThedaNative PRIVATE /bigobj)
endif()
enable_testing()
add_test(NAME native_device_correctness COMMAND ThedaNative --self-test)
set_tests_properties(native_device_correctness PROPERTIES TIMEOUT 30)
add_test(NAME native_pattern_workflow COMMAND ThedaNative --pattern-test)
set_tests_properties(native_pattern_workflow PROPERTIES TIMEOUT 30)
