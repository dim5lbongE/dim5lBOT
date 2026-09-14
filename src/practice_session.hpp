#pragma once
#include <Geode/Geode.hpp>
#include <array>
#include <unordered_map>
#include <cstdint>

namespace dimbot {
// GD 2.2081 exposes a full native player checkpoint. Use its typed save/load
// operations instead of copying offsets from xdBot's GD 2.2074 player layout.
struct PracticePlayer {
    geode::Ref<PlayerCheckpoint> state;
    gd::map<int, bool> holding;
    static PracticePlayer capture(PlayerObject* player) {
        PracticePlayer result;
        if (player) {
            result.state = PlayerCheckpoint::create();
            player->saveToCheckpoint(result.state);
            result.holding = player->m_holdingButtons;
        }
        return result;
    }
    void restore(PlayerObject* player) const {
        if (!player || !state) return;
        player->loadFromCheckpoint(state);
        player->m_holdingButtons = holding;
    }
};
struct PracticeSnapshot {
    // Retention prevents a deleted checkpoint's address matching a new one.
    geode::Ref<CheckpointObject> checkpoint;
    PracticePlayer p1, p2;
    uint64_t frame = 0, session = 0;
    static PracticeSnapshot capture(CheckpointObject* cp, PlayerObject* p1,
        PlayerObject* p2, uint64_t frame, uint64_t session) {
        PracticeSnapshot result;
        result.checkpoint = cp;
        result.p1 = PracticePlayer::capture(p1);
        result.p2 = PracticePlayer::capture(p2);
        result.frame = frame;
        result.session = session;
        return result;
    }
    void restore(PlayerObject* first, PlayerObject* second) const {
        p1.restore(first);
        p2.restore(second);
    }
};
}
