"""BSP29, BSP2 and IBSP46 study readers on project-authored synthetic bytes only.

No stock map, archive, asset or external parser is read. Every fixture below is
packed in this file from small hand-written records (a 128-unit floor face, one
leaf, one cluster), and every corrupt envelope must raise FormatError.
"""

import copy
import json
import math
import struct
import sys
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT))

from tools.map_study.binary import FormatError
from tools.map_study import bsp29, ibsp46
from tools.map_study.bsp29 import Q1Bsp
from tools.map_study.ibsp46 import Q3Bsp
from tools.map_study.stock_survey import survey

Q1_ENTITIES = (b'{"classname" "worldspawn" "message" "synthetic b1 fixture" "wad" "none.wad"}\n'
               b'{"classname" "info_player_start" "origin" "0 0 24"}\n'
               b'{"classname" "light" "origin" "0 0 48" "light" "200"}\n\0')


def q1_records():
    """Structured records for a single lit floor face over one empty leaf."""
    return {
        "entities": Q1_ENTITIES,
        "planes": [(0.0, 0.0, 1.0, 0.0, 2)],
        "textures": [("b1_floor", 16, 16, True)],  # (name, width, height, embedded pixels) or None
        "vertices": [(-64, -64, 0), (64, -64, 0), (64, 64, 0), (-64, 64, 0)],
        "visibility": b"\x01",  # leaf 1 sees itself
        "nodes": [(0, -2, -1, -64, -64, -16, 64, 64, 64, 0, 1)],
        "texinfo": [(1, 0, 0, 0, 0, 1, 0, 0, 0, 0)],
        "faces": [(0, 0, 0, 4, 0, 0, 255, 255, 255, 0)],
        "lighting": bytes(81),  # 9 x 9 samples for a 128-unit face at 16-unit luxels
        "clipnodes": [(0, -1, -2), (0, -1, -2)],
        "leafs": [(-2, -1, -64, -64, -16, 64, 64, 64, 0, 0, 0, 0, 0, 0),
                  (-1, 0, -64, -64, 0, 64, 64, 64, 0, 1, 0, 0, 0, 0)],
        "marksurfaces": [0],
        "edges": [(0, 0), (0, 1), (1, 2), (2, 3), (3, 0)],
        "surfedges": [1, 2, 3, 4],
        "models": [(-64, -64, -16, 64, 64, 64, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1)],
    }


def miptex_lump(textures):
    count = len(textures)
    table = []
    body = b""
    base = 4 + 4 * count
    for texture in textures:
        if texture is None:
            table.append(-1)
            continue
        name, width, height, embedded = texture
        table.append(base + len(body))
        mips = [0, 0, 0, 0]
        pixels = b""
        if embedded:
            offset = 40
            for level in range(4):
                mips[level] = offset
                size = (width >> level) * (height >> level)
                pixels += bytes(size)
                offset += size
        body += struct.pack("<16s2I4I", name.encode(), width, height, *mips) + pixels
    return struct.pack(f"<i{count}i", count, *table) + body


def pack_lumps(prefix, lumps, header_size):
    body = b""
    table = b""
    for lump in lumps:
        table += struct.pack("<ii", header_size + len(body), len(lump))
        body += lump
    return prefix + table + body


def q1_bytes(records=None, fmt="bsp29", magic=None):
    r = records or q1_records()
    layout = bsp29.LAYOUTS[fmt]
    pack = lambda f, rows: b"".join(struct.pack(f, *row) for row in rows)
    if fmt == "bsp29":
        clip = [(p, a & 0xFFFF, b & 0xFFFF) for p, a, b in r["clipnodes"]]
    else:
        clip = r["clipnodes"]
    lumps = [r["entities"], pack("<4fi", r["planes"]),
             r["textures"] if isinstance(r["textures"], bytes) else miptex_lump(r["textures"]),
             pack("<3f", r["vertices"]), r["visibility"], pack(layout["node"], r["nodes"]),
             pack("<8f2i", r["texinfo"]), pack(layout["face"], r["faces"]), r["lighting"],
             pack(layout["clipnode"], clip), pack(layout["leaf"], r["leafs"]),
             pack(layout["marksurface"], [(m,) for m in r["marksurfaces"]]), pack(layout["edge"], r["edges"]),
             pack("<i", [(s,) for s in r["surfedges"]]), pack("<9f7i", r["models"])]
    if magic is None:
        magic = struct.pack("<i", 29) if fmt == "bsp29" else b"BSP2"
    return pack_lumps(magic, lumps, bsp29.HEADER_SIZE)


def q3_records():
    planes = [(1, 0, 0, 64), (-1, 0, 0, 64), (0, 1, 0, 64), (0, -1, 0, 64), (0, 0, 1, 0), (0, 0, -1, 16)]
    verts = [(-64, -64, 0, 0, 0, 0, 0, 0, 0, 1, 255, 255, 255, 255),
             (64, -64, 0, 1, 0, 1, 0, 0, 0, 1, 255, 255, 255, 255),
             (64, 64, 0, 1, 1, 1, 1, 0, 0, 1, 255, 255, 255, 255),
             (-64, 64, 0, 0, 1, 0, 1, 0, 0, 1, 255, 255, 255, 255)]
    surface = [0, -1, 1, 0, 4, 0, 6, 0, 0, 0, 8, 8] + [0.0] * 12 + [0, 0]
    return {
        "entities": (b'{"classname" "worldspawn" "message" "synthetic b1 q3 fixture"}\n'
                     b'{"classname" "info_player_deathmatch" "origin" "0 0 24"}\n\0'),
        "shaders": [("textures/b1/floor", 0, 1), ("textures/b1/unused", 0, 0x10000)],
        "planes": planes,
        "nodes": [(4, -2, -1, -64, -64, -16, 64, 64, 64)],
        "leafs": [(-1, -1, -64, -64, -16, 64, 64, 0, 0, 0, 0, 1),
                  (0, 0, -64, -64, 0, 64, 64, 64, 0, 1, 0, 0)],
        "leafsurfaces": [0],
        "leafbrushes": [0],
        "models": [(-64, -64, -16, 64, 64, 64, 0, 1, 0, 1)],
        "brushes": [(0, 6, 0)],
        "brushsides": [(i, 0) for i in range(6)],
        "drawverts": verts,
        "drawindexes": [0, 1, 2, 0, 2, 3],
        "fogs": [],
        "surfaces": [surface],
        "lightmaps": bytes(ibsp46.LIGHTMAP_BYTES),
        "lightgrid": bytes(9 * 8),  # 3 x 3 x 1 cells for the default 64 64 128 grid
        "visibility": struct.pack("<2i", 1, 8) + b"\x01" + bytes(7),
    }


def q3_bytes(records=None, magic=b"IBSP", version=46):
    r = records or q3_records()
    pack = lambda f, rows: b"".join(struct.pack(f, *row) for row in rows)
    lumps = [r["entities"], pack("<64sii", [(n.encode(), a, b) for n, a, b in r["shaders"]]),
             pack("<4f", r["planes"]), pack("<9i", r["nodes"]), pack("<12i", r["leafs"]),
             pack("<i", [(x,) for x in r["leafsurfaces"]]), pack("<i", [(x,) for x in r["leafbrushes"]]),
             pack("<6f4i", r["models"]), pack("<3i", r["brushes"]), pack("<2i", r["brushsides"]),
             pack("<10f4B", r["drawverts"]), pack("<i", [(x,) for x in r["drawindexes"]]),
             pack("<64sii", [(n.encode(), a, b) for n, a, b in r["fogs"]]), pack("<12i12f2i", r["surfaces"]),
             r["lightmaps"], r["lightgrid"], r["visibility"]]
    return pack_lumps(struct.pack("<4si", magic, version), lumps, ibsp46.HEADER_SIZE)


def set_lump(data, index, offset, size, table_start):
    data = bytearray(data)
    struct.pack_into("<ii", data, table_start + index * 8, offset, size)
    return bytes(data)


class Q1ReaderTests(unittest.TestCase):
    def test_bsp29_fixture_parses_with_descriptive_views(self):
        bsp = Q1Bsp(q1_bytes())
        summary = bsp.summary()
        self.assertEqual(summary["format"], "bsp29")
        self.assertEqual(summary["world_bounds"], {"mins": [-64.0, -64.0, -16.0], "maxs": [64.0, 64.0, 64.0]})
        self.assertEqual(summary["entities"]["count"], 3)
        self.assertEqual(summary["entities"]["classnames"], {"info_player_start": 1, "light": 1, "worldspawn": 1})
        self.assertEqual([e["classname"] for e in bsp.entities], ["worldspawn", "info_player_start", "light"])
        self.assertEqual(summary["textures"]["face_histogram"], {"b1_floor": 1})
        self.assertEqual((summary["textures"]["slots"], summary["textures"]["missing"],
                          summary["textures"]["external"]), (1, 0, 0))
        self.assertEqual(summary["leaf_contents"], {"empty": 1})
        self.assertEqual(bsp.clipnodes[0], (0, -1, -2))
        vis = summary["visibility"]
        self.assertTrue(vis["decoded"])
        self.assertEqual((vis["visleafs"], vis["rows_decoded"], vis["rows_missing_self"]), (1, 1, 0))
        self.assertEqual(vis["mean_visible_fraction"], 1.0)
        light = summary["lighting"]
        self.assertEqual((light["bytes"], light["lit_faces"], light["max_sample_end"]), (81, 1, 81))
        self.assertEqual(light["style_histogram"], {"0": 1})
        self.assertEqual(light["faces_past_lump_end"], 0)
        self.assertEqual(summary["issues"], [])
        self.assertEqual(summary["counts"]["models"], 1)
        json.dumps(summary, allow_nan=False)

    def test_bsp2_matches_bsp29_descriptors(self):
        small, large = Q1Bsp(q1_bytes()).summary(), Q1Bsp(q1_bytes(fmt="bsp2")).summary()
        self.assertEqual(large["format"], "bsp2")
        for key in ("world_bounds", "entities", "textures", "leaf_contents", "visibility", "lighting", "counts",
                    "issues"):
            self.assertEqual(small[key], large[key], key)
        self.assertNotEqual(small["sha256"], large["sha256"])

    def test_vis_rle_and_overruns(self):
        r = q1_records()
        # Three world leafs; leaf 1 row is RLE: literal 0b101, then a zero run of one byte (row is 1 byte).
        r["leafs"] = [r["leafs"][0]] + [(-1, 0, -64, -64, 0, 64, 64, 64, 0, 1, 0, 0, 0, 0)] * 3
        r["models"] = [(-64, -64, -16, 64, 64, 64, 0, 0, 0, 0, 0, 1, 0, 3, 0, 1)]
        r["visibility"] = b"\x05"
        vis = Q1Bsp(q1_bytes(r)).summary()["visibility"]
        self.assertEqual((vis["rows_decoded"], vis["distinct_rows"]), (3, 1))
        self.assertEqual(vis["rows_missing_self"], 1)  # leaf 2 is not in the shared row
        self.assertAlmostEqual(vis["mean_visible_fraction"], round(2 / 3, 6))
        r["visibility"] = b"\x00\x03"  # zero run longer than the one-byte row
        bsp = Q1Bsp(q1_bytes(r))
        self.assertEqual(bsp.visibility["run_overruns"], 3)
        self.assertIn("vis_overrun", bsp.summary()["issue_counts"])

    def test_tolerated_oddities_are_issues_not_rejections(self):
        r = q1_records()
        r["textures"] = [("b1_floor", 24, 16, False), None]
        r["texinfo"].append((1, 0, 0, 0, 0, 1, 0, 0, 1, 0))
        r["faces"][0] = (0, 0, 0, 4, 1, 0, 255, 255, 255, 0)
        summary = Q1Bsp(q1_bytes(r)).summary()
        self.assertEqual(summary["textures"]["face_histogram"], {"<missing>": 1})
        self.assertEqual((summary["textures"]["missing"], summary["textures"]["external"]), (1, 1))
        self.assertEqual(set(summary["issue_counts"]), {"missing_texture", "unaligned_texture"})

        r = q1_records()
        r["visibility"] = b""
        r["lighting"] = b""
        summary = Q1Bsp(q1_bytes(r)).summary()
        self.assertEqual(set(summary["issue_counts"]), {"vis_without_lump", "light_without_lump"})
        self.assertFalse(summary["visibility"]["decoded"])
        self.assertEqual(summary["lighting"]["lit_faces"], 0)

        r = q1_records()
        r["lighting"] = bytes(40)
        summary = Q1Bsp(q1_bytes(r)).summary()
        self.assertEqual(summary["lighting"]["faces_past_lump_end"], 1)
        self.assertIn("light_overrun", summary["issue_counts"])

        r = q1_records()
        r["leafs"][1] = (-9, 0, -64, -64, 0, 64, 64, 64, 0, 1, 0, 0, 0, 0)
        r["leafs"].append((-42, -1, -64, -64, 0, 64, 64, 64, 0, 0, 0, 0, 0, 0))
        summary = Q1Bsp(q1_bytes(r)).summary()
        self.assertEqual(summary["issue_counts"], {"leaf_contents": 1})

    def test_variant_magic_rejected(self):
        for magic, message in ((b"2PSB", "2PSB"), (b"IBSP", "IBSP"), (struct.pack("<i", 30), "expected")):
            with self.assertRaisesRegex(FormatError, message):
                Q1Bsp(q1_bytes(magic=magic))
        with self.assertRaises(FormatError):
            Q3Bsp(q1_bytes())

    def test_corrupt_envelopes_rejected(self):
        good = q1_bytes()
        table = 4
        corrupt = {
            "truncated header": good[:100],
            "lump past end": set_lump(good, 1, len(good) - 4, 20, table),
            "lump in header": set_lump(good, 1, 8, 20, table),
            "overlapping lumps": set_lump(good, 3, struct.unpack_from("<i", good, table + 8)[0], 12, table),
            "partial record": set_lump(good, 1, struct.unpack_from("<i", good, table + 8)[0], 19, table),
        }
        mutations = {
            "non-unit plane": lambda r: r.__setitem__("planes", [(0.0, 0.0, 2.0, 0.0, 2)]),
            "face plane": lambda r: r["faces"].__setitem__(0, (3, 0, 0, 4, 0, 0, 255, 255, 255, 0)),
            "face side": lambda r: r["faces"].__setitem__(0, (0, 2, 0, 4, 0, 0, 255, 255, 255, 0)),
            "face edges": lambda r: r["faces"].__setitem__(0, (0, 0, 0, 2, 0, 0, 255, 255, 255, 0)),
            "face edge range": lambda r: r["faces"].__setitem__(0, (0, 0, 2, 4, 0, 0, 255, 255, 255, 0)),
            "face texinfo": lambda r: r["faces"].__setitem__(0, (0, 0, 0, 4, 5, 0, 255, 255, 255, 0)),
            "light offset": lambda r: r["faces"].__setitem__(0, (0, 0, 0, 4, 0, 0, 255, 255, 255, 500)),
            "surfedge": lambda r: r["surfedges"].__setitem__(0, 9),
            "edge vertex": lambda r: r["edges"].__setitem__(1, (0, 9)),
            "texinfo miptex": lambda r: r["texinfo"].__setitem__(0, (1, 0, 0, 0, 0, 1, 0, 0, 4, 0)),
            "marksurface": lambda r: r.__setitem__("marksurfaces", [3]),
            "leaf marksurfaces": lambda r: r["leafs"].__setitem__(1, (-1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0)),
            "leaf vis offset": lambda r: r["leafs"].__setitem__(1, (-1, 7, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0)),
            "vis truncated": lambda r: r.__setitem__("visibility", b"\x00"),
            "visleafs": lambda r: r.__setitem__("models", [(-64, -64, -16, 64, 64, 64, 0, 0, 0, 0, 0, 1, 0, 9, 0, 1)]),
            "model faces": lambda r: r.__setitem__("models", [(-64, -64, -16, 64, 64, 64, 0, 0, 0, 0, 0, 1, 0, 1, 0, 2)]),
            "node faces": lambda r: r["nodes"].__setitem__(0, (0, -2, -1, 0, 0, 0, 0, 0, 0, 0, 3)),
            "cyclic node": lambda r: r["nodes"].__setitem__(0, (0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 1)),
            "node leaf": lambda r: r["nodes"].__setitem__(0, (0, -9, -1, 0, 0, 0, 0, 0, 0, 0, 1)),
            "clipnode child": lambda r: r["clipnodes"].__setitem__(0, (0, 7, -2)),
            "clipnode plane": lambda r: r["clipnodes"].__setitem__(0, (4, -1, -2)),
            "texture count": lambda r: r.__setitem__("textures", struct.pack("<ii", 5, 8)),
            "miptex header": lambda r: r.__setitem__("textures", struct.pack("<ii", 1, 400)),
            "miptex level": lambda r: r.__setitem__("textures", struct.pack("<ii16s2I4I", 1, 8, b"x", 16, 16,
                                                                            40, 296, 360, 9999) + bytes(340)),
            "miptex size": lambda r: r.__setitem__("textures", [("b1_floor", 0, 16, False)]),
            "entities": lambda r: r.__setitem__("entities", b'{"classname" "light"}'),
            "entity syntax": lambda r: r.__setitem__("entities", b'{"classname" "worldspawn"'),
            "no faces": lambda r: (r.__setitem__("faces", []), r.__setitem__("marksurfaces", []),
                                   r["leafs"].__setitem__(1, (-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)),
                                   r["nodes"].__setitem__(0, (0, -2, -1, 0, 0, 0, 0, 0, 0, 0, 0)),
                                   r.__setitem__("models", [(-64, -64, -16, 64, 64, 64, 0, 0, 0, 0, 0, 1, 0,
                                                             1, 0, 0)])),
        }
        for fmt in ("bsp29", "bsp2"):
            for name, mutate in mutations.items():
                records = copy.deepcopy(q1_records())
                mutate(records)
                corrupt[f"{fmt} {name}"] = q1_bytes(records, fmt=fmt)
        for name, data in corrupt.items():
            with self.subTest(name), self.assertRaises(FormatError):
                Q1Bsp(data)
        self.assertGreaterEqual(len(corrupt), 60)

    def test_summary_is_deterministic(self):
        first = json.dumps(Q1Bsp(q1_bytes()).summary(), sort_keys=True)
        self.assertEqual(first, json.dumps(Q1Bsp(q1_bytes()).summary(), sort_keys=True))


class Q3ReaderTests(unittest.TestCase):
    def test_ibsp46_fixture_parses_with_descriptive_views(self):
        bsp = Q3Bsp(q3_bytes())
        summary = bsp.summary()
        self.assertEqual(summary["format"], "ibsp46")
        self.assertEqual(summary["world_bounds"], {"mins": [-64.0, -64.0, -16.0], "maxs": [64.0, 64.0, 64.0]})
        self.assertEqual(summary["entities"]["classnames"], {"info_player_deathmatch": 1, "worldspawn": 1})
        self.assertEqual(summary["textures"]["surface_histogram"], {"textures/b1/floor": 1})
        self.assertEqual(summary["textures"]["brush_histogram"], {"textures/b1/floor": 1})
        self.assertEqual(summary["surfaces"], {"types": {"planar": 1}, "triangles": 2})
        self.assertEqual((summary["counts"]["brushes"], summary["counts"]["brushsides"]), (1, 6))
        self.assertEqual(summary["visibility"]["clusters"], 1)
        self.assertEqual(summary["visibility"]["mean_visible_fraction"], 1.0)
        grid = summary["lighting"]["lightgrid"]
        self.assertEqual((grid["bounds"], grid["cells_expected"], grid["consistent"]), ([3, 3, 1], 9, True))
        self.assertEqual(summary["lighting"]["lightmaps"], 1)
        self.assertEqual(summary["issues"], [])
        json.dumps(summary, allow_nan=False)

    def test_tolerated_oddities_are_issues_not_rejections(self):
        cases = {
            "lightgrid": lambda r: r.__setitem__("lightgrid", bytes(64)),
            "external_lightmaps": lambda r: r.__setitem__("lightmaps", b""),
            "surface_fog": lambda r: r["surfaces"][0].__setitem__(1, 0),
            "leaf_cluster": lambda r: r["leafs"].__setitem__(1, (3, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0)),
            "drawvert_lightmap_st": lambda r: r["drawverts"].__setitem__(
                0, (-64, -64, 0, 0, 0, math.nan, 0, 0, 0, 1, 255, 255, 255, 255)),
            "vis_self": lambda r: r.__setitem__("visibility", struct.pack("<2i", 1, 8) + bytes(8)),
            "vis_tail": lambda r: r.__setitem__("visibility", struct.pack("<2i", 1, 8) + b"\x01" + bytes(9)),
        }
        for issue, mutate in cases.items():
            records = copy.deepcopy(q3_records())
            mutate(records)
            with self.subTest(issue):
                self.assertEqual(set(Q3Bsp(q3_bytes(records)).summary()["issue_counts"]), {issue})
        records = q3_records()
        records["entities"] = (b'{"classname" "worldspawn" "gridsize" "128 128 256"}\n'
                               b'{"classname" "info_player_deathmatch" "origin" "0 0 24"}\n')
        records["lightgrid"] = bytes(8)  # 128-unit cells snap the 128-unit world to a single grid point per axis
        grid = Q3Bsp(q3_bytes(records)).lightgrid
        self.assertEqual((grid["grid_size"], grid["bounds"], grid["consistent"]),
                         ([128.0, 128.0, 256.0], [1, 1, 1], True))

    def test_patch_surfaces(self):
        records = q3_records()
        records["surfaces"][0] = [0, -1, 2, 0, 4, 0, 0, -1, 0, 0, 0, 0] + [0.0] * 12 + [2, 2]
        summary = Q3Bsp(q3_bytes(records)).summary()
        self.assertEqual(summary["surfaces"], {"types": {"patch": 1}, "triangles": 0})
        self.assertEqual(set(summary["issue_counts"]), {"patch_shape"})

    def test_wrong_magic_or_version_rejected(self):
        with self.assertRaisesRegex(FormatError, "version 46"):
            Q3Bsp(q3_bytes(version=38))
        with self.assertRaisesRegex(FormatError, "magic"):
            Q3Bsp(q3_bytes(magic=b"RBSP"))
        with self.assertRaisesRegex(FormatError, "IBSP"):
            Q1Bsp(q3_bytes())

    def test_corrupt_envelopes_rejected(self):
        good = q3_bytes()
        table = 8
        surface = lambda fields: [fields.get(i, v) for i, v in enumerate(q3_records()["surfaces"][0])]
        corrupt = {
            "truncated header": good[:120],
            "lump past end": set_lump(good, 2, len(good) - 4, 16, table),
            "lump in header": set_lump(good, 2, 16, 16, table),
            "overlapping lumps": set_lump(good, 3, struct.unpack_from("<i", good, table + 16)[0], 36, table),
        }
        mutations = {
            "non-unit plane": lambda r: r["planes"].__setitem__(4, (0, 0, 3, 0)),
            "brush sides": lambda r: r.__setitem__("brushes", [(0, 7, 0)]),
            "brush shader": lambda r: r.__setitem__("brushes", [(0, 6, 2)]),
            "brushside plane": lambda r: r["brushsides"].__setitem__(0, (6, 0)),
            "brushside shader": lambda r: r["brushsides"].__setitem__(0, (0, -1)),
            "leaf surface index": lambda r: r.__setitem__("leafsurfaces", [1]),
            "leaf brush index": lambda r: r.__setitem__("leafbrushes", [1]),
            "leaf range": lambda r: r["leafs"].__setitem__(1, (0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0)),
            "model surfaces": lambda r: r.__setitem__("models", [(-64, -64, -16, 64, 64, 64, 0, 2, 0, 1)]),
            "model brushes": lambda r: r.__setitem__("models", [(-64, -64, -16, 64, 64, 64, 0, 1, 1, 1)]),
            "surface shader": lambda r: r.__setitem__("surfaces", [surface({0: 5})]),
            "surface fog below -1": lambda r: r.__setitem__("surfaces", [surface({1: -2})]),
            "surface type 0": lambda r: r.__setitem__("surfaces", [surface({2: 0})]),
            "surface type 5": lambda r: r.__setitem__("surfaces", [surface({2: 5})]),
            "surface vertices": lambda r: r.__setitem__("surfaces", [surface({4: 5})]),
            "surface indexes": lambda r: r.__setitem__("surfaces", [surface({5: 3, 6: 6})]),
            "index multiple": lambda r: r.__setitem__("surfaces", [surface({6: 4})]),
            "draw index": lambda r: r["drawindexes"].__setitem__(2, 4),
            "negative draw index": lambda r: r["drawindexes"].__setitem__(2, -1),
            "patch grid": lambda r: r.__setitem__("surfaces", [surface({2: 2, 24: 3, 25: 3})]),
            "lightmap index": lambda r: r.__setitem__("surfaces", [surface({7: 1})]),
            "drawvert position": lambda r: r["drawverts"].__setitem__(
                0, (math.inf, -64, 0, 0, 0, 0, 0, 0, 0, 1, 255, 255, 255, 255)),
            "fog brush": lambda r: r.__setitem__("fogs", [("textures/b1/fog", 1, -1)]),
            "fog side": lambda r: r.__setitem__("fogs", [("textures/b1/fog", 0, 6)]),
            "vis rows": lambda r: r.__setitem__("visibility", struct.pack("<2i", 2, 8) + bytes(8)),
            "vis row size": lambda r: r.__setitem__("visibility", struct.pack("<2i", 16, 1) + bytes(16)),
            "vis header": lambda r: r.__setitem__("visibility", b"\x01\x00"),
            "cyclic node": lambda r: r.__setitem__("nodes", [(4, 0, -1, 0, 0, 0, 0, 0, 0)]),
            "node leaf": lambda r: r.__setitem__("nodes", [(4, -3, -1, 0, 0, 0, 0, 0, 0)]),
            "node plane": lambda r: r.__setitem__("nodes", [(6, -2, -1, 0, 0, 0, 0, 0, 0)]),
            "no nodes": lambda r: r.__setitem__("nodes", []),
            "no shaders": lambda r: (r.__setitem__("shaders", []), r.__setitem__("brushes", []),
                                     r.__setitem__("brushsides", []), r.__setitem__("surfaces", [])),
            "entities": lambda r: r.__setitem__("entities", b'{"classname" "light"}'),
        }
        for name, mutate in mutations.items():
            records = copy.deepcopy(q3_records())
            mutate(records)
            corrupt[name] = q3_bytes(records)
        for name, data in corrupt.items():
            with self.subTest(name), self.assertRaises(FormatError):
                Q3Bsp(data)
        self.assertGreaterEqual(len(corrupt), 35)


class SurveyTests(unittest.TestCase):
    def test_survey_records_provenance_coverage_and_no_raw_lumps(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "maps"
            (root / "test").mkdir(parents=True)
            (root / "alpha.bsp").write_bytes(q1_bytes())
            (root / "test" / "beta.bsp").write_bytes(q1_bytes(fmt="bsp2"))
            (root / "broken.bsp").write_bytes(q1_bytes()[:90])
            (root / "notes.txt").write_text("ignored")
            lit = struct.pack("<4si", b"QLIT", 1) + bytes(3 * 81)
            (root / "alpha.lit").write_bytes(lit)
            outputs = []
            for run in ("a", "b"):
                out = Path(temporary) / run
                coverage = survey("q1", [("stock-id/q1/id1", root)], out)
                outputs.append({p.relative_to(out).as_posix(): p.read_bytes() for p in sorted(out.rglob("*"))
                                if p.is_file()})
            self.assertEqual(outputs[0], outputs[1])
            self.assertEqual(coverage["totals"], {"files": 3, "parsed": 2, "rejected": 1, "error": 0})
            self.assertEqual(set(coverage["groups"]), {"stock-id/q1/id1", "stock-id/q1/id1/test"})
            self.assertEqual(coverage["groups"]["stock-id/q1/id1/test"]["formats"], {"bsp2": 1})
            rejected = [row for row in coverage["rows"] if row["status"] == "rejected"]
            self.assertEqual([row["map_id"] for row in rejected], ["stock-id/q1/id1/broken"])
            self.assertTrue(rejected[0]["reason"])
            record = json.loads(outputs[0]["maps/stock-id/q1/id1/alpha.json"])
            self.assertEqual(record["provenance"]["class"], "stock-id")
            self.assertIs(record["provenance"]["training_eligible"], False)
            self.assertEqual(record["provenance"]["licence"], "commercial EULA, local reference only")
            self.assertEqual(len(record["sha256"]), 64)
            self.assertTrue(record["summary"]["lighting"]["lit_sidecar"]["consistent"])
            self.assertIn("maps/stock-id/q1/id1/test/beta.json", outputs[0])
            self.assertNotIn("maps/stock-id/q1/id1/broken.json", outputs[0])
            for content in outputs[0].values():
                self.assertNotIn(b"synthetic b1 fixture", content)  # entity text is never written
            self.assertIn(b"| stock-id/q1/id1/broken | - |", outputs[0]["coverage.md"])

    def test_survey_q3_and_label_guard(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "maps"
            root.mkdir()
            (root / "gamma.bsp").write_bytes(q3_bytes())
            (root / "delta.bsp").write_bytes(q3_bytes(version=47))
            coverage = survey("q3", [("stock-id/q3/baseq3", root)], Path(temporary) / "out")
            self.assertEqual(coverage["totals"], {"files": 2, "parsed": 1, "rejected": 1, "error": 0})
            with self.assertRaises(ValueError):
                survey("q3", [("../escape", root)], Path(temporary) / "out2")


if __name__ == "__main__":
    unittest.main()
