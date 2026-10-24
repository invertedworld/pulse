cmake_minimum_required(VERSION 3.22)
cmake_policy(SET CMP0169 OLD)   # allow FetchContent_Populate
cmake_policy(SET CMP0175 OLD)   # suppress add_custom_command DEPENDS warning from JUCE

project(Pulse VERSION 1.4.0)

# Use ccache for compiler cache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
endif()

# JUCE is fetched at configure time unless PULSE_JUCE_DIR names a checkout of
# the same version (the shared Windows build passes one for every plugin). A
# checkout of any other version is ignored, so Pulse always builds against the
# JUCE it is pinned to.
set(PULSE_JUCE_VERSION 8.0.12)
set(PULSE_JUCE_DIR "" CACHE PATH "JUCE ${PULSE_JUCE_VERSION} checkout to use instead of fetching one")
set(PULSE_JUCE_SOURCE "")
if(PULSE_JUCE_DIR)
    file(STRINGS "${PULSE_JUCE_DIR}/CMakeLists.txt" juce_project REGEX "^project\\(JUCE VERSION")
    if(juce_project MATCHES "VERSION ${PULSE_JUCE_VERSION}")
        set(PULSE_JUCE_SOURCE "${PULSE_JUCE_DIR}")
    else()
        message(STATUS "${PULSE_JUCE_DIR} is not JUCE ${PULSE_JUCE_VERSION}: fetching it instead")
    endif()
endif()
if(NOT PULSE_JUCE_SOURCE)
    include(FetchContent)
    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${PULSE_JUCE_VERSION}
    )
    FetchContent_Populate(juce)
    set(PULSE_JUCE_SOURCE "${juce_SOURCE_DIR}")
endif()
add_subdirectory(${PULSE_JUCE_SOURCE} ${CMAKE_BINARY_DIR}/juce_framework)

# The AAX options shared by all the plugins (github.com/invertedworld/aax-tools).
# That repository is private, so a checkout without it (CI, anyone building from
# source) builds every format except AAX.
if(WIN32)
    set(MH_AAX_TOOLS_DEFAULT "C:/src/aax-tools")
else()
    set(MH_AAX_TOOLS_DEFAULT "$ENV{HOME}/code/aax-tools")
endif()
set(MH_AAX_TOOLS "${MH_AAX_TOOLS_DEFAULT}" CACHE PATH "aax-tools checkout")

# AU only exists on Apple platforms; listing it elsewhere makes JUCE warn and
# skip it, so pick the format list per platform instead.
if(APPLE)
    set(PULSE_FORMATS AU VST3 Standalone)
else()
    set(PULSE_FORMATS VST3 Standalone)
endif()
if(EXISTS "${MH_AAX_TOOLS}/cmake/MHAax.cmake")
    list(APPEND CMAKE_MODULE_PATH "${MH_AAX_TOOLS}/cmake")
    include(MHAax)
    list(APPEND PULSE_FORMATS AAX)
else()
    message(STATUS "No aax-tools at ${MH_AAX_TOOLS}: building without AAX")
endif()

juce_add_plugin(Pulse
    COMPANY_NAME "Mark Hammond"
    BUNDLE_ID "com.MarkHammond.Pulse"
    PLUGIN_MANUFACTURER_CODE MHmd
    PLUGIN_CODE Puls
    FORMATS ${PULSE_FORMATS}
    PRODUCT_NAME "Pulse"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE)

# Each instance stands alone, so multi-mono and AudioSuite stay on.
if("AAX" IN_LIST PULSE_FORMATS)
    mh_aax_options(Pulse CATEGORY Modulation)
endif()

# The user guide ships inside the binary rather than in the bundle's Resources,
# so it survives every plugin format and needs no path resolution at runtime.
juce_add_binary_data(PulseData SOURCES docs/PulseUserGuide.pdf)
set_target_properties(PulseData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

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
        PulseData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Loads the built VST3 as a host would; the shared Windows build runs it
# (PulseCheck --host <Pulse.vst3>).
juce_add_console_app(PulseCheck PRODUCT_NAME "PulseCheck")

target_sources(PulseCheck PRIVATE Tests/Check.cpp)

target_compile_definitions(PulseCheck PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1)

target_link_libraries(PulseCheck PRIVATE
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags)
