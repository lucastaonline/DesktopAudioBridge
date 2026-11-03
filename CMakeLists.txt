cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime: no VC++ redistributable needed on the user's machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(DesktopAudioBridge VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(DesktopAudioBridge
    COMPANY_NAME "DesktopAudioBridge"
    PLUGIN_MANUFACTURER_CODE Dabr
    PLUGIN_CODE Dab1
    FORMATS VST3
    PRODUCT_NAME "DesktopAudioBridge"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(DesktopAudioBridge PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/LoopbackCapture.cpp)

target_compile_definitions(DesktopAudioBridge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DesktopAudioBridge
    PRIVATE
        juce::juce_audio_utils
        ole32
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
