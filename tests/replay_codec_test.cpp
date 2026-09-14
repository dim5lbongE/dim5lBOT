#include "../src/replay_codec.hpp"
#include "../src/replay_timeline.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace dimbot;
    // Fixture uses GDR's external field names and two inputs on the same tick.
    std::string source = R"({"version":1.0,"framerate":240.0,"bot":{"name":"xdBot","version":"v2.4.1"},"inputs":[{"frame":1,"btn":1,"2p":false,"down":true},{"frame":1,"btn":1,"2p":false,"down":false}],"frameFixes":[{"frame":2,"p1":{"x":0,"y":12,"r":0}}]})";
    auto bytes = encodeGdr(source);
    assert(bytes.front() != '{');
    std::string packed(bytes.begin(), bytes.end());
    auto restored = nlohmann::json::parse(decodeGdr(packed, true));
    assert(restored == nlohmann::json::parse(source));
    assert(restored["inputs"][0]["down"] == true);
    assert(restored["inputs"][1]["down"] == false);
    assert(nlohmann::json::parse(decodeGdr(source, false)) == restored);
    assert(xdFrameOffset("v2.3.5") == 1);
    assert(xdFrameOffset("2.3.6") == 0);
    assert(xdFrameOffset("v2.4.1") == 0);
    for (auto bad : {"", "v2", "2.4.1.beta", "2.4.1-beta"}) {
        bool failed = false;
        try { xdFrameOffset(bad); } catch (...) { failed = true; }
        assert(failed);
    }
    for (auto bad : {std::string("\xc1",1), packed.substr(0,packed.size()-1), packed+"junk"}) {
        bool failed = false;
        try { decodeGdr(bad,true); } catch (...) { failed = true; }
        assert(failed);
    }
    struct Event { uint64_t frame; };
    std::vector<Event> events{{1},{3},{3},{7}};
    assert(replayCursorAfter(events,0)==0);
    assert(replayCursorAfter(events,3)==3);
    assert(replayCursorAfter(events,7)==4);
    for (int retry=0;retry<100;++retry) {
        size_t cursor=replayCursorAfter(events,3);
        std::vector<uint64_t> played;
        ReplayTimeline::drain(events,cursor,7,[&](auto e){played.push_back(e.frame);});
        assert((played==std::vector<uint64_t>{7}));
    }
    std::cout << "PASS: GDR MessagePack/JSON, input order, version offsets, corrupt files, checkpoint seek\n";
}
