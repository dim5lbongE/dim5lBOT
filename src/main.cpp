#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>
#include "replay_timeline.hpp"
#include "replay_storage.hpp"
#include "practice_session.hpp"

using namespace geode::prelude;

namespace dimbot {

std::string stateText() {
    auto const& engine = Engine::get();
    char const* mode = "IDLE";
    if (engine.mode == Mode::Recording) mode = "RECORDING";
    if (engine.mode == Mode::Playing) mode = "PLAYING";
    return fmt::format(
        "{}  |  Frame {}  |  {} inputs",
        mode,
        engine.frame,
        engine.inputs.size()
    );
}

class SaveReplayPopup final : public Popup {
protected:
    TextInput* m_nameInput = nullptr;
    CCLabelBMFont* m_status = nullptr;

    bool init() {
        if (!Popup::init(340.f, 180.f)) return false;
        setTitle("SAVE REPLAY");
        m_nameInput = TextInput::create(245.f, "Replay name");
        m_nameInput->setCommonFilter(CommonFilter::Any);
        m_nameInput->setMaxCharCount(60);
        m_nameInput->setPosition({170.f, 105.f});
        m_mainLayer->addChild(m_nameInput);

        m_status = CCLabelBMFont::create("GDR format - existing saves are kept", "chatFont.fnt");
        m_status->setScale(.5f);
        m_status->setColor({190, 205, 235});
        m_status->setPosition({170.f, 72.f});
        m_mainLayer->addChild(m_status);

        auto sprite = ButtonSprite::create("Save", 105, true, "bigFont.fnt", "GJ_button_01.png", 30.f, .6f);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(SaveReplayPopup::onSave));
        button->setPosition({170.f, 38.f});
        m_buttonMenu->addChild(button);
        return true;
    }

    void onSave(CCObject*) {
        std::string name = m_nameInput ? std::string(m_nameInput->getString()) : "";
        auto result = saveMacro(name);
        if (result.isErr()) {
            m_status->setString(result.unwrapErr().c_str());
            m_status->setColor({255, 100, 100});
            return;
        }
        onClose(nullptr);
    }

public:
    static SaveReplayPopup* create() {
        auto popup = new SaveReplayPopup();
        if (popup && popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

class LoadReplayPopup final : public Popup {
protected:
    static constexpr size_t PageSize = 5;
    std::vector<std::filesystem::path> m_files;
    std::vector<CCNode*> m_rows;
    size_t m_page = 0;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCLabelBMFont* m_emptyLabel = nullptr;

    bool init() {
        if (!Popup::init(390.f, 280.f)) return false;
        setTitle("LOAD REPLAY");
        m_files = listMacros();

        m_pageLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_pageLabel->setScale(.55f);
        m_pageLabel->setPosition({195.f, 30.f});
        m_mainLayer->addChild(m_pageLabel);

        addNavButton("<", {55.f, 30.f}, menu_selector(LoadReplayPopup::onPrevious));
        addNavButton(">", {335.f, 30.f}, menu_selector(LoadReplayPopup::onNext));
        rebuildPage();
        return true;
    }

    void addNavButton(char const* text, CCPoint position, SEL_MenuHandler callback) {
        auto sprite = ButtonSprite::create(text, 50, true, "bigFont.fnt", "GJ_button_04.png", 28.f, .6f);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, callback);
        button->setPosition(position);
        m_buttonMenu->addChild(button);
    }

    void rebuildPage() {
        for (auto node : m_rows) node->removeFromParent();
        m_rows.clear();
        if (m_emptyLabel) {
            m_emptyLabel->removeFromParent();
            m_emptyLabel = nullptr;
        }

        auto pages = std::max<size_t>(1, (m_files.size() + PageSize - 1) / PageSize);
        if (m_page >= pages) m_page = pages - 1;
        m_pageLabel->setString(fmt::format("Page {}/{}  |  {} replays", m_page + 1, pages, m_files.size()).c_str());

        if (m_files.empty()) {
            m_emptyLabel = CCLabelBMFont::create("No saved replays", "bigFont.fnt");
            m_emptyLabel->setScale(.5f);
            m_emptyLabel->setPosition({195.f, 145.f});
            m_mainLayer->addChild(m_emptyLabel);
            return;
        }

        auto begin = m_page * PageSize;
        auto end = std::min(begin + PageSize, m_files.size());
        for (size_t index = begin; index < end; ++index) {
            auto displayPath = m_files[index].stem();
            if (displayPath.extension() == ".gdr") displayPath = displayPath.stem();
            auto name = displayPath.string();
            auto sprite = ButtonSprite::create(name.c_str(), 205, true, "bigFont.fnt", "GJ_button_01.png", 30.f, .48f);
            auto button = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(LoadReplayPopup::onSelect));
            button->setTag(static_cast<int>(index));
            button->setPosition({145.f, 220.f - static_cast<float>(index - begin) * 38.f});
            m_buttonMenu->addChild(button);
            m_rows.push_back(button);

            auto deleteSprite = ButtonSprite::create("Delete", 80, true, "bigFont.fnt", "GJ_button_06.png", 24.f, .45f);
            auto deleteButton = CCMenuItemSpriteExtra::create(
                deleteSprite, this, menu_selector(LoadReplayPopup::onDelete)
            );
            deleteButton->setTag(static_cast<int>(index));
            deleteButton->setPosition({315.f, 220.f - static_cast<float>(index - begin) * 38.f});
            m_buttonMenu->addChild(deleteButton);
            m_rows.push_back(deleteButton);
        }
    }

    void onSelect(CCObject* sender) {
        auto index = static_cast<size_t>(static_cast<CCNode*>(sender)->getTag());
        if (index >= m_files.size()) return;
        auto result = loadMacro(m_files[index]);
        if (result.isErr()) {
            Engine::get().message = result.unwrapErr();
            return;
        }
        onClose(nullptr);
    }

    void onPrevious(CCObject*) {
        if (m_page > 0) --m_page;
        rebuildPage();
    }

    void onNext(CCObject*) {
        auto pages = std::max<size_t>(1, (m_files.size() + PageSize - 1) / PageSize);
        if (m_page + 1 < pages) ++m_page;
        rebuildPage();
    }

    void onDelete(CCObject* sender) {
        auto index = static_cast<size_t>(static_cast<CCNode*>(sender)->getTag());
        if (index >= m_files.size()) return;
        auto deletedName = m_files[index].stem().string();
        std::error_code error;
        std::filesystem::remove(m_files[index], error);
        if (error) {
            m_pageLabel->setString("Could not delete replay");
            m_pageLabel->setColor({255, 100, 100});
            return;
        }
        Engine::get().message = fmt::format("Deleted: {}", deletedName);
        m_pageLabel->setColor({255, 255, 255});
        m_files = listMacros();
        rebuildPage();
    }

public:
    static LoadReplayPopup* create() {
        auto popup = new LoadReplayPopup();
        if (popup && popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

class SpeedInputPopup final : public Popup {
protected:
    TextInput* m_input = nullptr;
    CCLabelBMFont* m_status = nullptr;

    bool init() {
        if (!Popup::init(300.f, 180.f)) return false;
        setTitle("SET SPEED");

        m_input = TextInput::create(190.f, "0.1 - 10.0");
        m_input->setCommonFilter(CommonFilter::Float);
        m_input->setMaxCharCount(5);
        m_input->setString(fmt::format("{:.1f}", Engine::get().speed()));
        m_input->setPosition({150.f, 105.f});
        m_mainLayer->addChild(m_input);

        m_status = CCLabelBMFont::create("Enter a multiplier from 0.1x to 10.0x", "chatFont.fnt");
        m_status->setScale(.45f);
        m_status->setPosition({150.f, 72.f});
        m_mainLayer->addChild(m_status);

        auto sprite = ButtonSprite::create("Apply", 100, true, "bigFont.fnt", "GJ_button_01.png", 28.f, .55f);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(SpeedInputPopup::onApply));
        button->setPosition({150.f, 38.f});
        m_buttonMenu->addChild(button);
        return true;
    }

    void onApply(CCObject*) {
        auto text = m_input ? std::string(m_input->getString()) : "";
        try {
            size_t parsed = 0;
            auto value = std::stof(text, &parsed);
            if (parsed != text.size() || value < .1f || value > 10.f) throw std::out_of_range("speed");
            Engine::get().setSpeed(value);
            onClose(nullptr);
        } catch (...) {
            m_status->setString("Enter a valid number from 0.1 to 10.0");
            m_status->setColor({255, 100, 100});
        }
    }

public:
    static SpeedInputPopup* create() {
        auto popup = new SpeedInputPopup();
        if (popup && popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

class ToolsPopup final : public Popup {
protected:
    ButtonSprite* m_speedSprite = nullptr;
    ButtonSprite* m_noclipSprite = nullptr;

    bool init() {
        if (!Popup::init(320.f, 190.f)) return false;
        setTitle("GAMEPLAY TOOLS");

        auto warning = CCLabelBMFont::create("Safe Mode blocks assisted completions", "chatFont.fnt");
        warning->setScale(.5f);
        warning->setColor({255, 200, 90});
        warning->setPosition({160.f, 125.f});
        m_mainLayer->addChild(warning);

        m_speedSprite = addButton("", {95.f, 75.f}, menu_selector(ToolsPopup::onSpeed));
        m_noclipSprite = addButton("", {225.f, 75.f}, menu_selector(ToolsPopup::onNoclip));
        refreshButtons();
        return true;
    }

    ButtonSprite* addButton(char const* text, CCPoint position, SEL_MenuHandler callback) {
        auto sprite = ButtonSprite::create(text, 115, true, "bigFont.fnt", "GJ_button_01.png", 30.f, .5f);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, callback);
        button->setPosition(position);
        m_buttonMenu->addChild(button);
        return sprite;
    }

    void refreshButtons() {
        auto& engine = Engine::get();
        m_speedSprite->setString(fmt::format("Speed {:.1f}x", engine.speed()).c_str());
        m_speedSprite->setColor(engine.speed() == 1.f ? ccColor3B{85, 105, 130} : ccColor3B{205, 120, 45});
        m_noclipSprite->setString(engine.noclip ? "Noclip: ON" : "Noclip: OFF");
        m_noclipSprite->setColor(engine.noclip ? ccColor3B{190, 70, 55} : ccColor3B{85, 105, 130});
    }

    void onSpeed(CCObject*) {
        if (auto popup = SpeedInputPopup::create()) popup->show();
    }

    void onNoclip(CCObject*) {
        Engine::get().toggleNoclip();
        refreshButtons();
    }

    void onEnter() override {
        Popup::onEnter();
        schedule(schedule_selector(ToolsPopup::refresh), .1f);
    }

    void refresh(float) {
        refreshButtons();
    }

public:
    static ToolsPopup* create() {
        auto popup = new ToolsPopup();
        if (popup && popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

class BotPopup final : public Popup {
protected:
    CCLabelBMFont* m_stateLabel = nullptr;
    CCLabelBMFont* m_messageLabel = nullptr;
    PauseLayer* m_pauseLayer = nullptr;
    ButtonSprite* m_safeModeSprite = nullptr;

    bool init() {
        if (!Popup::init(390.f, 270.f)) return false;
        setTitle("dim5lBOT");
        m_bgSprite->setColor({25, 20, 52});
        m_bgSprite->setOpacity(245);

        auto subtitle = CCLabelBMFont::create("REPLAY CONTROL", "goldFont.fnt");
        subtitle->setScale(.48f);
        subtitle->setPosition({m_size.width / 2.f, m_size.height - 55.f});
        m_mainLayer->addChild(subtitle);

        m_stateLabel = CCLabelBMFont::create(stateText().c_str(), "bigFont.fnt");
        m_stateLabel->setScale(.38f);
        m_stateLabel->setPosition({m_size.width / 2.f, m_size.height - 82.f});
        m_mainLayer->addChild(m_stateLabel);

        m_messageLabel = CCLabelBMFont::create(Engine::get().message.c_str(), "chatFont.fnt");
        m_messageLabel->setScale(.55f);
        m_messageLabel->setColor({175, 205, 255});
        m_messageLabel->setPosition({m_size.width / 2.f, 165.f});
        m_mainLayer->addChild(m_messageLabel);

        // Popup::m_buttonMenu uses the popup's bottom-left as its origin.
        // Keep every button inside the 390x245 content area on all aspect ratios.
        addButton("Record", {75.f, 120.f}, menu_selector(BotPopup::onRecord), {210, 55, 72});
        addButton("Stop", {195.f, 120.f}, menu_selector(BotPopup::onStop), {95, 100, 122});
        addButton("Play", {315.f, 120.f}, menu_selector(BotPopup::onPlay), {55, 175, 105});
        addButton("Save", {75.f, 72.f}, menu_selector(BotPopup::onSave), {65, 120, 210});
        addButton("Load", {195.f, 72.f}, menu_selector(BotPopup::onLoad), {116, 80, 205});
        addButton("Clear", {315.f, 72.f}, menu_selector(BotPopup::onClear), {205, 115, 45});
        auto& engine = Engine::get();
        m_safeModeSprite = addButton(engine.safeMode ? "Safe: ON" : "Safe: OFF", {130.f, 27.f}, menu_selector(BotPopup::onSafeMode), engine.safeMode ? ccColor3B{45, 170, 90} : ccColor3B{190, 70, 55});
        addButton("Tools", {260.f, 27.f}, menu_selector(BotPopup::onTools), {205, 85, 45});

        schedule(schedule_selector(BotPopup::refresh), .05f);
        return true;
    }

    ButtonSprite* addButton(char const* text, CCPoint position, SEL_MenuHandler callback, ccColor3B color) {
        auto sprite = ButtonSprite::create(text, 95, true, "bigFont.fnt", "GJ_button_01.png", 30.f, .55f);
        sprite->setColor(color);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, callback);
        button->setPosition(position);
        m_buttonMenu->addChild(button);
        return sprite;
    }

    void refresh(float) {
        if (m_stateLabel) m_stateLabel->setString(stateText().c_str());
        if (m_messageLabel) m_messageLabel->setString(Engine::get().message.c_str());
    }

    void onRecord(CCObject*) {
        if (!PlayLayer::get()) {
            Engine::get().message = "Enter a level first";
            return;
        }
        auto layer = PlayLayer::get();
        Engine::get().beginRecording();
        Engine::get().flippedControls = false;
        Engine::get().replayLevelId = layer->m_level->m_levelID.value();
        Engine::get().replayLevelName = layer->m_level->m_levelName;
        layer->resetLevelFromStart();
        auto pauseLayer = m_pauseLayer;
        onClose(nullptr);
        if (pauseLayer) pauseLayer->onResume(nullptr);
    }

    void onStop(CCObject*) {
        Engine::get().stop();
    }

    void onPlay(CCObject*) {
        auto layer = PlayLayer::get();
        if (!layer) {
            Engine::get().message = "Enter a level first";
            return;
        }
        if (Engine::get().replayLevelId > 0 &&
            Engine::get().replayLevelId != layer->m_level->m_levelID.value()) {
            Engine::get().message = "Replay belongs to a different level";
            return;
        }
        Engine::get().requestPlayback();
        if (Engine::get().playAfterReset) {
            layer->resetLevelFromStart();
        }
        auto pauseLayer = m_pauseLayer;
        onClose(nullptr);
        // The bot popup is opened from PauseLayer. Closing only this popup
        // leaves the game frozen, so resume the underlying level as part of Play.
        if (pauseLayer) pauseLayer->onResume(nullptr);
    }

    void onSave(CCObject*) {
        if (auto popup = SaveReplayPopup::create()) popup->show();
    }

    void onLoad(CCObject*) {
        if (auto popup = LoadReplayPopup::create()) popup->show();
    }

    void onClear(CCObject*) {
        auto& engine = Engine::get();
        engine.stop();
        engine.inputs.clear();
        engine.frameFixes.clear();
        engine.frame = 0;
        engine.replayEndFrame = 0;
        engine.playbackIndex = 0;
        engine.message = "Replay cleared";
    }

    void onSafeMode(CCObject*) {
        auto& engine = Engine::get();
        engine.safeMode = !engine.safeMode;
        m_safeModeSprite->setString(engine.safeMode ? "Safe: ON" : "Safe: OFF");
        m_safeModeSprite->setColor(engine.safeMode ? ccColor3B{45, 170, 90} : ccColor3B{190, 70, 55});
        engine.message = engine.safeMode
            ? "Safe Mode blocks replay completions"
            : "Warning: Safe Mode disabled";
    }

    void onTools(CCObject*) {
        if (auto popup = ToolsPopup::create()) popup->show();
    }

public:
    static BotPopup* create(PauseLayer* pauseLayer) {
        auto popup = new BotPopup();
        if (popup && popup->init()) {
            popup->m_pauseLayer = pauseLayer;
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

} // namespace dimbot

class $modify(dim5lBotScheduler, CCScheduler) {
    void update(float dt) {
        auto& engine = dimbot::Engine::get();
        auto multiplier = engine.speed();
        if (multiplier != 1.f && PlayLayer::get())
            engine.assistedSession = true;
        CCScheduler::update(dt * multiplier);
    }
};

class $modify(dim5lBotPlayLayer, PlayLayer) {
    struct Fields {
        std::unordered_map<CheckpointObject*, dimbot::PracticeSnapshot> checkpoints;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        auto& engine = dimbot::Engine::get();
        engine.stop("Ready");
        engine.assistedSession = engine.noclip || engine.speed() != 1.f;
        engine.physicalButtons.fill(false);
        ++engine.session;
        engine.applySpeed();
        engine.frame = 0;
        engine.playbackIndex = 0;
        engine.frameFixIndex = 0;
        engine.previousProcessedFrame = std::numeric_limits<uint64_t>::max();
        engine.pendingDeathCheck = false;
        engine.levelCompletionInProgress = false;
        return true;
    }

    void update(float dt) {
        auto& engine = dimbot::Engine::get();
        PlayLayer::update(dt);
        if (engine.mode == dimbot::Mode::Recording || engine.mode == dimbot::Mode::Playing)
            engine.frame = dimbot::currentGameFrame();
        if (engine.mode == dimbot::Mode::Recording) {
            engine.replayEndFrame = engine.frame;
        }
    }

    void resetLevel() {
        auto& engine = dimbot::Engine::get();
        bool playing = engine.playAfterReset || engine.mode == dimbot::Mode::Playing;
        bool recording = engine.mode == dimbot::Mode::Recording;
        engine.resetting = true;
        engine.checkpointRestored = false;
        engine.reconcileFrame = 0;
        engine.pendingDeathCheck = false;
        PlayLayer::resetLevel();
        engine.resetting = false;
        engine.applySpeed();
        if (engine.checkpointRestored) return;
        m_fields->checkpoints.clear();
        engine.timeline.reset();
        engine.resumeFrame = 0;
        if (m_player1) m_player1->releaseAllButtons();
        if (m_player2) m_player2->releaseAllButtons();
        if (playing) engine.beginPlaybackAfterReset();
        else if (recording && !engine.levelCompletionInProgress) engine.beginRecording();
        engine.assistedSession = engine.noclip || engine.speed() != 1.f;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& engine = dimbot::Engine::get();
        if (engine.noclip && !m_isPaused) {
            engine.assistedSession = true;
            return;
        }
        PlayLayer::destroyPlayer(player, object);

        // Only mark a possible death here. resetLevel validates the real dead
        // state, so pause transitions can never discard the recording.
        if (engine.mode == dimbot::Mode::Recording && !m_isPracticeMode)
            engine.pendingDeathCheck = true;
    }

    void levelComplete() {
        auto& engine = dimbot::Engine::get();
        // Completion can emit death/reset callbacks while the end screen is
        // being created. Preserve the finished recording through that reset.
        engine.levelCompletionInProgress = true;
        engine.pendingDeathCheck = false;
        if (engine.safeMode && (engine.replaySessionActive || engine.assistedSession)) {
            engine.stop("Safe Mode blocked replay completion");
            PlayLayer::resetLevel();
            return;
        }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        dimbot::Engine::get().stop();
        dimbot::Engine::get().resetCheats();
        m_fields->checkpoints.clear();
        PlayLayer::onQuit();
    }

    void storeCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::storeCheckpoint(checkpoint);
        auto& engine = dimbot::Engine::get();
        if (!checkpoint || engine.mode == dimbot::Mode::Idle || engine.resetting) return;
        m_fields->checkpoints.insert_or_assign(checkpoint,
            dimbot::PracticeSnapshot::capture(checkpoint, m_player1, m_player2,
                dimbot::currentGameFrame(), engine.session));
    }

    void removeCheckpoint(bool first) {
        PlayLayer::removeCheckpoint(first);
        std::erase_if(m_fields->checkpoints, [&](auto const& entry) {
            return !m_checkpointArray || !m_checkpointArray->containsObject(entry.first);
        });
    }

    void removeAllCheckpoints() {
        PlayLayer::removeAllCheckpoints();
        m_fields->checkpoints.clear();
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        auto& engine = dimbot::Engine::get();
        bool wasResetting = engine.resetting;
        engine.resetting = true;
        PlayLayer::loadFromCheckpoint(checkpoint);
        engine.resetting = wasResetting;
        auto found = m_fields->checkpoints.find(checkpoint);
        if (engine.mode == dimbot::Mode::Idle || found == m_fields->checkpoints.end() ||
            found->second.session != engine.session) return;
        auto const& saved = found->second;
        saved.restore(m_player1, m_player2);
        m_queuedButtons.clear();
        m_queuedRecordedButtons.clear();
        engine.checkpointRestored = true;
        engine.frame = saved.frame;
        engine.resumeFrame = saved.frame;
        engine.timeline.reset();
        engine.playbackIndex = dimbot::replayCursorAfter(engine.inputs, saved.frame);
        engine.frameFixIndex = dimbot::replayCursorAfter(engine.frameFixes, saved.frame);
        if (engine.mode == dimbot::Mode::Recording) {
            engine.trimToFrame(saved.frame);
            engine.reconcileFrame = saved.frame + 2;
        }
        engine.applySpeed();
    }

};

class $modify(dim5lBotBaseGameLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto& engine = dimbot::Engine::get();

        // xdBot processes the vanilla command step first, then applies
        // the macro action for the numbered command tick.
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != static_cast<GJBaseGameLayer*>(this) ||
            engine.resetting || m_levelEndAnimationStarted ||
            !m_player1 || m_player1->m_isDead) return;

        if (engine.mode != dimbot::Mode::Recording && engine.mode != dimbot::Mode::Playing)
            return;

        auto frame = dimbot::currentGameFrame();
        engine.frame = frame;
        if (engine.resumeFrame && frame <= engine.resumeFrame) return;
        engine.resumeFrame = 0;
        if (engine.mode == dimbot::Mode::Recording && engine.reconcileFrame && frame >= engine.reconcileFrame) {
            engine.reconcileFrame = 0;
            for (int side = 0; side < (m_levelSettings->m_twoPlayerMode ? 2 : 1); ++side) {
                for (int button = 1; button <= 3; ++button) {
                    int index = side * 3 + button - 1;
                    bool held = engine.physicalButtons[index];
                    if (!m_levelSettings->m_twoPlayerMode) held = held || engine.physicalButtons[index + 3];
                    // Route the transition through recording so a practice splice
                    // replays the same hold/release state from the beginning.
                    handleButton(held, button, side != 0);
                }
            }
        }

        // A rendered frame may visit processCommands more than once. Never
        // consume the same macro tick twice.
        if (engine.timeline.previous != std::numeric_limits<uint64_t>::max() &&
            frame < engine.timeline.previous) {
            engine.stop("Unexpected rewind; restart the replay from the beginning");
            return;
        }
        if (!engine.timeline.enter(frame)) return;
        engine.previousProcessedFrame = frame;

        if (engine.mode == dimbot::Mode::Recording) {
            engine.replayEndFrame = std::max(engine.replayEndFrame, frame);
            // Frame Fixes mode samples after command processing, independently
            // of input callbacks. Do not mix it with Input Fixes sampling.
            if (!engine.inputs.empty())
                engine.recordFrameFix(frame, m_player1, m_player2);
            return;
        }

        engine.injecting = true;
        dimbot::ReplayTimeline::drain(engine.inputs, engine.playbackIndex, frame,
            [&](dimbot::Input const& input) {
                bool flipped = !m_levelSettings->m_platformerMode && GameManager::get()->getGameVariable("0010");
                auto side = input.player1;
                if (flipped != engine.flippedControls) side = !side;
                GJBaseGameLayer::handleButton(input.down, input.button, side);
            });
        engine.injecting = false;

        // Apply the separate Frame Fixes stream after ordered replay inputs.
        while (engine.frameFixIndex < engine.frameFixes.size() &&
               engine.frameFixes[engine.frameFixIndex].frame <= frame) {
            auto const& fix = engine.frameFixes[engine.frameFixIndex++];
            if (m_player1) {
                if (fix.player1.valid) m_player1->setPosition({fix.player1.x, fix.player1.y});
                if (fix.player1.rotate) m_player1->setRotation(fix.player1.rotation);
            }
            if (m_player2 && m_gameState.m_isDualMode) {
                if (fix.player2.valid) m_player2->setPosition({fix.player2.x, fix.player2.y});
                if (fix.player2.rotate) m_player2->setRotation(fix.player2.rotation);
            }
        }

        // Keep playback armed until Stop/quit. The last input can be a hold,
        // and a subsequent death must restart this same replay.
    }

    void handleButton(bool down, int button, bool player2) {
        auto& engine = dimbot::Engine::get();
        if (engine.resetting) return GJBaseGameLayer::handleButton(down, button, player2);
        if (button < 1 || button > 3) return GJBaseGameLayer::handleButton(down, button, player2);
        if (!engine.injecting) engine.physicalButtons[(player2 ? 3 : 0) + button - 1] = down;
        if (engine.reconcileFrame && engine.mode == dimbot::Mode::Recording) return;
        if (engine.mode == dimbot::Mode::Playing && !engine.injecting) return;

        if (engine.mode == dimbot::Mode::Recording && !engine.injecting) {
            auto frame = dimbot::currentGameFrame();
            engine.frame = frame;
            GJBaseGameLayer::handleButton(down, button, player2);
            if (m_player1 && !m_player1->m_isDead && !m_levelEndAnimationStarted) {
                if (!m_levelSettings->m_twoPlayerMode) player2 = false;
                if (!m_levelSettings->m_platformerMode && GameManager::get()->getGameVariable("0010"))
                    player2 = !player2;
                engine.record(down, button, player2);
            }
            return;
        }

        GJBaseGameLayer::handleButton(down, button, player2);
    }
};

class $modify(dim5lBotPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto sprite = ButtonSprite::create("dim5lBOT", 90, true, "bigFont.fnt", "GJ_button_04.png", 28.f, .5f);
        auto button = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            menu_selector(dim5lBotPauseLayer::onOpenDim5lBot)
        );

        auto menu = CCMenu::create();
        menu->setPosition({CCDirector::sharedDirector()->getWinSize().width - 62.f, 32.f});
        menu->addChild(button);
        menu->setID("dim5lbot-menu"_spr);
        addChild(menu, 100);
    }

    void onOpenDim5lBot(CCObject*) {
        if (auto popup = dimbot::BotPopup::create(this)) popup->show();
    }
};

class $modify(dim5lBotEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();

        auto sprite = ButtonSprite::create("dim5lBOT", 90, true, "bigFont.fnt", "GJ_button_04.png", 28.f, .5f);
        auto button = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            menu_selector(dim5lBotEndLevelLayer::onOpenDim5lBot)
        );

        auto menu = CCMenu::create();
        menu->setPosition({CCDirector::sharedDirector()->getWinSize().width - 62.f, 32.f});
        menu->addChild(button);
        menu->setID("dim5lbot-end-menu"_spr);
        addChild(menu, 100);
    }

    void onOpenDim5lBot(CCObject*) {
        if (auto popup = dimbot::BotPopup::create(nullptr)) popup->show();
    }
};
