#include "WasmBridge.h"

#include "../plugin/Source/Engine/PolySynthEngine.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

using namespace webplaysynth::engine;

namespace {

static_assert(sizeof(WpsWasmEvent) == 20, "Wasm event ABI must remain 20 bytes");
static_assert(offsetof(WpsWasmEvent, type) == 0, "Wasm event ABI changed");
static_assert(offsetof(WpsWasmEvent, frameOffset) == 4, "Wasm event ABI changed");
static_assert(offsetof(WpsWasmEvent, channel) == 8, "Wasm event ABI changed");
static_assert(offsetof(WpsWasmEvent, note) == 12, "Wasm event ABI changed");
static_assert(offsetof(WpsWasmEvent, value) == 16, "Wasm event ABI changed");
static_assert(std::is_standard_layout<WpsWasmEvent>::value, "Wasm event ABI must be standard layout");

PolySynthEngine engine;

class WasmEventSource final : public IEngineEventSource {
public:
    WasmEventSource(const WpsWasmEvent* events, std::uint32_t count) noexcept
        : events_(events), count_(count) {}

    void rewind() noexcept override { cursor_ = 0; }

    bool next(EngineEvent& event) noexcept override
    {
        if (events_ == nullptr || cursor_ >= count_)
            return false;
        const auto& source = events_[cursor_++];
        if (source.type > static_cast<std::uint32_t>(EngineEventType::resetControllers)
            || source.channel > 255 || source.note > 255)
            return false;
        event.type = static_cast<EngineEventType>(source.type);
        event.frameOffset = source.frameOffset;
        event.channel = static_cast<std::uint8_t>(source.channel);
        event.note = static_cast<std::uint8_t>(source.note);
        event.value = source.value;
        return true;
    }

private:
    const WpsWasmEvent* events_ = nullptr;
    std::uint32_t count_ = 0;
    std::uint32_t cursor_ = 0;
};

} // namespace

extern "C" {

void* wps_alloc(std::uint32_t bytes) noexcept { return std::malloc(bytes); }
void wps_free(void* pointer) noexcept { std::free(pointer); }

std::uint32_t wps_abi_version() noexcept { return 1; }
std::uint32_t wps_parameter_count() noexcept { return synthParameterCount; }
std::uint32_t wps_event_size() noexcept { return static_cast<std::uint32_t>(sizeof(WpsWasmEvent)); }

std::uint32_t wps_prepare(double sampleRateHz, std::uint32_t maximumFrames,
                          std::uint32_t outputChannels) noexcept
{
    const ProcessSpec specification { sampleRateHz, maximumFrames, 0, outputChannels };
    return static_cast<std::uint32_t>(engine.prepare(specification));
}

void wps_release() noexcept { engine.release(); }

std::uint32_t wps_reset(std::uint32_t mode) noexcept
{
    if (mode > static_cast<std::uint32_t>(ResetMode::all))
        return static_cast<std::uint32_t>(EngineStatus::invalidSpecification);
    engine.reset(static_cast<ResetMode>(mode));
    return static_cast<std::uint32_t>(EngineStatus::ok);
}

std::uint32_t wps_is_prepared() noexcept { return engine.isPrepared() ? 1u : 0u; }

std::uint32_t wps_render(const float* parameters, std::uint32_t parameterCount,
                         std::uint64_t revision, const WpsWasmEvent* events,
                         std::uint32_t eventCount, float* left, float* right,
                         std::uint32_t frames) noexcept
{
    if (parameters == nullptr || left == nullptr || (eventCount != 0 && events == nullptr))
        return static_cast<std::uint32_t>(EngineStatus::invalidAudioBlock);

    const auto channels = engine.isPrepared() ? 2u : 0u;
    (void) channels;

    // The prepared output layout is selected by wps_prepare. A non-null right
    // pointer means stereo; otherwise the caller requests mono.
    float* outputChannels[2] { left, right };
    const auto outputCount = right != nullptr ? 2u : 1u;
    WasmEventSource source(events, eventCount);
    EngineProcessBlock block;
    block.output = { outputChannels, outputCount, frames };
    block.events = { nullptr, eventCount, eventCount == 0 ? nullptr : &source };
    block.parameters = { parameters, parameterCount, parameterSchemaVersion, revision };
    block.modulation = { nullptr, 0, frames };
    return static_cast<std::uint32_t>(engine.process(block));
}

}
