#pragma once

#include <cstdint>

// Stable, flat C ABI for the browser host. WasmEvent is a 20-byte packed-by-layout
// record (five 32-bit fields); static assertions in the implementation pin offsets.
extern "C" {

struct WpsWasmEvent {
    std::uint32_t type;
    std::uint32_t frameOffset;
    std::uint32_t channel;
    std::uint32_t note;
    float value;
};

std::uint32_t wps_abi_version() noexcept;
std::uint32_t wps_parameter_count() noexcept;
std::uint32_t wps_event_size() noexcept;
std::uint32_t wps_prepare(double sampleRateHz, std::uint32_t maximumFrames,
                          std::uint32_t outputChannels) noexcept;
void wps_release() noexcept;
std::uint32_t wps_reset(std::uint32_t mode) noexcept;
std::uint32_t wps_is_prepared() noexcept;

// All pointers refer to caller-owned linear-memory regions and remain valid only
// for this call. Output is planar: left is required, right is required for stereo.
// Event type values match EngineEventType's stable 0..8 order documented in README.
std::uint32_t wps_render(const float* parameters, std::uint32_t parameterCount,
                         std::uint64_t revision, const WpsWasmEvent* events,
                         std::uint32_t eventCount, float* left, float* right,
                         std::uint32_t frames) noexcept;

}
