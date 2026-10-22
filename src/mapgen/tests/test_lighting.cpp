#include "qmg/lighting.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

[[nodiscard]] qmg::Layout one_room_layout() {
    qmg::Layout layout;
    layout.spec.game = qmg::Game::quake1;
    layout.spec.mode = qmg::GameMode::single_player;
    layout.lighting_style = qmg::LightingStyle::balanced;
    layout.rooms.push_back({{1}, "room", {{-512, -256, 0}, {512, 256, 256}}, {"start"}});
    return layout;
}

void add_light(qmg::Layout &layout, const std::uint32_t id, const qmg::GridVec3 origin,
               const std::int32_t intensity = 600, const qmg::RgbColor color = {1000, 1000, 1000}) {
    qmg::Placement light;
    light.id = {id};
    light.role = qmg::PlacementRole::light;
    light.origin = origin;
    light.light_intensity = intensity;
    light.light_color = color;
    layout.placements.push_back(light);
}

void add_spawn(qmg::Layout &layout, const std::uint32_t id, const qmg::GridVec3 origin) {
    qmg::Placement spawn;
    spawn.id = {id};
    spawn.role = qmg::PlacementRole::player_start;
    spawn.origin = origin;
    layout.placements.push_back(spawn);
}

template <typename Callable>
void require_error(Callable &&callable, const std::string &code, const std::string &message) {
    try {
        callable();
    } catch (const qmg::LightingError &error) {
        require(error.code() == code, message + ": wrong code " + error.code());
        return;
    }
    throw std::runtime_error(message + ": no LightingError");
}

[[nodiscard]] const qmg::LightingSampleMetric &placement_sample(const qmg::LightingAnalysis &analysis,
                                                                const qmg::PlacementId placement) {
    const auto found = std::find_if(analysis.samples.begin(), analysis.samples.end(), [&](const auto &sample) {
        return sample.kind == qmg::LightingSampleKind::gameplay_placement && sample.source_id == placement.value;
    });
    require(found != analysis.samples.end(), "missing placement lighting sample");
    return *found;
}

void test_readable_samples_and_summary() {
    qmg::Layout layout = one_room_layout();
    add_spawn(layout, 1, {0, 0, 48});
    add_light(layout, 2, {0, 0, 192});
    const qmg::LightingAnalysis analysis = qmg::LightingAnalyzer{}.analyze(layout);
    require(analysis.summary.light_count == 1 && analysis.summary.sample_count == 2,
            "lighting summary omitted room or gameplay samples");
    require(analysis.summary.visibility_tests == 2 && analysis.summary.readable_samples == 2,
            "visible light did not illuminate deterministic samples");
    require(analysis.summary.critical_samples == 1 && analysis.summary.readable_critical_samples == 1,
            "critical spawn readability was not recorded");
    require(analysis.findings.empty(), "readable one-room fixture produced lighting findings");
    const auto &spawn = placement_sample(analysis, {1});
    require(spawn.visible_lights == 1 && spawn.brightness_milli > 0 && spawn.readable,
            "spawn did not receive its visible light contribution");
}

void test_occlusion_and_critical_finding() {
    qmg::Layout layout = one_room_layout();
    add_spawn(layout, 1, {-256, 0, 48});
    add_light(layout, 2, {256, 0, 48});
    layout.solids.push_back({{1},
                             qmg::GridBounds{{-32, -128, 0}, {32, 128, 192}},
                             qmg::MaterialRole::wall,
                             qmg::BrushPurpose::structural,
                             qmg::BrushOwnership::structural,
                             qmg::BrushContents::solid,
                             std::nullopt});
    const qmg::LightingAnalysis analysis = qmg::LightingAnalyzer{}.analyze(layout);
    const auto &spawn = placement_sample(analysis, {1});
    require(spawn.visible_lights == 0 && spawn.occluded_lights == 1 && !spawn.readable,
            "solid occluder did not block a critical sample");
    require(std::any_of(analysis.findings.begin(), analysis.findings.end(),
                        [&](const auto &finding) {
                            return finding.code == qmg::LightingFindingCode::unlit_critical_placement &&
                                   finding.sample == spawn.id && finding.severity == qmg::Severity::error;
                        }),
            "occluded spawn did not produce a typed critical finding");
}

[[nodiscard]] qmg::Layout two_room_layout() {
    qmg::Layout layout;
    layout.spec.game = qmg::Game::quake3;
    layout.spec.mode = qmg::GameMode::duel;
    layout.rooms.push_back({{1}, "near", {{-512, -256, 0}, {0, 256, 256}}, {}});
    layout.rooms.push_back({{2}, "far", {{256, -256, 0}, {768, 256, 256}}, {}});
    layout.connections.push_back({{1}, {1}, {2}, 256, 192, {{{0, -128, 0}, {256, 128, 192}}}});
    add_light(layout, 1, {-448, 0, 48}, 200);
    return layout;
}

void test_connection_contrast_and_reordering() {
    qmg::Layout ordered = two_room_layout();
    qmg::LightingOptions options;
    options.thresholds.minimum_readable_brightness_milli = 1000;
    options.thresholds.maximum_connection_contrast_basis_points = 20'000;
    const qmg::LightingAnalysis first = qmg::LightingAnalyzer{}.analyze(ordered, options);
    require(first.connections.size() == 1 && !first.connections.front().readable &&
                first.connections.front().contrast_basis_points >
                    options.thresholds.maximum_connection_contrast_basis_points,
            "dark route did not produce unreadable/high-contrast connection metrics");
    require(std::any_of(
                first.findings.begin(), first.findings.end(),
                [](const auto &finding) { return finding.code == qmg::LightingFindingCode::unreadable_connection; }) &&
                std::any_of(first.findings.begin(), first.findings.end(),
                            [](const auto &finding) {
                                return finding.code == qmg::LightingFindingCode::excessive_connection_contrast;
                            }),
            "connection lighting findings were omitted");

    qmg::Layout shuffled = ordered;
    std::reverse(shuffled.rooms.begin(), shuffled.rooms.end());
    std::reverse(shuffled.connections.begin(), shuffled.connections.end());
    std::reverse(shuffled.placements.begin(), shuffled.placements.end());
    require(first == qmg::LightingAnalyzer{}.analyze(shuffled, options),
            "layout collection order changed lighting analysis bytes");
}

void test_style_thresholds() {
    qmg::Layout dark = one_room_layout();
    add_spawn(dark, 1, {0, 0, 48});
    add_light(dark, 2, {0, 0, 112}, 10);
    qmg::LightingOptions options;
    options.thresholds.minimum_readable_brightness_milli = 3000;
    dark.lighting_style = qmg::LightingStyle::low_key;
    const qmg::LightingAnalysis dark_analysis = qmg::LightingAnalyzer{}.analyze(dark, options);
    require(placement_sample(dark_analysis, {1}).readable, "dark style did not apply its documented lower threshold");

    qmg::Layout balanced = dark;
    balanced.lighting_style = qmg::LightingStyle::balanced;
    const qmg::LightingAnalysis balanced_analysis = qmg::LightingAnalyzer{}.analyze(balanced, options);
    require(!placement_sample(balanced_analysis, {1}).readable,
            "balanced style did not retain the stricter base threshold");
}

void test_fail_closed_limits_and_invalid_lights() {
    qmg::Layout no_lights = one_room_layout();
    add_spawn(no_lights, 1, {0, 0, 48});
    const qmg::LightingAnalysis missing = qmg::LightingAnalyzer{}.analyze(no_lights);
    require(missing.summary.error_findings >= 2 && missing.findings.front().code == qmg::LightingFindingCode::no_lights,
            "missing lights did not fail closed with global and critical findings");

    qmg::LightingOptions capped;
    capped.limits.max_samples = 1;
    require_error([&] { static_cast<void>(qmg::LightingAnalyzer{}.analyze(no_lights, capped)); }, "sample_limit",
                  "sample cap was not enforced before quadratic analysis");

    qmg::Layout invalid = one_room_layout();
    add_light(invalid, 1, {0, 0, 128}, 0);
    require_error([&] { static_cast<void>(qmg::LightingAnalyzer{}.analyze(invalid)); }, "invalid_light",
                  "non-positive typed light was accepted");

    qmg::LightingOptions invalid_options;
    invalid_options.thresholds.minimum_distance_squared = 0;
    require_error([&] { static_cast<void>(qmg::LightingAnalyzer{}.analyze(one_room_layout(), invalid_options)); },
                  "invalid_options", "invalid attenuation clamp was accepted");

    qmg::LightingOptions incompatible;
    incompatible.limits.max_abs_coordinate = incompatible.visibility.limits.max_abs_coordinate + 1;
    require_error([&] { static_cast<void>(qmg::LightingAnalyzer{}.analyze(one_room_layout(), incompatible)); },
                  "incompatible_visibility_options", "narrower nested visibility coordinate cap was accepted");

    incompatible = {};
    incompatible.visibility.limits.max_free_volumes = incompatible.limits.max_rooms;
    require_error([&] { static_cast<void>(qmg::LightingAnalyzer{}.analyze(one_room_layout(), incompatible)); },
                  "incompatible_visibility_options", "nested free-volume cap cannot cover configured lighting inputs");

    qmg::LightingOptions nonportable;
    nonportable.limits.max_abs_coordinate = std::numeric_limits<qmg::GridCoord>::max();
    nonportable.visibility.limits.max_abs_coordinate = std::numeric_limits<qmg::GridCoord>::max();
    require_error([&] { static_cast<void>(qmg::LightingAnalyzer{}.analyze(one_room_layout(), nonportable)); },
                  "invalid_options", "nonportable extreme coordinate range was accepted");
}

void test_duplicate_passages_do_not_skew_samples() {
    qmg::Layout baseline = two_room_layout();
    const qmg::LightingAnalysis first = qmg::LightingAnalyzer{}.analyze(baseline);
    baseline.connections.front().passage_volumes.push_back(baseline.connections.front().passage_volumes.front());
    const qmg::LightingAnalysis duplicate = qmg::LightingAnalyzer{}.analyze(baseline);
    require(first.summary.sample_count == duplicate.summary.sample_count &&
                first.summary.visibility_tests == duplicate.summary.visibility_tests &&
                first.connections.front().passage_samples.size() == 1 &&
                duplicate.connections.front().passage_samples.size() == 1,
            "duplicate passage volumes skewed lighting samples or visibility work");
}

} // namespace

int main() {
    try {
        test_readable_samples_and_summary();
        test_occlusion_and_critical_finding();
        test_connection_contrast_and_reordering();
        test_style_thresholds();
        test_fail_closed_limits_and_invalid_lights();
        test_duplicate_passages_do_not_skew_samples();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "lighting analyzer tests passed\n";
    return 0;
}
