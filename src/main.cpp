#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
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
    bool replaySessionActive = false;
    bool safeMode = true;
    bool noclip = false;
    bool assistedSession = false;
    bool pendingDeathCheck = false;
    float speedMultiplier = 1.f;
    std::string message = "Ready";

    static Engine& get() {
        static Engine engine;
        return engine;
    }

    void stop(std::string text = "Stopped") {
        mode = Mode::Idle;
        playAfterReset = false;
        replaySessionActive = false;
        injecting = false;
        pendingDeathCheck = false;
        message = std::move(text);
    }

    void beginRecording() {
        inputs.clear();
        frame = 0;
        replayEndFrame = 0;
        playbackIndex = 0;
        mode = Mode::Recording;
        playAfterReset = false;
        replaySessionActive = false;
        pendingDeathCheck = false;
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
        replaySessionActive = true;
        mode = Mode::Playing;
        message = "Playing";
    }

    void finishPlayback() {
        mode = Mode::Idle;
        playAfterReset = false;
        injecting = false;
        message = "Replay finished";
    }

    float speed() const {
        return speedMultiplier;
    }

    void applySpeed() {
        cocos2d::CCScheduler::get()->setTimeScale(speed());
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

    void trimToFrame(uint64_t targetFrame) {
        std::erase_if(inputs, [targetFrame](Input const& input) {
            return input.frame >= targetFrame;
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
    }
};

uint64_t currentGameFrame() {
    auto layer = PlayLayer::get();
    if (!layer) return 0;
    auto time = std::max(0.0, static_cast<double>(layer->m_gameState.m_levelTime));
    return static_cast<uint64_t>(std::llround(time * 240.0));
}

std::filesystem::path macroDirectory() {
    return Mod::get()->getSaveDir() / "macros";
}

std::string sanitizeMacroName(std::string name) {
    auto invalid = std::string("\\/:*?\"<>|");
    std::erase_if(name, [&](char ch) {
        return static_cast<unsigned char>(ch) < 32 || invalid.find(ch) != std::string::npos;
    });
    while (!name.empty() && (name.front() == ' ' || name.front() == '.')) name.erase(name.begin());
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    if (name.size() > 60) name.resize(60);
    return name;
}

std::filesystem::path macroPath(std::string const& name) {
    return macroDirectory() / (sanitizeMacroName(name) + ".json");
}

std::vector<std::filesystem::path> listMacros() {
    std::vector<std::filesystem::path> result;
    std::error_code error;
    std::filesystem::create_directories(macroDirectory(), error);
    if (error) return result;
    for (auto const& entry : std::filesystem::directory_iterator(macroDirectory(), error)) {
        if (!error && entry.is_regular_file() && entry.path().extension() == ".json")
            result.push_back(entry.path());
    }
    std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) {
        return a.filename().string() < b.filename().string();
    });
    return result;
}

Result<> saveMacro(std::string const& requestedName) {
    auto& engine = Engine::get();
    auto name = sanitizeMacroName(requestedName);
    if (name.empty()) return Err("Enter a replay name");
    if (engine.inputs.empty()) return Err("No inputs to save");
    std::error_code error;
    std::filesystem::create_directories(macroDirectory(), error);
    if (error) return Err("Could not create the macros folder: {}", error.message());

    matjson::Value root = matjson::Value::object();
    root["format"] = "dim5lbot-replay";
    root["version"] = 1;
    root["tps"] = 240;
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

    root["name"] = name;
    std::ofstream stream(macroPath(name), std::ios::binary | std::ios::trunc);
    if (!stream) return Err("Could not open the replay file");
    stream << root.dump(2);
    if (!stream.good()) return Err("Could not write the replay file");
    return Ok();
}

Result<> loadMacro(std::filesystem::path const& path) {
    std::ifstream stream(path, std::ios::binary);
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
    engine.message = fmt::format("Loaded {} ({} inputs)", path.stem().string(), engine.inputs.size());
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

class SaveReplayPopup final : public Popup {
protected:
    TextInput* m_nameInput = nullptr;
    CCLabelBMFont* m_status = nullptr;
    std::string m_pendingOverwrite;

    bool init() {
        if (!Popup::init(340.f, 180.f)) return false;
        setTitle("SAVE REPLAY");
        m_nameInput = TextInput::create(245.f, "Replay name");
        m_nameInput->setCommonFilter(CommonFilter::Any);
        m_nameInput->setMaxCharCount(60);
        m_nameInput->setPosition({170.f, 105.f});
        m_mainLayer->addChild(m_nameInput);

        m_status = CCLabelBMFont::create("Existing names will be overwritten", "chatFont.fnt");
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
        auto cleanName = sanitizeMacroName(name);
        if (!cleanName.empty() && std::filesystem::exists(macroPath(cleanName)) && m_pendingOverwrite != cleanName) {
            m_pendingOverwrite = cleanName;
            m_status->setString("Already exists - press Save again to overwrite");
            m_status->setColor({255, 205, 80});
            return;
        }
        auto result = saveMacro(name);
        if (result.isErr()) {
            m_status->setString(result.unwrapErr().c_str());
            m_status->setColor({255, 100, 100});
            return;
        }
        Engine::get().message = fmt::format("Saved: {}", cleanName);
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
            auto name = m_files[index].stem().string();
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

class $modify(dim5lBotPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        auto& engine = dimbot::Engine::get();
        engine.stop("Ready");
        engine.assistedSession = engine.noclip || engine.speed() != 1.f;
        engine.applySpeed();
        engine.frame = 0;
        engine.playbackIndex = 0;
        engine.pendingDeathCheck = false;
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
        auto startPlayback = engine.playAfterReset;
        engine.pendingDeathCheck = false;
        PlayLayer::resetLevel();
        // Geometry Dash resets the scheduler time scale during a death restart.
        // Restore the user's selected multiplier after every level reset.
        engine.applySpeed();
        if (startPlayback) {
            engine.beginPlaybackAfterReset();
        } else {
            engine.replaySessionActive = false;
            engine.assistedSession = engine.noclip || engine.speed() != 1.f;
            if (engine.mode == dimbot::Mode::Recording) engine.frame = 0;
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& engine = dimbot::Engine::get();
        if (engine.noclip && !m_isPaused) {
            engine.assistedSession = true;
            return;
        }
        PlayLayer::destroyPlayer(player, object);

        // Pause transitions can call destroyPlayer without killing the player.
        // Verify the player's state on the next game update before discarding.
        if (engine.mode == dimbot::Mode::Recording && !m_isPracticeMode)
            engine.pendingDeathCheck = true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& engine = dimbot::Engine::get();
        if (!engine.pendingDeathCheck) return;
        engine.pendingDeathCheck = false;

        auto actuallyDead = !m_isPaused && !m_isPracticeMode &&
            ((m_player1 && m_player1->m_isDead) || (m_player2 && m_player2->m_isDead));
        if (engine.mode == dimbot::Mode::Recording && actuallyDead)
            engine.discardFailedRecording();
    }

    void levelComplete() {
        auto& engine = dimbot::Engine::get();
        if (engine.safeMode && (engine.replaySessionActive || engine.assistedSession)) {
            engine.stop("Safe Mode blocked replay completion");
            PlayLayer::resetLevel();
            return;
        }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        dimbot::Engine::get().resetCheats();
        PlayLayer::onQuit();
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::loadFromCheckpoint(checkpoint);
        auto& engine = dimbot::Engine::get();
        if (engine.mode != dimbot::Mode::Recording || !checkpoint) return;
        engine.trimToFrame(dimbot::currentGameFrame());
    }
};

class $modify(dim5lBotBaseGameLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto& engine = dimbot::Engine::get();
        auto frame = dimbot::currentGameFrame();
        engine.frame = frame;

        // Apply recorded input before this tick's physics, matching the order in
        // which a live input reaches Geometry Dash's command processor.
        if (engine.mode == dimbot::Mode::Playing) {
            engine.injecting = true;
            while (engine.playbackIndex < engine.inputs.size() &&
                   engine.inputs[engine.playbackIndex].frame <= frame) {
                auto const input = engine.inputs[engine.playbackIndex++];
                GJBaseGameLayer::handleButton(input.down, input.button, input.player1);
            }
            engine.injecting = false;
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        if (engine.mode == dimbot::Mode::Playing &&
            engine.playbackIndex >= engine.inputs.size() && frame > engine.replayEndFrame)
            engine.finishPlayback();
    }

    void handleButton(bool down, int button, bool player2) {
        auto& engine = dimbot::Engine::get();
        if (engine.mode == dimbot::Mode::Playing && !engine.injecting) return;

        // In GD 2.2081 all real gameplay inputs pass through GJBaseGameLayer,
        // not PlayLayer. The stored bool keeps the original API value so P1/P2
        // playback is identical to recording.
        // processCommands owns the stable 240 TPS frame clock. Inputs occurring
        // between ticks stay attached to the tick that will process them.
        if (engine.mode != dimbot::Mode::Recording)
            engine.frame = dimbot::currentGameFrame();
        engine.record(down, button, player2);
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
