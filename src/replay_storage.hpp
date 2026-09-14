#pragma once
#include <filesystem>
#include <fstream>
#include "replay_engine.hpp"
#include "replay_codec.hpp"

namespace dimbot {
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
    return macroDirectory() / (sanitizeMacroName(name) + ".gdr");
}

std::vector<std::filesystem::path> listMacros() {
    std::vector<std::filesystem::path> result;
    std::error_code error;
    std::filesystem::create_directories(macroDirectory(), error);
    if (error) return result;
    for (auto const& entry : std::filesystem::directory_iterator(macroDirectory(), error)) {
        if (!error && entry.is_regular_file() && (entry.path().extension() == ".json" || entry.path().extension() == ".gdr"))
            result.push_back(entry.path());
    }
    std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) {
        return a.filename().string() < b.filename().string();
    });
    return result;
}

Result<> saveMacro(std::string const& requestedName) try {
    auto& engine = Engine::get();
    auto name = sanitizeMacroName(requestedName);
    if (name.empty()) return Err("Enter a replay name");
    if (engine.inputs.empty()) return Err("No inputs to save");
    std::error_code error;
    std::filesystem::create_directories(macroDirectory(), error);
    if (error) return Err("Could not create the macros folder: {}", error.message());

    matjson::Value root = matjson::Value::object();
    root["version"] = 1.0;
    root["accuracy"] = "frame-fixes";
    root["gameVersion"] = 2.2081;
    root["framerate"] = 240.0;
    root["bot"] = matjson::Value::object();
    root["bot"]["name"] = "dim5lBOT";
    root["bot"]["version"] = "v1.2.0";
    root["duration"] = engine.inputs.back().frame / 240.0;
    root["description"] = "Frame Fixes; GD 2.2081; 240 TPS";
    root["author"] = "";
    root["seed"] = 0;
    root["coins"] = 0;
    root["ldm"] = false;
    root["dim5lSchema"] = 4;
    root["flippedControls"] = engine.flippedControls;
    root["level"] = matjson::Value::object();
    root["level"]["id"] = engine.replayLevelId;
    root["level"]["name"] = engine.replayLevelName;
    root["totalFrames"] = static_cast<double>(engine.replayEndFrame);

    matjson::Value inputs = matjson::Value::array();
    for (auto const& input : engine.inputs) {
        matjson::Value item = matjson::Value::object();
        item["frame"] = static_cast<double>(input.frame);
        item["down"] = input.down;
        item["btn"] = input.button;
        item["2p"] = input.player1;
        inputs.push(std::move(item));
    }
    root["inputs"] = std::move(inputs);

    matjson::Value fixes = matjson::Value::array();
    for (auto const& fix : engine.frameFixes) {
        matjson::Value item = matjson::Value::object();
        item["frame"] = static_cast<double>(fix.frame);
        item["p1"] = matjson::Value::object();
        item["p2"] = matjson::Value::object();
        item["p1"]["x"] = fix.player1.x;
        item["p1"]["y"] = fix.player1.y;
        item["p1"]["r"] = fix.player1.rotation;
        item["p2"]["x"] = fix.player2.x;
        item["p2"]["y"] = fix.player2.y;
        item["p2"]["r"] = fix.player2.rotation;
        fixes.push(std::move(item));
    }
    root["frameFixes"] = std::move(fixes);

    root["name"] = name;
    auto destination = macroPath(name);
    for (unsigned index = 1; std::filesystem::exists(destination); ++index)
        destination = macroPath(fmt::format("{} ({})", name, index));
    auto temporary = destination;
    temporary += ".tmp";
    auto backup = destination;
    backup += ".bak";
    if (std::filesystem::exists(backup)) return Err("Recovery backup exists; keep it before saving again");
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) return Err("Could not open the replay file");
    auto bytes = encodeGdr(root.dump());
    stream.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    if (!stream.good()) return Err("Could not write the replay file");
    stream.close();
    if (stream.fail()) return Err("Could not close the replay file");
    bool existed = std::filesystem::exists(destination);
    if (existed) {
        std::filesystem::rename(destination, backup, error);
        if (error) return Err("Could not back up previous replay: {}", error.message());
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code rollback;
        if (existed) std::filesystem::rename(backup, destination, rollback);
        return Err("Save failed; previous data retained: {}", error.message());
    }
    if (existed) std::filesystem::remove(backup, error);
    engine.message = fmt::format("Saved: {}", destination.stem().string());
    return Ok();
} catch (std::exception const& error) {
    return Err("Save failed: {}", error.what());
}

Result<> loadMacro(std::filesystem::path const& path) try {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return Err("No saved replay found");

    stream.seekg(0, std::ios::end);
    if (stream.tellg() < 0 || stream.tellg() > 64 * 1024 * 1024)
        return Err("Replay exceeds the 64 MiB import limit");
    stream.seekg(0);
    std::string contents(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>()
    );
    auto parsed = matjson::parse(decodeGdr(contents, path.extension() == ".gdr"));
    if (parsed.isErr()) return Err("Replay JSON is invalid");
    auto root = parsed.unwrap();
    if (!root.isObject() || !root["inputs"].isArray()) {
        return Err("Unsupported replay format");
    }
    const bool gdr = root["framerate"].isNumber();
    auto rate = (gdr ? root["framerate"] : root["tps"]).asDouble();
    if (rate.isErr() || rate.unwrap() != 240.0)
        return Err("Only 240 TPS replays are supported; no silent timing conversion");
    unsigned frameOffset = 0;
    if (gdr && root["bot"]["name"].asString().unwrapOr("") == "xdBot") {
        frameOffset = xdFrameOffset(root["bot"]["version"].asString().unwrapOr(""));
    }
    std::vector<Input> loaded;
    uint64_t lastFrame = 0;
    for (auto const& item : root["inputs"]) {
        auto frame = item["frame"].asDouble();
        auto down = item["down"].asBool();
        auto button = (gdr ? item["btn"] : item["button"]).asInt();
        auto player1 = (gdr ? item["2p"] : item["player1"]).asBool();
        if (frame.isErr() || down.isErr() || button.isErr() || player1.isErr()) {
            return Err("Replay contains a malformed input");
        }
        auto frameNumber = checkedReplayFrame(frame.unwrap() + frameOffset);
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

    std::vector<FrameFix> loadedFixes;
    if (root["frameFixes"].isArray()) {
        for (auto const& item : root["frameFixes"]) {
            if (gdr) {
                FrameFix fix;
                auto tick = item["frame"].asDouble();
                if (tick.isErr()) return Err("Correction frame is missing or invalid");
                fix.frame = checkedReplayFrame(tick.unwrap() + frameOffset);
                auto readPlayer = [&](matjson::Value const& value) {
                    PlayerFix p;
                    p.valid = value["x"].isNumber() && value["y"].isNumber();
                    p.rotate = value["r"].isNumber();
                    p.x = value["x"].asDouble().unwrapOr(0.0);
                    p.y = value["y"].asDouble().unwrapOr(0.0);
                    p.rotation = value["r"].asDouble().unwrapOr(0.0);
                    if (root["bot"]["name"].asString().unwrapOr("") == "xdBot") {
                        p.valid = p.valid && p.x != 0.f && p.y != 0.f;
                        p.rotate = p.rotate && p.rotation != 0.f;
                    }
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.rotation))
                        throw std::invalid_argument("Non-finite correction");
                    return p;
                };
                fix.player1 = readPlayer(item["p1"]);
                fix.player2 = readPlayer(item["p2"]);
                loadedFixes.push_back(fix);
                lastFrame = std::max(lastFrame, fix.frame);
                continue;
            }
            auto frame = item["frame"].asDouble();
            auto p1x = item["p1x"].asDouble();
            auto p1y = item["p1y"].asDouble();
            auto p1r = item["p1r"].asDouble();
            auto p2x = item["p2x"].asDouble();
            auto p2y = item["p2y"].asDouble();
            auto p2r = item["p2r"].asDouble();
            if (frame.isErr() || p1x.isErr() || p1y.isErr() || p1r.isErr() ||
                p2x.isErr() || p2y.isErr() || p2r.isErr()) {
                return Err("Replay contains a malformed frame fix");
            }
            FrameFix fix;
            fix.frame = checkedReplayFrame(frame.unwrap() + frameOffset);
            fix.player1 = {
                static_cast<float>(p1x.unwrap()), static_cast<float>(p1y.unwrap()),
                static_cast<float>(p1r.unwrap())
            };
            fix.player2 = {
                static_cast<float>(p2x.unwrap()), static_cast<float>(p2y.unwrap()),
                static_cast<float>(p2r.unwrap())
            };
            if (!std::isfinite(fix.player1.x) || !std::isfinite(fix.player1.y) ||
                !std::isfinite(fix.player1.rotation) || !std::isfinite(fix.player2.x) ||
                !std::isfinite(fix.player2.y) || !std::isfinite(fix.player2.rotation))
                return Err("Non-finite correction");
            loadedFixes.push_back(fix);
            lastFrame = std::max(lastFrame, fix.frame);
        }
        std::stable_sort(loadedFixes.begin(), loadedFixes.end(), [](FrameFix const& a, FrameFix const& b) {
            return a.frame < b.frame;
        });
    }

    auto totalFrames = root["totalFrames"].asDouble();
    auto endFrame = totalFrames.isOk()
        ? checkedReplayFrame(totalFrames.unwrap())
        : lastFrame;
    auto& engine = Engine::get();
    engine.stop();
    ++engine.session;
    engine.inputs = std::move(loaded);
    engine.frameFixes = std::move(loadedFixes);
    engine.replayEndFrame = std::max(endFrame, lastFrame);
    engine.frame = 0;
    engine.playbackIndex = 0;
    engine.frameFixIndex = 0;
    engine.previousProcessedFrame = std::numeric_limits<uint64_t>::max();
    engine.timeline.reset();
    engine.flippedControls = root["flippedControls"].asBool().unwrapOr(false);
    engine.replayLevelId = root["level"]["id"].asInt().unwrapOr(0);
    engine.replayLevelName = root["level"]["name"].asString().unwrapOr("");
    engine.message = fmt::format(
        "Loaded {} ({} inputs, {} fixes)",
        path.stem().string(), engine.inputs.size(), engine.frameFixes.size()
    );
    return Ok();
} catch (std::exception const& error) {
    return Err("Invalid replay: {}", error.what());
}


}
