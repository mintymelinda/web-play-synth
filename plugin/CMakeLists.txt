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

include(CTest)
if(BUILD_TESTING)
    add_executable(WebPlaySynthSmokeTest tests/PluginSmokeTest.cpp)
    target_include_directories(WebPlaySynthSmokeTest PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/WebPlaySynth_artefacts/JuceLibraryCode"
    )
    target_compile_features(WebPlaySynthSmokeTest PRIVATE cxx_std_17)
    target_link_libraries(WebPlaySynthSmokeTest PRIVATE WebPlaySynth juce::juce_audio_utils)
    add_test(NAME native-synth-smoke COMMAND WebPlaySynthSmokeTest)

    add_executable(WebPlaySynthAudioPathTest tests/AudioPathRegressionTest.cpp)
    target_include_directories(WebPlaySynthAudioPathTest PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/WebPlaySynth_artefacts/JuceLibraryCode"
    )
    target_compile_features(WebPlaySynthAudioPathTest PRIVATE cxx_std_17)
    target_link_libraries(WebPlaySynthAudioPathTest PRIVATE WebPlaySynth juce::juce_audio_utils)
    add_test(NAME native-audio-path-regressions COMMAND WebPlaySynthAudioPathTest)

    add_executable(WebPlaySynthEnvelopePathTest tests/EnvelopeAudioPathTest.cpp)
    target_include_directories(WebPlaySynthEnvelopePathTest PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/WebPlaySynth_artefacts/JuceLibraryCode"
    )
    target_compile_features(WebPlaySynthEnvelopePathTest PRIVATE cxx_std_17)
    target_link_libraries(WebPlaySynthEnvelopePathTest PRIVATE WebPlaySynth juce::juce_audio_utils)
    add_test(NAME native-envelope-audio-paths COMMAND WebPlaySynthEnvelopePathTest)

    add_executable(WebPlaySynthHardSyncUnisonTest tests/HardSyncUnisonTest.cpp)
    target_include_directories(WebPlaySynthHardSyncUnisonTest PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/WebPlaySynth_artefacts/JuceLibraryCode"
    )
    target_compile_features(WebPlaySynthHardSyncUnisonTest PRIVATE cxx_std_17)
    target_link_libraries(WebPlaySynthHardSyncUnisonTest PRIVATE WebPlaySynth juce::juce_audio_utils)
    add_test(NAME native-hard-sync-unison COMMAND WebPlaySynthHardSyncUnisonTest)

    add_executable(WebPlaySynthMidiExpressionTest tests/MidiExpressionPerformanceTest.cpp)
    target_include_directories(WebPlaySynthMidiExpressionTest PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/WebPlaySynth_artefacts/JuceLibraryCode"
    )
    target_compile_features(WebPlaySynthMidiExpressionTest PRIVATE cxx_std_17)
    target_link_libraries(WebPlaySynthMidiExpressionTest PRIVATE WebPlaySynth juce::juce_audio_utils)
    add_test(NAME native-midi-expression-performance COMMAND WebPlaySynthMidiExpressionTest)

    add_executable(WebPlaySynthAftertouchPathTest tests/AftertouchAudioPathTest.cpp)
    target_include_directories(WebPlaySynthAftertouchPathTest PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/WebPlaySynth_artefacts/JuceLibraryCode"
    )
    target_compile_features(WebPlaySynthAftertouchPathTest PRIVATE cxx_std_17)
    target_link_libraries(WebPlaySynthAftertouchPathTest PRIVATE WebPlaySynth juce::juce_audio_utils)
    add_test(NAME native-aftertouch-audio-paths COMMAND WebPlaySynthAftertouchPathTest)
endif()
