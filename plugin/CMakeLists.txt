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
    add_executable(WebPlaySynthOscillatorPrimitivesTest tests/OscillatorPrimitivesTest.cpp)
    target_include_directories(WebPlaySynthOscillatorPrimitivesTest PRIVATE Source)
    target_compile_features(WebPlaySynthOscillatorPrimitivesTest PRIVATE cxx_std_17)
    add_test(NAME native-portable-oscillator-primitives COMMAND WebPlaySynthOscillatorPrimitivesTest)

    add_executable(WebPlaySynthTptFilterParityTest tests/TptFilterParityTest.cpp)
    target_include_directories(WebPlaySynthTptFilterParityTest PRIVATE Source)
    target_compile_features(WebPlaySynthTptFilterParityTest PRIVATE cxx_std_17)
    target_compile_definitions(WebPlaySynthTptFilterParityTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(WebPlaySynthTptFilterParityTest PRIVATE juce::juce_dsp)
    add_test(NAME native-tpt-filter-primitive-parity COMMAND WebPlaySynthTptFilterParityTest)

    add_executable(WebPlaySynthDspPrimitiveParityTest tests/DspPrimitiveParityTest.cpp)
    target_include_directories(WebPlaySynthDspPrimitiveParityTest PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/WebPlaySynth_artefacts/JuceLibraryCode"
    )
    target_compile_features(WebPlaySynthDspPrimitiveParityTest PRIVATE cxx_std_17)
    target_link_libraries(WebPlaySynthDspPrimitiveParityTest PRIVATE WebPlaySynth juce::juce_audio_utils)
    add_test(NAME native-dsp-primitive-render-parity COMMAND WebPlaySynthDspPrimitiveParityTest)

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

    add_executable(WebPlaySynthDspStressTest tests/DspStressTest.cpp)
    target_include_directories(WebPlaySynthDspStressTest PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/WebPlaySynth_artefacts/JuceLibraryCode"
    )
    target_compile_features(WebPlaySynthDspStressTest PRIVATE cxx_std_17)
    target_link_libraries(WebPlaySynthDspStressTest PRIVATE WebPlaySynth juce::juce_audio_utils)
    add_test(NAME native-dsp-stress-matrix COMMAND WebPlaySynthDspStressTest)

    add_executable(WebPlaySynthDspAuditTest tests/DspAuditTest.cpp)
    target_include_directories(WebPlaySynthDspAuditTest PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/WebPlaySynth_artefacts/JuceLibraryCode"
    )
    target_compile_features(WebPlaySynthDspAuditTest PRIVATE cxx_std_17)
    target_link_libraries(WebPlaySynthDspAuditTest PRIVATE WebPlaySynth juce::juce_audio_utils)
    add_test(NAME native-dsp-audit COMMAND WebPlaySynthDspAuditTest)
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
    add_executable(WebPlaySynthFilterModeDriveTest tests/FilterModeDriveAudioPathTest.cpp)
    target_include_directories(WebPlaySynthFilterModeDriveTest PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/WebPlaySynth_artefacts/JuceLibraryCode"
    )
    target_compile_features(WebPlaySynthFilterModeDriveTest PRIVATE cxx_std_17)
    target_link_libraries(WebPlaySynthFilterModeDriveTest PRIVATE WebPlaySynth juce::juce_audio_utils)
    add_test(NAME native-filter-mode-drive-audio-paths COMMAND WebPlaySynthFilterModeDriveTest)
    add_executable(WebPlaySynthLfoPathTest tests/LfoAudioPathTest.cpp)
    target_include_directories(WebPlaySynthLfoPathTest PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/WebPlaySynth_artefacts/JuceLibraryCode"
    )
    target_compile_features(WebPlaySynthLfoPathTest PRIVATE cxx_std_17)
    target_link_libraries(WebPlaySynthLfoPathTest PRIVATE WebPlaySynth juce::juce_audio_utils)
    add_test(NAME native-lfo-audio-paths COMMAND WebPlaySynthLfoPathTest)

    add_executable(WebPlaySynthLfoAmpPanPathTest tests/LfoAmpPanAudioPathTest.cpp)
    target_include_directories(WebPlaySynthLfoAmpPanPathTest PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/WebPlaySynth_artefacts/JuceLibraryCode"
    )
    target_compile_features(WebPlaySynthLfoAmpPanPathTest PRIVATE cxx_std_17)
    target_link_libraries(WebPlaySynthLfoAmpPanPathTest PRIVATE WebPlaySynth juce::juce_audio_utils)
    add_test(NAME native-lfo-amp-pan-audio-paths COMMAND WebPlaySynthLfoAmpPanPathTest)

    # Exercise the actual VST3 binary through JUCE's plugin-host wrapper.
    # The direct processor tests above cannot catch wrapper/bus/MIDI integration
    # failures that occur only after a DAW loads the exported plugin.
    add_executable(WebPlaySynthVst3HostAudioTest tests/Vst3HostAudioTest.cpp)
    target_compile_features(WebPlaySynthVst3HostAudioTest PRIVATE cxx_std_17)
    target_compile_definitions(WebPlaySynthVst3HostAudioTest PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(WebPlaySynthVst3HostAudioTest PRIVATE juce::juce_audio_utils)
    add_test(NAME native-vst3-host-audio
        COMMAND WebPlaySynthVst3HostAudioTest
            "${CMAKE_CURRENT_BINARY_DIR}/WebPlaySynth_artefacts/$<CONFIG>/VST3/Web Play Synth.vst3")
endif()
