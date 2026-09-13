cmake_minimum_required(VERSION 3.24)
project(ThetaForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(THETA_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../native/.deps/juce" CACHE PATH "JUCE source checkout")
if (NOT EXISTS "${THETA_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set THETA_JUCE_DIR to a JUCE checkout, or fetch Theta's native dependencies first.")
endif()

add_subdirectory("${THETA_JUCE_DIR}" juce EXCLUDE_FROM_ALL)

juce_add_plugin(ThetaForge
    COMPANY_NAME "Theta"
    BUNDLE_ID "com.theta.forge"
    PLUGIN_MANUFACTURER_CODE ThTa
    PLUGIN_CODE Frge
    FORMATS VST3 Standalone
    PRODUCT_NAME "Theta Forge"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE)

target_sources(ThetaForge PRIVATE
    src/ForgeProcessor.cpp
    src/ForgeProcessor.h
    src/ForgeVoice.cpp
    src/ForgeVoice.h
    src/ForgeEditor.cpp
    src/ForgeEditor.h)
target_link_libraries(ThetaForge PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(ThetaForge PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
