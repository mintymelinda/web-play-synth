cmake_minimum_required(VERSION 3.22)
project(WebPlaySynthPlugin VERSION 0.1.0 LANGUAGES C CXX)
include(FetchContent)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
juce_add_plugin(WebPlaySynth
    COMPANY_NAME "Web Play Synth"
    BUNDLE_ID "audio.webplaysynth.instrument"
    PLUGIN_MANUFACTURER_CODE WpSy
    PLUGIN_CODE WpS1
    PRODUCT_NAME "Web Play Synth"
    DESCRIPTION "Dual-oscillator subtractive synthesizer"
    FORMATS VST3 Standalone
    IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)
juce_generate_juce_header(WebPlaySynth)
target_sources(WebPlaySynth PRIVATE Source/PluginProcessor.cpp Source/PluginProcessor.h Source/PluginEditor.cpp Source/PluginEditor.h)
target_compile_features(WebPlaySynth PRIVATE cxx_std_17)
target_compile_definitions(WebPlaySynth PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(WebPlaySynth PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
