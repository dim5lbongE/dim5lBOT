#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace geode::prelude;

namespace dimbot {

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

struct Engine {
    Mode mode = Mode::Idle;
    std::vector<Input> inputs;
    uint64_t frame = 0;
    uint64_t replayEndFrame = 0;
    size_t playbackIndex = 0;
    bool injecting = false;
    bool playAfterReset = false;
    std::string message = "Ready";

    static Engine& get() {
        static Engine engine;
        return engine;
    }

    void stop(std::string text = "Stopped") {
        mode = Mode::Idle;
        playAfterReset = false;
        injecting = false;
        message = std::move(text);
    }

    void beginRecording() {
        inputs.clear();
        frame = 0;
        replayEndFrame = 0;
        playbackIndex = 0;
        mode = Mode::Recording;
        playAfterReset = false;
        message = "Recording";
    }

    void requestPlayback() {
        if (inputs.empty()) {
            message = "No replay loaded";
            return;
        }
        mode = Mode::Idle;
        frame = 0;
        playbackIndex = 0;
        playAfterReset = true;
        message = "Preparing replay";
    }

    void beginPlaybackAfterReset() {
        frame = 0;
        playbackIndex = 0;
        playAfterReset = false;
        mode = Mode::Playing;
        message = "Playing";
    }

    void record(bool down, int button, bool player1) {
        if (mode != Mode::Recording || injecting) return;
        Input next{frame, down, button, player1};

        // Geometry Dash or another mod can forward the same input twice.
        // Keep distinct buttons/players, but discard an exact duplicate event.
        if (!inputs.empty()) {
            auto const& previous = inputs.back();
            if (previous.frame == next.frame && previous.down == next.down &&
                previous.button == next.button && previous.player1 == next.player1) {
                return;
            }
        }
        inputs.push_back(next);
    }
};

std::filesystem::path macroDirectory() {
    return Mod::get()->getSaveDir() / "macros";
}

std::filesystem::path macroPath() {
    return macroDirectory() / "last-replay.json";
}

Result<> saveMacro() {
    auto& engine = Engine::get();
    std::error_code error;
    std::filesystem::create_directories(macroDirectory(), error);
    if (error) return Err("Could not create the macros folder: {}", error.message());

    matjson::Value root = matjson::Value::object();
    root["format"] = "dim5lbot-replay";
    root["version"] = 1;
    root["gameVersion"] = "2.2081";
    root["totalFrames"] = static_cast<double>(engine.replayEndFrame);

    matjson::Value inputs = matjson::Value::array();
    for (auto const& input : engine.inputs) {
        matjson::Value item = matjson::Value::object();
        item["frame"] = static_cast<double>(input.frame);
        item["down"] = input.down;
        item["button"] = input.button;
        item["player1"] = input.player1;
        inputs.push(std::move(item));
    }
    root["inputs"] = std::move(inputs);

    std::ofstream stream(macroPath(), std::ios::binary | std::ios::trunc);
    if (!stream) return Err("Could not open the replay file");
    stream << root.dump(2);
    if (!stream.good()) return Err("Could not write the replay file");
    return Ok();
}

Result<> loadMacro() {
    std::ifstream stream(macroPath(), std::ios::binary);
    if (!stream) return Err("No saved replay found");

    std::string contents(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>()
    );
    auto parsed = matjson::parse(contents);
    if (parsed.isErr()) return Err("Replay JSON is invalid");
    auto root = parsed.unwrap();
    if (!root.isObject() || !root["inputs"].isArray()) {
        return Err("Unsupported replay format");
    }

    std::vector<Input> loaded;
    uint64_t lastFrame = 0;
    for (auto const& item : root["inputs"]) {
        auto frame = item["frame"].asDouble();
        auto down = item["down"].asBool();
        auto button = item["button"].asInt();
        auto player1 = item["player1"].asBool();
        if (frame.isErr() || down.isErr() || button.isErr() || player1.isErr()) {
            return Err("Replay contains a malformed input");
        }
        auto frameNumber = static_cast<uint64_t>(std::max(0.0, frame.unwrap()));
        auto buttonNumber = static_cast<int>(button.unwrap());
        if (buttonNumber < 1 || buttonNumber > 3) {
            return Err("Replay contains an unsupported button");
        }
        loaded.push_back({frameNumber, down.unwrap(), buttonNumber, player1.unwrap()});
        lastFrame = std::max(lastFrame, frameNumber);
    }

    std::stable_sort(loaded.begin(), loaded.end(), [](Input const& a, Input const& b) {
        return a.frame < b.frame;
    });

    auto& engine = Engine::get();
    engine.stop();
    engine.inputs = std::move(loaded);
    auto totalFrames = root["totalFrames"].asDouble();
    engine.replayEndFrame = totalFrames.isOk()
        ? static_cast<uint64_t>(std::max(0.0, totalFrames.unwrap()))
        : lastFrame;
    engine.replayEndFrame = std::max(engine.replayEndFrame, lastFrame);
    engine.frame = 0;
    engine.playbackIndex = 0;
    engine.message = fmt::format("Loaded {} inputs", engine.inputs.size());
    return Ok();
}

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

class BotPopup final : public Popup {
protected:
    CCLabelBMFont* m_stateLabel = nullptr;
    CCLabelBMFont* m_messageLabel = nullptr;

    bool init() {
        if (!Popup::init(390.f, 245.f)) return false;
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
        m_messageLabel->setPosition({m_size.width / 2.f, 35.f});
        m_mainLayer->addChild(m_messageLabel);

        addButton("Record", {-112.f, 22.f}, menu_selector(BotPopup::onRecord), {210, 55, 72});
        addButton("Stop", {0.f, 22.f}, menu_selector(BotPopup::onStop), {95, 100, 122});
        addButton("Play", {112.f, 22.f}, menu_selector(BotPopup::onPlay), {55, 175, 105});
        addButton("Save", {-112.f, -35.f}, menu_selector(BotPopup::onSave), {65, 120, 210});
        addButton("Load", {0.f, -35.f}, menu_selector(BotPopup::onLoad), {116, 80, 205});
        addButton("Clear", {112.f, -35.f}, menu_selector(BotPopup::onClear), {205, 115, 45});

        schedule(schedule_selector(BotPopup::refresh), .05f);
        return true;
    }

    void addButton(char const* text, CCPoint position, SEL_MenuHandler callback, ccColor3B color) {
        auto sprite = ButtonSprite::create(text, 95, true, "bigFont.fnt", "GJ_button_01.png", 30.f, .55f);
        sprite->setColor(color);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, callback);
        button->setPosition(position);
        m_buttonMenu->addChild(button);
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
        Engine::get().beginRecording();
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
        Engine::get().requestPlayback();
        if (Engine::get().playAfterReset) {
            layer->resetLevel();
        }
        onClose(nullptr);
    }

    void onSave(CCObject*) {
        auto result = saveMacro();
        Engine::get().message = result.isOk()
            ? fmt::format("Saved {} inputs", Engine::get().inputs.size())
            : result.unwrapErr();
    }

    void onLoad(CCObject*) {
        auto result = loadMacro();
        if (result.isErr()) Engine::get().message = result.unwrapErr();
    }

    void onClear(CCObject*) {
        auto& engine = Engine::get();
        engine.stop();
        engine.inputs.clear();
        engine.frame = 0;
        engine.replayEndFrame = 0;
        engine.playbackIndex = 0;
        engine.message = "Replay cleared";
    }

public:
    static BotPopup* create() {
        auto popup = new BotPopup();
        if (popup && popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

} // namespace dimbot

class $modify(dim5lBotPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        auto& engine = dimbot::Engine::get();
        engine.stop("Ready");
        engine.frame = 0;
        engine.playbackIndex = 0;
        return true;
    }

    void handleButton(bool down, int button, bool player1) {
        auto& engine = dimbot::Engine::get();
        if (engine.mode == dimbot::Mode::Playing && !engine.injecting) {
            return;
        }
        engine.record(down, button, player1);
        PlayLayer::handleButton(down, button, player1);
    }

    void update(float dt) {
        auto& engine = dimbot::Engine::get();

        if (engine.mode == dimbot::Mode::Playing) {
            while (engine.playbackIndex < engine.inputs.size() &&
                   engine.inputs[engine.playbackIndex].frame <= engine.frame) {
                auto const input = engine.inputs[engine.playbackIndex++];
                engine.injecting = true;
                PlayLayer::handleButton(input.down, input.button, input.player1);
                engine.injecting = false;
            }
        }

        PlayLayer::update(dt);

        if (engine.mode == dimbot::Mode::Recording || engine.mode == dimbot::Mode::Playing) {
            ++engine.frame;
        }
        if (engine.mode == dimbot::Mode::Recording) {
            engine.replayEndFrame = engine.frame;
        }
        if (engine.mode == dimbot::Mode::Playing &&
            engine.playbackIndex >= engine.inputs.size() &&
            engine.frame > engine.replayEndFrame) {
            engine.stop("Replay finished");
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto& engine = dimbot::Engine::get();
        if (engine.playAfterReset) {
            engine.beginPlaybackAfterReset();
        } else if (engine.mode == dimbot::Mode::Recording) {
            engine.frame = 0;
        }
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
        if (auto popup = dimbot::BotPopup::create()) popup->show();
    }
};
