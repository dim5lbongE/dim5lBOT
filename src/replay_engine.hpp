#pragma once
#include <Geode/Geode.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>
#include "replay_timeline.hpp"

namespace dimbot {
using namespace geode::prelude;
enum class Mode {
    Idle,
    Recording,
    Playing
};

struct Input {
    uint64_t frame = 0;
    bool down = false;
    int button = 1;
    bool player1 = true;
};

struct PlayerFix {
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    bool valid = true;
    bool rotate = true;
};

struct FrameFix {
    uint64_t frame = 0;
    PlayerFix player1;
    PlayerFix player2;
};

struct Engine {
    Mode mode = Mode::Idle;
    std::vector<Input> inputs;
    std::vector<FrameFix> frameFixes;
    uint64_t frame = 0;
    uint64_t replayEndFrame = 0;
    uint64_t previousProcessedFrame = std::numeric_limits<uint64_t>::max();
    size_t playbackIndex = 0;
    size_t frameFixIndex = 0;
    bool injecting = false;
    bool playAfterReset = false;
    bool replaySessionActive = false;
    bool safeMode = true;
    bool noclip = false;
    bool assistedSession = false;
    bool pendingDeathCheck = false;
    bool levelCompletionInProgress = false;
    bool resetting = false;
    bool flippedControls = false;
    uint64_t session = 0;
    bool checkpointRestored = false;
    uint64_t resumeFrame = 0;
    uint64_t reconcileFrame = 0;
    std::array<bool, 6> physicalButtons{};
    std::array<bool, 6> recordingBlockedButtons{};
    ReplayTimeline timeline;
    int replayLevelId = 0;
    std::string replayLevelName;
    float speedMultiplier = 1.f;
    std::string message = "Ready";

    static Engine& get() {
        static Engine engine;
        return engine;
    }

    void stop(std::string text = "Stopped") {
        bool wasPlaying = mode == Mode::Playing;
        reconcileFrame = resumeFrame = 0;
        mode = Mode::Idle;
        playAfterReset = false;
        replaySessionActive = false;
        injecting = false;
        pendingDeathCheck = false;
        message = std::move(text);
        if (wasPlaying) {
            assistedSession = true;
            if (auto layer = PlayLayer::get()) {
                if (layer->m_player1) layer->m_player1->releaseAllButtons();
                if (layer->m_player2) layer->m_player2->releaseAllButtons();
            }
        }
    }

    void beginRecording() {
        ++session;
        // Ignore keys already held when Record was pressed until release.
        recordingBlockedButtons = physicalButtons;
        reconcileFrame = resumeFrame = 0;
        timeline.reset();
        inputs.clear();
        frame = 0;
        replayEndFrame = 0;
        playbackIndex = 0;
        frameFixIndex = 0;
        previousProcessedFrame = std::numeric_limits<uint64_t>::max();
        frameFixes.clear();
        mode = Mode::Recording;
        playAfterReset = false;
        replaySessionActive = false;
        pendingDeathCheck = false;
        levelCompletionInProgress = false;
        message = "Recording";
    }

    void requestPlayback() {
        ++session;
        reconcileFrame = resumeFrame = 0;
        if (inputs.empty()) {
            message = "No replay loaded";
            return;
        }
        mode = Mode::Idle;
        frame = 0;
        playbackIndex = 0;
        frameFixIndex = 0;
        previousProcessedFrame = std::numeric_limits<uint64_t>::max();
        playAfterReset = true;
        message = "Preparing replay";
    }

    void beginPlaybackAfterReset() {
        timeline.reset();
        frame = 0;
        playbackIndex = 0;
        frameFixIndex = 0;
        previousProcessedFrame = std::numeric_limits<uint64_t>::max();
        playAfterReset = false;
        replaySessionActive = true;
        assistedSession = true;
        mode = Mode::Playing;
        message = "Playing";
    }

    float speed() const {
        return speedMultiplier;
    }

    void applySpeed() {
        // Keep cocos' own time scale neutral. The scheduler hook below applies
        // the multiplier to every update, so a restart cannot silently reset
        // the selected speed back to 1x.
        cocos2d::CCScheduler::get()->setTimeScale(1.f);
        if (PlayLayer::get() && speed() != 1.f) assistedSession = true;
    }

    void setSpeed(float value) {
        speedMultiplier = std::clamp(value, .1f, 10.f);
        applySpeed();
        message = fmt::format("Speedhack: {:.1f}x", speed());
    }

    void toggleNoclip() {
        noclip = !noclip;
        if (PlayLayer::get() && noclip) assistedSession = true;
        message = noclip ? "Noclip enabled" : "Noclip disabled";
    }

    void resetCheats() {
        noclip = false;
        speedMultiplier = 1.f;
        assistedSession = false;
        cocos2d::CCScheduler::get()->setTimeScale(1.f);
    }

    void record(bool down, int button, bool player1) {
        if (mode != Mode::Recording || injecting) return;
        Input next{frame, down, button, player1};

        // Preserve every accepted event in arrival order, including same-tick
        // press/release sequences; a repeated callback is not proof of a duplicate.
        inputs.push_back(next);
    }

    void recordFrameFix(uint64_t atFrame, PlayerObject* p1, PlayerObject* p2) {
        if (mode != Mode::Recording || !p1 || !p2) return;
        auto normalize = [](float rotation) {
            rotation = std::fmod(rotation, 360.f);
            return rotation < 0.f ? rotation + 360.f : rotation;
        };
        FrameFix fix;
        fix.frame = atFrame;
        fix.player1 = {p1->getPositionX(), p1->getPositionY(), normalize(p1->getRotation())};
        fix.player2 = {p2->getPositionX(), p2->getPositionY(), normalize(p2->getRotation())};
        if (!frameFixes.empty() && frameFixes.back().frame == atFrame)
            frameFixes.back() = fix;
        else frameFixes.push_back(fix);
    }

    void trimToFrame(uint64_t targetFrame) {
        std::erase_if(inputs, [targetFrame](Input const& input) {
            return input.frame >= targetFrame;
        });
        std::erase_if(frameFixes, [targetFrame](FrameFix const& fix) {
            return fix.frame >= targetFrame;
        });
        frame = targetFrame;
        replayEndFrame = targetFrame;
        message = fmt::format("Practice rewind: frame {}", targetFrame);
    }

    void discardFailedRecording() {
        stop("Recording deleted after death");
        inputs.clear();
        frame = 0;
        replayEndFrame = 0;
        playbackIndex = 0;
        frameFixIndex = 0;
        frameFixes.clear();
    }
};

uint64_t currentGameFrame() {
    auto layer = PlayLayer::get();
    if (!layer) return 0;
    auto time = std::max(0.0, static_cast<double>(layer->m_gameState.m_levelTime));
    // Match xdBot's clock: truncate the level-time product and address the
    // command tick that is about to receive the input.
    return static_cast<uint64_t>(time * 240.0) + 1;
}


}
