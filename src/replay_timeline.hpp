#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace dimbot {
// Shared by the game adapter and standalone regression tests.
struct ReplayTimeline {
    uint64_t previous = std::numeric_limits<uint64_t>::max();
    size_t input = 0;
    size_t correction = 0;
    void reset() { previous = std::numeric_limits<uint64_t>::max(); input = correction = 0; }
    bool enter(uint64_t tick) {
        if (previous == tick) return false;
        if (previous != std::numeric_limits<uint64_t>::max() && tick < previous)
            throw std::logic_error("Timeline moved backwards without a reset");
        previous = tick;
        return true;
    }
    template<class Events, class Apply>
    static void drain(Events const& events, size_t& cursor, uint64_t tick, Apply apply) {
        while (cursor < events.size() && events[cursor].frame <= tick)
            apply(events[cursor++]);
    }
};
inline uint64_t checkedReplayFrame(double frame) {
    // GDR frame numbers are uint32; reject NaN, fractional and out-of-range data.
    if (!std::isfinite(frame) || frame < 0 || std::floor(frame) != frame ||
        frame > std::numeric_limits<uint32_t>::max())
        throw std::invalid_argument("Invalid replay frame");
    return static_cast<uint64_t>(frame);
}
}
