#include "qmg/layout_fingerprint.hpp"

#include "qmg/sha256.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace qmg {
namespace {

constexpr std::size_t kMaximumFingerprintBytes = 64U * 1024U * 1024U;

class CanonicalBytes {
  public:
    void byte(const std::uint8_t value) {
        require(1);
        bytes_.push_back(static_cast<char>(value));
    }

    template <typename Integer> void integer(const Integer value) {
        static_assert(std::is_integral_v<Integer>);
        using Unsigned = std::make_unsigned_t<Integer>;
        std::uintmax_t encoded = static_cast<std::uintmax_t>(static_cast<Unsigned>(value));
        require(sizeof(Integer));
        for (std::size_t index = 0; index < sizeof(Integer); ++index) {
            bytes_.push_back(static_cast<char>(encoded & 0xFFU));
            encoded >>= 8U;
        }
    }

    template <typename Enum> void enumeration(const Enum value) {
        static_assert(std::is_enum_v<Enum>);
        integer(static_cast<std::underlying_type_t<Enum>>(value));
    }

    void boolean(const bool value) { byte(value ? 1U : 0U); }

    void real(const double value) {
        const double canonical = value == 0.0 ? 0.0 : value;
        integer(std::bit_cast<std::uint64_t>(canonical));
    }

    void string(const std::string_view value) {
        integer(static_cast<std::uint64_t>(value.size()));
        require(value.size());
        bytes_.append(value.data(), value.size());
    }

    [[nodiscard]] const std::string &bytes() const noexcept { return bytes_; }

  private:
    void require(const std::size_t count) const {
        if (count > kMaximumFingerprintBytes - std::min(bytes_.size(), kMaximumFingerprintBytes))
            throw std::length_error("layout fingerprint input exceeds the 64 MiB canonical byte cap");
    }

    std::string bytes_;
};

void append(CanonicalBytes &output, const GridVec3 value) {
    output.integer(value.x);
    output.integer(value.y);
    output.integer(value.z);
}

void append(CanonicalBytes &output, const GridBounds &value) {
    append(output, value.min);
    append(output, value.max);
}

template <typename Id> void append_id(CanonicalBytes &output, const Id id) { output.integer(id.value); }

template <typename Value, typename Append>
void append_optional(CanonicalBytes &output, const std::optional<Value> &value, Append append_value) {
    output.boolean(value.has_value());
    if (value)
        append_value(*value);
}

void append_prompt(CanonicalBytes &output, const PromptSpec &spec) {
    output.string(spec.source_prompt);
    output.string(spec.title);
    output.string(spec.theme);
    output.enumeration(spec.game);
    output.enumeration(spec.mode);
    output.integer(spec.seed);
    output.integer(spec.room_count);
    output.integer(spec.target_players);
    output.real(spec.verticality);
    output.real(spec.intensity);
    output.boolean(spec.prefer_loops);
}

void append_room(CanonicalBytes &output, const Room &room) {
    append_id(output, room.id);
    output.string(room.name);
    append(output, room.bounds);
    std::vector<std::string> tags = room.tags;
    std::sort(tags.begin(), tags.end());
    output.integer(static_cast<std::uint64_t>(tags.size()));
    for (const std::string &tag : tags)
        output.string(tag);
}

void append_connection(CanonicalBytes &output, const Connection &connection) {
    append_id(output, connection.id);
    append_id(output, std::min(connection.from_room, connection.to_room));
    append_id(output, std::max(connection.from_room, connection.to_room));
    output.integer(connection.width);
    output.integer(connection.height);
    std::vector<GridBounds> passages = connection.passage_volumes;
    std::sort(passages.begin(), passages.end());
    output.integer(static_cast<std::uint64_t>(passages.size()));
    for (const GridBounds &passage : passages)
        append(output, passage);
}

void append_solid(CanonicalBytes &output, const LayoutSolid &solid) {
    append_id(output, solid.id);
    output.integer(static_cast<std::uint8_t>(solid.geometry.index()));
    if (const GridBounds *box = get_box(solid.geometry)) {
        append(output, *box);
    } else {
        const ConvexPolyhedron &polyhedron = std::get<ConvexPolyhedron>(solid.geometry);
        output.integer(static_cast<std::uint64_t>(polyhedron.planes.size()));
        for (const PlanePointTriple &plane : polyhedron.planes) {
            append(output, plane.first);
            append(output, plane.second);
            append(output, plane.third);
        }
    }
    output.enumeration(solid.material);
    output.enumeration(solid.purpose);
    output.enumeration(solid.ownership);
    output.enumeration(solid.contents);
    append_optional(output, solid.owner, [&](const PlacementId id) { append_id(output, id); });
}

void append_placement(CanonicalBytes &output, const Placement &placement) {
    append_id(output, placement.id);
    output.enumeration(placement.role);
    append(output, placement.origin);
    append_optional(output, placement.volume, [&](const GridBounds &bounds) { append(output, bounds); });
    append_optional(output, placement.team, [&](const Team team) { output.enumeration(team); });
    output.integer(placement.variant);
    append_optional(output, placement.angle_degrees, [&](const std::int16_t angle) { output.integer(angle); });
    append_optional(output, placement.light_intensity,
                    [&](const std::int32_t intensity) { output.integer(intensity); });
    append_optional(output, placement.light_color, [&](const RgbColor color) {
        output.integer(color.red_milli);
        output.integer(color.green_milli);
        output.integer(color.blue_milli);
    });
    append_optional(output, placement.destination_map,
                    [&](const std::string &destination) { output.string(destination); });
    append_optional(output, placement.link, [&](const PlacementId id) { append_id(output, id); });
    // Change r1s-q1-intermission: only the intermission role carries a pitch, and its role code precedes this
    // field, so every other role keeps its historical bytes.
    if (placement.role == PlacementRole::intermission)
        append_optional(output, placement.pitch_degrees, [&](const std::int16_t pitch) { output.integer(pitch); });
}

template <typename Value, typename Compare, typename Append>
void append_sorted(CanonicalBytes &output, const std::vector<Value> &values, Compare compare, Append append_value) {
    std::vector<const Value *> sorted;
    sorted.reserve(values.size());
    for (const Value &value : values)
        sorted.push_back(&value);
    std::sort(sorted.begin(), sorted.end(),
              [&](const Value *left, const Value *right) { return compare(*left, *right); });
    output.integer(static_cast<std::uint64_t>(sorted.size()));
    for (const Value *value : sorted)
        append_value(*value);
}

} // namespace

std::string canonical_layout_sha256(const Layout &layout) {
    CanonicalBytes output;
    // D7: a layout generated with any stage switch away from its default hashes in its own
    // domain, which leads with the non-default switches in registry (name) order. With every
    // switch at its default nothing is added, so historical bytes are preserved.
    if (!layout.spec.switches.all_default()) {
        output.string("qmg.layout.switches.v1");
        const std::vector<StageSwitches::Entry> switched = layout.spec.switches.non_default();
        output.integer(static_cast<std::uint64_t>(switched.size()));
        for (const StageSwitches::Entry &entry : switched) {
            output.string(entry.name);
            output.boolean(entry.on);
        }
    }
    // Preserve every historical unspecified-request byte. Explicit intent uses
    // a separate domain so its typed value cannot alias the original encoding.
    if (layout.spec.layout_intent == LayoutIntent::unspecified) {
        output.string("qmg.layout.sha256.v1");
    } else {
        require_supported_layout_intent(layout.spec);
        output.string("qmg.layout.sha256.v2");
        output.enumeration(layout.spec.layout_intent);
    }
    append_prompt(output, layout.spec);
    append_sorted(
        output, layout.rooms, [](const Room &left, const Room &right) { return left.id < right.id; },
        [&](const Room &room) { append_room(output, room); });
    append_sorted(
        output, layout.connections, [](const Connection &left, const Connection &right) { return left.id < right.id; },
        [&](const Connection &connection) { append_connection(output, connection); });
    append_sorted(
        output, layout.solids, [](const LayoutSolid &left, const LayoutSolid &right) { return left.id < right.id; },
        [&](const LayoutSolid &solid) { append_solid(output, solid); });
    append_sorted(
        output, layout.placements, [](const Placement &left, const Placement &right) { return left.id < right.id; },
        [&](const Placement &placement) { append_placement(output, placement); });
    output.real(layout.planned_verticality);
    output.real(layout.planned_intensity);
    output.enumeration(layout.lighting_style);
    output.integer(layout.candidate_seed);
    output.real(layout.rank_score);
    return sha256(output.bytes());
}

} // namespace qmg
