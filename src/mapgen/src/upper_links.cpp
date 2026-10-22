#include "qmg/upper_links.hpp"

#include "qmg/map_builder.hpp"
#include "qmg/navigation.hpp"
#include "qmg/validator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace qmg {
namespace {

constexpr GridCoord width = 160;
constexpr GridCoord thickness = 24;
constexpr GridCoord clearance = 192;
constexpr std::size_t strip_cap = 65536;
constexpr std::size_t lowering_cap = 64;

void require(bool condition, const char *message) {
    if (!condition)
        throw std::invalid_argument(message);
}

bool tagged(const Room &room, const char *tag) {
    return std::find(room.tags.begin(), room.tags.end(), tag) != room.tags.end();
}

bool overlap(const GridBounds &a, const GridBounds &b) {
    return a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y && a.min.z < b.max.z &&
           b.min.z < a.max.z;
}

bool contains(const GridBounds &outer, const GridBounds &inner) {
    return outer.contains(inner.min) && outer.contains(inner.max);
}

bool bounded(const GridBounds &box) {
    constexpr GridBounds limit{{-32768, -32768, -32768}, {32768, 32768, 32768}};
    return box.valid() && contains(limit, box);
}

GridCoord along(GridVec3 point, bool x_axis) { return x_axis ? point.x : point.y; }
GridCoord across(GridVec3 point, bool x_axis) { return x_axis ? point.y : point.x; }

GridBounds strip(bool x_axis, GridCoord begin, GridCoord end, GridCoord center, GridCoord low, GridCoord high) {
    return x_axis ? GridBounds{{begin, center - width / 2, low}, {end, center + width / 2, high}}
                  : GridBounds{{center - width / 2, begin, low}, {center + width / 2, end, high}};
}

GridVec3 point(bool x_axis, GridCoord position, GridCoord center, GridCoord height) {
    return x_axis ? GridVec3{position, center, height} : GridVec3{center, position, height};
}

// SplitMix64's invertible finalizer. This is a procedural selection key, not a
// quality score. The geometry tie key below makes iteration order immaterial.
std::uint64_t mix(std::uint64_t value) {
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

struct Proposal {
    const Room *from{};
    const Room *to{};
    const LayoutSolid *from_deck{};
    const LayoutSolid *to_deck{};
    GridBounds passage;
    std::array<GridBounds, 3> slabs;
    GridBounds upper_strip;
    GridVec3 from_feet;
    GridVec3 to_feet;
    std::uint64_t key{};
};

auto identity(const Proposal &value) {
    return std::tuple{value.passage, value.from->id, value.to->id, value.from_deck->id, value.to_deck->id};
}

std::uint64_t selection_key(const Proposal &value, std::uint64_t seed) {
    auto key = mix(seed ^ 0x75707065722d7631ULL); // "upper-v1"
    for (const auto coordinate : {value.passage.min.x, value.passage.min.y, value.passage.min.z, value.passage.max.x,
                                  value.passage.max.y, value.passage.max.z})
        key = mix(key ^ static_cast<std::uint32_t>(coordinate));
    key = mix(key ^ value.from_deck->id.value);
    return mix(key ^ value.to_deck->id.value);
}

// Public-input validation deliberately matches the existing room-local box
// construction. In particular, every pre-existing passage has the common room
// floor, so local floor nodes cannot acquire a hole from a lower free volume.
std::map<SolidId, RoomId> validate_context(const Layout &layout) {
    require(layout.rooms.size() == static_cast<std::size_t>(layout.spec.room_count),
            "upper links require request/room count cohesion");
    require(layout.placements.empty(), "upper links must be constructed before entity allocation");
    require(layout.solids.size() <= 512 && layout.connections.size() <= 32,
            "upper link construction input exceeds the bounded compact search");
    const auto floor = layout.rooms.front().bounds.min.z;
    std::set<RoomId> rooms;
    for (const auto &room : layout.rooms) {
        require(room.id && rooms.insert(room.id).second && bounded(room.bounds) && room.bounds.min.z == floor &&
                    tagged(room, "compact_mp") && tagged(room, "compact_family_pinwheel"),
                "upper links require uniquely identified marked rooms with a common floor");
        require(!tagged(room, "inter_room_upper_link"), "upper links are a single construction pass");
    }
    for (std::size_t i = 0; i < layout.rooms.size(); ++i)
        for (std::size_t j = i + 1; j < layout.rooms.size(); ++j)
            require(!overlap(layout.rooms[i].bounds, layout.rooms[j].bounds), "upper link room volumes overlap");
    std::set<ConnectionId> connections;
    for (const auto &connection : layout.connections) {
        const auto *from = find_room(layout, connection.from_room);
        const auto *to = find_room(layout, connection.to_room);
        require(connection.id && connections.insert(connection.id).second && from && to && from != to &&
                    connection.passage_volumes.size() == 1 && connection.width == width &&
                    connection.height == clearance && connection_has_valid_passage(*from, *to, connection),
                "upper links require existing ordinary compact face connections");
        const auto &volume = connection.passage_volumes.front();
        require(bounded(volume) && volume.min.z == floor && volume.max.z == floor + clearance,
                "upper links require original ground-only passage geometry");
    }
    std::map<SolidId, RoomId> ownership;
    for (const auto &solid : layout.solids) {
        const auto *box = get_box(solid.geometry);
        require(solid.id && box && bounded(*box) && solid.contents == BrushContents::solid && !solid.owner &&
                    (solid.ownership == BrushOwnership::structural || solid.ownership == BrushOwnership::detail) &&
                    solid.material >= MaterialRole::floor && solid.material <= MaterialRole::hidden &&
                    solid.material != MaterialRole::liquid &&
                    (solid.purpose == BrushPurpose::structural || solid.purpose == BrushPurpose::raised_platform ||
                     solid.purpose == BrushPurpose::navigation_stair),
                "upper links require static all-solid AABB architecture");
        const Room *owner = nullptr;
        for (const auto &room : layout.rooms)
            if (contains(room.bounds, *box)) {
                require(owner == nullptr, "upper link solid has ambiguous room containment");
                owner = &room;
            }
        require(owner && ownership.emplace(solid.id, owner->id).second,
                "upper links require uniquely identified room-contained solids");
        if (solid.purpose == BrushPurpose::raised_platform)
            require(box->max.z - box->min.z == thickness && (box->max.z == floor + 128 || box->max.z == floor + 192),
                    "upper links require existing24-thick128/192-height decks");
        if (solid.purpose == BrushPurpose::navigation_stair) {
            const auto dx = box->max.x - box->min.x, dy = box->max.y - box->min.y;
            require(box->min.z == floor && box->max.z > floor && box->max.z <= floor + 192 &&
                        (box->max.z - floor) % 16 == 0 && ((dx == 128 && dy == 36) || (dx == 36 && dy == 128)),
                    "upper links require the existing full-width16-rise36-tread stairs");
        }
    }
    return ownership;
}

// Check an actual ordinary local route to the room floor, restricted to that
// room and its solids. A room-level graph edge or platform tag is insufficient.
struct RoomAccess {
    std::set<NavigationNodeId> from_ground;
    std::set<NavigationNodeId> to_ground;
};

RoomAccess local_ground_access(const NavigationGraph &graph, RoomId room, GridCoord floor,
                               const std::map<SolidId, RoomId> &ownership) {
    std::set<NavigationNodeId> allowed, ground;
    for (const auto &node : graph.nodes()) {
        const bool local =
            (node.source.kind == NavigationSourceKind::room && node.source.id == room.value) ||
            (node.source.kind == NavigationSourceKind::solid && ownership.contains(SolidId{node.source.id}) &&
             ownership.at(SolidId{node.source.id}) == room);
        if (!local)
            continue;
        allowed.insert(node.id);
        if (node.area.floor_z == floor)
            ground.insert(node.id);
    }
    const auto flood = [&](bool reverse) {
        auto reached = ground;
        std::vector<NavigationNodeId> pending(ground.begin(), ground.end());
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            for (const auto &edge : graph.edges()) {
                const auto from = reverse ? edge.to : edge.from, to = reverse ? edge.from : edge.to;
                if (from == id && allowed.contains(to) &&
                    (edge.kind == NavigationTransitionKind::walk || edge.kind == NavigationTransitionKind::step) &&
                    reached.insert(to).second)
                    pending.push_back(to);
            }
        }
        return reached;
    };
    return {flood(false), flood(true)};
}

bool endpoint_has_ground_access(const NavigationGraph &graph, const RoomAccess &access, SolidId deck, GridVec3 feet) {
    return std::any_of(graph.nodes().begin(), graph.nodes().end(), [&](const auto &node) {
        return node.source.kind == NavigationSourceKind::solid && node.source.id == deck.value &&
               node.area.floor_z == feet.z && node.area.contains_xy(feet) && access.from_ground.contains(node.id) &&
               access.to_ground.contains(node.id);
    });
}

bool physically_clear(const Layout &layout, const Proposal &proposal) {
    for (const auto &solid : layout.solids) {
        const auto &box = *get_box(solid.geometry);
        if (overlap(proposal.upper_strip, box))
            return false;
        for (const auto &slab : proposal.slabs) {
            // Same-height deck overlap is harmless, but crossing through an old
            // barrier or a different-height platform is not a validated motif.
            if (overlap(slab, box) && !(solid.purpose == BrushPurpose::raised_platform && box.max.z == slab.max.z))
                return false;
            if (solid.purpose == BrushPurpose::navigation_stair || solid.purpose == BrushPurpose::raised_platform) {
                // A Q2 step attempt raises the hull up to18 before advancing.
                // Protect that envelope over the entire old tread footprint,
                // not only the standing state at each tread's authored height.
                const auto step_raise = solid.purpose == BrushPurpose::navigation_stair ? 18 : 0;
                const GridBounds standing{{box.min.x, box.min.y, box.max.z},
                                          {box.max.x, box.max.y, box.max.z + 56 + step_raise}};
                if (overlap(slab, standing))
                    return false;
            }
        }
    }
    return true;
}

} // namespace

UpperLinkResult add_compact_upper_link(Layout &layout) {
    require_supported_layout_intent(layout.spec);
    UpperLinkResult result;
    if (layout.spec.layout_intent != LayoutIntent::wrapped_center)
        return result;
    require(std::isfinite(layout.planned_verticality) && layout.planned_verticality >= 0 &&
                layout.planned_verticality <= 1,
            "upper links require finite planned verticality in0..1");
    if (layout.planned_verticality < .3)
        return result;
    result.applicable = true;
    const auto ownership = validate_context(layout);
    const auto navigation = NavigationAnalyzer{}.analyze(layout);
    const auto floor = layout.rooms.front().bounds.min.z;
    std::map<RoomId, RoomAccess> access;
    for (const auto &room : layout.rooms)
        access.emplace(room.id, local_ground_access(navigation.graph, room.id, floor, ownership));
    std::vector<const LayoutSolid *> decks;
    for (const auto &solid : layout.solids) {
        const auto &box = *get_box(solid.geometry);
        if (solid.purpose == BrushPurpose::raised_platform && box.max.z - box.min.z == thickness &&
            (box.max.z == floor + 128 || box.max.z == floor + 192)) {
            decks.push_back(&solid);
        }
    }
    std::vector<Proposal> feasible;
    for (std::size_t i = 0; i < layout.rooms.size(); ++i)
        for (std::size_t j = i + 1; j < layout.rooms.size(); ++j)
            for (const bool x_axis : {false, true}) {
                const Room *from = &layout.rooms[i], *to = &layout.rooms[j];
                if (along(from->bounds.min, x_axis) > along(to->bounds.min, x_axis))
                    std::swap(from, to);
                const auto near = along(from->bounds.max, x_axis), far = along(to->bounds.min, x_axis);
                if (far - near != 48)
                    continue;
                for (const auto *a : decks)
                    for (const auto *b : decks) {
                        if (ownership.at(a->id) != from->id || ownership.at(b->id) != to->id)
                            continue;
                        const auto &ab = *get_box(a->geometry), &bb = *get_box(b->geometry);
                        const auto height = ab.max.z;
                        if (height != bb.max.z || from->bounds.max.z < height + clearance ||
                            to->bounds.max.z < height + clearance ||
                            along(ab.max, x_axis) - along(ab.min, x_axis) < 32 ||
                            along(bb.max, x_axis) - along(bb.min, x_axis) < 32)
                            continue;
                        const auto first = along(ab.max, x_axis), last = along(bb.min, x_axis);
                        if (first >= near || last <= far)
                            continue;
                        const auto low = std::max(across(ab.min, x_axis), across(bb.min, x_axis)) + width / 2;
                        const auto high = std::min(across(ab.max, x_axis), across(bb.max, x_axis)) - width / 2;
                        // Room recentering can put the entire motif lattice at
                        // 8 mod16. Anchor the16-unit sequence to the legal deck
                        // interval, preserving its sole center when width=160.
                        for (auto center = low; center <= high; center += 16) {
                            require(++result.considered_strips <= strip_cap, "upper link strip search exceeds bound");
                            Proposal candidate;
                            candidate.from = from;
                            candidate.to = to;
                            candidate.from_deck = a;
                            candidate.to_deck = b;
                            candidate.passage = strip(x_axis, near, far, center, height, height + clearance);
                            candidate.slabs = {strip(x_axis, first, near, center, height - thickness, height),
                                               strip(x_axis, far, last, center, height - thickness, height),
                                               strip(x_axis, near, far, center, height - thickness, height)};
                            candidate.from_feet = point(x_axis, first - 16, center, height);
                            candidate.to_feet = point(x_axis, last + 16, center, height);
                            candidate.upper_strip = strip(x_axis, first - 32, last + 32, center, height, height + 56);
                            const bool third_room_overlap =
                                std::any_of(layout.rooms.begin(), layout.rooms.end(), [&](const auto &room) {
                                    return room.id != from->id && room.id != to->id &&
                                           room.bounds.min.x < candidate.passage.max.x &&
                                           room.bounds.max.x > candidate.passage.min.x &&
                                           room.bounds.min.y < candidate.passage.max.y &&
                                           room.bounds.max.y > candidate.passage.min.y;
                                });
                            if (!contains(from->bounds, candidate.slabs[0]) ||
                                !contains(to->bounds, candidate.slabs[1]) || third_room_overlap ||
                                !endpoint_has_ground_access(navigation.graph, access.at(from->id), a->id,
                                                            candidate.from_feet) ||
                                !endpoint_has_ground_access(navigation.graph, access.at(to->id), b->id,
                                                            candidate.to_feet) ||
                                !physically_clear(layout, candidate))
                                continue;
                            ++result.feasible_strips;
                            candidate.key = selection_key(candidate, layout.candidate_seed);
                            feasible.push_back(candidate);
                        }
                    }
            }
    std::sort(feasible.begin(), feasible.end(), [](const auto &a, const auto &b) {
        return std::tuple{a.key, identity(a)} < std::tuple{b.key, identity(b)};
    });
    MapBuildOptions options;
    options.dialect = MapDialect::quake2_valve220;
    for (const auto &choice : feasible) {
        if (result.lowering_attempts == lowering_cap) {
            result.lowering_cap_reached = true;
            break;
        }
        ++result.lowering_attempts;
        // Commit on a copy: ID exhaustion, unsupported lowering errors or
        // allocation failure cannot partially change the caller.
        Layout work = layout;
        UpperLinkSelection certificate{
            next_connection_id(work), choice.from->id, choice.to->id,    choice.from_deck->id, choice.to_deck->id,
            choice.passage.min.z,     choice.passage,  choice.from_feet, choice.to_feet,       {}};
        for (const auto &slab : choice.slabs) {
            const auto id = next_solid_id(work);
            work.solids.push_back({id,
                                   slab,
                                   MaterialRole::platform,
                                   BrushPurpose::raised_platform,
                                   BrushOwnership::structural,
                                   BrushContents::solid,
                                   {}});
            certificate.added_solids.push_back(id);
        }
        work.connections.push_back({certificate.connection,
                                    certificate.from_room,
                                    certificate.to_room,
                                    width,
                                    clearance,
                                    {certificate.passage}});
        find_room(work, certificate.from_room)->tags.push_back("inter_room_upper_link");
        find_room(work, certificate.to_room)->tags.push_back("inter_room_upper_link");
        try {
            (void)MapBuilder{}.build(work, options);
        } catch (const MapBuildError &error) {
            if (error.code() != "shell_clearance")
                throw;
            ++result.lowering_rejections;
            continue;
        }
        result.selected = std::move(certificate);
        layout = std::move(work);
        break;
    }
    return result;
}

} // namespace qmg
