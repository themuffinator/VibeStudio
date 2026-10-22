"""Independent readers for the public IBSP38 and EAAS4/5 file envelopes.

No engine/tool source is copied or linked. See THIRD_PARTY.md for format references.
These readers never extract archives, decompile brushes, or execute map properties.
"""

from __future__ import annotations

import hashlib
import math
import re
import struct
from pathlib import Path

MAX_FILE_BYTES = 128 * 1024 * 1024
MAX_RECORDS = 2_000_000
MAX_GEOMETRY_REFERENCES = 2_000_000


class FormatError(ValueError):
    pass


def read_bytes(path: Path, limit: int = MAX_FILE_BYTES) -> bytes:
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise FormatError(f"file exceeds {limit} bytes: {path.name}")
    return data


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def decode(data: bytes) -> str:
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return data.decode("cp1252", errors="replace")


def entities(data: bytes, issues=None) -> list[dict[str, str]]:
    """Strict Quake entity syntax, including comments; never regex-salvage entities."""
    if len(data) > 4 * 1024 * 1024:
        raise FormatError("entity text exceeds cap")
    text = decode(data.rstrip(b"\0"))
    token = re.compile(r'\s+|//[^\n]*|/\*[\s\S]*?\*/|[{}]|"(?:\\.|[^"\\])*"')
    tokens = []
    position = 0
    while position < len(text):
        match = token.match(text, position)
        if not match:
            raise FormatError(f"malformed entity text at character {position}")
        value = match.group()
        position = match.end()
        if not value.isspace() and not value.startswith(("//", "/*")):
            tokens.append(value)
    result = []
    index = 0
    while index < len(tokens):
        if tokens[index] != "{":
            raise FormatError("expected entity opening brace")
        index += 1
        entity = {}
        while index < len(tokens) and tokens[index] != "}":
            if index + 1 >= len(tokens) or any(not t.startswith('"') for t in tokens[index:index + 2]):
                raise FormatError("expected quoted entity key/value")
            key, value = (t[1:-1] for t in tokens[index:index + 2])
            if key in entity:
                if issues is not None:
                    issues.append(f"entity {len(result)} repeats {key}; native last-value semantics used")
            entity[key] = value
            index += 2
        if index == len(tokens):
            raise FormatError("unclosed entity")
        result.append(entity)
        index += 1
        if len(result) > 32768:
            raise FormatError("entity count exceeds cap")
    if not result or result[0].get("classname") != "worldspawn":
        raise FormatError("first entity must be worldspawn")
    return result


def vec(text: str) -> tuple[float, float, float]:
    try:
        values = tuple(float(v) for v in text.split())
    except ValueError as error:
        raise FormatError("invalid entity vector") from error
    if len(values) != 3 or not all(math.isfinite(v) and abs(v) <= 1_000_000 for v in values):
        raise FormatError("invalid or out-of-bounds entity vector")
    return values


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def add_z(point, height):
    return (point[0], point[1], point[2] + height)


def check_index(index, count, label):
    if not 0 <= index < count:
        raise FormatError(f"{label} index out of range")


def check_range(first, count, size, label):
    if first < 0 or count < 0 or first + count > size:
        raise FormatError(f"{label} range out of bounds")


class Lumps:
    def __init__(self, data, header, table_start, count, header_size):
        self.data = data
        self.lumps = []
        occupied = []
        for i in range(count):
            offset, size = struct.unpack_from("<ii", header, table_start + i * 8)
            if offset < 0 or size < 0 or offset + size > len(data) or (size and offset < header_size):
                raise FormatError(f"lump {i} outside file")
            self.lumps.append(data[offset:offset + size])
            if size:
                occupied.append((offset, offset + size))
        occupied.sort()
        if any(a[1] > b[0] for a, b in zip(occupied, occupied[1:])):
            raise FormatError("overlapping lumps")

    def records(self, index, fmt):
        data = self.lumps[index]
        size = struct.calcsize(fmt)
        if len(data) % size or len(data) // size > MAX_RECORDS:
            raise FormatError(f"lump {index} has invalid record size/count")
        rows = list(struct.iter_unpack(fmt, data))
        for row in rows:
            if any(isinstance(value, float) and (not math.isfinite(value) or abs(value) > 1e9) for value in row):
                raise FormatError(f"lump {index} contains invalid floating point data")
        return rows


def validate_tree(nodes, roots, plane_count, leaf_count, aas=False):
    """Validate all nodes, including disconnected submodels, without recursion."""
    for node in nodes:
        check_index(node[0], plane_count, "node plane")
        for child in node[1:3]:
            if aas and child == 0:
                continue
            if child >= 0:
                check_index(child, len(nodes), "node child")
            else:
                check_index(-child if aas else -child - 1, leaf_count, "node leaf")
    state = bytearray(len(nodes))
    for root in list(roots) + list(range(1 if aas else 0, len(nodes))):
        if root < 0:
            check_index(-root if aas else -root - 1, leaf_count, "root leaf")
            continue
        check_index(root, len(nodes), "tree root")
        pending = [(root, False, 0)]
        while pending:
            current, finish, depth = pending.pop()
            if finish:
                state[current] = 2
                continue
            if state[current] == 1:
                raise FormatError("cyclic BSP tree")
            if state[current] == 2:
                continue
            if depth > 512:
                raise FormatError("BSP tree depth exceeds cap")
            state[current] = 1
            pending.append((current, True, depth))
            for child in nodes[current][1:3]:
                if child >= (1 if aas else 0):
                    pending.append((child, False, depth + 1))


class Bsp(Lumps):
    def __init__(self, data: bytes):
        if len(data) < 160 or len(data) > MAX_FILE_BYTES or struct.unpack_from("<4si", data) != (b"IBSP", 38):
            raise FormatError("expected bounded Quake II IBSP version 38")
        super().__init__(data, data, 8, 19, 160)
        self.sha256 = sha256(data)
        self.entity_issues = []
        self.entities = entities(self.lumps[0], self.entity_issues)
        self.file_issues = []
        self.planes = self.records(1, "<4fi")
        self.vertices = self.records(2, "<3f")
        self.nodes = self.records(4, "<iii6hHH")
        self.texinfo = self.records(5, "<8fii32si")
        self.faces = self.records(6, "<Hhihh4Bi")
        self.leaves = self.records(8, "<ihh6h4H")
        self.leaffaces = [r[0] for r in self.records(9, "<H")]
        self.leafbrushes = [r[0] for r in self.records(10, "<H")]
        self.edges = self.records(11, "<HH")
        self.surfedges = [r[0] for r in self.records(12, "<i")]
        self.models = self.records(13, "<9f3i")
        self.brushes = self.records(14, "<iii")
        self.brushsides = self.records(15, "<Hh")
        if not self.models or not self.leaves or not self.faces or not self.vertices:
            raise FormatError("missing world geometry/model/leaves")
        if sum(f[3] for f in self.faces) > MAX_GEOMETRY_REFERENCES or sum(b[1] for b in self.brushes) > MAX_GEOMETRY_REFERENCES:
            raise FormatError("aggregate geometry references exceed analysis cap")
        for plane in self.planes:
            if not 0.99 < dot(plane, plane) < 1.01:
                raise FormatError("non-unit BSP plane")
        for edge in self.edges:
            for vertex in edge:
                check_index(vertex, len(self.vertices), "edge vertex")
        for index in self.surfedges:
            check_index(abs(index), len(self.edges), "surface edge")
        for face in self.faces:
            check_index(face[0], len(self.planes), "face plane")
            check_index(face[4], len(self.texinfo), "face texture")
            check_range(face[2], face[3], len(self.surfedges), "face edges")
            if face[1] not in (0, 1) or face[3] < 3:
                raise FormatError("invalid face side/vertex count")
            if face[-1] == 0 and not self.lumps[7]:
                # Some fullbright reference maps retain zero offsets with no light lump.
                if not self.file_issues:
                    self.file_issues.append("lighting lump absent; zero light offsets treated as unavailable")
            elif face[-1] != -1:
                check_index(face[-1], len(self.lumps[7]), "light sample")
        for side in self.brushsides:
            check_index(side[0], len(self.planes), "brush plane")
        for first, count, _ in self.brushes:
            check_range(first, count, len(self.brushsides), "brush sides")
        for index in self.leafbrushes:
            check_index(index, len(self.brushes), "leaf brush")
        for index in self.leaffaces:
            check_index(index, len(self.faces), "leaf face")
        for leaf in self.leaves:
            check_range(leaf[9], leaf[10], len(self.leaffaces), "leaf faces")
            check_range(leaf[11], leaf[12], len(self.leafbrushes), "leaf brushes")
        for model in self.models:
            check_range(model[10], model[11], len(self.faces), "model faces")
        validate_tree(self.nodes, [m[9] for m in self.models], len(self.planes), len(self.leaves))
        # Ignores entity/light changes but includes compiler-dependent geometry ordering.
        self.geometry_sha256 = sha256(b"".join(struct.pack("<I", len(self.lumps[i])) + self.lumps[i]
                                              for i in (1, 2, 4, 8, 10, 11, 12, 13, 14, 15)))

    def polygons(self):
        world = self.models[0]
        for index in range(world[10], world[10] + world[11]):
            face = self.faces[index]
            points = []
            for signed_edge in self.surfedges[face[2]:face[2] + face[3]]:
                edge = self.edges[abs(signed_edge)]
                points.append(self.vertices[edge[0 if signed_edge >= 0 else 1]])
            origin = points[0]
            area = 0.0
            for a, b in zip(points[1:], points[2:]):
                u = [a[i] - origin[i] for i in range(3)]
                v = [b[i] - origin[i] for i in range(3)]
                cross = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
                area += math.sqrt(dot(cross, cross)) / 2
            plane = self.planes[face[0]]
            normal = tuple(p * (-1 if face[1] else 1) for p in plane[:3])
            texture = self.texinfo[face[4]]
            yield {"id": index, "points": points, "area": area, "normal": normal,
                   "center": tuple(sum(p[i] for p in points) / len(points) for i in range(3)),
                   "texture": decode(texture[10].split(b"\0")[0]), "flags": texture[8]}

    def trace(self, start, end, mask=1 | 2 | 8 | 16):
        """World-brush point segment: entry fraction, windows/liquid hazards block.

        Deliberately excludes entity submodels, player hulls and moving geometry.
        Uses the BSP tree for broad phase and convex plane intervals for clipping.
        """
        pending = [self.models[0][9]]
        brush_ids = set()
        visited = set()
        while pending:
            index = pending.pop()
            if index in visited:
                continue
            visited.add(index)
            if index < 0:
                leaf = self.leaves[-index - 1]
                brush_ids.update(self.leafbrushes[leaf[11]:leaf[11] + leaf[12]])
                continue
            node = self.nodes[index]
            plane = self.planes[node[0]]
            a, b = dot(plane, start) - plane[3], dot(plane, end) - plane[3]
            if max(a, b) >= -1e-5:
                pending.append(node[1])
            if min(a, b) <= 1e-5:
                pending.append(node[2])
        earliest = 1.0
        for index in brush_ids:
            first, count, contents = self.brushes[index]
            if not contents & mask or not count:
                continue
            low, high = 0.0, 1.0
            for side in self.brushsides[first:first + count]:
                plane = self.planes[side[0]]
                a, b = dot(plane, start) - plane[3], dot(plane, end) - plane[3]
                if a > 1e-5 and b > 1e-5:
                    low, high = 1.0, 0.0
                    break
                if a <= 1e-5 and b <= 1e-5:
                    continue
                fraction = a / (a - b)
                if a > b:
                    low = max(low, fraction)
                else:
                    high = min(high, fraction)
                if low > high:
                    break
            if low <= high:
                earliest = min(earliest, max(0.0, low))
        return earliest


TRAVEL_NAMES = {2: "walk", 3: "crouch", 4: "barrier_jump", 5: "jump", 6: "ladder",
                7: "drop", 8: "swim", 9: "water_jump", 10: "teleport", 11: "elevator",
                12: "rocket_jump", 13: "bfg_jump", 14: "grapple", 15: "double_jump",
                16: "ramp_jump", 17: "strafe_jump", 18: "jump_pad", 19: "bobbing_mover"}


class Aas(Lumps):
    def __init__(self, data: bytes):
        if len(data) < 124 or len(data) > MAX_FILE_BYTES:
            raise FormatError("invalid AAS file size")
        magic, self.version = struct.unpack_from("<4si", data)
        if magic != b"EAAS" or self.version not in (4, 5):
            raise FormatError("expected EAAS version 4 or 5")
        header = bytearray(data[:124])
        if self.version == 5:
            key = bytes((i * 119) % 256 for i in range(116))
            header[8:] = bytes(a ^ b for a, b in zip(header[8:], key))
        super().__init__(data, header, 12, 14, 124)
        self.sha256 = sha256(data)
        self.bsp_checksum = struct.unpack_from("<I", header, 8)[0]
        self.planes = self.records(2, "<4fi")
        self.areas = self.records(7, "<3i9f")
        self.settings = self.records(8, "<7i")
        self.reachability = self.records(9, "<3i6fiH2x")
        self.nodes = self.records(10, "<3i")
        if not self.areas or len(self.areas) != len(self.settings) or len(self.nodes) < 2:
            raise FormatError("invalid AAS area/settings/node counts")
        for area, settings in zip(self.areas, self.settings):
            check_range(settings[6], settings[5], len(self.reachability), "AAS reachability")
        for reach in self.reachability:
            check_index(reach[0], len(self.areas), "AAS destination")
        # Node zero is an unused dummy and is exempt from plane validation.
        dummy = self.nodes[0]
        self.nodes[0] = (0, 0, 0)
        validate_tree(self.nodes, [1], len(self.planes), len(self.areas), aas=True)
        self.nodes[0] = dummy

    def area_at(self, point):
        index = 1
        while index > 0:
            node = self.nodes[index]
            plane = self.planes[node[0]]
            index = node[1 if dot(plane, point) - plane[3] >= 0 else 2]
        return -index

    def graph(self):
        graph = [set() for _ in self.areas]
        for index, settings in enumerate(self.settings):
            for reach in self.reachability[settings[6]:settings[6] + settings[5]]:
                travel = reach[9] & 0xFFFFFF
                # Stock movement repertoire only; special jumps cannot prove a normal route.
                if reach[0] and travel in range(2, 12) and not reach[9] & 0xFF000000:
                    graph[index].add(reach[0])
        return graph
