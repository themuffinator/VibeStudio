#include "qmg/leak.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

[[nodiscard]] qmg::GridBounds box(const qmg::GridCoord min_x, const qmg::GridCoord min_y, const qmg::GridCoord min_z,
                                  const qmg::GridCoord max_x, const qmg::GridCoord max_y, const qmg::GridCoord max_z) {
    return {{min_x, min_y, min_z}, {max_x, max_y, max_z}};
}

[[nodiscard]] qmg::Room room(const std::uint32_t id, const qmg::GridBounds bounds) {
    return {qmg::RoomId{id}, "room-" + std::to_string(id), bounds, {}};
}

[[nodiscard]] qmg::LeakWitness
parse_witness(const std::string_view text,
              const qmg::CompilerPointfileDialect dialect = qmg::CompilerPointfileDialect::quake1,
              const qmg::PointfileLimits &limits = {}) {
    qmg::PointfileIngestResult parsed = qmg::parse_compiler_pointfile(text, dialect, limits);
    require(parsed.ok(), parsed.message);
    return std::move(*parsed.witness);
}

[[nodiscard]] qmg::LeakPoint fixed_point(const std::int64_t x, const std::int64_t y, const std::int64_t z) {
    return {x * qmg::leak_subunits_per_world_unit, y * qmg::leak_subunits_per_world_unit,
            z * qmg::leak_subunits_per_world_unit};
}

[[nodiscard]] qmg::Layout single_room_layout() {
    qmg::Layout layout;
    layout.rooms = {room(1, box(0, 0, 0, 256, 256, 128))};
    layout.rank_score = 12.5;
    return layout;
}

void real_shaped_pointfile_ingestion() {
    // NetRadiant q3map2 writes six-decimal XYZ lines in this form.
    const qmg::PointfileIngestResult q3 = qmg::parse_compiler_pointfile("128.000000 296.000000 260.000000\n"
                                                                        "128.000000 128.000000 256.000000\n"
                                                                        "0.000000 0.000000 32.000000\n",
                                                                        qmg::CompilerPointfileDialect::quake3);
    require(q3.ok(), "Q3-shaped pointfile was rejected");
    require(q3.witness->source_point_count == 3 && q3.witness->points.size() == 3,
            "Q3-shaped pointfile counts are wrong");
    require(q3.witness->points.front() == fixed_point(0, 0, 32),
            "Q3 witness direction was not normalized deterministically");

    // Pinned ericw qbsp emits general-format triples and, with its default
    // leak spacing, repeats the shared endpoint of consecutive portal trails.
    const qmg::PointfileIngestResult q1 = qmg::parse_compiler_pointfile("0 0 32\n64.5 -8 16\n64.5 -8 16\n1.28e2 0 32\n",
                                                                        qmg::CompilerPointfileDialect::quake1);
    require(q1.ok(), "Q1-shaped pointfile was rejected");
    require(q1.witness->source_point_count == 4 && q1.witness->points.size() == 3,
            "consecutive ericw portal endpoints were not normalized");

    const qmg::PointfileIngestResult q2 = qmg::parse_compiler_pointfile(
        "-0.000000\t64.000000 32.000000\r\n128.000000 64.000000 32.000000\r\n", qmg::CompilerPointfileDialect::quake2);
    require(q2.ok(), "Q2-shaped CRLF pointfile was rejected");
    require(q2.witness->points.front().x == 0, "negative zero was not normalized");
}

void deterministic_witness_normalization() {
    const qmg::LeakWitness forward = parse_witness("0 0 0\n32 8 4\n32 8 4\n64 16 8\n");
    const qmg::LeakWitness reverse = parse_witness("64 16 8\n32 8 4\n0 0 0\n");
    require(forward.points == reverse.points, "reversed witness did not canonicalize identically");
    require(forward.fingerprint == reverse.fingerprint, "reversed witness fingerprint changed");
    require(forward.source_point_count == 4 && reverse.source_point_count == 3,
            "source point counts should retain ingestion evidence");
}

void file_ingestion() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("qmg-leak-pointfile-" + std::to_string(suffix) + ".lin");
    {
        std::ofstream output(path, std::ios::binary);
        require(static_cast<bool>(output), "could not create temporary pointfile");
        output << "0 0 0\n64 0 0\n";
    }
    const qmg::PointfileIngestResult loaded = qmg::load_compiler_pointfile(path, qmg::CompilerPointfileDialect::quake3);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    require(loaded.ok() && loaded.witness->points.size() == 2, "file pointfile ingestion failed");

    const qmg::PointfileIngestResult missing =
        qmg::load_compiler_pointfile(path, qmg::CompilerPointfileDialect::quake3);
    require(missing.error == qmg::PointfileIngestError::io_error, "missing pointfile did not fail closed");
}

void malformed_and_capped_pointfiles() {
    const auto expect = [](const std::string_view text, const qmg::PointfileIngestError error,
                           const qmg::PointfileLimits &limits = qmg::PointfileLimits{}) {
        const qmg::PointfileIngestResult parsed =
            qmg::parse_compiler_pointfile(text, qmg::CompilerPointfileDialect::quake3, limits);
        require(!parsed.ok() && parsed.error == error && !parsed.witness,
                "malformed pointfile expected error " + std::to_string(static_cast<unsigned int>(error)) + ", got " +
                    std::to_string(static_cast<unsigned int>(parsed.error)));
    };
    expect("", qmg::PointfileIngestError::empty_input);
    expect(" \t\n", qmg::PointfileIngestError::empty_input);
    expect("1 2\n", qmg::PointfileIngestError::malformed_line);
    expect("1 2 3 4\n", qmg::PointfileIngestError::malformed_line);
    expect("1,0 2 3\n4 5 6\n", qmg::PointfileIngestError::malformed_line);
    expect("nan 0 0\n1 1 1\n", qmg::PointfileIngestError::non_finite_coordinate);
    expect("1e9999 0 0\n1 1 1\n", qmg::PointfileIngestError::coordinate_out_of_range);
    expect("0 0 0\n0 0 0\n", qmg::PointfileIngestError::too_few_distinct_points);
    std::string with_nul{"0 0 0\n1"};
    with_nul.push_back('\0');
    with_nul += " 1 1\n";
    expect(with_nul, qmg::PointfileIngestError::malformed_line);

    qmg::PointfileLimits coordinate_cap;
    coordinate_cap.max_absolute_coordinate = 10.0;
    expect("0 0 0\n11 0 0\n", qmg::PointfileIngestError::coordinate_out_of_range, coordinate_cap);
    qmg::PointfileLimits byte_cap;
    byte_cap.max_bytes = 5;
    expect("0 0 0\n1 1 1\n", qmg::PointfileIngestError::input_too_large, byte_cap);
    qmg::PointfileLimits line_cap;
    line_cap.max_line_bytes = 4;
    expect("0 0 0\n1 1 1\n", qmg::PointfileIngestError::line_too_long, line_cap);
    qmg::PointfileLimits point_cap;
    point_cap.max_points = 2;
    expect("0 0 0\n1 1 1\n2 2 2\n", qmg::PointfileIngestError::too_many_points, point_cap);
    qmg::PointfileLimits invalid_limits;
    invalid_limits.max_points = 1;
    expect("0 0 0\n1 1 1\n", qmg::PointfileIngestError::invalid_limits, invalid_limits);
    const qmg::PointfileIngestResult unsupported =
        qmg::parse_compiler_pointfile("0 0 0\n1 1 1\n", static_cast<qmg::CompilerPointfileDialect>(255));
    require(unsupported.error == qmg::PointfileIngestError::unsupported_dialect && !unsupported.witness,
            "unsupported compiler pointfile dialect was accepted");
}

void exterior_grid_representation() {
    const qmg::Layout layout = single_room_layout();
    const qmg::ExteriorGridResult built = qmg::build_layout_exterior_grid(layout);
    require(built.ok(), "single-room exterior grid failed");
    const qmg::ExteriorCellGrid &grid = *built.grid;
    require(grid.x_planes == std::vector<std::int64_t>({-1, 0, 256, 257}), "exterior X coordinate planes are wrong");
    require(grid.y_planes == std::vector<std::int64_t>({-1, 0, 256, 257}), "exterior Y coordinate planes are wrong");
    require(grid.z_planes == std::vector<std::int64_t>({-1, 0, 128, 129}), "exterior Z coordinate planes are wrong");
    const auto interior = qmg::locate_exterior_cell(grid, fixed_point(128, 128, 64));
    const qmg::LeakPoint outside{128 * qmg::leak_subunits_per_world_unit, 256 * qmg::leak_subunits_per_world_unit + 512,
                                 64 * qmg::leak_subunits_per_world_unit};
    const auto exterior = qmg::locate_exterior_cell(grid, outside);
    require(interior && interior->state == qmg::ExteriorCellState::interior, "room cell is not classified as interior");
    require(exterior && exterior->state == qmg::ExteriorCellState::exterior,
            "outside cell is not exterior-flood reachable");
    require(!qmg::locate_exterior_cell(grid, fixed_point(256, 128, 64)),
            "point on a coordinate plane should not claim a unique cell");
}

void room_boundary_diagnosis_and_proposal() {
    const qmg::Layout layout = single_room_layout();
    const qmg::LeakWitness witness = parse_witness("128 128 64\n128 300 64\n");
    const qmg::LeakDiagnosisResult diagnosis = qmg::diagnose_layout_leak(layout, witness);
    require(diagnosis.ok(), diagnosis.message);
    require(diagnosis.layout_sha256.size() == 64 &&
                diagnosis.proposal->applicability.layout_sha256 == diagnosis.layout_sha256,
            "leak diagnosis and proposal are not bound to the complete layout SHA-256");
    const qmg::LeakDiagnosisResult wrong_dialect = qmg::diagnose_layout_leak(
        layout, parse_witness("128 128 64\n128 300 64\n", qmg::CompilerPointfileDialect::quake3));
    require(wrong_dialect.status == qmg::LeakDiagnosisStatus::invalid_witness,
            "pointfile dialect incompatible with the layout title was accepted");
    require(diagnosis.exposed_crossing_count == 1 && diagnosis.ambiguous_crossing_count == 0,
            "single exposed room face produced wrong crossing counts");
    require(diagnosis.crossing->owner.kind == qmg::BoundaryOwnerKind::room &&
                diagnosis.crossing->owner.room == qmg::RoomId{1},
            "room crossing owner is wrong");
    require(diagnosis.crossing->axis == qmg::BoundaryAxis::y &&
                diagnosis.crossing->face == qmg::BoundaryFace::maximum && !diagnosis.crossing->entering_interior,
            "room crossing face or direction is wrong");
    require(diagnosis.crossing->point == fixed_point(128, 256, 64), "room crossing point is wrong");
    require(
        qmg::verify_exposed_boundary_proof(*diagnosis.exterior_grid, *diagnosis.crossing, *diagnosis.exterior_proof),
        "returned exterior-cell proof does not independently verify");
    require(diagnosis.proposal->bounds == box(64, 256, 0, 192, 272, 128), "local room seal bounds are wrong");
    require(diagnosis.proposal->regression.baseline_rank_score == 12.5 &&
                diagnosis.proposal->regression.baseline_exposed_crossings == 1 &&
                diagnosis.proposal->regression.required_max_exposed_crossings == 0,
            "proposal regression baseline is incomplete");
    require(qmg::local_seal_proposal_preconditions_hold(layout, *diagnosis.proposal),
            "fresh local seal proposal does not satisfy its preconditions");

    qmg::Layout changed = layout;
    changed.rooms.front().bounds.max.y = 260;
    require(!qmg::local_seal_proposal_preconditions_hold(changed, *diagnosis.proposal),
            "stale local seal proposal retained valid preconditions after geometry changed");
    changed = layout;
    changed.solids.push_back({qmg::SolidId{1}, box(16, 16, 0, 32, 32, 32), qmg::MaterialRole::wall,
                              qmg::BrushPurpose::structural, qmg::BrushOwnership::detail, qmg::BrushContents::solid,
                              std::nullopt});
    require(!qmg::local_seal_proposal_preconditions_hold(changed, *diagnosis.proposal),
            "stale local seal proposal retained valid preconditions after a solid changed");
    changed = layout;
    qmg::Placement light;
    light.id = qmg::PlacementId{1};
    light.role = qmg::PlacementRole::light;
    light.origin = {64, 64, 64};
    light.light_intensity = 300;
    light.light_color = qmg::RgbColor{1000, 1000, 1000};
    changed.placements.push_back(light);
    require(!qmg::local_seal_proposal_preconditions_hold(changed, *diagnosis.proposal),
            "stale local seal proposal retained valid preconditions after a placement changed");
    qmg::LocalSealProposal tampered = *diagnosis.proposal;
    tampered.bounds.min.x = -64;
    require(!qmg::local_seal_proposal_preconditions_hold(layout, tampered),
            "proposal extending beyond its responsible face was accepted");
    tampered = *diagnosis.proposal;
    ++tampered.bounds.min.x;
    require(!qmg::local_seal_proposal_preconditions_hold(layout, tampered),
            "proposal geometry no longer matching its applicability record was accepted");
    tampered = *diagnosis.proposal;
    tampered.regression.reject_new_validation_errors = false;
    require(!qmg::local_seal_proposal_preconditions_hold(layout, tampered),
            "proposal with weakened regression requirements was accepted");
    tampered = *diagnosis.proposal;
    ++tampered.applicability.exterior_proof.linear_index;
    require(!qmg::local_seal_proposal_preconditions_hold(layout, tampered),
            "proposal with a corrupt exterior proof retained valid preconditions");

    const qmg::LeakDiagnosisResult ceiling =
        qmg::diagnose_layout_leak(layout, parse_witness("128 128 64\n128 128 200\n"));
    require(ceiling.ok() && ceiling.crossing->axis == qmg::BoundaryAxis::z &&
                ceiling.crossing->face == qmg::BoundaryFace::maximum &&
                ceiling.proposal->material == qmg::MaterialRole::ceiling &&
                ceiling.proposal->bounds == box(64, 64, 128, 192, 192, 144),
            "ceiling crossing did not produce a typed local ceiling seal");
}

[[nodiscard]] qmg::Layout connection_layout() {
    qmg::Layout layout;
    layout.rooms = {room(1, box(0, 0, 0, 100, 100, 100)), room(2, box(200, 0, 0, 300, 100, 100)),
                    room(3, box(-400, 0, 0, -300, 100, 100))};
    layout.connections = {
        {qmg::ConnectionId{7},
         qmg::RoomId{1},
         qmg::RoomId{2},
         20,
         60,
         {box(100, 40, 20, 200, 60, 80), box(120, -200, 20, 180, -100, 80)}},
        {qmg::ConnectionId{8}, qmg::RoomId{3}, qmg::RoomId{1}, 20, 60, {box(-300, 40, 20, 0, 60, 80)}}};
    layout.rank_score = 7.25;
    return layout;
}

void passage_boundary_and_reorder_invariance() {
    const qmg::LeakWitness witness = parse_witness("150 50 50\n150 100 50\n");
    const qmg::Layout layout = connection_layout();
    const qmg::LeakDiagnosisResult first = qmg::diagnose_layout_leak(layout, witness);
    require(first.ok(), first.message);
    require(first.crossing->owner.kind == qmg::BoundaryOwnerKind::connection &&
                first.crossing->owner.connection == qmg::ConnectionId{7},
            "passage crossing was not attributed to its connection");
    require(first.crossing->owner.source_bounds == box(100, 40, 20, 200, 60, 80) &&
                first.crossing->axis == qmg::BoundaryAxis::y && first.crossing->face == qmg::BoundaryFace::maximum,
            "passage face diagnosis is wrong");

    qmg::Layout reordered = layout;
    std::reverse(reordered.rooms.begin(), reordered.rooms.end());
    std::reverse(reordered.connections.front().passage_volumes.begin(),
                 reordered.connections.front().passage_volumes.end());
    std::reverse(reordered.connections.begin(), reordered.connections.end());
    for (qmg::Connection &connection : reordered.connections)
        std::swap(connection.from_room, connection.to_room);
    const qmg::LeakDiagnosisResult second = qmg::diagnose_layout_leak(reordered, witness);
    require(second.ok(), second.message);
    require(first.crossing == second.crossing && first.proposal == second.proposal &&
                first.exterior_grid->x_planes == second.exterior_grid->x_planes &&
                first.exterior_grid->y_planes == second.exterior_grid->y_planes &&
                first.exterior_grid->z_planes == second.exterior_grid->z_planes &&
                first.exterior_grid->cells == second.exterior_grid->cells,
            "layout vector reordering changed leak diagnosis or proof");
    require(qmg::local_seal_proposal_preconditions_hold(reordered, *first.proposal),
            "layout reordering invalidated deterministic proposal preconditions");
    qmg::Layout reassigned = layout;
    reassigned.connections.front().to_room = qmg::RoomId{3};
    require(!qmg::local_seal_proposal_preconditions_hold(reassigned, *first.proposal),
            "connection reassignment did not invalidate proposal preconditions");
}

void first_crossing_is_stable() {
    qmg::Layout layout;
    layout.rooms = {room(20, box(200, 0, 0, 260, 100, 100)), room(10, box(0, 0, 0, 60, 100, 100))};
    const qmg::LeakWitness witness = parse_witness("-10 50 50\n300 50 50\n");
    const qmg::LeakDiagnosisResult diagnosis = qmg::diagnose_layout_leak(layout, witness);
    require(diagnosis.ok(), diagnosis.message);
    require(diagnosis.crossing->owner.room == qmg::RoomId{10} && diagnosis.crossing->face == qmg::BoundaryFace::minimum,
            "diagnosis did not select the first canonical exterior crossing");
    require(diagnosis.exposed_crossing_count == 4, "all exposed transitions were not recorded for regression metadata");

    const qmg::LeakWitness endpoint_witness = parse_witness("-10 50 50\n0 50 50\n50 50 50\n");
    qmg::Layout endpoint_layout;
    endpoint_layout.rooms = {room(1, box(0, 0, 0, 100, 100, 100))};
    const qmg::LeakDiagnosisResult endpoint = qmg::diagnose_layout_leak(endpoint_layout, endpoint_witness);
    require(endpoint.ok() && endpoint.crossing->witness_segment == 0 &&
                endpoint.crossing->segment_fraction_millionths == 1000000,
            "crossing exactly at a pointfile vertex was missed");
}

void no_crossing_and_ambiguity() {
    const qmg::Layout layout = single_room_layout();
    const qmg::LeakDiagnosisResult inside = qmg::diagnose_layout_leak(layout, parse_witness("32 32 32\n64 64 64\n"));
    require(inside.status == qmg::LeakDiagnosisStatus::no_exposed_crossing && !inside.proposal,
            "interior-only witness produced a seal proposal");
    const qmg::LeakDiagnosisResult outside =
        qmg::diagnose_layout_leak(layout, parse_witness("300 300 64\n400 400 64\n"));
    require(outside.status == qmg::LeakDiagnosisStatus::no_exposed_crossing && !outside.proposal,
            "exterior-only witness produced a seal proposal");

    qmg::Layout overlapping;
    overlapping.rooms = {room(1, box(0, 0, 0, 100, 100, 100)), room(2, box(0, 0, 0, 100, 100, 100))};
    const qmg::LeakDiagnosisResult ambiguous =
        qmg::diagnose_layout_leak(overlapping, parse_witness("50 50 50\n150 50 50\n"));
    require(ambiguous.status == qmg::LeakDiagnosisStatus::ambiguous_crossing &&
                ambiguous.ambiguous_crossing_count == 1 && !ambiguous.proposal,
            "coincident owner faces were not rejected as ambiguous");

    const qmg::LeakDiagnosisResult corner =
        qmg::diagnose_layout_leak(layout, parse_witness("128 128 64\n300 300 64\n"));
    require(corner.status == qmg::LeakDiagnosisStatus::ambiguous_crossing && !corner.proposal,
            "edge/corner crossing was assigned an unsafe local face");

    qmg::Layout split_cell = layout;
    split_cell.rooms.push_back(room(2, box(128, 500, 0, 200, 600, 128)));
    const qmg::LeakDiagnosisResult on_unrelated_plane =
        qmg::diagnose_layout_leak(split_cell, parse_witness("128 128 64\n128 300 64\n"));
    require(on_unrelated_plane.status == qmg::LeakDiagnosisStatus::ambiguous_crossing && !on_unrelated_plane.proposal,
            "crossing without a unique adjacent exterior cell was not rejected as ambiguous");
}

void enclosed_void_is_not_exterior() {
    qmg::Layout shell;
    shell.rooms = {
        room(1, box(0, 0, 0, 10, 100, 100)), room(2, box(90, 0, 0, 100, 100, 100)),
        room(3, box(10, 0, 0, 90, 10, 100)), room(4, box(10, 90, 0, 90, 100, 100)),
        room(5, box(10, 10, 0, 90, 90, 10)), room(6, box(10, 10, 90, 90, 100, 100)),
    };
    const qmg::ExteriorGridResult built = qmg::build_layout_exterior_grid(shell);
    require(built.ok(), "closed-shell exterior grid failed");
    const auto cavity = qmg::locate_exterior_cell(*built.grid, fixed_point(50, 50, 50));
    require(cavity && cavity->state == qmg::ExteriorCellState::enclosed_void,
            "closed cavity was incorrectly exterior-flood reachable");
    const qmg::LeakDiagnosisResult diagnosis = qmg::diagnose_layout_leak(shell, parse_witness("5 50 50\n50 50 50\n"));
    require(diagnosis.status == qmg::LeakDiagnosisStatus::no_exposed_crossing && diagnosis.exposed_crossing_count == 0,
            "enclosed-void transition was misdiagnosed as an exterior leak");
}

void diagnosis_caps_and_fail_closed_layouts() {
    const qmg::LeakWitness witness = parse_witness("50 50 50\n150 50 50\n");
    qmg::Layout two_rooms;
    two_rooms.rooms = {room(1, box(0, 0, 0, 100, 100, 100)), room(2, box(200, 0, 0, 300, 100, 100))};

    qmg::LeakDiagnosisLimits volume_cap;
    volume_cap.max_interior_volumes = 1;
    require(qmg::diagnose_layout_leak(two_rooms, witness, volume_cap).status ==
                qmg::LeakDiagnosisStatus::capacity_exceeded,
            "interior volume cap did not fail closed");
    qmg::LeakDiagnosisLimits invalid_limits;
    invalid_limits.max_cells = 0;
    require(qmg::diagnose_layout_leak(two_rooms, witness, invalid_limits).status ==
                qmg::LeakDiagnosisStatus::invalid_limits,
            "invalid diagnosis limits did not fail with a typed status");
    qmg::LeakDiagnosisLimits plane_cap;
    plane_cap.max_axis_planes = 3;
    require(qmg::diagnose_layout_leak(single_room_layout(), witness, plane_cap).status ==
                qmg::LeakDiagnosisStatus::capacity_exceeded,
            "axis-plane cap did not fail closed");
    qmg::LeakDiagnosisLimits cell_cap;
    cell_cap.max_cells = 10;
    require(qmg::diagnose_layout_leak(single_room_layout(), witness, cell_cap).status ==
                qmg::LeakDiagnosisStatus::capacity_exceeded,
            "cell cap did not fail closed");
    qmg::Layout overlapping_fill;
    overlapping_fill.rooms = {room(1, box(0, 0, 0, 100, 100, 100)), room(2, box(0, 0, 0, 100, 100, 100))};
    qmg::LeakDiagnosisLimits fill_cap;
    fill_cap.max_fill_operations = 1;
    require(qmg::diagnose_layout_leak(overlapping_fill, witness, fill_cap).status ==
                qmg::LeakDiagnosisStatus::capacity_exceeded,
            "grid fill-operation cap did not fail closed");
    qmg::LeakDiagnosisLimits test_cap;
    test_cap.max_segment_volume_tests = 1;
    require(qmg::diagnose_layout_leak(two_rooms, witness, test_cap).status ==
                qmg::LeakDiagnosisStatus::capacity_exceeded,
            "segment-volume test cap did not fail closed");
    qmg::LeakDiagnosisLimits witness_cap;
    witness_cap.max_witness_points = 2;
    const qmg::LeakWitness three_points = parse_witness("50 50 50\n75 50 50\n150 50 50\n");
    require(qmg::diagnose_layout_leak(two_rooms, three_points, witness_cap).status ==
                qmg::LeakDiagnosisStatus::capacity_exceeded,
            "witness point cap did not fail closed");

    qmg::Layout duplicate_ids = two_rooms;
    duplicate_ids.rooms.back().id = duplicate_ids.rooms.front().id;
    require(qmg::diagnose_layout_leak(duplicate_ids, witness).status == qmg::LeakDiagnosisStatus::invalid_layout,
            "duplicate room ownership did not fail closed");
    qmg::Layout missing_passage = two_rooms;
    missing_passage.connections = {{qmg::ConnectionId{1}, qmg::RoomId{1}, qmg::RoomId{2}, 16, 64, {}}};
    require(qmg::diagnose_layout_leak(missing_passage, witness).status == qmg::LeakDiagnosisStatus::invalid_layout,
            "connection without a passage volume did not fail closed");
    qmg::LeakWitness corrupt = witness;
    corrupt.fingerprint ^= 1U;
    require(qmg::diagnose_layout_leak(two_rooms, corrupt).status == qmg::LeakDiagnosisStatus::invalid_witness,
            "corrupt normalized witness metadata was accepted");

    qmg::PointfileLimits wide_coordinates;
    wide_coordinates.max_absolute_coordinate = 3000000000.0;
    const qmg::LeakWitness overflow_witness = parse_witness("-2147483660 50 50\n-2147483600 50 50\n",
                                                            qmg::CompilerPointfileDialect::quake1, wide_coordinates);
    qmg::Layout edge;
    edge.rooms = {room(1, box(std::numeric_limits<qmg::GridCoord>::min(), 0, 0,
                              std::numeric_limits<qmg::GridCoord>::min() + 100, 100, 100))};
    require(qmg::diagnose_layout_leak(edge, overflow_witness).status == qmg::LeakDiagnosisStatus::coordinate_overflow,
            "local seal grid overflow did not fail closed");
}

} // namespace

int main() {
    try {
        real_shaped_pointfile_ingestion();
        deterministic_witness_normalization();
        file_ingestion();
        malformed_and_capped_pointfiles();
        exterior_grid_representation();
        room_boundary_diagnosis_and_proposal();
        passage_boundary_and_reorder_invariance();
        first_crossing_is_stable();
        no_crossing_and_ambiguity();
        enclosed_void_is_not_exterior();
        diagnosis_caps_and_fail_closed_layouts();
        std::cout << "leak diagnostics tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "leak diagnostics test failure: " << error.what() << '\n';
        return 1;
    }
}
