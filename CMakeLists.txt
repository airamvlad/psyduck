cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
project(PsyDuck VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PSYDUCK_BUILD_PLUGIN "Build the VST3 plugin (downloads JUCE)" ON)
option(PSYDUCK_BUILD_TESTS "Build the DSP unit tests" ON)
option(PSYDUCK_COPY_AFTER_BUILD "Copy the VST3 into the system plugin folder after building" OFF)

# DSP core: plain C++, no JUCE, so it can be tested without a DAW.
add_library(psyduck_dsp INTERFACE)
target_include_directories(psyduck_dsp INTERFACE src)

if(PSYDUCK_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(PsyDuck
        PRODUCT_NAME "PsyDuck"
        COMPANY_NAME "PsyDuck Audio"
        BUNDLE_ID "audio.psyduck.PsyDuck"
        PLUGIN_MANUFACTURER_CODE Psdk
        PLUGIN_CODE Pdk1
        FORMATS VST3
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        VST3_CATEGORIES Fx Dynamics
        COPY_PLUGIN_AFTER_BUILD ${PSYDUCK_COPY_AFTER_BUILD})

    target_sources(PsyDuck PRIVATE
        src/plugin/Parameters.cpp
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp)

    target_compile_definitions(PsyDuck PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(PsyDuck
        PRIVATE
            psyduck_dsp
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

if(PSYDUCK_BUILD_TESTS)
    enable_testing()
    add_executable(psyduck_dsp_tests tests/DspTests.cpp)
    target_link_libraries(psyduck_dsp_tests PRIVATE psyduck_dsp)
    add_test(NAME psyduck_dsp_tests COMMAND psyduck_dsp_tests)

    if(PSYDUCK_BUILD_PLUGIN)
        # Runs the real plugin processor offline: modes, lookahead, state.
        add_executable(psyduck_plugin_tests tests/PluginTests.cpp)
        target_include_directories(psyduck_plugin_tests PRIVATE src
            $<TARGET_PROPERTY:PsyDuck,INCLUDE_DIRECTORIES>)
        target_compile_definitions(psyduck_plugin_tests PRIVATE
            $<TARGET_PROPERTY:PsyDuck,COMPILE_DEFINITIONS>)
        target_link_libraries(psyduck_plugin_tests PRIVATE PsyDuck)
        add_test(NAME psyduck_plugin_tests COMMAND psyduck_plugin_tests)
    endif()
endif()
