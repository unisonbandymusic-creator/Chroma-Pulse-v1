cmake_minimum_required(VERSION 3.22)

# --- Platform settings (must come before project()) -------------------------
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")   # Universal binary
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")
endif()

# Static MSVC runtime: no VC++ redistributable needed on the user's machine
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Pulse VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE -------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- Plugin formats ---------------------------------------------------------
if(APPLE)
    set(PULSE_FORMATS VST3 AU)
else()
    set(PULSE_FORMATS VST3)   # AU only exists on macOS
endif()

juce_add_plugin(Pulse
    COMPANY_NAME "Him'z DSP"
    BUNDLE_ID "com.himzdsp.pulse"
    PRODUCT_NAME "Pulse"
    VERSION "1.0.0"
    PLUGIN_MANUFACTURER_CODE Hmdz
    PLUGIN_CODE Puls
    FORMATS ${PULSE_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
)

target_sources(Pulse
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
)

target_compile_definitions(Pulse
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Pulse
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
