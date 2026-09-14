#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace dimbot {
inline std::vector<uint8_t> encodeGdr(std::string const& json) {
    return nlohmann::json::to_msgpack(nlohmann::json::parse(json));
}
inline std::string decodeGdr(std::string const& bytes, bool binary) {
    if (bytes.empty() || bytes.size() > 64 * 1024 * 1024)
        throw std::invalid_argument("Empty or oversized replay");
    auto value = binary ? nlohmann::json::from_msgpack(bytes, true, true)
                        : nlohmann::json::parse(bytes);
    if (!value.is_object()) throw std::invalid_argument("Replay must be an object");
    return value.dump();
}
inline unsigned xdFrameOffset(std::string_view version) {
    if (version.starts_with('v')) version.remove_prefix(1);
    std::array<unsigned, 3> parts{};
    for (size_t i = 0; i < parts.size(); ++i) {
        auto dot = version.find('.');
        auto component = version.substr(0, dot);
        auto [end, ec] = std::from_chars(component.data(), component.data() + component.size(), parts[i]);
        if (ec != std::errc{} || end != component.data() + component.size())
            throw std::invalid_argument("Unsupported xdBot prerelease version");
        if (i < 2) {
            if (dot == std::string_view::npos) throw std::invalid_argument("Missing xdBot version component");
            version.remove_prefix(dot + 1);
        } else if (dot != std::string_view::npos) throw std::invalid_argument("Unsupported xdBot version suffix");
    }
    return parts < std::array<unsigned, 3>{2, 3, 6} ? 1 : 0;
}
}
