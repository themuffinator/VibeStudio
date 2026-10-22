// Change d7-switches: the D7 stage-switch registry, its CLI assignment parser, the layout fingerprint domain
// and the report's top-level "switches" object.
#include "qmg/layout_fingerprint.hpp"
#include "qmg/map_writer.hpp"
#include "qmg/pipeline.hpp"
#include "qmg/report.hpp"
#include "qmg/switches.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

template <typename Exception, typename Function>
void require_throws(Function &&function, const std::string_view expected_text, const std::string_view message) {
    try {
        function();
    } catch (const Exception &error) {
        if (std::string_view(error.what()).find(expected_text) != std::string_view::npos)
            return;
        throw std::runtime_error(std::string(message) + ": unexpected message: " + error.what());
    }
    throw std::runtime_error(std::string(message) + ": no exception");
}

[[nodiscard]] qmg::StageSwitches parse(const std::vector<std::string> &assignments) {
    return qmg::parse_stage_switch_assignments(assignments);
}

// A mechanism-test registry: one switch that models a flipped default (on) and one that models a new switch
// (off). Names are sorted, as every registry must be.
constexpr std::array<qmg::StageSwitchDefinition, 2> kTestRegistry{{
    {qmg::StageSwitch{}, "t1.flipped", true, qmg::SwitchSubLane::mp, "test: a switch whose default was flipped",
     "test"},
    {qmg::StageSwitch{}, "t1.introduced", false, qmg::SwitchSubLane::sp, "test: a new switch", "test"},
}};

void registry_is_typed_sorted_and_holds_the_self_test() {
    const auto registry = qmg::stage_switch_registry();
    require(qmg::is_valid_switch_registry(registry), "the generator registry is valid");
    // Causes register their own rows (r1s.q1_intermission first, change r1s-q1-intermission); each cause's own test
    // checks its row, so this test checks only the mechanism invariants and the self-test row.
    require(!registry.empty(), "the registry holds at least the self-test switch");
    require(registry.size() <= qmg::kMaximumStageSwitches, "the registry fits one bit per switch");
    for (std::size_t index = 0; index < registry.size(); ++index) {
        require(static_cast<std::size_t>(registry[index].id) == index, "each enumerator is its registry index");
        require(!registry[index].cause.empty() && !registry[index].introduced_by.empty(),
                "each switch names its cause and the change that introduced it");
    }
    const qmg::StageSwitchDefinition &selftest = registry[static_cast<std::size_t>(qmg::StageSwitch::selftest_noop)];
    require(selftest.name == "selftest.noop", "the self-test switch is named selftest.noop");
    require(!selftest.default_on, "the self-test switch is off by default");
    require(selftest.sub_lane == qmg::SwitchSubLane::none, "the self-test switch changes no sub-lane");
    require(selftest.introduced_by == "d7-switches", "the self-test switch records its change id");
    require(qmg::to_string(qmg::SwitchSubLane::none) == "none" && qmg::to_string(qmg::SwitchSubLane::mp) == "mp" &&
                qmg::to_string(qmg::SwitchSubLane::sp) == "sp" && qmg::to_string(qmg::SwitchSubLane::both) == "both",
            "sub-lane names");
}

void names_are_dotted_lowercase() {
    for (const std::string_view name :
         {"r1s.q1_intermission", "a.b", "f1.q2_lamp_ceiling", "g1.lightmap_probe.report_only", "x9.a1_b2"})
        require(qmg::is_valid_switch_name(name), std::string("valid switch name rejected: ") + std::string(name));
    const std::string longest = "a." + std::string(qmg::kMaximumSwitchNameBytes - 2, 'b');
    require(qmg::is_valid_switch_name(longest), "a 64-byte name is valid");
    for (const std::string_view name : {"", "noop", "R1s.x", "r1s.X", "r1s..x", ".x", "x.", "r1s.x-y", "1a.b", "a.1b",
                                        "a._b", "a.b ", " a.b", "a.b=c", "a/b.c", "a.b\xc3\xa9"})
        require(!qmg::is_valid_switch_name(name), std::string("invalid switch name accepted: ") + std::string(name));
    require(!qmg::is_valid_switch_name(longest + "c"), "a 65-byte name is invalid");
}

void defaults_and_typed_queries() {
    qmg::StageSwitches switches;
    require(switches.all_default(), "a new set is at its defaults");
    require(switches.non_default().empty(), "a default set lists nothing");
    require(!switches.on(qmg::StageSwitch::selftest_noop), "typed query at default");
    require(!switches.on("selftest.noop"), "named query at default");
    require(switches == qmg::StageSwitches{}, "default sets compare equal");

    switches.set("selftest.noop", true);
    require(switches.on(qmg::StageSwitch::selftest_noop) && switches.on("selftest.noop"), "set on");
    require(!switches.all_default(), "a set switch is non-default");
    const std::vector<qmg::StageSwitches::Entry> entries = switches.non_default();
    require(entries.size() == 1 && entries.front().name == "selftest.noop" && entries.front().on,
            "the non-default list names the switch and its value");
    require(!(switches == qmg::StageSwitches{}), "a switched set differs from the default set");

    switches.set("selftest.noop", false);
    require(switches.all_default() && switches == qmg::StageSwitches{}, "setting the default restores the default");

    require_throws<std::invalid_argument>([&] { static_cast<void>(switches.on("r1s.not_registered")); },
                                          "unknown switch 'r1s.not_registered'", "unknown named query");
    // The message lists every registered switch in name order, so once a cause registers a row (change
    // r1s-q1-intermission) the list no longer starts with selftest.noop.
    require_throws<std::invalid_argument>([&] { switches.set("r1s.not_registered", true); }, "selftest.noop",
                                          "unknown set");
    require_throws<std::invalid_argument>([&] { switches.set("r1s.not_registered", true); },
                                          "; known switches: ", "unknown set lists the known switches");
}

void cli_assignments() {
    require(parse({}).all_default(), "no --switch is the default set");
    require(parse({"selftest.noop=off"}).all_default(), "an explicit default leaves the set at its defaults");
    require(parse({"selftest.noop=off"}) == qmg::StageSwitches{}, "an explicit default equals no assignment");
    const qmg::StageSwitches on = parse({"selftest.noop=on"});
    require(on.on(qmg::StageSwitch::selftest_noop) && !on.all_default(), "=on sets the switch");

    require_throws<std::invalid_argument>([] { static_cast<void>(parse({"r1s.not_registered=on"})); },
                                          "unknown switch 'r1s.not_registered'", "unknown name");
    require_throws<std::invalid_argument>([] { static_cast<void>(parse({"Selftest.Noop=on"})); },
                                          "unknown switch 'Selftest.Noop'", "names are case-sensitive");
    require_throws<std::invalid_argument>([] { static_cast<void>(parse({"noop=on"})); }, "unknown switch 'noop'",
                                          "undotted name");
    require_throws<std::invalid_argument>([] { static_cast<void>(parse({"selftest.noop=on", "selftest.noop=on"})); },
                                          "selftest.noop is given more than once", "duplicate with the same value");
    require_throws<std::invalid_argument>([] { static_cast<void>(parse({"selftest.noop=off", "selftest.noop=on"})); },
                                          "selftest.noop is given more than once", "duplicate with a new value");
    for (const std::string &malformed : std::vector<std::string>{
             "selftest.noop", "selftest.noop=", "=on", "=", "selftest.noop=true", "selftest.noop=1", "selftest.noop=ON",
             "selftest.noop=on ", "selftest.noop=on=off", ""})
        require_throws<std::invalid_argument>([&] { static_cast<void>(parse({malformed})); }, "expects <name>=on|off",
                                              "malformed value '" + malformed + "'");
}

void custom_registries_cover_default_on_switches() {
    qmg::StageSwitches switches{kTestRegistry};
    require(switches.all_default() && switches.on("t1.flipped") && !switches.on("t1.introduced"),
            "a custom registry starts at its own defaults");
    require_throws<std::logic_error>([&] { static_cast<void>(switches.on(qmg::StageSwitch::selftest_noop)); },
                                     "generator's switch registry", "typed queries need the generator registry");

    const std::vector<std::string> both{"t1.introduced=on", "t1.flipped=off"};
    const qmg::StageSwitches parsed = qmg::parse_stage_switch_assignments(both, kTestRegistry);
    const std::vector<qmg::StageSwitches::Entry> entries = parsed.non_default();
    require(entries.size() == 2, "both switches are away from their defaults");
    require(entries[0] == qmg::StageSwitches::Entry{"t1.flipped", false} &&
                entries[1] == qmg::StageSwitches::Entry{"t1.introduced", true},
            "non-default entries are listed in registry (name) order, whatever the assignment order");
    const std::vector<std::string> reversed{"t1.flipped=off", "t1.introduced=on"};
    require(qmg::parse_stage_switch_assignments(reversed, kTestRegistry) == parsed, "assignment order does not matter");
    const std::vector<std::string> flipped_on{"t1.flipped=on"};
    require(qmg::parse_stage_switch_assignments(flipped_on, kTestRegistry).all_default(),
            "=on is the default of a flipped switch");
    require(!(parsed == qmg::StageSwitches{}), "sets over different registries differ");

    const std::array<qmg::StageSwitchDefinition, 2> unsorted{{kTestRegistry[1], kTestRegistry[0]}};
    const std::array<qmg::StageSwitchDefinition, 2> duplicate{{kTestRegistry[0], kTestRegistry[0]}};
    const std::array<qmg::StageSwitchDefinition, 1> bad_name{
        {{qmg::StageSwitch{}, "Bad.Name", false, qmg::SwitchSubLane::none, "test", "test"}}};
    for (const std::span<const qmg::StageSwitchDefinition> invalid :
         {std::span<const qmg::StageSwitchDefinition>(unsorted), std::span<const qmg::StageSwitchDefinition>(duplicate),
          std::span<const qmg::StageSwitchDefinition>(bad_name)}) {
        require(!qmg::is_valid_switch_registry(invalid), "invalid registry accepted by the check");
        require_throws<std::invalid_argument>([&] { qmg::StageSwitches rejected{invalid}; }, "registry",
                                              "invalid registry accepted by the set");
    }
    std::vector<std::string> names;
    std::vector<qmg::StageSwitchDefinition> oversized;
    names.reserve(qmg::kMaximumStageSwitches + 1);
    for (std::size_t index = 0; index <= qmg::kMaximumStageSwitches; ++index) {
        const char tens = static_cast<char>('a' + index / 26);
        const char units = static_cast<char>('a' + index % 26);
        names.push_back(std::string("t1.") + tens + units);
    }
    for (const std::string &name : names)
        oversized.push_back({qmg::StageSwitch{}, name, false, qmg::SwitchSubLane::none, "test", "test"});
    require(qmg::is_valid_switch_registry(std::span(oversized).first(qmg::kMaximumStageSwitches)), "64 switches fit");
    require(!qmg::is_valid_switch_registry(oversized), "65 switches do not fit one bit each");
    qmg::StageSwitches full{std::span<const qmg::StageSwitchDefinition>(oversized).first(qmg::kMaximumStageSwitches)};
    full.set(names[qmg::kMaximumStageSwitches - 1], true);
    require(full.on(names[qmg::kMaximumStageSwitches - 1]) && !full.on(names[0]) && full.non_default().size() == 1,
            "the 64th switch uses the top bit");
}

void fingerprint_includes_only_non_default_switches() {
    qmg::Layout layout;
    layout.spec.source_prompt = "fingerprint fixture";
    layout.spec.game = qmg::Game::quake2;
    layout.spec.mode = qmg::GameMode::deathmatch;
    layout.rooms = {{qmg::RoomId{1}, "a", {{0, 0, 0}, {256, 256, 128}}, {"start"}}};
    const std::string base = qmg::canonical_layout_sha256(layout);
    require(base.size() == 64, "fingerprint is a sha256");

    qmg::Layout explicit_default = layout;
    explicit_default.spec.switches = parse({"selftest.noop=off"});
    require(qmg::canonical_layout_sha256(explicit_default) == base, "a default switch adds no fingerprint byte");

    qmg::Layout switched = layout;
    switched.spec.switches = parse({"selftest.noop=on"});
    const std::string switched_hash = qmg::canonical_layout_sha256(switched);
    require(switched_hash != base, "a non-default switch moves the fingerprint into its own domain");
    require(qmg::canonical_layout_sha256(switched) == switched_hash, "the switched fingerprint is deterministic");

    qmg::Layout custom = layout;
    const std::vector<std::string> one{"t1.introduced=on"};
    const std::vector<std::string> two{"t1.introduced=on", "t1.flipped=off"};
    custom.spec.switches = qmg::parse_stage_switch_assignments(one, kTestRegistry);
    const std::string custom_one = qmg::canonical_layout_sha256(custom);
    custom.spec.switches = qmg::parse_stage_switch_assignments(two, kTestRegistry);
    const std::string custom_two = qmg::canonical_layout_sha256(custom);
    require(custom_one != base && custom_two != base && custom_one != custom_two && custom_one != switched_hash,
            "each non-default switch set has its own fingerprint");
    custom.spec.switches = qmg::StageSwitches{kTestRegistry};
    require(qmg::canonical_layout_sha256(custom) == base, "a custom registry at its defaults adds nothing");
}

[[nodiscard]] std::string replace_all(std::string text, const std::string_view from, const std::string_view to) {
    for (std::size_t position = text.find(from); position != std::string::npos;
         position = text.find(from, position + to.size()))
        text.replace(position, from.size(), to);
    return text;
}

void pipeline_carries_switches_to_stages_and_report(const std::string_view prompt) {
    const std::string label(prompt);
    const qmg::PipelineResult baseline = qmg::GenerationPipeline{}.run(prompt);
    qmg::PipelineOptions explicit_off;
    explicit_off.switches = parse({"selftest.noop=off"});
    const qmg::PipelineResult defaulted = qmg::GenerationPipeline{}.run(prompt, {}, explicit_off);
    qmg::PipelineOptions on;
    on.switches = parse({"selftest.noop=on"});
    const qmg::PipelineResult switched = qmg::GenerationPipeline{}.run(prompt, {}, on);

    require(baseline.map_build && defaulted.map_build && switched.map_build, label + ": maps are built");
    require(switched.layout.spec.switches.on(qmg::StageSwitch::selftest_noop),
            label + ": the chosen layout carries the switch to every later stage");
    require(baseline.layout.spec.switches.all_default(), label + ": the default run carries the default set");

    const std::string baseline_map = qmg::MapWriter{}.render(baseline.map_build->document);
    require(qmg::MapWriter{}.render(defaulted.map_build->document) == baseline_map,
            label + ": an explicit default leaves the map bytes unchanged");
    require(qmg::MapWriter{}.render(switched.map_build->document) == baseline_map,
            label + ": the no-op self-test switch leaves the map bytes unchanged");

    const std::string baseline_report = qmg::render_report(baseline);
    require(qmg::render_report(defaulted) == baseline_report,
            label + ": an explicit default leaves the report bytes unchanged");
    require(baseline_report.find("\"switches\"") == std::string::npos,
            label + ": a default report has no switches member");

    const std::string switched_report = qmg::render_report(switched);
    const std::string switches_line = "  \"switches\": {\"selftest.noop\": \"on\"},\n";
    const std::size_t line = switched_report.find(switches_line);
    require(line != std::string::npos, label + ": the switched report lists the non-default switch");
    require(switched_report.find(switches_line, line + 1) == std::string::npos, label + ": listed once");
    require(switched_report.find("\"generator_version\"") < line && line < switched_report.find("\"layout_sha256\""),
            label + ": the switches member sits after generator_version");

    const std::string baseline_hash = qmg::canonical_layout_sha256(baseline.layout);
    const std::string switched_hash = qmg::canonical_layout_sha256(switched.layout);
    require(baseline_hash != switched_hash, label + ": the switched layout fingerprint differs");
    std::string stripped = switched_report;
    stripped.erase(line, switches_line.size());
    stripped = replace_all(stripped, switched_hash, baseline_hash);
    require(stripped == baseline_report,
            label + ": a no-op switch changes the report only by its switches member and the layout fingerprint");
}

} // namespace

int main() {
    try {
        registry_is_typed_sorted_and_holds_the_self_test();
        names_are_dotted_lowercase();
        defaults_and_typed_queries();
        cli_assignments();
        custom_registries_cover_default_on_switches();
        fingerprint_includes_only_non_default_switches();
        pipeline_carries_switches_to_stages_and_report("Quake 1 single-player base, 5 rooms, seed 31");
        pipeline_carries_switches_to_stages_and_report("Quake 2 deathmatch arena, 6 rooms, seed 42");
        pipeline_carries_switches_to_stages_and_report("Quake 3 capture the flag map, 6 rooms, seed 7");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "stage switch tests passed\n";
    return 0;
}
