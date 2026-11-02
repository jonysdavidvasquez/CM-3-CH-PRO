cmake_minimum_required(VERSION 3.22)
project(CM3CHPro VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                          GIT_TAG 8.0.6 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(CM3CHUI SOURCES ui/index.html)

juce_add_plugin(CM3CHPro
    COMPANY_NAME "Home Sounds"
    PLUGIN_MANUFACTURER_CODE HmSn
    PLUGIN_CODE Cm3c
    FORMATS VST3
    PRODUCT_NAME "CM 3 CH PRO"
    VST3_CATEGORIES Fx Dynamics
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEBVIEW2 TRUE)

target_sources(CM3CHPro PRIVATE
    Source/PluginProcessor.cpp Source/PluginEditor.cpp)

target_compile_definitions(CM3CHPro PUBLIC
    JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)

target_link_libraries(CM3CHPro
    PRIVATE CM3CHUI juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
