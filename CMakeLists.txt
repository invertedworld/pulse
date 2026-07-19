cmake_minimum_required(VERSION 3.22)
cmake_policy(SET CMP0169 OLD)   # allow FetchContent_Populate
cmake_policy(SET CMP0175 OLD)   # suppress add_custom_command DEPENDS warning from JUCE

project(Pulse VERSION 1.0.0)

# Use ccache for compiler cache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
endif()

include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
)
FetchContent_Populate(juce)
add_subdirectory(${juce_SOURCE_DIR} ${CMAKE_BINARY_DIR}/juce_framework)

juce_add_plugin(Pulse
    COMPANY_NAME "Mark Hammond"
    BUNDLE_ID "com.MarkHammond.Pulse"
    PLUGIN_MANUFACTURER_CODE MHmd
    PLUGIN_CODE Puls
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Pulse"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE)

target_sources(Pulse
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

target_compile_definitions(Pulse
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Pulse
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
