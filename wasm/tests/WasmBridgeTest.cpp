#include "../WasmBridge.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {
constexpr std::uint32_t parameterCount = 52;
constexpr std::uint32_t ampAttack = 22;
constexpr std::uint32_t ampDecay = 23;
constexpr std::uint32_t ampSustain = 24;
constexpr std::uint32_t ampRelease = 25;
constexpr std::uint32_t vco1Enabled = 48;
constexpr std::uint32_t vco2Enabled = 49;
constexpr std::uint32_t filterEnvEnabled = 50;
constexpr std::uint32_t ampEnvEnabled = 51;
}

int main()
{
    if (wps_abi_version() != 1 || wps_parameter_count() != parameterCount
        || wps_event_size() != sizeof(WpsWasmEvent)) {
        std::fprintf(stderr, "WASM ABI metadata mismatch\n");
        return 1;
    }

    if (wps_prepare(48000.0, 256, 1) != 0 || wps_is_prepared() != 1) {
        std::fprintf(stderr, "WASM bridge failed to prepare\n");
        return 1;
    }

    std::array<float, parameterCount> parameters {};
    parameters[0] = 2.0f;
    parameters[1] = 3.0f;
    parameters[4] = 0.72f;
    parameters[8] = 0.38f;
    parameters[12] = 1800.0f;
    parameters[13] = 0.8f;
    parameters[14] = 0.55f;
    parameters[ampAttack] = 0.015f;
    parameters[ampDecay] = 0.25f;
    parameters[ampSustain] = 0.78f;
    parameters[ampRelease] = 0.32f;
    parameters[48] = 1.0f;
    parameters[vco1Enabled] = 1.0f;
    parameters[vco2Enabled] = 1.0f;
    parameters[filterEnvEnabled] = 1.0f;
    parameters[ampEnvEnabled] = 1.0f;

    const WpsWasmEvent noteOn { 0, 0, 1, 69, 0.82f };
    std::array<float, 256> output {};
    const auto status = wps_render(parameters.data(), parameterCount, 1,
        &noteOn, 1, output.data(), nullptr, static_cast<std::uint32_t>(output.size()));
    if (status != 0) {
        std::fprintf(stderr, "WASM bridge render failed: %u\n", status);
        return 1;
    }

    double energy = 0.0;
    for (const auto sample : output) {
        if (!std::isfinite(sample)) {
            std::fprintf(stderr, "WASM bridge produced non-finite audio\n");
            return 1;
        }
        energy += static_cast<double>(sample) * sample;
    }
    if (energy < 1.0e-7) {
        std::fprintf(stderr, "WASM bridge produced silence for a note-on\n");
        return 1;
    }

    if (wps_reset(2) != 0 || wps_is_prepared() != 1) {
        std::fprintf(stderr, "WASM bridge reset changed its prepared state\n");
        return 1;
    }
    wps_release();
    if (wps_is_prepared() != 0) {
        std::fprintf(stderr, "WASM bridge release did not clear prepared state\n");
        return 1;
    }
    return 0;
}
