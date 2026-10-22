#include "qmg/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace qmg {

Vec3 Bounds::center() const {
    return {
        (min.x + max.x) * 0.5,
        (min.y + max.y) * 0.5,
        (min.z + max.z) * 0.5,
    };
}

bool Bounds::valid() const {
    return std::isfinite(min.x) && std::isfinite(min.y) && std::isfinite(min.z) && std::isfinite(max.x) &&
           std::isfinite(max.y) && std::isfinite(max.z) && min.x < max.x && min.y < max.y && min.z < max.z;
}

bool Bounds::contains(const Vec3 &point, const double margin) const {
    return point.x >= min.x + margin && point.x <= max.x - margin && point.y >= min.y + margin &&
           point.y <= max.y - margin && point.z >= min.z + margin && point.z <= max.z - margin;
}

bool ValidationResult::ok() const { return error_count() == 0; }

std::size_t ValidationResult::error_count() const {
    return static_cast<std::size_t>(std::count_if(
        issues.begin(), issues.end(), [](const Issue &issue) { return issue.severity == Severity::error; }));
}

std::string to_string(const Game game) {
    switch (game) {
    case Game::quake1:
        return "quake1";
    case Game::quake2:
        return "quake2";
    case Game::quake3:
        return "quake3";
    }
    return "unknown";
}

std::string to_string(const GameMode mode) {
    switch (mode) {
    case GameMode::single_player:
        return "single_player";
    case GameMode::deathmatch:
        return "deathmatch";
    case GameMode::duel:
        return "duel";
    case GameMode::capture_the_flag:
        return "capture_the_flag";
    }
    return "unknown";
}

std::string to_string(const Severity severity) { return severity == Severity::error ? "error" : "warning"; }

std::string to_string(const LayoutIntent intent) {
    switch (intent) {
    case LayoutIntent::unspecified:
        return "unspecified";
    case LayoutIntent::wrapped_center:
        return "wrapped_center";
    }
    throw std::invalid_argument("invalid LayoutIntent enum value");
}

void require_supported_layout_intent(const PromptSpec &spec) {
    switch (spec.layout_intent) {
    case LayoutIntent::unspecified:
        return;
    case LayoutIntent::wrapped_center:
        if (spec.game == Game::quake2 && (spec.mode == GameMode::deathmatch || spec.mode == GameMode::duel) &&
            spec.room_count >= 5 && spec.room_count <= 8)
            return;
        throw std::invalid_argument(
            "wrapped_center layout intent requires Quake 2 deathmatch or duel with 5 to 8 rooms");
    }
    throw std::invalid_argument("invalid LayoutIntent enum value");
}

bool is_valid_utf8(const std::string_view value) {
    std::size_t index = 0;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7F) {
            ++index;
            continue;
        }
        std::size_t length{};
        std::uint32_t codepoint{};
        if ((first & 0xE0U) == 0xC0U) {
            length = 2;
            codepoint = first & 0x1FU;
        } else if ((first & 0xF0U) == 0xE0U) {
            length = 3;
            codepoint = first & 0x0FU;
        } else if ((first & 0xF8U) == 0xF0U) {
            length = 4;
            codepoint = first & 0x07U;
        } else {
            return false;
        }
        if (index + length > value.size())
            return false;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xC0U) != 0x80U)
                return false;
            codepoint = (codepoint << 6U) | (continuation & 0x3FU);
        }
        if ((length == 2 && codepoint < 0x80U) || (length == 3 && codepoint < 0x800U) ||
            (length == 4 && codepoint < 0x10000U) || codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
            return false;
        index += length;
    }
    return true;
}

} // namespace qmg
