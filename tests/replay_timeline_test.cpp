#include "../src/replay_timeline.hpp"
#include <cassert>
#include <vector>
#include <iostream>
struct Event { uint64_t frame; int value; };
int main() {
    dimbot::ReplayTimeline t;
    std::vector<Event> events{{1,1},{1,0},{1,1},{3,0},{9,1}};
    std::vector<int> seen;
    auto run = [&](uint64_t tick) {
        if (t.enter(tick)) dimbot::ReplayTimeline::drain(events,t.input,tick,
            [&](auto const& e) { seen.push_back(e.value); });
    };
    run(1); run(1);
    assert((seen == std::vector<int>{1,0,1}));
    run(5); assert(seen.back() == 0 && t.input == 4);
    run(9); assert(t.input == 5);
    bool rejected = false;
    try { run(2); } catch (std::logic_error const&) { rejected = true; }
    assert(rejected);
    t.correction = 42;
    t.reset(); assert(t.input == 0 && t.correction == 0);
    seen.clear(); run(1); assert(seen.size() == 3);
    for (double bad : {-1.0, 0.5, 4294967296.0,
                      std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::quiet_NaN()}) {
        rejected = false;
        try { dimbot::checkedReplayFrame(bad); } catch (std::invalid_argument const&) { rejected = true; }
        assert(rejected);
    }
    assert(dimbot::checkedReplayFrame(4294967295.0) == 4294967295ULL);
    for (int retry = 0; retry < 100; ++retry) {
        t.reset(); seen.clear(); run(1); run(3); run(9);
        assert((seen == std::vector<int>{1,0,1,0,1}));
    }
    std::cout << "PASS: same-tick ordering, duplicate ticks, catch-up, rewind guard, 100 retries, frame validation\n";
}
