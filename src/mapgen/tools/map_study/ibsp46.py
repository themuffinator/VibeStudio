"""Independent bounded reader for the Quake III Arena IBSP46 file envelope.

Format references (read, not copied): id Software's GPL Quake III Arena release
(code/qcommon/qfiles.h record contract; cm_load.c and renderer/tr_bsp.c load
rules for visibility, fogs, surfaces and the light grid). No engine, compiler
or third-party parser code is copied, translated or linked.

Like binary.py, this reader never extracts archives, decompiles brushes,
exports lightmaps or executes map properties. Every index and range is
validated and a structural fault raises FormatError. Conditions the stock
engine tolerates (light grid mismatch, external lightmaps, out-of-range leaf
clusters, padded vis rows) are recorded in `issues`.
"""

from __future__ import annotations

import math
import struct
from collections import Counter

from .binary import (MAX_FILE_BYTES, MAX_GEOMETRY_REFERENCES, MAX_RECORDS, FormatError, Lumps, check_index,
                     check_range, decode, dot, entities, sha256, validate_tree)

IBSP_MAGIC = b"IBSP"
IBSP_VERSION = 46
LUMP_COUNT = 17
HEADER_SIZE = 8 + LUMP_COUNT * 8
LUMP_NAMES = ("entities", "shaders", "planes", "nodes", "leafs", "leafsurfaces", "leafbrushes", "models",
              "brushes", "brushsides", "drawverts", "drawindexes", "fogs", "surfaces", "lightmaps",
              "lightgrid", "visibility")
(ENTITIES, SHADERS, PLANES, NODES, LEAFS, LEAFSURFACES, LEAFBRUSHES, MODELS, BRUSHES, BRUSHSIDES, DRAWVERTS,
 DRAWINDEXES, FOGS, SURFACES, LIGHTMAPS, LIGHTGRID, VISIBILITY) = range(LUMP_COUNT)

SHADER = "<64sii"            # name, surfaceFlags, contentFlags
PLANE = "<4f"
NODE = "<9i"                 # plane, child0, child1, mins[3], maxs[3]
LEAF = "<12i"                # cluster, area, mins[3], maxs[3], firstsurf, numsurf, firstbrush, numbrush
MODEL = "<6f4i"              # mins[3], maxs[3], firstsurface, numsurfaces, firstbrush, numbrushes
BRUSH = "<3i"                # firstside, numsides, shader
BRUSHSIDE = "<2i"            # plane, shader
DRAWVERT = "<10f4B"          # xyz, st, lightmap st, normal, rgba
FOG = "<64sii"               # shader name, brush, visible side
SURFACE = "<12i12f2i"        # see field indices below
(S_SHADER, S_FOG, S_TYPE, S_FIRSTVERT, S_NUMVERTS, S_FIRSTINDEX, S_NUMINDEXES, S_LIGHTMAP) = range(8)
S_PATCHWIDTH, S_PATCHHEIGHT = 24, 25
SURFACE_TYPES = {1: "planar", 2: "patch", 3: "triangle_soup", 4: "flare"}
LIGHTMAP_BYTES = 128 * 128 * 3
LIGHTGRID_CELL_BYTES = 8
DEFAULT_GRID_SIZE = (64.0, 64.0, 128.0)
MAX_PATCH_SIDE = 32
MAX_VIS_BYTES = 64 * 1024 * 1024
MAX_ISSUES = 64


def _grid_size(worldspawn):
    """Worldspawn "gridsize": leading floats override the defaults, as a scanf would."""
    size = list(DEFAULT_GRID_SIZE)
    for axis, token in enumerate(worldspawn.get("gridsize", "").split()[:3]):
        try:
            value = float(token)
        except ValueError:
            break
        if not math.isfinite(value):
            break
        size[axis] = value
    return tuple(size)


class Q3Bsp(Lumps):
    """Bounded IBSP46 study reader."""

    format = "ibsp46"

    def __init__(self, data: bytes):
        if len(data) > MAX_FILE_BYTES:
            raise FormatError(f"file exceeds {MAX_FILE_BYTES} bytes")
        if len(data) < HEADER_SIZE:
            raise FormatError("file shorter than the IBSP46 header")
        magic, version = struct.unpack_from("<4si", data)
        if magic != IBSP_MAGIC:
            raise FormatError("expected Quake III IBSP magic")
        if version != IBSP_VERSION:
            raise FormatError(f"expected IBSP version 46, found {version}")
        super().__init__(data, data, 8, LUMP_COUNT, HEADER_SIZE)
        self.sha256 = sha256(data)
        self.size = len(data)
        self.issues: list[str] = []
        self._issue_counts: Counter = Counter()
        self.entity_issues: list[str] = []
        self.entities = entities(self.lumps[ENTITIES], self.entity_issues)
        self.shaders = [(decode(name.split(b"\0")[0]), surface_flags, content_flags)
                        for name, surface_flags, content_flags in self.records(SHADERS, SHADER)]
        self.planes = self.records(PLANES, PLANE)
        self.nodes = self.records(NODES, NODE)
        self.leafs = self.records(LEAFS, LEAF)
        self.leafsurfaces = [r[0] for r in self.records(LEAFSURFACES, "<i")]
        self.leafbrushes = [r[0] for r in self.records(LEAFBRUSHES, "<i")]
        self.models = self.records(MODELS, MODEL)
        self.brushes = self.records(BRUSHES, BRUSH)
        self.brushsides = self.records(BRUSHSIDES, BRUSHSIDE)
        self.drawverts = self._drawverts()
        self.drawindexes = [r[0] for r in self.records(DRAWINDEXES, "<i")]
        self.fogs = [(decode(name.split(b"\0")[0]), brush, side) for name, brush, side in self.records(FOGS, FOG)]
        self.surfaces = self.records(SURFACES, SURFACE)
        self.lightmap_count = len(self.lumps[LIGHTMAPS]) // LIGHTMAP_BYTES
        self._validate()
        self.visibility = self._visibility()
        self.lightgrid = self._lightgrid()
        # Ignores entity, light and vis changes; includes compiler-dependent ordering.
        self.geometry_sha256 = sha256(b"".join(struct.pack("<I", len(self.lumps[i])) + self.lumps[i]
                                              for i in (PLANES, NODES, LEAFSURFACES, LEAFBRUSHES, MODELS,
                                                        BRUSHES, BRUSHSIDES, DRAWINDEXES)))

    def _drawverts(self):
        """Positions, texture coordinates and normals must be finite; lightmap coordinates only warn.

        Stock q3map output leaves lightmap coordinates uninitialized on some vertex-lit patches.
        """
        data = self.lumps[DRAWVERTS]
        size = struct.calcsize(DRAWVERT)
        if len(data) % size or len(data) // size > MAX_RECORDS:
            raise FormatError(f"lump {DRAWVERTS} has invalid record size/count")
        rows = list(struct.iter_unpack(DRAWVERT, data))
        for row in rows:
            if not all(math.isfinite(v) and abs(v) <= 1e9 for v in row[0:5] + row[7:10]):
                raise FormatError("draw vertex position, texture coordinate or normal is not finite")
            if not all(math.isfinite(v) and abs(v) <= 1e9 for v in row[5:7]):
                self._issue("drawvert_lightmap_st", "draw vertex lightmap coordinates are not finite")
        return rows

    def _issue(self, key, message):
        self._issue_counts[key] += 1
        if self._issue_counts[key] == 1 and len(self.issues) < MAX_ISSUES:
            self.issues.append(message)

    def _validate(self):
        if not self.shaders or not self.planes or not self.nodes or not self.leafs or not self.models:
            raise FormatError("missing shaders, planes, nodes, leafs or models")
        if (sum(s[S_NUMINDEXES] for s in self.surfaces) > MAX_GEOMETRY_REFERENCES
                or sum(b[1] for b in self.brushes) > MAX_GEOMETRY_REFERENCES):
            raise FormatError("aggregate geometry references exceed analysis cap")
        shader_count = len(self.shaders)
        for plane in self.planes:
            if not 0.99 < dot(plane, plane) < 1.01:
                raise FormatError("non-unit BSP plane")
        for first, count, shader in self.brushes:
            check_range(first, count, len(self.brushsides), "brush sides")
            check_index(shader, shader_count, "brush shader")
        for plane, shader in self.brushsides:
            check_index(plane, len(self.planes), "brush side plane")
            check_index(shader, shader_count, "brush side shader")
        for index in self.leafsurfaces:
            check_index(index, len(self.surfaces), "leaf surface")
        for index in self.leafbrushes:
            check_index(index, len(self.brushes), "leaf brush")
        for leaf in self.leafs:
            check_range(leaf[8], leaf[9], len(self.leafsurfaces), "leaf surfaces")
            check_range(leaf[10], leaf[11], len(self.leafbrushes), "leaf brushes")
            if leaf[1] < -1:
                self._issue("leaf_area", "leaf area below -1")
        for model in self.models:
            check_range(model[6], model[7], len(self.surfaces), "model surfaces")
            check_range(model[8], model[9], len(self.brushes), "model brushes")
        for name, brush, side in self.fogs:
            check_index(brush, len(self.brushes), "fog brush")
            if side != -1:
                check_index(side, self.brushes[brush][1], "fog visible side")
        validate_tree(self.nodes, [0], len(self.planes), len(self.leafs))
        has_lightmaps = self.lightmap_count > 0
        if len(self.lumps[LIGHTMAPS]) % LIGHTMAP_BYTES:
            self._issue("lightmap_tail", "lightmap lump is not a whole number of 128x128 pages")
        for surface in self.surfaces:
            check_index(surface[S_SHADER], shader_count, "surface shader")
            if surface[S_FOG] < -1:
                raise FormatError("surface fog index below -1")
            if surface[S_FOG] >= len(self.fogs):
                # Stock maps carry fogNum 0 with an empty fog table; the renderer does not dereference it.
                self._issue("surface_fog", "surface fog index outside the fog table")
            kind = surface[S_TYPE]
            if kind not in SURFACE_TYPES:
                raise FormatError(f"invalid surface type {kind}")
            check_range(surface[S_FIRSTVERT], surface[S_NUMVERTS], len(self.drawverts), "surface vertices")
            check_range(surface[S_FIRSTINDEX], surface[S_NUMINDEXES], len(self.drawindexes), "surface indexes")
            if kind in (1, 3):
                if surface[S_NUMINDEXES] % 3:
                    raise FormatError("surface index count is not a multiple of three")
                indexes = self.drawindexes[surface[S_FIRSTINDEX]:surface[S_FIRSTINDEX] + surface[S_NUMINDEXES]]
                if indexes and (min(indexes) < 0 or max(indexes) >= surface[S_NUMVERTS]):
                    raise FormatError("surface index outside its vertices")
            elif kind == 2:
                width, height = surface[S_PATCHWIDTH], surface[S_PATCHHEIGHT]
                if not (1 <= width <= MAX_PATCH_SIDE and 1 <= height <= MAX_PATCH_SIDE
                        and width * height == surface[S_NUMVERTS]):
                    raise FormatError("patch control grid does not match its vertices")
                if width < 3 or height < 3 or not width % 2 or not height % 2:
                    self._issue("patch_shape", "patch control grid is not odd and at least 3x3")
            lightmap = surface[S_LIGHTMAP]
            if lightmap >= 0:
                if has_lightmaps:
                    check_index(lightmap, self.lightmap_count, "surface lightmap")
                else:
                    self._issue("external_lightmaps", "surfaces reference lightmaps but the lump is empty")

    def _visibility(self):
        vis = self.lumps[VISIBILITY]
        clusters_in_leafs = max((leaf[0] for leaf in self.leafs), default=-1) + 1
        stats = {"bytes": len(vis), "present": bool(vis), "clusters": None, "cluster_bytes": None,
                 "leaf_clusters": clusters_in_leafs, "leafs_cluster_out_of_range": 0, "rows_missing_self": 0,
                 "mean_visible_fraction": None}
        if not vis:
            return stats
        if len(vis) < 8:
            raise FormatError("visibility lump shorter than its header")
        clusters, cluster_bytes = struct.unpack_from("<2i", vis)
        if clusters < 0 or cluster_bytes < 0 or cluster_bytes < (clusters + 7) >> 3:
            raise FormatError("visibility cluster count/row size invalid")
        needed = 8 + clusters * cluster_bytes
        if needed > len(vis) or needed > MAX_VIS_BYTES:
            raise FormatError("visibility rows outside the lump")
        if needed < len(vis):
            self._issue("vis_tail", "visibility lump longer than its rows")
        stats["clusters"], stats["cluster_bytes"] = clusters, cluster_bytes
        stats["leafs_cluster_out_of_range"] = sum(1 for leaf in self.leafs if leaf[0] < -1 or leaf[0] >= clusters)
        if stats["leafs_cluster_out_of_range"]:
            self._issue("leaf_cluster", "leaf clusters outside the visibility cluster count")
        if not clusters:
            return stats
        mask = (1 << clusters) - 1
        visible = 0
        for cluster in range(clusters):
            start = 8 + cluster * cluster_bytes
            bits = int.from_bytes(vis[start:start + cluster_bytes], "little") & mask
            visible += bits.bit_count()
            if not bits >> cluster & 1:
                stats["rows_missing_self"] += 1
        if stats["rows_missing_self"]:
            self._issue("vis_self", "visibility rows that do not include their own cluster")
        stats["mean_visible_fraction"] = round(visible / (clusters * clusters), 6)
        return stats

    def _lightgrid(self):
        size = _grid_size(self.entities[0])
        world = self.models[0]
        stats = {"bytes": len(self.lumps[LIGHTGRID]), "grid_size": list(size), "bounds": None,
                 "cells_expected": None, "consistent": False}
        if any(axis <= 0 for axis in size):
            self._issue("grid_size", "worldspawn gridsize is not positive")
            return stats
        bounds = []
        for axis in range(3):
            origin = size[axis] * math.ceil(world[axis] / size[axis])
            top = size[axis] * math.floor(world[3 + axis] / size[axis])
            bounds.append(int((top - origin) / size[axis] + 1))
        cells = bounds[0] * bounds[1] * bounds[2] if all(b > 0 for b in bounds) else 0
        stats["bounds"], stats["cells_expected"] = bounds, cells
        stats["consistent"] = len(self.lumps[LIGHTGRID]) == cells * LIGHTGRID_CELL_BYTES
        if not stats["consistent"]:
            self._issue("lightgrid", "light grid size does not match the world bounds (engine drops the grid)")
        return stats

    # -- descriptive views -------------------------------------------------

    def world_bounds(self):
        world = self.models[0]
        return {"mins": list(world[0:3]), "maxs": list(world[3:6])}

    def texture_histogram(self):
        """Draw-surface count per shader name across all models."""
        counts = Counter(self.shaders[s[S_SHADER]][0] for s in self.surfaces)
        return dict(sorted(counts.items(), key=lambda item: (-item[1], item[0])))

    def brush_shader_histogram(self):
        counts = Counter(self.shaders[b[2]][0] for b in self.brushes)
        return dict(sorted(counts.items(), key=lambda item: (-item[1], item[0])))

    def classname_histogram(self):
        counts = Counter(entity.get("classname", "<none>") for entity in self.entities)
        return dict(sorted(counts.items()))

    def summary(self):
        types = Counter(SURFACE_TYPES[s[S_TYPE]] for s in self.surfaces)
        triangles = sum(s[S_NUMINDEXES] // 3 for s in self.surfaces if s[S_TYPE] in (1, 3))
        areas = max((leaf[1] for leaf in self.leafs), default=-1) + 1
        return {
            "format": self.format,
            "sha256": self.sha256,
            "bytes": self.size,
            "geometry_sha256": self.geometry_sha256,
            "lumps": {name: len(self.lumps[i]) for i, name in enumerate(LUMP_NAMES)},
            "counts": {"shaders": len(self.shaders), "planes": len(self.planes), "nodes": len(self.nodes),
                       "leafs": len(self.leafs), "leafsurfaces": len(self.leafsurfaces),
                       "leafbrushes": len(self.leafbrushes), "models": len(self.models),
                       "brushes": len(self.brushes), "brushsides": len(self.brushsides),
                       "drawverts": len(self.drawverts), "drawindexes": len(self.drawindexes),
                       "fogs": len(self.fogs), "surfaces": len(self.surfaces), "areas": areas},
            "world_bounds": self.world_bounds(),
            "entities": {"count": len(self.entities), "classnames": self.classname_histogram(),
                         "issues": self.entity_issues[:MAX_ISSUES]},
            "surfaces": {"types": dict(sorted(types.items())), "triangles": triangles},
            "textures": {"shaders": len(self.shaders), "surface_histogram": self.texture_histogram(),
                         "brush_histogram": self.brush_shader_histogram()},
            "visibility": self.visibility,
            "lighting": {"lightmap_bytes": len(self.lumps[LIGHTMAPS]), "lightmaps": self.lightmap_count,
                         "lightgrid": self.lightgrid},
            "issues": list(self.issues),
            "issue_counts": dict(sorted(self._issue_counts.items())),
        }
