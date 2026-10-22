"""Bounded compiled-plane ramp clearance audit; no engine movement simulation.

An ordinary RampSpec wedge has axial sides, a horizontal bottom and one sloped
top. This audit matches that compiled surface to authored metadata, then checks
continuous translated standing boxes against every model-zero player blocker.
Float error bounds are used only to identify the compiled surface. Collision and
support use its exact stored coefficients, without tolerances or height snaps.
"""

from fractions import Fraction
from math import frexp, isfinite
import struct

from tools.map_recipes.geometry import (
    GeometryError, convex_polyhedron, intersects_aabb, intersects_swept_aabb,
    model_brushes, supported_hull,
)


# Original Q2 shared.h: SOLID | WINDOW | PLAYERCLIP | MONSTER. Static BSP brush
# checks cannot include dynamic entity collisions even though the mask is complete.
MASK_PLAYERSOLID = 1 | 2 | (1 << 16) | (1 << 25)
MAX_WORLD_BRUSHES = 4096
MAX_RAMPS = 16
MAX_ROUTE_SEGMENTS = 128
MAX_DIMENSION = 8192
MAX_COORDINATE = 65536
HALF_WIDTH = Fraction(16)
HULL_HEIGHT = Fraction(56)
MAX_STEP = Fraction(18)
MATCH_ULPS = 4


def _require(condition, message):
    if not condition:
        raise GeometryError(message)


def _number(value):
    _require(not isinstance(value, bool) and isinstance(value, (int, float, Fraction)),
             "ramp metadata requires numeric coordinates")
    _require(not isinstance(value, float) or isfinite(value), "nonfinite ramp metadata")
    value = Fraction(value)
    _require(abs(value) <= MAX_COORDINATE, "ramp coordinate exceeds cap")
    return value


def _vector(value, dimensions=3):
    _require(isinstance(value, (list, tuple)) and len(value) == dimensions,
             "ramp metadata has malformed coordinates")
    return tuple(_number(v) for v in value)


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _ulp32(value):
    """A float32 coefficient's binade spacing, including subnormal zero."""
    value = float(value)
    _require(isfinite(value), "nonfinite compiled plane")
    try:
        rounded = struct.unpack('<f', struct.pack('<f', value))[0]
    except OverflowError as error:
        raise GeometryError("compiled coefficient exceeds float32") from error
    _require(rounded == value, "compiled plane is not stored float32 data")
    exponent = max(-149, frexp(abs(value))[1] - 24) if value else -149
    return Fraction(2 ** exponent) if exponent >= 0 else Fraction(1, 2 ** -exponent)


def _surface_error_bound(raw_plane, point):
    # Four float32 ULPs per coefficient allow normalization/distance arithmetic
    # as well as final storage. This bound never enlarges collision geometry.
    return MATCH_ULPS * (sum(abs(point[i]) * _ulp32(raw_plane[i]) for i in range(3))
                         + _ulp32(raw_plane[3]))


def _record_number(value):
    value = Fraction(value)
    return int(value) if value.denominator == 1 else float(value)


def _record_vector(value):
    return [_record_number(v) for v in value]


def _record_pose(box):
    low, high = box
    origin = ((low[0] + high[0]) / 2, (low[1] + high[1]) / 2, low[2] + 24)
    return {"standing_origin": _record_vector(origin), "foot_z": _record_number(low[2]),
            "foot_z_exact": str(low[2]), "hull": [_record_vector(low), _record_vector(high)]}


def _world(bsp):
    brush_indices = model_brushes(bsp)
    _require(len(brush_indices) <= MAX_WORLD_BRUSHES, "world brush count exceeds ramp audit cap")
    result = []
    for index in brush_indices:
        first, count, contents = bsp.brushes[index]
        if not contents & MASK_PLAYERSOLID:
            continue
        _require(isinstance(first, int) and isinstance(count, int) and first >= 0
                 and count >= 0 and first + count <= len(bsp.brushsides),
                 "compiled ramp brushside range out of bounds")
        raw = []
        for plane_index, _ in bsp.brushsides[first:first + count]:
            _require(isinstance(plane_index, int) and 0 <= plane_index < len(bsp.planes),
                     "compiled ramp plane index out of bounds")
            raw.append(tuple(bsp.planes[plane_index][:4]))
        result.append({"index": index, "contents": contents, "raw": raw,
                       "shape": convex_polyhedron(raw)})
    return result


def _frame(metadata):
    required = {'id', 'solid_id', 'from', 'to', 'bounds', 'surface', 'width', 'run', 'rise', 'base_floor'}
    _require(isinstance(metadata, dict) and required <= metadata.keys(), "ramp metadata is incomplete")
    _require(all(isinstance(metadata[k], int) and not isinstance(metadata[k], bool) and metadata[k] > 0
                 for k in ('id', 'solid_id')), "ramp identifiers must be positive integers")
    _require(isinstance(metadata['bounds'], (list, tuple)) and len(metadata['bounds']) == 2,
             "ramp metadata requires two bounds")
    _require(isinstance(metadata['surface'], (list, tuple)) and len(metadata['surface']) == 4,
             "ramp requires four authored top vertices")
    source, target = _vector(metadata['from']), _vector(metadata['to'])
    low, high = tuple(_vector(v) for v in metadata['bounds'])
    surface = tuple(_vector(p) for p in metadata['surface'])
    _require(len(surface) == 4, "ramp requires four authored top vertices")
    width, run, rise, base = (_number(metadata[k]) for k in ('width', 'run', 'rise', 'base_floor'))
    _require(64 <= width <= MAX_DIMENSION and 32 < run <= MAX_DIMENSION,
             "ramp width or run outside standing-lane contract")
    _require(0 < rise <= run / 2, "ramp slope outside shallow-ramp audit contract")
    delta = tuple(target[i] - source[i] for i in range(2))
    _require(sum(v != 0 for v in delta) == 1, "ramp ascent must follow one horizontal axis")
    axis = 0 if delta[0] else 1
    sign = 1 if delta[axis] > 0 else -1
    _require(abs(delta[axis]) == run and target[2] - source[2] == rise,
             "ramp endpoint metadata disagrees with run or rise")
    _require(0 < source[2] - base < MAX_STEP, "ramp low-end lip outside bounded-step contract")
    _require(low[2] == base and high[2] == target[2], "ramp height bounds disagree with endpoints")
    _require(high[axis] - low[axis] == run and high[1 - axis] - low[1 - axis] == width,
             "ramp bounds disagree with width or run")
    _require(source[axis] == (low[axis] if sign > 0 else high[axis])
             and target[axis] == (high[axis] if sign > 0 else low[axis])
             and source[1 - axis] == target[1 - axis] == (low[1 - axis] + high[1 - axis]) / 2,
             "ramp centers disagree with bounds")
    expected = set()
    for end in (source, target):
        for side in (low[1 - axis], high[1 - axis]):
            point = list(end)
            point[1 - axis] = side
            expected.add(tuple(point))
    _require(set(surface) == expected, "ramp authored top vertices disagree with its frame")
    return {"source": source, "target": target, "bounds": (low, high), "surface": surface,
            "axis": axis, "sign": sign, "run": run, "width": width, "base": base}


def _match(world, frame):
    low, high = frame['bounds']
    matches = []
    for brush in world:
        shape = brush['shape']
        if (shape.bounds[0][:2], shape.bounds[1][:2], shape.bounds[0][2]) != (low[:2], high[:2], low[2]):
            continue
        for raw in dict.fromkeys(brush['raw']):
            plane = tuple(Fraction(v) for v in raw)
            if plane[2] <= 0 or not any(plane[:2]):
                continue
            if plane[1 - frame['axis']] or plane[frame['axis']] * frame['sign'] >= 0:
                continue
            face = [v for v in shape.vertices if _dot(plane[:3], v) == plane[3]]
            if len(face) < 3:
                continue
            scale = next(abs(v) for v in plane[:3] if v)
            normalized = tuple(v / scale for v in plane)
            # QBSP may add axial bevel caps. A slightly high stored sloped plane
            # can intersect the high Z cap just before the authored high edge,
            # creating a real, tiny flat crest. Preserve it and its support knot.
            if any(sum(v != 0 for v in p[:3]) > 1 and p != normalized for p in shape.planes):
                continue
            residuals = [abs(_dot(plane[:3], p) - plane[3]) for p in frame['surface']]
            tolerances = [_surface_error_bound(raw, p) for p in frame['surface']]
            if (all(error <= tolerance for error, tolerance in zip(residuals, tolerances))
                    and abs(shape.bounds[1][2] - high[2]) <= max(tolerances) / plane[2]):
                matches.append((brush, plane, max(residuals), max(tolerances)))
    _require(len(matches) == 1, f"expected one matching compiled sloped ramp, found {len(matches)}")
    return matches[0]


def _xy(frame, longitudinal, transverse):
    point = list(frame['source'][:2])
    point[frame['axis']] += frame['sign'] * longitudinal
    point[1 - frame['axis']] += transverse
    return tuple(point)


def _box_at(xy, foot):
    return ((xy[0] - HALF_WIDTH, xy[1] - HALF_WIDTH, foot),
            (xy[0] + HALF_WIDTH, xy[1] + HALF_WIDTH, foot + HULL_HEIGHT))


def _flat_support(world, low_xy, high_xy, foot, label):
    corners = [(x, y, foot) for x in (low_xy[0], high_xy[0]) for y in (low_xy[1], high_xy[1])]
    supporters = []
    for brush in world:
        shape = brush['shape']
        if not any(not p[0] and not p[1] and p[2] > 0 and p[3] / p[2] == foot
                   for p in shape.planes):
            continue
        if all(all(_dot(p[:3], corner) <= p[3] for p in shape.planes) for corner in corners):
            supporters.append(brush['index'])
    _require(supporters, f"{label} has no compiled flat support for the complete hull footprint")
    return supporters


def _flat_station(world, xy, foot, label):
    box = _box_at(xy, foot)
    supporters = _flat_support(world, box[0][:2], box[1][:2], foot, label)
    _require(not any(intersects_aabb(b['shape'], *box) for b in world), f"{label} standing hull blocked")
    return box, supporters


def _sweep_clear(world, start, finish, label, counter):
    delta = tuple(finish[0][i] - start[0][i] for i in range(3))
    _require(all(finish[1][i] - start[1][i] == delta[i] for i in range(3)),
             "ramp route changes standing hull dimensions")
    envelope_low = tuple(min(start[0][i], finish[0][i]) for i in range(3))
    envelope_high = tuple(max(start[1][i], finish[1][i]) for i in range(3))
    for brush in world:
        counter['world_pair_checks'] += 1
        bounds = brush['shape'].bounds
        if any(envelope_high[i] <= bounds[0][i] or envelope_low[i] >= bounds[1][i] for i in range(3)):
            continue
        counter['exact_swept_checks'] += 1
        _require(not intersects_swept_aabb(brush['shape'], *start, delta),
                 f"{label} continuous hull sweep overlaps player-blocking brush {brush['index']}")


def audit_ramps(bsp, layout):
    """Return JSON evidence for fully swept three-lane shallow-ramp routes.

    Raises GeometryError on unmatched/unsupported geometry, blocked sweeps,
    missing full-footprint flat approach/landing support, or exceeded budgets.
    The audit is geometric. It does not claim an engine pmove, drop, step,
    rendering, combat or balance result. No point samples substitute for sweeps.
    """
    ramps = layout.get('ramps', [])
    _require(isinstance(ramps, list) and len(ramps) <= MAX_RAMPS, "ramp count exceeds cap")
    world = _world(bsp)
    counter = {"world_pair_checks": 0, "exact_swept_checks": 0}
    records, used_brushes = [], set()
    for metadata in ramps:
        frame = _frame(metadata)
        brush, top_plane, residual, match_bound = _match(world, frame)
        _require(brush['index'] not in used_brushes, "two ramp records match the same compiled brush")
        used_brushes.add(brush['index'])
        ramp = brush['shape']
        lanes, route_segments, endpoint_boxes = [], 0, []
        for transverse in (-frame['width'] / 2 + 24, Fraction(0), frame['width'] / 2 - 24):
            lower, lower_support = _flat_station(world, _xy(frame, -48, transverse), frame['base'], 'lower approach')
            upper, upper_support = _flat_station(world, _xy(frame, frame['run'] + 48, transverse),
                                                frame['target'][2], 'upper landing')
            endpoint_boxes.append((lower, upper))
            before, _ = _flat_station(world, _xy(frame, -16, transverse), frame['base'], 'lower lip approach')
            entry = supported_hull(ramp, _xy(frame, -15, transverse))
            step = entry[0][2] - frame['base']
            _require(0 < step <= MAX_STEP, "actual compiled ramp entry exceeds 18-unit step bound")
            raised = _box_at(_xy(frame, -16, transverse), entry[0][2])
            # For this axis-aligned wedge with optional axial bevels, top-profile
            # slope changes occur at actual polyhedron vertex coordinates. The
            # uphill edge of a 32-wide hull reaches each knot 16 units earlier.
            # Derive these coordinates exactly; nominal run-16 alone is unsafe
            # when a stored slope meets QBSP's top bevel slightly before the end.
            support_knots = sorted({frame['run'] - 16} | {
                (vertex[frame['axis']] - frame['source'][frame['axis']]) * frame['sign'] - 16
                for vertex in ramp.vertices
                if -15 < (vertex[frame['axis']] - frame['source'][frame['axis']]) * frame['sign'] - 16
                < frame['run'] - 16})
            incline_poses = [(f"incline support knot {i + 1}", supported_hull(ramp, _xy(frame, knot, transverse)))
                             for i, knot in enumerate(support_knots)]
            incline_end = incline_poses[-1][1]
            crest_height = max(incline_end[0][2], frame['target'][2])
            _require(abs(incline_end[0][2] - frame['target'][2]) <= MAX_STEP,
                     "compiled ramp-to-deck transition exceeds step bound")
            crest = _box_at(_xy(frame, frame['run'] - 16, transverse), crest_height)
            above_deck = _box_at(_xy(frame, frame['run'] + 16, transverse), crest_height)
            on_deck = _box_at(_xy(frame, frame['run'] + 16, transverse), frame['target'][2])
            poses = [("lower approach", lower), ("before low lip", before),
                     ("raised entry", raised), ("partial footprint entry", entry)] + incline_poses + [
                     ("upper crossing height", crest),
                     ("ramp trailing edge support knot", above_deck), ("flat deck", on_deck),
                     ("upper landing", upper)]
            segments = []
            for (name_a, a), (name_b, b) in zip(poses, poses[1:]):
                if a == b:
                    continue
                segments.append((name_a + ' to ' + name_b, a, b))
            route_segments += len(segments) * 2
            _require(route_segments <= MAX_ROUTE_SEGMENTS, "ramp segment budget exceeded")
            for direction in ('ascent', 'descent'):
                ordered = segments if direction == 'ascent' else [(label, b, a) for label, a, b in reversed(segments)]
                for label, a, b in ordered:
                    _sweep_clear(world, a, b, direction + ': ' + label, counter)
            lanes.append({"transverse_offset": _record_number(transverse),
                          "entry_step_height": _record_number(step), "entry_step_height_exact": str(step),
                          "compiled_upper_support_exact": str(incline_end[0][2]),
                          "incline_support_knots_exact": [str(knot) for knot in support_knots],
                          "deck_rounding_transition_exact": str(frame['target'][2] - incline_end[0][2]),
                          "lower_support_brushes": lower_support, "upper_support_brushes": upper_support,
                          "flat_endpoints_full_footprint_support": True,
                          "flat_endpoint_standing_hulls_clear": True,
                          "ascent_segments": len(segments), "descent_segments": len(segments),
                          "continuous_sweeps_clear": True,
                          "poses": [{"phase": label, **_record_pose(box)} for label, box in poses]})
        turns = []
        for station, label in ((0, 'lower landing lateral turn'), (1, 'upper landing lateral turn')):
            start, finish = endpoint_boxes[0][station], endpoint_boxes[-1][station]
            low_xy = tuple(min(start[0][i], finish[0][i]) for i in range(2))
            high_xy = tuple(max(start[1][i], finish[1][i]) for i in range(2))
            supporters = _flat_support(world, low_xy, high_xy, start[0][2], label)
            _sweep_clear(world, start, finish, label, counter)
            _sweep_clear(world, finish, start, label + ' reverse', counter)
            route_segments += 2
            _require(route_segments <= MAX_ROUTE_SEGMENTS, "ramp segment budget exceeded")
            turns.append({"phase": label, "from": _record_pose(start), "to": _record_pose(finish),
                          "full_swept_footprint_support_brushes": supporters,
                          "both_directions_clear": True})
        records.append({"id": metadata['id'], "solid_id": metadata['solid_id'],
                        "compiled_brush": brush['index'], "contents": brush['contents'],
                        "actual_top_plane_exact": [str(v) for v in top_plane],
                        "metadata_surface_max_residual": float(residual),
                        "metadata_surface_error_bound": float(match_bound),
                        "bounds": [_record_vector(v) for v in ramp.bounds],
                        "source_floor_band": _record_number(frame['target'][2] - frame['base']),
                        "lanes": lanes, "landing_turns": turns,
                        "total_swept_route_segments": route_segments})
    return {"kind": "exact_compiled_ramp_hull_audit", "ok": True, "ramps": records,
            "ramps_checked": len(records), "lateral_lanes_per_ramp": 3,
            "world_player_blocking_brushes": len(world), "player_collision_mask": MASK_PLAYERSOLID,
            "standing_hull_dimensions": [32, 32, 56], "step_bound": 18,
            "endpoint_extension": 48, "supporting_ramp_included_in_sweeps": True,
            "collision_epsilon": 0, "metadata_matching_float32_ulps": MATCH_ULPS,
            **counter, "runtime_verified": False,
            "support_knots": "The leading hull edge reaches each actual compiled top-profile vertex 16 units before its center does. Preserve extra knots where stored slopes meet axial bevel caps. The nominal crest is run-16; the trailing edge leaves the ramp at run+16. Interpolating across these support changes can cut the solid.",
            "limits": ["Three explicit lateral lanes, not every possible lateral trajectory or an engine pmove test.",
                       "Entry step and exit settling are checked geometric translations, not measured native step/drop behavior.",
                       "All model-zero player-mask brushes are checked; dynamic entities and moving submodels are not simulated.",
                       "Stored-float matching bounds identify source correspondence only. Exact rational support and SAT retain every positive-volume collision.",
                       "No fake stair construction or sample-only traversal evidence; each reported route segment is swept in both directions."]}
