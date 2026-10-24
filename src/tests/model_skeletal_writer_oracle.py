#!/usr/bin/env python3
"""Independent oracle for VibeStudio's MD5, IQM and ASE writers.

Reads the files model_skeletal_writer_oracle_test wrote, with parsers written
here from the public formats and the engines' loaders, poses them the way the
engine does, and compares the result with poses computed from each fixture's
definition.json (joint transforms, weights and bind positions given as
numbers). No VibeStudio code is used. Standard library only.

Conventions, each read from the released source or specification:

- MD5 (Doom 3 GPL release, neo/renderer/Model_md5.cpp, neo/idlib/math/Quat.h
  and Quat.cpp, neo/idlib/math/JointTransform.h, neo/game/anim/Anim.cpp):
  joints store x, y, z of a unit quaternion and w = sqrt(|1 - x^2 - y^2 -
  z^2|) (idQuat::CalcW); a point turns as the row vector `offset * ToMat3()`;
  md5mesh joints are in model space, md5anim joints are parent-relative and
  concatenate child then parent; a vertex is the sum of bias * (offset * R +
  t) over its weights. Triangles are clockwise: the face plane is
  cross(c - a, b - a) (R_DeriveFacePlanes in neo/renderer/tr_trisurf.cpp).
- IQM (iqm.txt, iqm.h and the reference demo by Lee Salzman,
  https://github.com/lsalzman/iqm): little-endian header, joints and poses
  are parent-relative translate / rotate (x, y, z, w) / scale with
  M = T * R * S (Matrix3x3(rotation, scale) scales R's columns); a frame
  matrix is base[parent] * pose * inverse(base[joint]), concatenated down the
  hierarchy; blend weights are bytes summing to 255, so a posed vertex may sit
  up to one byte step per influence, times the distance between the positions
  its influences give it, from the exact blend. Triangles are clockwise,
  as the reference Blender exporter writes them and ioquake3 culls them.
- ASE (Doom 3 neo/renderer/Model_ase.cpp; q3map2 tools/quake3/q3map2/model.cpp
  in q3mapx): A B C list counter-clockwise front faces; TVERT v is 1 - v.
  Doom 3 names the material from *BITMAP after its first complete "base"
  folder (idFileSystemLocal::OSPathToRelativePath), lower case, without the
  text from the last '.' (idDeclManagerLocal::MakeNameCanonical). q3map2
  uses *MATERIAL_NAME when it starts with textures/ or models/, else the
  bitmap without its extension, trimmed at /models/ or /textures/ when
  absolute.

Each check is named. Negative controls feed deliberately broken copies (a
flipped winding, a wrong quaternion sign, swapped joints) to the same checks
and require the targeted check to fail, so a check that cannot fail is
caught. Exit 0 when every check passes and every control fails as intended.
"""

import argparse
import hashlib
import json
import math
import re
import struct
import sys
from pathlib import Path

POSITION_TOLERANCE = 1e-3
IQM_TOLERANCE = 5e-3


class CheckError(Exception):
    pass


# A broken file can fail a check with any of these instead of a CheckError.
FAILURES = (CheckError, IndexError, KeyError, ValueError, ZeroDivisionError, struct.error)


# --- Small affine maths (column vectors, M = (R, t)) -----------------------

def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def mat_vec(m, v):
    return [sum(m[i][k] * v[k] for k in range(3)) for i in range(3)]


def affine_mul(a, b):
    ra, ta = a
    rb, tb = b
    return mat_mul(ra, rb), [x + y for x, y in zip(mat_vec(ra, tb), ta)]


def affine_apply(m, p):
    r, t = m
    return [x + y for x, y in zip(mat_vec(r, p), t)]


def mat_inverse(m):
    a, b, c = m
    det = (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0]))
    if abs(det) < 1e-12:
        raise CheckError("singular joint matrix")
    inv = [[(b[1] * c[2] - b[2] * c[1]) / det, (a[2] * c[1] - a[1] * c[2]) / det, (a[1] * b[2] - a[2] * b[1]) / det],
           [(b[2] * c[0] - b[0] * c[2]) / det, (a[0] * c[2] - a[2] * c[0]) / det, (a[2] * b[0] - a[0] * b[2]) / det],
           [(b[0] * c[1] - b[1] * c[0]) / det, (a[1] * c[0] - a[0] * c[1]) / det, (a[0] * b[1] - a[1] * b[0]) / det]]
    return inv


def affine_inverse(m):
    r, t = m
    inv = mat_inverse(r)
    return inv, [-x for x in mat_vec(inv, t)]


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


# --- The fixture definition -------------------------------------------------

def axis_angle(axis, degrees):
    """Right-handed rotation of column vectors (Rodrigues)."""
    length = math.sqrt(dot(axis, axis))
    x, y, z = (c / length for c in axis)
    theta = math.radians(degrees)
    c, s, k = math.cos(theta), math.sin(theta), 1 - math.cos(theta)
    return [[c + x * x * k, x * y * k - z * s, x * z * k + y * s],
            [y * x * k + z * s, c + y * y * k, y * z * k - x * s],
            [z * x * k - y * s, z * y * k + x * s, c + z * z * k]]


def local_matrix(local):
    rotation = axis_angle(local["axis"], local["degrees"])
    scale = local["scale"]
    return [[rotation[i][j] * scale[j] for j in range(3)] for i in range(3)], list(local["translation"])


def model_space(definition, locals_):
    result = []
    for index, joint in enumerate(definition["joints"]):
        matrix = local_matrix(locals_[index])
        parent = joint["parent"]
        result.append(affine_mul(result[parent], matrix) if parent >= 0 else matrix)
    return result


def expected_poses(definition, spreads=False):
    """Linear blend skinning of the bind positions for every clip frame.

    With `spreads`, each vertex instead gets the largest distance between the
    positions its influences give it, which bounds how far a change of weight
    can move it."""
    inverse_bind = [affine_inverse(m) for m in model_space(definition, definition["bind"])]
    clips = {}
    for clip in definition["clips"]:
        frames = []
        for locals_ in clip["frames"]:
            pose = model_space(definition, locals_)
            skin = [affine_mul(pose[j], inverse_bind[j]) for j in range(len(pose))]
            positions = []
            for vertex in definition["vertices"]:
                total = sum(w for _, w in vertex["weights"])
                influenced = [affine_apply(skin[joint], vertex["position"]) for joint, _ in vertex["weights"]]
                if spreads:
                    positions.append(max((distance(a, b) for a in influenced for b in influenced), default=0.0))
                    continue
                p = [0.0, 0.0, 0.0]
                for (joint, weight), q in zip(vertex["weights"], influenced):
                    p = [a + weight / total * b for a, b in zip(p, q)]
                positions.append(p)
            frames.append(positions)
        clips[clip["name"]] = frames
    return clips


def front_normal(positions, triangle):
    """The outward normal of a definition (counter-clockwise) face."""
    a, b, c = (positions[i] for i in triangle)
    return cross(sub(b, a), sub(c, a))


# --- Correspondence between file and definition vertices --------------------

def match_vertices(definition, positions, uvs, what):
    """Each file vertex's definition vertex, by bind position and UV."""
    mapping = []
    for index, (p, uv) in enumerate(zip(positions, uvs)):
        best, best_distance = None, None
        for d, vertex in enumerate(definition["vertices"]):
            gap = distance(p, vertex["position"]) + distance(uv, vertex["uv"])
            if best_distance is None or gap < best_distance:
                best, best_distance = d, gap
        if best_distance is None or best_distance > 10 * POSITION_TOLERANCE:
            raise CheckError(f"{what} vertex {index} at {p} uv {uv} matches no fixture vertex")
        mapping.append(best)
    if sorted(set(mapping)) != list(range(len(definition["vertices"]))):
        raise CheckError(f"{what} vertices do not cover the fixture's vertices one to one")
    return mapping


def check_winding(definition, positions, triangles, mapping, clockwise, what):
    defined = {frozenset(t): t for t in definition["triangles"]}
    if len(triangles) != len(definition["triangles"]):
        raise CheckError(f"{what} has {len(triangles)} triangles; the fixture has {len(definition['triangles'])}")
    bind = [v["position"] for v in definition["vertices"]]
    for index, triangle in enumerate(triangles):
        key = frozenset(mapping[i] for i in triangle)
        if key not in defined:
            raise CheckError(f"{what} triangle {index} joins vertices no fixture face joins")
        a, b, c = (positions[i] for i in triangle)
        normal = cross(sub(c, a), sub(b, a)) if clockwise else cross(sub(b, a), sub(c, a))
        if dot(normal, front_normal(bind, defined[key])) <= 0:
            raise CheckError(f"{what} triangle {index} faces the wrong way")


def compare_positions(expected, actual, mapping, tolerance, what, slack=None):
    """`slack`, per fixture vertex, widens the tolerance (IQM's byte weights)."""
    worst = 0.0
    for index, p in enumerate(actual):
        gap = distance(p, expected[mapping[index]])
        worst = max(worst, gap)
        if gap > tolerance + (slack[mapping[index]] if slack else 0.0):
            raise CheckError(f"{what}: vertex {index} is at {fmt(p)}, expected {fmt(expected[mapping[index]])} ({gap:.6f} apart)")
    return worst


def check_bounds(bounds, positions, tolerance, what):
    low, high = bounds
    for p in positions:
        for axis in range(3):
            if p[axis] < low[axis] - tolerance or p[axis] > high[axis] + tolerance:
                raise CheckError(f"{what}: vertex {fmt(p)} lies outside the frame bounds {fmt(low)} {fmt(high)}")


def fmt(v):
    return "(" + ", ".join(f"{x:.4f}" for x in v) + ")"


# --- MD5 text ---------------------------------------------------------------

TOKEN = re.compile(r'//[^\n]*|"([^"]*)"|([{}()])|([^\s{}()"]+)')


def md5_tokens(text):
    tokens = []
    for match in TOKEN.finditer(text):
        if match.group(0).startswith("//"):
            continue
        if match.group(1) is not None:
            tokens.append(("string", match.group(1)))
        elif match.group(2) is not None:
            tokens.append(("symbol", match.group(2)))
        else:
            tokens.append(("word", match.group(3)))
    return tokens


class Reader:
    def __init__(self, tokens, what):
        self.tokens, self.at, self.what = tokens, 0, what

    def peek(self):
        return self.tokens[self.at][1] if self.at < len(self.tokens) else None

    def next(self):
        if self.at >= len(self.tokens):
            raise CheckError(f"{self.what} ends early")
        self.at += 1
        return self.tokens[self.at - 1][1]

    def expect(self, value):
        got = self.next()
        if got != value:
            raise CheckError(f"{self.what}: expected {value!r}, found {got!r}")

    def number(self):
        return float(self.next())

    def integer(self):
        return int(self.next())

    def vector(self, count):
        self.expect("(")
        values = [self.number() for _ in range(count)]
        self.expect(")")
        return values


def doom3_rotation(xyz, negative_w=False):
    """idQuat::ToMat3 of (x, y, z, CalcW()), as rows."""
    x, y, z = xyz
    w = math.sqrt(abs(1.0 - (x * x + y * y + z * z)))
    if negative_w:
        w = -w
    x2, y2, z2 = x + x, y + y, z + z
    xx, xy, xz = x * x2, x * y2, x * z2
    yy, yz, zz = y * y2, y * z2, z * z2
    wx, wy, wz = w * x2, w * y2, w * z2
    return [[1 - (yy + zz), xy - wz, xz + wy],
            [xy + wz, 1 - (xx + zz), yz - wx],
            [xz - wy, yz + wx, 1 - (xx + yy)]]


def row_times(v, m):
    return [sum(v[i] * m[i][j] for i in range(3)) for j in range(3)]


def parse_md5mesh(text):
    r = Reader(md5_tokens(text), "md5mesh")
    r.expect("MD5Version")
    if r.integer() != 10:
        raise CheckError("md5mesh is not MD5Version 10")
    mesh = {"joints": [], "meshes": []}
    while r.peek() is not None:
        key = r.next()
        if key == "commandline":
            r.next()
        elif key in ("numJoints", "numMeshes"):
            mesh[key] = r.integer()
        elif key == "joints":
            r.expect("{")
            for _ in range(mesh["numJoints"]):
                name = r.next()
                parent = r.integer()
                mesh["joints"].append({"name": name, "parent": parent, "position": r.vector(3), "orientation": r.vector(3)})
            r.expect("}")
        elif key == "mesh":
            r.expect("{")
            item = {"verts": [], "tris": [], "weights": [], "shader": ""}
            while r.peek() != "}":
                field = r.next()
                if field == "shader":
                    item["shader"] = r.next()
                elif field in ("numverts", "numtris", "numweights"):
                    r.integer()
                elif field == "vert":
                    r.integer()
                    item["verts"].append({"st": r.vector(2), "start": r.integer(), "count": r.integer()})
                elif field == "tri":
                    r.integer()
                    item["tris"].append([r.integer(), r.integer(), r.integer()])
                elif field == "weight":
                    r.integer()
                    item["weights"].append({"joint": r.integer(), "bias": r.number(), "offset": r.vector(3)})
                else:
                    raise CheckError(f"md5mesh: unknown mesh key {field!r}")
            r.expect("}")
            mesh["meshes"].append(item)
        else:
            raise CheckError(f"md5mesh: unknown key {key!r}")
    if len(mesh["joints"]) != mesh.get("numJoints") or len(mesh["meshes"]) != mesh.get("numMeshes"):
        raise CheckError("md5mesh counts do not match its blocks")
    return mesh


def parse_md5anim(text):
    r = Reader(md5_tokens(text), "md5anim")
    r.expect("MD5Version")
    if r.integer() != 10:
        raise CheckError("md5anim is not MD5Version 10")
    anim = {"hierarchy": [], "bounds": [], "baseframe": [], "frames": {}}
    while r.peek() is not None:
        key = r.next()
        if key == "commandline":
            r.next()
        elif key in ("numFrames", "numJoints", "frameRate", "numAnimatedComponents"):
            anim[key] = r.integer()
        elif key == "hierarchy":
            r.expect("{")
            for _ in range(anim["numJoints"]):
                anim["hierarchy"].append({"name": r.next(), "parent": r.integer(), "flags": r.integer(), "start": r.integer()})
            r.expect("}")
        elif key == "bounds":
            r.expect("{")
            for _ in range(anim["numFrames"]):
                anim["bounds"].append((r.vector(3), r.vector(3)))
            r.expect("}")
        elif key == "baseframe":
            r.expect("{")
            for _ in range(anim["numJoints"]):
                anim["baseframe"].append((r.vector(3), r.vector(3)))
            r.expect("}")
        elif key == "frame":
            index = r.integer()
            r.expect("{")
            anim["frames"][index] = [r.number() for _ in range(anim["numAnimatedComponents"])]
            r.expect("}")
        else:
            raise CheckError(f"md5anim: unknown key {key!r}")
    if len(anim["frames"]) != anim.get("numFrames"):
        raise CheckError("md5anim frame count does not match numFrames")
    return anim


def md5_vertices(mesh, joints):
    """Each vertex: sum of bias * (offset * R + t) over its weights."""
    positions, uvs = [], []
    for item in mesh["meshes"]:
        for vert in item["verts"]:
            p = [0.0, 0.0, 0.0]
            for weight in item["weights"][vert["start"]:vert["start"] + vert["count"]]:
                rotation, translation = joints[weight["joint"]]
                q = [a + b for a, b in zip(row_times(weight["offset"], rotation), translation)]
                p = [a + weight["bias"] * b for a, b in zip(p, q)]
            positions.append(p)
            uvs.append(vert["st"])
    return positions, uvs


def md5mesh_joints(mesh, negative_w=False):
    return [(doom3_rotation(j["orientation"], negative_w), j["position"]) for j in mesh["joints"]]


def md5anim_joints(anim, frame, negative_w=False):
    values = anim["frames"][frame]
    result = []
    for index, joint in enumerate(anim["hierarchy"]):
        translation, orientation = (list(v) for v in anim["baseframe"][index])
        component = joint["start"]
        for bit in range(6):
            if joint["flags"] & (1 << bit):
                (translation if bit < 3 else orientation)[bit % 3] = values[component]
                component += 1
        rotation = doom3_rotation(orientation, negative_w)
        parent = joint["parent"]
        if parent >= index:
            raise CheckError(f"md5anim joint {joint['name']} comes before its parent")
        if parent >= 0:
            parent_rotation, parent_translation = result[parent]
            rotation = [row_times(row, parent_rotation) for row in rotation]
            translation = [a + b for a, b in zip(row_times(translation, parent_rotation), parent_translation)]
        result.append((rotation, translation))
    return result


def check_hierarchy(definition, joints, what):
    names = [j["name"] for j in definition["joints"]]
    found = [j["name"] for j in joints]
    if found != names:
        raise CheckError(f"{what} joints are {found}; the fixture's are {names}")
    for index, joint in enumerate(joints):
        expected = definition["joints"][index]["parent"]
        if joint["parent"] != expected:
            raise CheckError(f"{what} joint {joint['name']} has parent {joint['parent']}, expected {expected}")


def md5_checks(definition, directory, mutate=None, negative_w=False):
    """Named MD5 checks; `mutate` edits the file texts first (controls)."""
    files = definition["files"]
    mesh_text = (directory / files["md5mesh"]).read_text(encoding="utf-8")
    anim_texts = {clip: (directory / name).read_text(encoding="utf-8") for clip, name in files["md5anim"].items()}
    if mutate:
        mesh_text, anim_texts = mutate(mesh_text, anim_texts)
    expected = expected_poses(definition)
    results = {}

    def run(name, body):
        try:
            results[name] = (True, body())
        except FAILURES as error:
            results[name] = (False, f"{type(error).__name__}: {error}" if not isinstance(error, CheckError) else str(error))

    mesh = parse_md5mesh(mesh_text)
    state = {}

    def hierarchy():
        check_hierarchy(definition, mesh["joints"], "md5mesh")
        return f"{len(mesh['joints'])} joints in order"

    def bind():
        positions, uvs = md5_vertices(mesh, md5mesh_joints(mesh, negative_w))
        state["mapping"] = match_vertices(definition, positions, uvs, "md5mesh")
        state["positions"] = positions
        bind_positions = [v["position"] for v in definition["vertices"]]
        worst = compare_positions(bind_positions, positions, state["mapping"], POSITION_TOLERANCE, "md5mesh bind pose")
        return f"{len(positions)} vertices within {worst:.2e}"

    def winding():
        if "mapping" not in state:
            raise CheckError("the bind pose did not match")
        triangles, base = [], 0
        for item in mesh["meshes"]:
            triangles += [[base + i for i in t] for t in item["tris"]]
            base += len(item["verts"])
        check_winding(definition, state["positions"], triangles, state["mapping"], True, "md5mesh")
        return f"{len(triangles)} clockwise triangles"

    def material():
        shaders = [item["shader"] for item in mesh["meshes"]]
        if shaders != [definition["material"]]:
            raise CheckError(f"md5mesh shaders are {shaders}, expected {[definition['material']]}")
        return shaders[0]

    run("md5mesh-hierarchy", hierarchy)
    run("md5mesh-bind", bind)
    run("md5mesh-winding", winding)
    run("md5mesh-material", material)
    for clip in definition["clips"]:
        name = clip["name"]
        anim = parse_md5anim(anim_texts[name])

        def header(anim=anim, clip=clip):
            if anim["numFrames"] != len(clip["frames"]) or anim["frameRate"] != clip["framesPerSecond"]:
                raise CheckError(f"md5anim {clip['name']} has {anim['numFrames']} frames at {anim['frameRate']} fps, expected "
                                 f"{len(clip['frames'])} at {clip['framesPerSecond']}")
            check_hierarchy(definition, anim["hierarchy"], f"md5anim {clip['name']}")
            return f"{anim['numFrames']} frames at {anim['frameRate']} fps"

        def poses(anim=anim, clip=clip):
            if "mapping" not in state:
                raise CheckError("the bind pose did not match")
            worst = 0.0
            for frame in range(anim["numFrames"]):
                positions, _ = md5_vertices(mesh, md5anim_joints(anim, frame, negative_w))
                worst = max(worst, compare_positions(expected[clip["name"]][frame], positions, state["mapping"], POSITION_TOLERANCE,
                                                     f"md5anim {clip['name']} frame {frame}"))
                check_bounds(anim["bounds"][frame], positions, POSITION_TOLERANCE, f"md5anim {clip['name']} frame {frame}")
            return f"every frame within {worst:.2e} and inside its bounds"

        run(f"md5anim-header-{name}", header)
        run(f"md5anim-poses-{name}", poses)
    return results


# --- IQM binary ---------------------------------------------------------------

IQM_HEADER = struct.Struct("<16s27I")
IQM_FIELDS = ("version filesize flags num_text ofs_text num_meshes ofs_meshes num_vertexarrays num_vertexes ofs_vertexarrays "
              "num_triangles ofs_triangles ofs_adjacency num_joints ofs_joints num_poses ofs_poses num_anims ofs_anims num_frames "
              "num_framechannels ofs_frames ofs_bounds num_comment ofs_comment num_extensions ofs_extensions").split()
IQM_POSITION, IQM_TEXCOORD, IQM_BLENDINDEXES, IQM_BLENDWEIGHTS = 0, 1, 4, 5
IQM_UBYTE, IQM_FLOAT = 1, 7


def parse_iqm(data):
    if len(data) < IQM_HEADER.size:
        raise CheckError("the IQM is shorter than its header")
    values = IQM_HEADER.unpack_from(data, 0)
    if values[0] != b"INTERQUAKEMODEL\0":
        raise CheckError("the IQM magic is wrong")
    h = dict(zip(IQM_FIELDS, values[1:]))
    if h["version"] != 2 or h["filesize"] != len(data):
        raise CheckError(f"IQM version {h['version']} and size {h['filesize']} (file {len(data)} bytes)")

    def block(offset, count, size, what):
        if count and (offset < IQM_HEADER.size or offset + count * size > len(data)):
            raise CheckError(f"IQM {what} lie outside the file")
        return [data[offset + i * size:offset + (i + 1) * size] for i in range(count)]

    text = data[h["ofs_text"]:h["ofs_text"] + h["num_text"]]

    def string(offset):
        end = text.index(b"\0", offset)
        return text[offset:end].decode("utf-8")

    model = {"header": h}
    model["meshes"] = [dict(zip("name material first_vertex num_vertexes first_triangle num_triangles".split(), struct.unpack("<6I", b)))
                       for b in block(h["ofs_meshes"], h["num_meshes"], 24, "meshes")]
    for mesh in model["meshes"]:
        mesh["name"], mesh["material"] = string(mesh["name"]), string(mesh["material"])
    arrays = {}
    for b in block(h["ofs_vertexarrays"], h["num_vertexarrays"], 20, "vertex arrays"):
        kind, flags, form, size, offset = struct.unpack("<5I", b)
        width = 4 if form == IQM_FLOAT else 1
        code = "f" if form == IQM_FLOAT else "B"
        rows = block(offset, h["num_vertexes"], size * width, "vertex array")
        arrays[kind] = [list(struct.unpack("<" + code * size, row)) for row in rows]
    model["arrays"] = arrays
    model["triangles"] = [list(struct.unpack("<3I", b)) for b in block(h["ofs_triangles"], h["num_triangles"], 12, "triangles")]
    model["joints"] = []
    for b in block(h["ofs_joints"], h["num_joints"], 48, "joints"):
        values = struct.unpack("<Ii10f", b)
        model["joints"].append({"name": string(values[0]), "parent": values[1], "translate": values[2:5], "rotate": values[5:9],
                                "scale": values[9:12]})
    model["poses"] = []
    for b in block(h["ofs_poses"], h["num_poses"], 88, "poses"):
        values = struct.unpack("<iI20f", b)
        model["poses"].append({"parent": values[0], "mask": values[1], "offset": values[2:12], "scale": values[12:22]})
    model["anims"] = []
    for b in block(h["ofs_anims"], h["num_anims"], 20, "animations"):
        name, first, count, rate, flags = struct.unpack("<3IfI", b)
        model["anims"].append({"name": string(name), "first": first, "count": count, "rate": rate, "flags": flags})
    channels = h["num_frames"] * h["num_framechannels"]
    if channels and h["ofs_frames"] + channels * 2 > len(data):
        raise CheckError("IQM frames lie outside the file")
    model["frames"] = list(struct.unpack_from(f"<{channels}H", data, h["ofs_frames"])) if channels else []
    model["bounds"] = [(list(v[0:3]), list(v[3:6])) for v in (struct.unpack("<8f", b) for b in block(h["ofs_bounds"], h["num_frames"] if h["ofs_bounds"] else 0, 32, "bounds"))]
    return model


def iqm_matrix(translate, rotate, scale, conjugate=False):
    """M = T * R * S from a normalised quaternion, as Matrix3x4 does."""
    x, y, z, w = rotate
    length = math.sqrt(x * x + y * y + z * z + w * w)
    x, y, z, w = x / length, y / length, z / length, w / length
    if conjugate:
        x, y, z = -x, -y, -z
    tx, ty, tz = 2 * x, 2 * y, 2 * z
    txx, tyy, tzz, txy, txz, tyz = tx * x, ty * y, tz * z, tx * y, tx * z, ty * z
    twx, twy, twz = w * tx, w * ty, w * tz
    rows = [[1 - (tyy + tzz), txy - twz, txz + twy], [txy + twz, 1 - (txx + tzz), tyz - twx], [txz - twy, tyz + twx, 1 - (txx + tyy)]]
    return [[rows[i][j] * scale[j] for j in range(3)] for i in range(3)], list(translate)


def iqm_frames(model, conjugate=False):
    joints, poses = model["joints"], model["poses"]
    base, inverse = [], []
    for index, joint in enumerate(joints):
        if joint["parent"] >= index:
            raise CheckError(f"IQM joint {joint['name']} comes before its parent")
        m = iqm_matrix(joint["translate"], joint["rotate"], joint["scale"], conjugate)
        i = affine_inverse(m)
        if joint["parent"] >= 0:
            m = affine_mul(base[joint["parent"]], m)
            i = affine_mul(i, inverse[joint["parent"]])
        base.append(m)
        inverse.append(i)
    channels = model["header"]["num_framechannels"]
    result = []
    for frame in range(model["header"]["num_frames"]):
        data = model["frames"][frame * channels:(frame + 1) * channels]
        at, out = 0, []
        for index, pose in enumerate(poses):
            values = []
            for k in range(10):
                value = pose["offset"][k]
                if pose["mask"] & (1 << k):
                    value += data[at] * pose["scale"][k]
                    at += 1
                values.append(value)
            m = iqm_matrix(values[0:3], values[3:7], values[7:10], conjugate)
            parent = pose["parent"]
            f = affine_mul(affine_mul(base[parent], m), inverse[index]) if parent >= 0 else affine_mul(m, inverse[index])
            out.append(affine_mul(out[parent], f) if parent >= 0 else f)
        result.append(out)
    return result


def iqm_posed(model, frame_matrices):
    positions = model["arrays"][IQM_POSITION]
    indexes, weights = model["arrays"].get(IQM_BLENDINDEXES), model["arrays"].get(IQM_BLENDWEIGHTS)
    result = []
    for v, p in enumerate(positions):
        total = sum(weights[v]) if weights else 0
        if not total:
            result.append(list(p))
            continue
        q = [0.0, 0.0, 0.0]
        for joint, weight in zip(indexes[v], weights[v]):
            if weight:
                q = [a + weight / 255.0 * b for a, b in zip(q, affine_apply(frame_matrices[joint], p))]
        result.append(q)
    return result


def iqm_checks(definition, directory, mutate=None, conjugate=False):
    data = (directory / definition["files"]["iqm"]).read_bytes()
    if mutate:
        data = mutate(data)
    expected = expected_poses(definition)
    spreads = expected_poses(definition, spreads=True)
    results, state = {}, {}

    def run(name, body):
        try:
            results[name] = (True, body())
        except FAILURES as error:
            results[name] = (False, f"{type(error).__name__}: {error}" if not isinstance(error, CheckError) else str(error))

    model = parse_iqm(data)

    def header():
        h = model["header"]
        if h["num_poses"] != h["num_joints"]:
            raise CheckError("IQM poses and joints differ in number")
        for kind, form, size in ((IQM_POSITION, IQM_FLOAT, 3), (IQM_TEXCOORD, IQM_FLOAT, 2), (IQM_BLENDINDEXES, IQM_UBYTE, 4),
                                 (IQM_BLENDWEIGHTS, IQM_UBYTE, 4)):
            if kind not in model["arrays"] or len(model["arrays"][kind][0]) != size:
                raise CheckError(f"IQM vertex array {kind} is missing or has the wrong size")
        for v, weights in enumerate(model["arrays"][IQM_BLENDWEIGHTS]):
            if sum(weights) not in (0, 255):
                raise CheckError(f"IQM vertex {v} blend weights sum to {sum(weights)}, not 255")
        return f"{h['num_vertexes']} vertices, {h['num_triangles']} triangles, {h['num_joints']} joints, {h['num_frames']} frames"

    def hierarchy():
        check_hierarchy(definition, model["joints"], "IQM")
        return f"{len(model['joints'])} joints in order"

    def bind():
        positions = model["arrays"][IQM_POSITION]
        uvs = model["arrays"][IQM_TEXCOORD]
        state["mapping"] = match_vertices(definition, positions, uvs, "IQM")
        bind_positions = [v["position"] for v in definition["vertices"]]
        worst = compare_positions(bind_positions, positions, state["mapping"], POSITION_TOLERANCE, "IQM bind pose")
        return f"{len(positions)} vertices within {worst:.2e}"

    def winding():
        if "mapping" not in state:
            raise CheckError("the bind pose did not match")
        check_winding(definition, model["arrays"][IQM_POSITION], model["triangles"], state["mapping"], True, "IQM")
        return f"{len(model['triangles'])} clockwise triangles"

    def material():
        materials = [m["material"] for m in model["meshes"]]
        if materials != [definition["material"]]:
            raise CheckError(f"IQM materials are {materials}, expected {[definition['material']]}")
        return materials[0]

    def anims():
        found = [(a["name"], a["count"], round(a["rate"], 4)) for a in model["anims"]]
        wanted = [(c["name"], len(c["frames"]), float(c["framesPerSecond"])) for c in definition["clips"]]
        if found != wanted:
            raise CheckError(f"IQM animations are {found}, expected {wanted}")
        return ", ".join(f"{n} ({c} at {r:g} fps)" for n, c, r in found)

    def weights():
        if "mapping" not in state:
            raise CheckError("the bind pose did not match")
        names = [j["name"] for j in model["joints"]]
        wanted = [j["name"] for j in definition["joints"]]
        worst = 0.0
        for v, (indexes, stored) in enumerate(zip(model["arrays"][IQM_BLENDINDEXES], model["arrays"][IQM_BLENDWEIGHTS])):
            found = {}
            for joint, weight in zip(indexes, stored):
                if weight:
                    found[names[joint]] = found.get(names[joint], 0.0) + weight / 255.0
            vertex = definition["vertices"][state["mapping"][v]]
            total = sum(w for _, w in vertex["weights"])
            expected_weights = {wanted[joint]: weight / total for joint, weight in vertex["weights"]}
            if set(found) != set(expected_weights):
                raise CheckError(f"IQM vertex {v} is weighted to {sorted(found)}, expected {sorted(expected_weights)}")
            for name, weight in expected_weights.items():
                worst = max(worst, abs(found[name] - weight))
                if abs(found[name] - weight) > 1.0 / 255 + 1e-9:
                    raise CheckError(f"IQM vertex {v} gives {name} {found[name]:.4f}, expected {weight:.4f}")
        return f"every influence within {worst:.2e} of its weight (one byte step is {1 / 255:.2e})"

    def poses():
        if "mapping" not in state:
            raise CheckError("the bind pose did not match")
        frames = iqm_frames(model, conjugate)
        worst = 0.0
        for anim in model["anims"]:
            for frame in range(anim["count"]):
                index = anim["first"] + frame
                positions = iqm_posed(model, frames[index])
                # Byte weights may each be a step from the exact weight.
                slack = [spread * len(vertex["weights"]) / 255.0
                         for spread, vertex in zip(spreads[anim["name"]][frame], definition["vertices"])]
                worst = max(worst, compare_positions(expected[anim["name"]][frame], positions, state["mapping"], IQM_TOLERANCE,
                                                     f"IQM {anim['name']} frame {frame}", slack))
                if model["bounds"]:
                    check_bounds(model["bounds"][index], positions, IQM_TOLERANCE, f"IQM {anim['name']} frame {frame}")
        return f"every frame within {worst:.2e} and inside its bounds"

    run("iqm-header", header)
    run("iqm-hierarchy", hierarchy)
    run("iqm-bind", bind)
    run("iqm-winding", winding)
    run("iqm-material", material)
    run("iqm-anims", anims)
    run("iqm-weights", weights)
    run("iqm-poses", poses)
    return results


# --- ASE text ---------------------------------------------------------------

ASE_TOKEN = re.compile(r'"([^"]*)"|([{}])|([^\s{}"]+)')


def parse_ase(text):
    """A tree of (key, values, children) from *KEY lines and { } blocks."""
    root = {"key": None, "values": [], "children": []}
    stack, current = [root], None
    for match in ASE_TOKEN.finditer(text):
        token = match.group(1) if match.group(1) is not None else match.group(2) or match.group(3)
        if match.group(2) == "{":
            if current is None:
                raise CheckError("ASE block without a key")
            stack.append(current)
            current = None
        elif match.group(2) == "}":
            if len(stack) == 1:
                raise CheckError("ASE closes a block it never opened")
            stack.pop()
            current = None
        elif match.group(1) is None and token.startswith("*"):
            current = {"key": token, "values": [], "children": []}
            stack[-1]["children"].append(current)
        elif current is not None:
            current["values"].append(token)
    if len(stack) != 1:
        raise CheckError("ASE ends inside a block")
    return root


def children(node, key):
    return [c for c in node["children"] if c["key"] == key]


def doom3_material_name(bitmap):
    """OSPathToRelativePath (first complete "base" folder), then MakeNameCanonical."""
    path = bitmap.replace("\\", "/")
    at = path.find("base")
    while at >= 0:
        before = path[at - 1] if at > 0 else ""
        after = path[at + 4:at + 5]
        if before == "/" and after == "/":
            path = path[at + 5:]
            break
        at = path.find("base", at + 1)
    last_dot = path.rfind(".")
    result = "".join(c.lower() for c in path)
    return result[:last_dot] if last_dot >= 0 else result


def q3map2_material_name(material_name, bitmap, model_folder):
    def extensionless(name):
        slash = name.rfind("/")
        dot = name.rfind(".")
        return name[:dot] if dot > slash else name

    lowered = material_name.lower().replace("\\", "/")
    if bitmap and not (lowered.startswith("textures/") or lowered.startswith("models/")):
        shader = extensionless(bitmap.replace("\\", "/"))
    else:
        shader = extensionless(material_name.replace("\\", "/"))
    if "/" not in shader:
        return model_folder + shader
    if shader.startswith("/") or (len(shader) > 1 and shader[1] == ":") or ".." in shader:
        for marker in ("/models/", "/textures/"):
            at = shader.lower().find(marker)
            if at >= 0:
                return shader[at + 1:]
        return model_folder + shader.rsplit("/", 1)[-1]
    return shader


def ase_checks(definition, directory, mutate=None):
    text = (directory / definition["files"]["ase"]).read_text(encoding="utf-8")
    if mutate:
        text = mutate(text)
    results, state = {}, {}

    def run(name, body):
        try:
            results[name] = (True, body())
        except FAILURES as error:
            results[name] = (False, f"{type(error).__name__}: {error}" if not isinstance(error, CheckError) else str(error))

    tree = parse_ase(text)
    objects = children(tree, "*GEOMOBJECT")
    materials = []
    for block in children(tree, "*MATERIAL_LIST"):
        for material in children(block, "*MATERIAL"):
            name = (children(material, "*MATERIAL_NAME") or [{"values": [""]}])[0]["values"][0]
            bitmap = ""
            for diffuse in children(material, "*MAP_DIFFUSE"):
                for b in children(diffuse, "*BITMAP"):
                    bitmap = b["values"][0]
            materials.append((name, bitmap))

    def geometry():
        if len(objects) != 1:
            raise CheckError(f"the ASE has {len(objects)} objects, expected 1")
        mesh = children(objects[0], "*MESH")[0]
        vertices = [[float(x) for x in v["values"][1:4]] for v in children(children(mesh, "*MESH_VERTEX_LIST")[0], "*MESH_VERTEX")]
        tvertices = [[float(x) for x in v["values"][1:3]] for v in children(children(mesh, "*MESH_TVERTLIST")[0], "*MESH_TVERT")]
        faces = []
        for face in children(children(mesh, "*MESH_FACE_LIST")[0], "*MESH_FACE"):
            values = face["values"]
            faces.append([int(values[values.index(label) + 1]) for label in ("A:", "B:", "C:")])
        tfaces = [[int(x) for x in t["values"][1:4]] for t in children(children(mesh, "*MESH_TFACELIST")[0], "*MESH_TFACE")]
        # One corner per (vertex, texture vertex) pair, as both readers merge them.
        corners, positions, uvs, triangles = {}, [], [], []
        for face, tface in zip(faces, tfaces):
            triangle = []
            for v, t in zip(face, tface):
                if (v, t) not in corners:
                    corners[(v, t)] = len(positions)
                    positions.append(vertices[v])
                    uvs.append([tvertices[t][0], 1.0 - tvertices[t][1]])
                triangle.append(corners[(v, t)])
            triangles.append(triangle)
        state.update(positions=positions, triangles=triangles, mapping=match_vertices(definition, positions, uvs, "ASE"))
        bind_positions = [v["position"] for v in definition["vertices"]]
        worst = compare_positions(bind_positions, positions, state["mapping"], POSITION_TOLERANCE, "ASE")
        return f"{len(positions)} corners within {worst:.2e}, TVERT v read as 1 - v"

    def winding():
        if "mapping" not in state:
            raise CheckError("the geometry did not match")
        check_winding(definition, state["positions"], state["triangles"], state["mapping"], False, "ASE")
        return f"{len(state['triangles'])} counter-clockwise faces"

    def material_reference():
        reference = int(children(objects[0], "*MATERIAL_REF")[0]["values"][0])
        if reference >= len(materials):
            raise CheckError(f"the ASE object refers to material {reference} of {len(materials)}")
        return materials[reference]

    def doom3():
        name, bitmap = material_reference()
        found = doom3_material_name(bitmap)
        if found != definition["material"]:
            raise CheckError(f"Doom 3 names the ASE material {found!r} from *BITMAP {bitmap!r}, expected {definition['material']!r}")
        return found

    def q3map2():
        name, bitmap = material_reference()
        folder = "models/"  # only used for bare names
        found = q3map2_material_name(name, bitmap, folder)
        if found != definition["material"]:
            raise CheckError(f"q3map2 names the ASE shader {found!r}, expected {definition['material']!r}")
        return found

    run("ase-geometry", geometry)
    run("ase-winding", winding)
    run("ase-material-doom3", doom3)
    run("ase-material-q3map2", q3map2)
    return results


# --- Negative controls --------------------------------------------------------

def flip_md5_winding(mesh_text, anims):
    return re.sub(r"(tri\s+\d+\s+)(\d+)(\s+)(\d+)(\s+)(\d+)", r"\1\2\3\6\5\4", mesh_text), anims


def swap_md5_joints(mesh_text, anims):
    lines = mesh_text.split("\n")
    start = next(i for i, line in enumerate(lines) if line.strip().startswith("joints"))
    first, second = start + 2, start + 3
    lines[first], lines[second] = lines[second], lines[first]
    return "\n".join(lines), anims


def flip_iqm_winding(data):
    model = parse_iqm(data)
    out = bytearray(data)
    for index, (a, b, c) in enumerate(model["triangles"]):
        struct.pack_into("<3I", out, model["header"]["ofs_triangles"] + index * 12, a, c, b)
    return bytes(out)


def swap_iqm_joints(data):
    h = IQM_HEADER.unpack_from(data, 0)
    header = dict(zip(IQM_FIELDS, h[1:]))
    out = bytearray(data)
    first, second = header["ofs_joints"] + 48, header["ofs_joints"] + 96
    out[first:first + 48], out[second:second + 48] = data[second:second + 48], data[first:first + 48]
    return bytes(out)


def flip_ase_winding(text):
    """Swaps B and C of every face and of its texture face, so only the winding changes."""
    text = re.sub(r"(A:\s+\d+\s+)B:(\s+)(\d+)(\s+)C:(\s+)(\d+)", r"\1B:\2\6\4C:\5\3", text)
    return re.sub(r"(\*MESH_TFACE\s+\d+\s+\d+\s+)(\d+)(\s+)(\d+)", r"\1\4\3\2", text)


def controls(definition, directory):
    """Each control: (name, the check that must fail, a check that must still pass, results)."""
    first_clip = definition["clips"][0]["name"]
    return [
        ("md5mesh-flipped-winding", "md5mesh-winding", "md5mesh-bind", md5_checks(definition, directory, mutate=flip_md5_winding)),
        ("md5-positive-w", f"md5anim-poses-{first_clip}", "md5mesh-hierarchy", md5_checks(definition, directory, negative_w=True)),
        ("md5mesh-swapped-joints", "md5mesh-hierarchy", "md5mesh-material", md5_checks(definition, directory, mutate=swap_md5_joints)),
        ("iqm-flipped-winding", "iqm-winding", "iqm-bind", iqm_checks(definition, directory, mutate=flip_iqm_winding)),
        ("iqm-swapped-joints", "iqm-hierarchy", "iqm-material", iqm_checks(definition, directory, mutate=swap_iqm_joints)),
        ("iqm-conjugate-rotation", "iqm-poses", "iqm-bind", iqm_checks(definition, directory, conjugate=True)),
        ("ase-flipped-winding", "ase-winding", "ase-geometry", ase_checks(definition, directory, mutate=flip_ase_winding)),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--fixtures", required=True, type=Path, help="folder with one subfolder per fixture")
    parser.add_argument("--receipt", type=Path, help="write the checks, controls and file hashes as JSON here")
    args = parser.parse_args()
    receipt, ok = {"fixtures": {}}, True
    fixtures = sorted(p.parent for p in args.fixtures.glob("*/definition.json"))
    if not fixtures:
        print(f"FAIL: no fixtures under {args.fixtures}")
        return 1
    for directory in fixtures:
        definition = json.loads((directory / "definition.json").read_text(encoding="utf-8"))
        name = definition["name"]
        entry = {"files": {}, "checks": {}, "controls": {}}
        for path in sorted(directory.iterdir()):
            if path.name != "definition.json" and path.is_file():
                entry["files"][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
        checks = {}
        for group in (md5_checks(definition, directory), iqm_checks(definition, directory), ase_checks(definition, directory)):
            checks.update(group)
        for check, (passed, detail) in sorted(checks.items()):
            print(f"{'PASS' if passed else 'FAIL'}: {name} {check}: {detail}")
            ok &= passed
            entry["checks"][check] = {"passed": passed, "detail": detail}
        for control, target, unaffected, results in controls(definition, directory):
            caught = not results[target][0]
            independent = results[unaffected][0]
            good = caught and independent
            print(f"{'PASS' if good else 'FAIL'}: {name} control {control}: {target} "
                  f"{'fails' if caught else 'still passes'}, {unaffected} {'passes' if independent else 'fails too'}")
            if caught:
                print(f"      ({results[target][1]})")
            ok &= good
            entry["controls"][control] = {"target": target, "caught": caught, "unaffected": unaffected, "independent": independent,
                                          "detail": results[target][1]}
        receipt["fixtures"][name] = entry
    receipt["passed"] = ok
    if args.receipt:
        args.receipt.write_text(json.dumps(receipt, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print("model skeletal writer oracle: " + ("every check passed and every control was caught" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
