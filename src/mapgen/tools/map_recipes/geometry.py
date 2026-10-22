"""Exact stored-plane checks for bounded convex Q2 recipe solids.

This is an independently written geometry checker, not an engine hull trace.
Fractions preserve the BSP's stored floating-point coefficients exactly. It
retains the fast vertical-prism API and supports bounded general convex solids.
No model transform is inferred: moving submodels must be tested at each intended
position by the caller, separately from model zero's static world brushes.
"""

from dataclasses import dataclass
from fractions import Fraction
from itertools import combinations
from math import comb
from math import isfinite


MAX_PLANES = 128
MAX_CONVEX_PLANES = 32
MAX_VERTEX_CELLS = 8192
MAX_SAT_AXES = 1024


class GeometryError(ValueError):
    """Geometry lies outside this checker's explicit contract."""


def _index(value, length, label):
    if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value < length:
        raise GeometryError(f"{label} index out of bounds")


def model_brushes(bsp, model_index=0):
    """Return sorted unique brush indices in one model's BSP subtree.

    Uses models[*][9] as headnode, nodes[*][1:3] as children, and leaves[*]
    [11:13] as the leafbrush range, per IBSP38. Contents are intentionally not
    filtered. A caller inspecting solid collision usually keeps contents & 1.
    """
    _index(model_index, len(bsp.models), "model")
    root = bsp.models[model_index][9]
    states = {}
    leaves = set()
    brushes = set()
    pending = [(root, False)]
    while pending:
        current, finish = pending.pop()
        if not isinstance(current, int) or isinstance(current, bool):
            raise GeometryError("noninteger BSP tree reference")
        if current < 0:
            leaf_index = -current - 1
            _index(leaf_index, len(bsp.leaves), "leaf")
            if leaf_index in leaves:
                continue
            leaves.add(leaf_index)
            first, count = bsp.leaves[leaf_index][11:13]
            if (not isinstance(first, int) or not isinstance(count, int)
                    or first < 0 or count < 0 or first + count > len(bsp.leafbrushes)):
                raise GeometryError("leafbrush range out of bounds")
            for brush in bsp.leafbrushes[first:first + count]:
                _index(brush, len(bsp.brushes), "brush")
                brushes.add(brush)
            continue
        _index(current, len(bsp.nodes), "node")
        if finish:
            states[current] = 2
            continue
        if states.get(current) == 1:
            raise GeometryError("cyclic model BSP subtree")
        if states.get(current) == 2:
            continue
        states[current] = 1
        pending.append((current, True))
        first, second = bsp.nodes[current][1:3]
        pending.append((second, False))
        pending.append((first, False))
    return tuple(sorted(brushes))


def _rational(value):
    if isinstance(value, bool) or not isinstance(value, (int, float, Fraction)):
        raise GeometryError("expected a finite integer, float or Fraction")
    if isinstance(value, float) and not isfinite(value):
        raise GeometryError("nonfinite geometry coefficient")
    return Fraction(value)


def _cross(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def _convex_hull(points):
    points = sorted(set(points))
    lower = []
    for point in points:
        while len(lower) >= 2 and _cross(lower[-2], lower[-1], point) <= 0:
            lower.pop()
        lower.append(point)
    upper = []
    for point in reversed(points):
        while len(upper) >= 2 and _cross(upper[-2], upper[-1], point) <= 0:
            upper.pop()
        upper.append(point)
    return tuple(lower[:-1] + upper[:-1])


@dataclass(frozen=True)
class VerticalPrism:
    """CCW exact XY polygon, two Z caps, and precomputed separation axes.

    Coordinates are Fraction objects, not JSON numbers. Convert them explicitly
    to float only for presentation; keep the original object for collision checks.
    """
    polygon: tuple
    z_min: Fraction
    z_max: Fraction
    _axes: tuple

    @property
    def bounds(self):
        return ((min(p[0] for p in self.polygon), min(p[1] for p in self.polygon), self.z_min),
                (max(p[0] for p in self.polygon), max(p[1] for p in self.polygon), self.z_max))


def vertical_prism(planes):
    """Build a bounded convex prism from n.x*x+n.y*y+n.z*z <= distance.

    A row may be a four-tuple or a five-tuple from Bsp.planes (type is ignored).
    No epsilon snaps nearly vertical planes into the supported geometry class.
    Redundant planes are allowed; empty, unbounded and zero-volume regions reject.
    """
    sides = []
    lower = None
    upper = None
    count = 0
    for row in planes:
        count += 1
        if count > MAX_PLANES:
            raise GeometryError("prism plane count exceeds cap")
        if len(row) not in (4, 5):
            raise GeometryError("expected a four- or five-component plane")
        a, b, z, distance = (_rational(v) for v in row[:4])
        if a == b == z == 0:
            raise GeometryError("zero plane normal")
        if z:
            if a or b:
                raise GeometryError("sloping face is not a vertical prism")
            height = distance / z
            if z > 0:
                upper = height if upper is None else min(upper, height)
            else:
                lower = height if lower is None else max(lower, height)
        else:
            sides.append((a, b, distance))
    if lower is None or upper is None or lower >= upper:
        raise GeometryError("prism needs two finite positive-height Z caps")
    sides = tuple(sorted(set(sides)))
    if len(sides) < 3:
        raise GeometryError("prism XY region is unbounded or degenerate")
    # A nonzero direction satisfying every homogeneous side inequality is an
    # unbounded recession ray. In 2D an extreme ray follows a side boundary.
    for a, b, _ in sides:
        for dx, dy in ((-b, a), (b, -a)):
            if all(x * dx + y * dy <= 0 for x, y, _ in sides):
                raise GeometryError("prism XY region is unbounded")
    points = []
    for i, (a, b, distance) in enumerate(sides):
        for x, y, other_distance in sides[i + 1:]:
            determinant = a * y - b * x
            if not determinant:
                continue
            point = ((distance * y - b * other_distance) / determinant,
                     (a * other_distance - distance * x) / determinant)
            if all(nx * point[0] + ny * point[1] <= d for nx, ny, d in sides):
                points.append(point)
    polygon = _convex_hull(points)
    if len(polygon) < 3:
        raise GeometryError("prism XY region is empty or has zero area")
    axes = []
    for first, second in zip(polygon, polygon[1:] + polygon[:1]):
        a, b = second[1] - first[1], first[0] - second[0]
        projections = tuple(a * x + b * y for x, y in polygon)
        axes.append((a, b, min(projections), max(projections)))
    return VerticalPrism(polygon, lower, upper, tuple(axes))


def intersects_hull(prism, low, high):
    """Whether an axis-aligned standing hull overlaps the prism's interior.

    Uses Z interval overlap and the complete 2D separating-axis test for a
    convex polygon against a rectangle. Face/edge/corner contact is allowed.
    This detects stationary intersection only, not a swept movement collision.
    """
    if len(low) != 3 or len(high) != 3:
        raise GeometryError("hull requires three-dimensional bounds")
    low = tuple(_rational(v) for v in low)
    high = tuple(_rational(v) for v in high)
    if any(a >= b for a, b in zip(low, high)):
        raise GeometryError("hull must have positive extent on every axis")
    bounds_low, bounds_high = prism.bounds
    if any(high[i] <= bounds_low[i] or low[i] >= bounds_high[i] for i in range(3)):
        return False
    for a, b, polygon_low, polygon_high in prism._axes:
        rectangle_low = a * (low[0] if a >= 0 else high[0]) + b * (low[1] if b >= 0 else high[1])
        rectangle_high = a * (high[0] if a >= 0 else low[0]) + b * (high[1] if b >= 0 else low[1])
        if rectangle_high <= polygon_low or rectangle_low >= polygon_high:
            return False
    return True


def _dot3(a, b):
    return sum(x * y for x, y in zip(a, b))


def _cross3(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def _axis(vector):
    """Canonical unoriented nonzero axis, with its first nonzero entry one."""
    scale = next((v for v in vector if v), None)
    return tuple(v / scale for v in vector) if scale is not None else None


def _budget(value, maximum, label):
    if isinstance(value, bool) or not isinstance(value, int) or not 1 <= value <= maximum:
        raise GeometryError(f"{label} budget must be an integer in 1..{maximum}")
    return value


def _planes(rows, max_planes):
    result = set()
    for count, row in enumerate(rows, 1):
        if count > max_planes:
            raise GeometryError("convex plane count exceeds cap")
        if len(row) not in (4, 5):
            raise GeometryError("expected a four- or five-component plane")
        plane = tuple(_rational(v) for v in row[:4])
        scale = next((abs(v) for v in plane[:3] if v), None)
        if scale is None:
            raise GeometryError("zero plane normal")
        result.add(tuple(v / scale for v in plane))
    return tuple(sorted(result))


def _vertices(planes, max_cells):
    # Each cell is one three-plane intersection candidate. Charge degenerate
    # triples too, before work begins, so a pathological input fails predictably.
    if comb(len(planes), 3) > max_cells:
        raise GeometryError("convex vertex cell budget exceeded")
    vertices = set()
    for first, second, third in combinations(planes, 3):
        cross23 = _cross3(second, third)
        determinant = _dot3(first[:3], cross23)
        if not determinant:
            continue
        cross31, cross12 = _cross3(third, first), _cross3(first, second)
        point = tuple((first[3] * cross23[i] + second[3] * cross31[i]
                       + third[3] * cross12[i]) / determinant for i in range(3))
        if all(_dot3(p[:3], point) <= p[3] for p in planes):
            vertices.add(point)
    return tuple(sorted(vertices))


def _bounded(planes):
    normals = [p[:3] for p in planes]
    # Rank < 3 leaves a nonzero lineality direction. For rank 3, any nonzero
    # recession cone is pointed and has an extreme ray along two boundaries.
    if not any(_dot3(a, _cross3(b, c)) for a, b, c in combinations(normals, 3)):
        return False
    for first, second in combinations(normals, 2):
        direction = _cross3(first, second)
        if not any(direction):
            continue
        projections = tuple(_dot3(n, direction) for n in normals)
        if all(v <= 0 for v in projections) or all(v >= 0 for v in projections):
            return False
    return True


def _positive_volume(vertices):
    if len(vertices) < 4:
        return False
    base = vertices[0]
    vectors = [tuple(p[i] - base[i] for i in range(3)) for p in vertices[1:]]
    first = next((v for v in vectors if any(v)), None)
    second = next((v for v in vectors if any(_cross3(first, v))), None)
    return second is not None and any(_dot3(v, _cross3(first, second)) for v in vectors)


@dataclass(frozen=True)
class ConvexPolyhedron:
    """Bounded full-dimensional solid; exact vertices and complete box SAT axes."""
    planes: tuple
    vertices: tuple
    _axes: tuple
    _bounds: tuple

    @property
    def bounds(self):
        return self._bounds


def convex_polyhedron(planes, *, max_planes=MAX_CONVEX_PLANES, max_cells=MAX_VERTEX_CELLS):
    """Build a bounded positive-volume solid from stored n.dot(x) <= distance.

    Handles sloped/rotated faces and BSP five-component rows without epsilon
    snapping. Input rows (including duplicates) are capped at 32; at most 8192
    three-plane candidates and 1024 SAT axes are allowed. Callers may lower the
    budgets but cannot disable them. Empty, flat and unbounded solids fail closed.
    """
    max_planes = _budget(max_planes, MAX_CONVEX_PLANES, "plane")
    max_cells = _budget(max_cells, MAX_VERTEX_CELLS, "cell")
    planes = _planes(planes, max_planes)
    if comb(len(planes), 3) > max_cells:
        raise GeometryError("convex vertex cell budget exceeded")
    if not _bounded(planes):
        raise GeometryError("convex region is unbounded or degenerate")
    vertices = _vertices(planes, max_cells)
    if not _positive_volume(vertices):
        raise GeometryError("convex region is empty or has zero volume")
    box_axes = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
    axes = {_axis(p[:3]) for p in planes} | set(box_axes)
    # Face normals plus box axes alone are not a complete 3D SAT. Every actual
    # solid edge direction must also be crossed with every box edge direction.
    on_plane = [{i for i, vertex in enumerate(vertices) if _dot3(p[:3], vertex) == p[3]}
                for p in planes]
    for i, j in combinations(range(len(planes)), 2):
        if len(on_plane[i] & on_plane[j]) < 2:
            continue
        edge = _cross3(planes[i], planes[j])
        for box_axis in box_axes:
            candidate = _axis(_cross3(edge, box_axis))
            if candidate is not None:
                axes.add(candidate)
        if len(axes) > MAX_SAT_AXES:
            raise GeometryError("convex SAT axis count exceeds cap")
    projected = []
    for axis in sorted(axes):
        values = tuple(_dot3(axis, vertex) for vertex in vertices)
        projected.append((*axis, min(values), max(values)))
    bounds = (tuple(min(p[i] for p in vertices) for i in range(3)),
              tuple(max(p[i] for p in vertices) for i in range(3)))
    return ConvexPolyhedron(planes, vertices, tuple(projected), bounds)


def _box(low, high):
    if len(low) != 3 or len(high) != 3:
        raise GeometryError("hull requires three-dimensional bounds")
    low, high = tuple(map(_rational, low)), tuple(map(_rational, high))
    if any(a >= b for a, b in zip(low, high)):
        raise GeometryError("hull must have positive extent on every axis")
    return low, high


def _box_projection(axis, low, high):
    return (sum(a * (low[i] if a >= 0 else high[i]) for i, a in enumerate(axis)),
            sum(a * (high[i] if a >= 0 else low[i]) for i, a in enumerate(axis)))


def _require_polyhedron(value):
    if not isinstance(value, ConvexPolyhedron):
        raise GeometryError("operation requires a general ConvexPolyhedron")


def intersects_aabb(polyhedron, low, high):
    """Exact positive-volume solid/box intersection; touching is allowed."""
    if isinstance(polyhedron, VerticalPrism):
        return intersects_hull(polyhedron, low, high)
    _require_polyhedron(polyhedron)
    low, high = _box(low, high)
    if any(high[i] <= polyhedron.bounds[0][i] or low[i] >= polyhedron.bounds[1][i]
           for i in range(3)):
        return False
    for *axis, solid_low, solid_high in polyhedron._axes:
        box_low, box_high = _box_projection(axis, low, high)
        if box_high <= solid_low or box_low >= solid_high:
            return False
    return True


def translated(polyhedron, delta):
    """Translate an exact solid without inferring any entity/model transform."""
    _require_polyhedron(polyhedron)
    if len(delta) != 3:
        raise GeometryError("translation requires three components")
    delta = tuple(map(_rational, delta))
    return convex_polyhedron([(*p[:3], p[3] + _dot3(p[:3], delta)) for p in polyhedron.planes])


def intersects_swept_aabb(polyhedron, low, high, delta):
    """Whether a box translated linearly by delta ever overlaps the interior.

    Intersects exact open time intervals for the complete SAT axis set over
    t in [0,1]. It does not substitute the path's larger enclosing AABB and
    cannot miss an obstacle between endpoint poses. No rotation, acceleration,
    step-up, engine epsilon, player steering or runtime behavior is inferred.
    Requires ConvexPolyhedron; the legacy prism's two-dimensional axes are not
    silently reused for a three-dimensional movement check.
    """
    _require_polyhedron(polyhedron)
    low, high = _box(low, high)
    if len(delta) != 3:
        raise GeometryError("translation requires three components")
    delta = tuple(map(_rational, delta))
    start, finish = Fraction(0), Fraction(1)
    for *axis, solid_low, solid_high in polyhedron._axes:
        box_low, box_high = _box_projection(axis, low, high)
        speed = _dot3(axis, delta)
        if not speed:
            if box_high <= solid_low or box_low >= solid_high:
                return False
            continue
        first, last = (solid_low - box_high) / speed, (solid_high - box_low) / speed
        if first > last:
            first, last = last, first
        start, finish = max(start, first), min(finish, last)
        if start >= finish:
            return False
    return True


def support_height(polyhedron, xy_low, xy_high, *, max_cells=MAX_VERTEX_CELLS):
    """Maximum solid height underneath a rectangular standing-hull footprint.

    Clips the real convex solid to the XY rectangle and maximizes stored-plane
    Z exactly. Returns None when the overlap has no positive XY area. This is
    geometric support, not an engine floor trace; partial-footprint support is
    allowed and support from a different solid is not inferred. Added clipping
    planes count against the 8192 three-plane-candidate budget.
    """
    _require_polyhedron(polyhedron)
    max_cells = _budget(max_cells, MAX_VERTEX_CELLS, "cell")
    if len(xy_low) != 2 or len(xy_high) != 2:
        raise GeometryError("support footprint requires two-dimensional bounds")
    low, high = tuple(map(_rational, xy_low)), tuple(map(_rational, xy_high))
    if any(a >= b for a, b in zip(low, high)):
        raise GeometryError("support footprint must have positive area")
    if any(high[i] <= polyhedron.bounds[0][i] or low[i] >= polyhedron.bounds[1][i] for i in range(2)):
        return None
    clips = ((1, 0, 0, high[0]), (-1, 0, 0, -low[0]),
             (0, 1, 0, high[1]), (0, -1, 0, -low[1]))
    points = _vertices(tuple(sorted(set(polyhedron.planes + clips))), max_cells)
    if len(_convex_hull(tuple(p[:2] for p in points))) < 3:
        return None
    return max(p[2] for p in points)


def supported_hull(polyhedron, center_xy, *, half_width=16, height=56,
                   max_cells=MAX_VERTEX_CELLS):
    """Return the standing AABB with its foot at full-footprint support height.

    Use exact swept checks against all world solids, including the supporting
    solid, between returned poses. An interpolated segment crossing a concave
    support-profile corner can intersect the ramp despite valid endpoint poses;
    split at that corner or reject it, rather than treating samples as traversal.
    """
    if len(center_xy) != 2:
        raise GeometryError("standing center requires two XY components")
    center = tuple(map(_rational, center_xy))
    half_width, height = _rational(half_width), _rational(height)
    if half_width <= 0 or height <= 0:
        raise GeometryError("standing hull dimensions must be positive")
    low = tuple(v - half_width for v in center)
    high = tuple(v + half_width for v in center)
    foot = support_height(polyhedron, low, high, max_cells=max_cells)
    if foot is None:
        raise GeometryError("standing hull has no positive-area support")
    return (*low, foot), (*high, foot + height)
