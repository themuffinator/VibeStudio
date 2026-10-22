"""Independent bounded reader for the Quake BSP29 and BSP2 file envelopes.

Format references (read, not copied): id Software's GPL Quake release
(bspfile.h record contract, model.c load rules) for BSP29, and ericw-tools'
public BSP2 record contract (include/common/bspfile_q1.hh) for the 32-bit
"BSP2" variant. No engine, compiler or third-party parser code is copied,
translated or linked.

Like binary.py, this reader never extracts archives, decompiles brushes,
exports texture pixels or lightmaps, or executes map properties. Every index
and range is validated and a structural fault raises FormatError. Oddities a
stock engine tolerates (missing textures, unaligned texture sizes, unusual
leaf contents, light samples past the lump end) are recorded in `issues`.
"""

from __future__ import annotations

import math
import struct
from collections import Counter

from .binary import (MAX_FILE_BYTES, MAX_GEOMETRY_REFERENCES, FormatError, Lumps, check_index, check_range,
                     decode, dot, entities, sha256, validate_tree)

BSP29_VERSION = 29
BSP2_MAGIC = b"BSP2"
BSP2RMQ_MAGIC = b"2PSB"
LUMP_COUNT = 15
HEADER_SIZE = 4 + LUMP_COUNT * 8
LUMP_NAMES = ("entities", "planes", "textures", "vertices", "visibility", "nodes", "texinfo", "faces",
              "lighting", "clipnodes", "leafs", "marksurfaces", "edges", "surfedges", "models")
(ENTITIES, PLANES, TEXTURES, VERTICES, VISIBILITY, NODES, TEXINFO, FACES, LIGHTING, CLIPNODES, LEAFS,
 MARKSURFACES, EDGES, SURFEDGES, MODELS) = range(LUMP_COUNT)

# Field order is identical across both variants, so row indices below are shared:
# node (plane, child0, child1, mins[3], maxs[3], firstface, numfaces)
# face (plane, side, firstedge, numedges, texinfo, style[4], lightofs)
# leaf (contents, visofs, mins[3], maxs[3], firstmarksurface, nummarksurfaces, ambient[4])
LAYOUTS = {
    "bsp29": {"node": "<i2h6h2H", "clipnode": "<i2H", "face": "<hhihh4Bi", "edge": "<HH",
              "leaf": "<ii6h2H4B", "marksurface": "<H"},
    "bsp2": {"node": "<i2i6f2I", "clipnode": "<i2i", "face": "<iiiii4Bi", "edge": "<II",
             "leaf": "<ii6f2I4B", "marksurface": "<I"},
}
PLANE = "<4fi"
VERTEX = "<3f"
TEXINFO_FORMAT = "<8f2i"
MODEL = "<9f7i"  # mins[3], maxs[3], origin[3], headnode[4], visleafs, firstface, numfaces
MIPTEX = "<16s2I4I"
MIPTEX_SIZE = struct.calcsize(MIPTEX)
TEX_SPECIAL = 1
NO_STYLE = 255
CONTENTS_NAMES = {-1: "empty", -2: "solid", -3: "water", -4: "slime", -5: "lava", -6: "sky"}
# Contents values a compiler may legitimately emit beyond the 1996 set (clip, currents).
EXTENDED_CONTENTS = set(range(-14, -7))
MAX_TEXTURES = 8192
MAX_TEXTURE_SIDE = 8192
MAX_VIS_DECODE_BYTES = 128 * 1024 * 1024
MAX_ISSUES = 64


class Q1Bsp(Lumps):
    """Bounded BSP29/BSP2 study reader. `format` is "bsp29" or "bsp2"."""

    def __init__(self, data: bytes):
        if len(data) > MAX_FILE_BYTES:
            raise FormatError(f"file exceeds {MAX_FILE_BYTES} bytes")
        if len(data) < HEADER_SIZE:
            raise FormatError("file shorter than the BSP29/BSP2 header")
        magic = data[:4]
        if magic == BSP2_MAGIC:
            self.format = "bsp2"
        elif struct.unpack_from("<i", data)[0] == BSP29_VERSION:
            self.format = "bsp29"
        elif magic == BSP2RMQ_MAGIC:
            raise FormatError("2PSB (BSP2 RMQ) variant is not supported by this reader")
        elif magic == b"IBSP":
            raise FormatError("IBSP file is not a Quake BSP29/BSP2 map")
        else:
            raise FormatError("expected Quake BSP29 version 29 or BSP2")
        super().__init__(data, data, 4, LUMP_COUNT, HEADER_SIZE)
        layout = LAYOUTS[self.format]
        self.sha256 = sha256(data)
        self.size = len(data)
        self.issues: list[str] = []
        self._issue_counts: Counter = Counter()
        self.entity_issues: list[str] = []
        self.entities = entities(self.lumps[ENTITIES], self.entity_issues)
        self.planes = self.records(PLANES, PLANE)
        self.vertices = self.records(VERTICES, VERTEX)
        self.nodes = self.records(NODES, layout["node"])
        self.texinfo = self.records(TEXINFO, TEXINFO_FORMAT)
        self.faces = self.records(FACES, layout["face"])
        self.clipnodes = [self._clipnode(row) for row in self.records(CLIPNODES, layout["clipnode"])]
        self.leafs = self.records(LEAFS, layout["leaf"])
        self.marksurfaces = [r[0] for r in self.records(MARKSURFACES, layout["marksurface"])]
        self.edges = self.records(EDGES, layout["edge"])
        self.surfedges = [r[0] for r in self.records(SURFEDGES, "<i")]
        self.models = self.records(MODELS, MODEL)
        self.textures = self._textures()
        self._validate()
        self.visibility = self._visibility()
        self.lighting = self._lighting()
        # Ignores entity, texture, vis and light changes; includes compiler-dependent ordering.
        self.geometry_sha256 = sha256(b"".join(struct.pack("<I", len(self.lumps[i])) + self.lumps[i]
                                              for i in (PLANES, VERTICES, NODES, CLIPNODES, MARKSURFACES,
                                                        EDGES, SURFEDGES, MODELS)))

    # -- record helpers -------------------------------------------------

    def _clipnode(self, row):
        plane, front, back = row
        if self.format == "bsp29":
            # 16-bit children: values above 0xFFF0 are contents (-15..-1), the rest are indices.
            front = front - 0x10000 if front > 0xFFF0 else front
            back = back - 0x10000 if back > 0xFFF0 else back
        return plane, front, back

    def _issue(self, key, message):
        self._issue_counts[key] += 1
        if self._issue_counts[key] == 1 and len(self.issues) < MAX_ISSUES:
            self.issues.append(message)

    def _textures(self):
        data = self.lumps[TEXTURES]
        if not data:
            return []
        if len(data) < 4:
            raise FormatError("texture lump shorter than its count")
        count = struct.unpack_from("<i", data)[0]
        table_end = 4 + 4 * count if count >= 0 else -1
        if count < 0 or count > MAX_TEXTURES or table_end > len(data):
            raise FormatError("texture count outside the texture lump")
        textures = []
        for offset in struct.unpack_from(f"<{count}i", data, 4):
            if offset == -1:
                textures.append(None)
                self._issue("missing_texture", "texture slot without miptex data (engine substitutes notexture)")
                continue
            if offset < table_end or offset + MIPTEX_SIZE > len(data):
                raise FormatError("miptex header outside the texture lump")
            raw_name, width, height, *mips = struct.unpack_from(MIPTEX, data, offset)
            name = decode(raw_name.split(b"\0")[0])
            if not 0 < width <= MAX_TEXTURE_SIDE or not 0 < height <= MAX_TEXTURE_SIDE:
                raise FormatError("miptex dimensions out of range")
            if width % 16 or height % 16:
                self._issue("unaligned_texture", f"texture {name!r} is not 16-aligned (1996 engine rejects it)")
            if not name:
                self._issue("unnamed_texture", "texture with an empty name")
            external = not any(mips)
            if not external:
                for level, mip in enumerate(mips):
                    pixels = (width >> level) * (height >> level)
                    if mip < MIPTEX_SIZE or offset + mip + pixels > len(data):
                        raise FormatError("miptex level outside the texture lump")
            textures.append({"name": name, "width": width, "height": height, "external": external})
        return textures

    # -- structural validation ------------------------------------------

    def _validate(self):
        if not self.models or not self.leafs or not self.faces or not self.vertices or not self.planes:
            raise FormatError("missing world model, leafs, faces, vertices or planes")
        if sum(face[3] for face in self.faces) > MAX_GEOMETRY_REFERENCES:
            raise FormatError("aggregate geometry references exceed analysis cap")
        for plane in self.planes:
            if not 0.99 < dot(plane, plane) < 1.01:
                raise FormatError("non-unit BSP plane")
            if not 0 <= plane[4] <= 5:
                self._issue("plane_type", "plane type outside 0..5")
        for edge in self.edges:
            check_index(edge[0], len(self.vertices), "edge vertex")
            check_index(edge[1], len(self.vertices), "edge vertex")
        for index in self.surfedges:
            check_index(abs(index), len(self.edges), "surface edge")
        texture_count = len(self.textures)
        if self.texinfo and not self.lumps[TEXTURES]:
            self._issue("no_texture_lump", "texinfo present without a texture lump (engine uses notexture)")
        for info in self.texinfo:
            if self.lumps[TEXTURES]:
                check_index(info[8], texture_count, "texinfo miptex")
        has_light = bool(self.lumps[LIGHTING])
        for face in self.faces:
            check_index(face[0], len(self.planes), "face plane")
            if face[1] not in (0, 1):
                raise FormatError("invalid face side")
            if face[3] < 3:
                raise FormatError("face has fewer than three edges")
            check_range(face[2], face[3], len(self.surfedges), "face edges")
            check_index(face[4], len(self.texinfo), "face texinfo")
            if face[9] != -1:
                if has_light:
                    check_index(face[9], len(self.lumps[LIGHTING]), "face light sample")
                else:
                    self._issue("light_without_lump", "face light offsets with no lighting lump (ignored by engine)")
        for index in self.marksurfaces:
            check_index(index, len(self.faces), "leaf mark surface")
        vis_size = len(self.lumps[VISIBILITY])
        for leaf in self.leafs:
            check_range(leaf[8], leaf[9], len(self.marksurfaces), "leaf mark surfaces")
            if leaf[1] != -1:
                if vis_size:
                    check_index(leaf[1], vis_size, "leaf visibility offset")
                else:
                    # Brush-model BSPs (b_*.bsp) carry stray offsets; nothing reads them without vis data.
                    self._issue("vis_without_lump", "leaf visibility offsets with no visibility lump (unused)")
            if leaf[0] not in CONTENTS_NAMES and leaf[0] not in EXTENDED_CONTENTS:
                self._issue("leaf_contents", f"leaf contents {leaf[0]} outside the known set")
        for node in self.nodes:
            check_range(node[9], node[10], len(self.faces), "node faces")
        for model in self.models:
            check_range(model[14], model[15], len(self.faces), "model faces")
        if not 0 <= self.models[0][13] <= len(self.leafs) - 1:
            raise FormatError("world visleafs exceeds leaf count")
        validate_tree(self.nodes, [m[9] for m in self.models], len(self.planes), len(self.leafs))
        if self.clipnodes:
            # Clip hull children below zero are contents; validate_tree maps -1..-15 onto 15 slots.
            validate_tree(self.clipnodes, [h for m in self.models for h in m[10:12]], len(self.planes), 15)
        else:
            self._issue("no_clipnodes", "no clip hull nodes (hulls 1 and 2 absent)")

    # -- visibility -------------------------------------------------------

    def _decode_row(self, vis, start, row_bytes, mask):
        out = bytearray()
        position = start
        while len(out) < row_bytes:
            if position >= len(vis):
                raise FormatError("visibility row runs past the lump end")
            if vis[position]:
                limit = min(len(vis), position + row_bytes - len(out))
                end = vis.find(b"\0", position, limit)
                end = limit if end == -1 else end
                out += vis[position:end]
                position = end
            else:
                if position + 1 >= len(vis):
                    raise FormatError("visibility zero run truncated at the lump end")
                out += bytes(vis[position + 1])
                position += 2
        overrun = len(out) > row_bytes
        return int.from_bytes(out[:row_bytes], "little") & mask, overrun

    def _visibility(self):
        vis = self.lumps[VISIBILITY]
        visleafs = self.models[0][13]
        row_bytes = (visleafs + 7) >> 3
        stats = {"bytes": len(vis), "visleafs": visleafs, "row_bytes": row_bytes, "rows_decoded": 0,
                 "distinct_rows": 0, "leafs_without_vis": 0, "rows_missing_self": 0, "run_overruns": 0,
                 "mean_visible_fraction": None, "decoded": False}
        world_leafs = self.leafs[1:visleafs + 1]
        stats["leafs_without_vis"] = sum(1 for leaf in world_leafs if leaf[1] == -1)
        if not vis or not visleafs:
            return stats
        distinct = {leaf[1] for leaf in world_leafs if leaf[1] != -1}
        if len(distinct) * row_bytes > MAX_VIS_DECODE_BYTES:
            self._issue("vis_cap", "visibility decode skipped: decoded size exceeds the analysis cap")
            return stats
        mask = (1 << visleafs) - 1
        rows = {}
        for offset in sorted(distinct):
            rows[offset] = self._decode_row(vis, offset, row_bytes, mask)
        visible = 0
        for index, leaf in enumerate(world_leafs):
            if leaf[1] == -1:
                continue
            bits, overrun = rows[leaf[1]]
            stats["rows_decoded"] += 1
            visible += bits.bit_count()
            if not bits >> index & 1:
                stats["rows_missing_self"] += 1
            if overrun:
                stats["run_overruns"] += 1
        if stats["rows_missing_self"]:
            self._issue("vis_self", "visibility rows that do not include their own leaf")
        if stats["run_overruns"]:
            self._issue("vis_overrun", "visibility zero runs that extend past the row length")
        stats["distinct_rows"] = len(distinct)
        stats["decoded"] = True
        if stats["rows_decoded"]:
            stats["mean_visible_fraction"] = round(visible / (stats["rows_decoded"] * visleafs), 6)
        return stats

    # -- lighting ----------------------------------------------------------

    def face_points(self, face):
        points = []
        for signed_edge in self.surfedges[face[2]:face[2] + face[3]]:
            edge = self.edges[abs(signed_edge)]
            points.append(self.vertices[edge[0 if signed_edge >= 0 else 1]])
        return points

    def _lighting(self):
        light_size = len(self.lumps[LIGHTING])
        styles = Counter()
        lit = overruns = oversize = 0
        expected_end = 0
        for face in self.faces:
            for style in face[5:9]:
                if style != NO_STYLE:
                    styles[str(style)] += 1
            if face[9] == -1 or not light_size:
                continue
            lit += 1
            info = self.texinfo[face[4]]
            points = self.face_points(face)
            extents = []
            for axis in range(2):
                vec = info[axis * 4:axis * 4 + 4]
                values = [dot(vec, p) + vec[3] for p in points]
                extents.append((math.ceil(max(values) / 16) - math.floor(min(values) / 16)) * 16)
            if not info[9] & TEX_SPECIAL and max(extents) > 512:
                oversize += 1
            samples = ((extents[0] >> 4) + 1) * ((extents[1] >> 4) + 1)
            count = sum(1 for style in face[5:9] if style != NO_STYLE)
            end = face[9] + samples * count
            expected_end = max(expected_end, end)
            if end > light_size:
                overruns += 1
        if overruns:
            self._issue("light_overrun", "face light samples extend past the lighting lump")
        if oversize:
            self._issue("light_extents", "lit face texture extents exceed 512 units")
        return {"bytes": light_size, "lit_faces": lit, "unlit_faces": len(self.faces) - lit,
                "style_histogram": dict(sorted(styles.items(), key=lambda item: int(item[0]))),
                "faces_past_lump_end": overruns, "faces_oversize_extents": oversize,
                "max_sample_end": expected_end}

    # -- descriptive views -------------------------------------------------

    def world_bounds(self):
        world = self.models[0]
        return {"mins": list(world[0:3]), "maxs": list(world[3:6])}

    def texture_name(self, texinfo_index):
        if not self.lumps[TEXTURES]:
            return "<no-texture-lump>"
        texture = self.textures[self.texinfo[texinfo_index][8]]
        return "<missing>" if texture is None else texture["name"]

    def texture_histogram(self):
        """Face count per texture name across all models."""
        counts = Counter(self.texture_name(face[4]) for face in self.faces)
        return dict(sorted(counts.items(), key=lambda item: (-item[1], item[0])))

    def classname_histogram(self):
        counts = Counter(entity.get("classname", "<none>") for entity in self.entities)
        return dict(sorted(counts.items()))

    def summary(self):
        contents = Counter(CONTENTS_NAMES.get(leaf[0], str(leaf[0])) for leaf in self.leafs[1:])
        present = [t for t in self.textures if t is not None]
        return {
            "format": self.format,
            "sha256": self.sha256,
            "bytes": self.size,
            "geometry_sha256": self.geometry_sha256,
            "lumps": {name: len(self.lumps[i]) for i, name in enumerate(LUMP_NAMES)},
            "counts": {"planes": len(self.planes), "textures": len(self.textures), "texinfo": len(self.texinfo),
                       "vertices": len(self.vertices), "nodes": len(self.nodes), "clipnodes": len(self.clipnodes),
                       "leafs": len(self.leafs), "faces": len(self.faces), "marksurfaces": len(self.marksurfaces),
                       "edges": len(self.edges), "surfedges": len(self.surfedges), "models": len(self.models)},
            "world_bounds": self.world_bounds(),
            "entities": {"count": len(self.entities), "classnames": self.classname_histogram(),
                         "issues": self.entity_issues[:MAX_ISSUES]},
            "textures": {"slots": len(self.textures), "missing": len(self.textures) - len(present),
                         "external": sum(1 for t in present if t["external"]),
                         "face_histogram": self.texture_histogram()},
            "leaf_contents": dict(sorted(contents.items())),
            "visibility": self.visibility,
            "lighting": self.lighting,
            "issues": list(self.issues),
            "issue_counts": dict(sorted(self._issue_counts.items())),
        }
