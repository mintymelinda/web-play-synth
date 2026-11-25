cmake_minimum_required(VERSION 3.22)
project(WebPlaySynthWasm LANGUAGES CXX)

enable_testing()

set(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../plugin/Source/Engine")
set(ENGINE_SOURCES
    "${ENGINE_ROOT}/VoiceEngine.cpp"
    "${ENGINE_ROOT}/PolySynthEngine.cpp"
)

function(configure_engine_target target)
    target_include_directories(${target} PRIVATE "${ENGINE_ROOT}")
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic
        -fno-exceptions
        -fno-rtti
    )
endfunction()

if(EMSCRIPTEN)
    add_executable(webplaysynth_engine
        WasmBridge.cpp
        ${ENGINE_SOURCES}
    )
    configure_engine_target(webplaysynth_engine)
    set_target_properties(webplaysynth_engine PROPERTIES SUFFIX ".wasm")
    target_link_options(webplaysynth_engine PRIVATE
        -O3
        -sSTANDALONE_WASM=1
        -sFILESYSTEM=0
        -sALLOW_MEMORY_GROWTH=0
        -sERROR_ON_UNDEFINED_SYMBOLS=1
        "-sEXPORTED_FUNCTIONS=['_wps_abi_version','_wps_parameter_count','_wps_event_size','_wps_prepare','_wps_release','_wps_reset','_wps_is_prepared','_wps_render']"
        -Wl,--no-entry
        -Wl,--export-memory
    )
else()
    add_executable(WebPlaySynthWasmBridgeTest
        tests/WasmBridgeTest.cpp
        WasmBridge.cpp
        ${ENGINE_SOURCES}
    )
    configure_engine_target(WebPlaySynthWasmBridgeTest)
    add_test(NAME native-wasm-bridge-contract COMMAND WebPlaySynthWasmBridgeTest)
endif()
