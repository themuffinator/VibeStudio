#!/usr/bin/env python3
"""Generate VibeStudio's brand artwork from one geometry description.

Every logo, banner, icon and installer image is built here, so the SVG masters
and the raster exports always agree. Text is converted to outlines from the
vendored Manrope fonts (SIL Open Font License 1.1, assets/branding/fonts), so
the SVGs need no installed font. See docs/BRANDING.md for the brand rules.

    python scripts/generate_branding.py           # write everything under assets/branding
    python scripts/generate_branding.py --check   # fail if a committed SVG master is stale

Requires fontTools (outlines, kerning, web fonts); writing the raster files
also needs Pillow and numpy. ``--check`` needs fontTools only.
"""
from __future__ import annotations

import argparse
import io
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path

try:
    from fontTools.pens.basePen import BasePen
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.ttLib import TTFont
    MISSING_TOOLS: ImportError | None = None
except ImportError as error:  # reported by main()
    MISSING_TOOLS = error
    BasePen = object

ROOT = Path(__file__).resolve().parents[1]
BRANDING = ROOT / "assets" / "branding"
FONT_DIR = BRANDING / "fonts"

# ---------------------------------------------------------------------------
# Palette (docs/BRANDING.md). The orange is the studio's dark-theme accent.
# ---------------------------------------------------------------------------
ORANGE = "#E8841A"        # Vibe Orange: the brand colour
FLARE = "#F7A040"         # light end of the tile gradient
EMBER = "#D66A0E"         # deep end of the tile gradient
BURNT = "#B35900"         # orange for text on white (the light theme's accent)
PEACH = "#FDE6CC"         # quiet orange tint
WHITE = "#FFFFFF"
SNOW = "#F4F4F2"
SILVER = "#E2E2E2"
ASH = "#ABABAB"
GRAPHITE = "#3A3A3A"
CHARCOAL = "#242424"

TAGLINE = "The all-in-one idTech development studio"
DESCRIPTOR = ("The all-in-one development studio", "for classic idTech games")
APP_ID = "io.github.themuffinator.VibeStudio"


def rgb(colour: str) -> tuple[int, int, int]:
    value = colour.lstrip("#")
    return int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16)


# ---------------------------------------------------------------------------
# Paths: contours of M/L/Q/C segments, shared by the SVG writer and rasteriser.
# ---------------------------------------------------------------------------
KAPPA = 0.5522847498307936


@dataclass
class Path2D:
    contours: list[list[tuple]] = field(default_factory=list)

    def move(self, x: float, y: float) -> "Path2D":
        self.contours.append([("M", x, y)])
        return self

    def line(self, x: float, y: float) -> "Path2D":
        self.contours[-1].append(("L", x, y))
        return self

    def quad(self, cx: float, cy: float, x: float, y: float) -> "Path2D":
        self.contours[-1].append(("Q", cx, cy, x, y))
        return self

    def cubic(self, c1x: float, c1y: float, c2x: float, c2y: float, x: float, y: float) -> "Path2D":
        self.contours[-1].append(("C", c1x, c1y, c2x, c2y, x, y))
        return self

    def extend(self, other: "Path2D") -> "Path2D":
        self.contours.extend(other.contours)
        return self

    def svg(self) -> str:
        parts: list[str] = []
        for contour in self.contours:
            for segment in contour:
                parts.append(segment[0] + " ".join(fmt(value) for value in segment[1:]))
            parts.append("Z")
        return "".join(parts)

    def polygons(self, scale: float) -> list[list[tuple[float, float]]]:
        """Flatten to polygons in raster space (``scale`` raster pixels per unit)."""
        result = []
        for contour in self.contours:
            points: list[tuple[float, float]] = []
            x0 = y0 = 0.0
            for segment in contour:
                kind = segment[0]
                if kind in ("M", "L"):
                    x0, y0 = segment[1], segment[2]
                    points.append((x0 * scale, y0 * scale))
                elif kind == "Q":
                    cx, cy, x, y = segment[1:]
                    steps = curve_steps([(x0, y0), (cx, cy), (x, y)], scale)
                    for i in range(1, steps + 1):
                        t = i / steps
                        u = 1 - t
                        px = u * u * x0 + 2 * u * t * cx + t * t * x
                        py = u * u * y0 + 2 * u * t * cy + t * t * y
                        points.append((px * scale, py * scale))
                    x0, y0 = x, y
                elif kind == "C":
                    c1x, c1y, c2x, c2y, x, y = segment[1:]
                    steps = curve_steps([(x0, y0), (c1x, c1y), (c2x, c2y), (x, y)], scale)
                    for i in range(1, steps + 1):
                        t = i / steps
                        u = 1 - t
                        px = u ** 3 * x0 + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t ** 3 * x
                        py = u ** 3 * y0 + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t ** 3 * y
                        points.append((px * scale, py * scale))
                    x0, y0 = x, y
            if len(points) >= 3:
                result.append(points)
        return result


def curve_steps(points: list[tuple[float, float]], scale: float) -> int:
    length = sum(math.dist(a, b) for a, b in zip(points, points[1:])) * scale
    return max(4, min(256, int(length / 2.0) + 1))


def fmt(value: float) -> str:
    text = f"{value:.2f}".rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


def rect(x: float, y: float, w: float, h: float) -> Path2D:
    return Path2D().move(x, y).line(x + w, y).line(x + w, y + h).line(x, y + h)


def rounded_rect(x: float, y: float, w: float, h: float, r: float) -> Path2D:
    k = r * KAPPA
    p = Path2D().move(x + r, y)
    p.line(x + w - r, y).cubic(x + w - r + k, y, x + w, y + r - k, x + w, y + r)
    p.line(x + w, y + h - r).cubic(x + w, y + h - r + k, x + w - r + k, y + h, x + w - r, y + h)
    p.line(x + r, y + h).cubic(x + r - k, y + h, x, y + h - r + k, x, y + h - r)
    p.line(x, y + r).cubic(x, y + r - k, x + r - k, y, x + r, y)
    return p


def capsule(x0: float, y0: float, x1: float, y1: float, width: float) -> Path2D:
    """A stroke with round caps from (x0, y0) to (x1, y1), as one closed contour."""
    length = math.hypot(x1 - x0, y1 - y0)
    ux, uy = (x1 - x0) / length, (y1 - y0) / length
    nx, ny = -uy, ux
    r = width / 2
    k = r * KAPPA
    p = Path2D().move(x0 + nx * r, y0 + ny * r)
    p.line(x1 + nx * r, y1 + ny * r)
    # Round cap at the end: normal -> forward -> -normal.
    p.cubic(x1 + nx * r + ux * k, y1 + ny * r + uy * k, x1 + ux * r + nx * k, y1 + uy * r + ny * k, x1 + ux * r, y1 + uy * r)
    p.cubic(x1 + ux * r - nx * k, y1 + uy * r - ny * k, x1 - nx * r + ux * k, y1 - ny * r + uy * k, x1 - nx * r, y1 - ny * r)
    p.line(x0 - nx * r, y0 - ny * r)
    # Round cap at the start: -normal -> backward -> normal.
    p.cubic(x0 - nx * r - ux * k, y0 - ny * r - uy * k, x0 - ux * r - nx * k, y0 - uy * r - ny * k, x0 - ux * r, y0 - uy * r)
    p.cubic(x0 - ux * r + nx * k, y0 - uy * r + ny * k, x0 + nx * r - ux * k, y0 + ny * r - uy * k, x0 + nx * r, y0 + ny * r)
    return p


# ---------------------------------------------------------------------------
# Fills and scenes.
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class Linear:
    x1: float
    y1: float
    x2: float
    y2: float
    stops: tuple[tuple[float, str], ...]


@dataclass
class Item:
    path: Path2D
    fill: str | Linear
    opacity: float = 1.0
    shadow: tuple[float, float, float, float] | None = None  # dx, dy, blur radius, opacity


@dataclass
class Scene:
    width: float
    height: float
    title: str
    items: list[Item] = field(default_factory=list)
    background: str | Linear | None = None

    def add(self, path: Path2D, fill: str | Linear, opacity: float = 1.0, shadow=None) -> None:
        self.items.append(Item(path, fill, opacity, shadow))


def svg_document(scene: Scene) -> str:
    defs: list[str] = []
    body: list[str] = []
    gradient_ids: dict[Linear, str] = {}

    def paint(fill: str | Linear) -> str:
        if isinstance(fill, str):
            return fill
        if fill not in gradient_ids:
            gid = f"g{len(gradient_ids) + 1}"
            gradient_ids[fill] = gid
            stops = "".join(f'<stop offset="{fmt(offset)}" stop-color="{colour}"/>' for offset, colour in fill.stops)
            defs.append(
                f'<linearGradient id="{gid}" gradientUnits="userSpaceOnUse" x1="{fmt(fill.x1)}" y1="{fmt(fill.y1)}" '
                f'x2="{fmt(fill.x2)}" y2="{fmt(fill.y2)}">{stops}</linearGradient>'
            )
        return f"url(#{gradient_ids[fill]})"

    if scene.background is not None:
        body.append(f'<rect width="{fmt(scene.width)}" height="{fmt(scene.height)}" fill="{paint(scene.background)}"/>')
    shadow_ids: dict[tuple, str] = {}
    for item in scene.items:
        attributes = f'd="{item.path.svg()}" fill="{paint(item.fill)}"'
        if item.opacity < 1.0:
            attributes += f' fill-opacity="{fmt(item.opacity)}"'
        if item.shadow is not None:
            if item.shadow not in shadow_ids:
                sid = f"s{len(shadow_ids) + 1}"
                shadow_ids[item.shadow] = sid
                dx, dy, blur, opacity = item.shadow
                defs.append(
                    f'<filter id="{sid}" x="-20%" y="-20%" width="140%" height="140%" color-interpolation-filters="sRGB">'
                    f'<feDropShadow dx="{fmt(dx)}" dy="{fmt(dy)}" stdDeviation="{fmt(blur / 2)}" '
                    f'flood-color="#000000" flood-opacity="{fmt(opacity)}"/></filter>'
                )
            attributes += f' filter="url(#{shadow_ids[item.shadow]})"'
        body.append(f"<path {attributes}/>")
    head = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{fmt(scene.width)}" height="{fmt(scene.height)}" '
        f'viewBox="0 0 {fmt(scene.width)} {fmt(scene.height)}" role="img" aria-label="{scene.title}">'
    )
    lines = [head, f"<title>{scene.title}</title>"]
    if defs:
        lines.append("<defs>" + "".join(defs) + "</defs>")
    lines.extend(body)
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


Box = tuple[int, int, int, int]  # x0, y0, x1, y1 in raster pixels


def raster_box(polygons: list[list[tuple[float, float]]], size: tuple[int, int], margin: int = 0) -> Box | None:
    xs = [x for polygon in polygons for x, _ in polygon]
    ys = [y for polygon in polygons for _, y in polygon]
    if not xs:
        return None
    x0 = max(0, int(math.floor(min(xs))) - margin)
    y0 = max(0, int(math.floor(min(ys))) - margin)
    x1 = min(size[0], int(math.ceil(max(xs))) + 1 + margin)
    y1 = min(size[1], int(math.ceil(max(ys))) + 1 + margin)
    return (x0, y0, x1, y1) if x1 > x0 and y1 > y0 else None


def signed_area(polygon: list[tuple[float, float]]) -> float:
    return 0.5 * sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(polygon, polygon[1:] + polygon[:1]))


def coverage_mask(polygons: list[list[tuple[float, float]]], box: Box) -> np.ndarray:
    """Non-zero winding coverage of the polygons inside ``box`` (boolean array).

    Font outlines overlap (a 'b' is a stem plus a bowl), so even-odd filling would
    punch holes; each simple contour adds its orientation where it covers a pixel.
    """
    import numpy as np
    from PIL import Image, ImageDraw

    bx0, by0, bx1, by1 = box
    winding = np.zeros((by1 - by0, bx1 - bx0), dtype=np.int16)
    for polygon in polygons:
        area = signed_area(polygon)
        if area == 0:
            continue
        tile = Image.new("L", (bx1 - bx0, by1 - by0), 0)
        ImageDraw.Draw(tile).polygon([(x - bx0, y - by0) for x, y in polygon], fill=255)
        winding += np.where(np.asarray(tile) > 127, 1 if area > 0 else -1, 0).astype(np.int16)
    return winding != 0


def fill_rgb(fill: str | Linear, box: Box, scale: float) -> np.ndarray:
    import numpy as np

    x0, y0, x1, y1 = box
    if isinstance(fill, str):
        out = np.empty((y1 - y0, x1 - x0, 3), dtype=np.float32)
        out[:, :] = rgb(fill)
        return out
    ys, xs = np.mgrid[y0:y1, x0:x1].astype(np.float32)
    xs = (xs + 0.5) / scale
    ys = (ys + 0.5) / scale
    dx, dy = fill.x2 - fill.x1, fill.y2 - fill.y1
    t = np.clip(((xs - fill.x1) * dx + (ys - fill.y1) * dy) / (dx * dx + dy * dy), 0.0, 1.0)
    offsets = np.array([offset for offset, _ in fill.stops], dtype=np.float32)
    colours = np.array([rgb(colour) for _, colour in fill.stops], dtype=np.float32)
    out = np.empty((y1 - y0, x1 - x0, 3), dtype=np.float32)
    for channel in range(3):
        out[:, :, channel] = np.interp(t, offsets, colours[:, channel])
    return out


def rasterise(scene: Scene, width_px: int, supersample: int = 4) -> Image.Image:
    """Render ``scene`` at ``width_px`` wide (aspect kept) with premultiplied box downsampling."""
    import numpy as np
    from PIL import Image, ImageFilter

    scale = width_px / scene.width * supersample
    size = (round(scene.width * scale), round(scene.height * scale))
    # Premultiplied colour in 0..255 and coverage in 0..1.
    colour = np.zeros((size[1], size[0], 3), dtype=np.float32)
    alpha = np.zeros((size[1], size[0]), dtype=np.float32)

    def composite(box: Box, paint: np.ndarray, coverage: np.ndarray) -> None:
        x0, y0, x1, y1 = box
        a = coverage[:, :, None]
        colour[y0:y1, x0:x1] = paint * a + colour[y0:y1, x0:x1] * (1 - a)
        alpha[y0:y1, x0:x1] = coverage + alpha[y0:y1, x0:x1] * (1 - coverage)

    if scene.background is not None:
        whole = (0, 0, size[0], size[1])
        composite(whole, fill_rgb(scene.background, whole, scale), np.ones((size[1], size[0]), dtype=np.float32))
    for item in scene.items:
        polygons = item.path.polygons(scale)
        if item.shadow is not None:
            dx, dy, blur, opacity = item.shadow
            radius = blur * scale / 2
            margin = int(radius * 3) + int(abs(dx * scale)) + int(abs(dy * scale)) + 2
            box = raster_box(polygons, size, margin)
            if box is not None:
                shape = coverage_mask(polygons, box)
                shadow = Image.fromarray((shape * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(radius))
                shifted = Image.new("L", shadow.size, 0)
                shifted.paste(shadow, (round(dx * scale), round(dy * scale)))
                composite(box, np.zeros((box[3] - box[1], box[2] - box[0], 3), dtype=np.float32),
                          np.asarray(shifted, dtype=np.float32) / 255 * opacity)
        box = raster_box(polygons, size)
        if box is None:
            continue
        coverage = coverage_mask(polygons, box).astype(np.float32) * item.opacity
        composite(box, fill_rgb(item.fill, box, scale), coverage)
    pixels = np.ascontiguousarray(np.clip(np.dstack([colour, alpha[:, :, None] * 255.0]) + 0.5, 0, 255).astype(np.uint8))
    image = Image.frombytes("RGBa", size, pixels.tobytes())
    if supersample > 1:
        image = image.reduce(supersample)
    return image.convert("RGBA")


# ---------------------------------------------------------------------------
# Fonts and text outlines.
# ---------------------------------------------------------------------------
class SegmentPen(BasePen):
    """Collects glyph outlines as Path2D segments in font units (y up)."""

    def __init__(self, glyph_set):
        super().__init__(glyph_set)
        self.path = Path2D()

    def _moveTo(self, pt):
        self.path.move(*pt)

    def _lineTo(self, pt):
        self.path.line(*pt)

    def _qCurveToOne(self, pt1, pt2):
        self.path.quad(*pt1, *pt2)

    def _curveToOne(self, pt1, pt2, pt3):
        self.path.cubic(*pt1, *pt2, *pt3)

    def _closePath(self):
        pass


@dataclass
class BrandFont:
    path: Path
    ttf: TTFont
    upem: int
    kerning: dict[tuple[str, str], int] = field(default_factory=dict)

    @classmethod
    def load(cls, name: str) -> "BrandFont":
        path = FONT_DIR / name
        if not path.is_file():
            raise FileNotFoundError(f"Brand font missing: {path}")
        ttf = TTFont(str(path))
        return cls(path, ttf, ttf["head"].unitsPerEm)

    def glyph_name(self, char: str) -> str:
        return self.ttf.getBestCmap()[ord(char)]

    def outline(self, char: str) -> Path2D:
        glyphs = self.ttf.getGlyphSet()
        pen = SegmentPen(glyphs)
        glyphs[self.glyph_name(char)].draw(pen)
        return pen.path

    def bounds(self, glyph: str) -> tuple[float, float, float, float]:
        glyphs = self.ttf.getGlyphSet()
        pen = BoundsPen(glyphs)
        glyphs[glyph].draw(pen)
        return pen.bounds

    def kern(self, left: str, right: str) -> int:
        """Pair adjustment from the font's GPOS 'kern' feature (PairPos formats 1 and 2)."""
        key = (left, right)
        if key in self.kerning:
            return self.kerning[key]
        total = 0
        table = self.ttf["GPOS"].table if "GPOS" in self.ttf else None
        lookups = sorted({index for record in (table.FeatureList.FeatureRecord if table else [])
                          if record.FeatureTag == "kern" for index in record.Feature.LookupListIndex})
        for index in lookups:
            lookup = table.LookupList.Lookup[index]
            for subtable in lookup.SubTable:
                if lookup.LookupType == 9:
                    subtable = subtable.ExtSubTable
                if getattr(subtable, "LookupType", 2) != 2 or left not in subtable.Coverage.glyphs:
                    continue
                value = None
                if subtable.Format == 1:
                    pairs = subtable.PairSet[subtable.Coverage.glyphs.index(left)].PairValueRecord
                    for pair in pairs:
                        if pair.SecondGlyph == right:
                            value = getattr(pair.Value1, "XAdvance", 0) or 0
                            break
                else:
                    first = subtable.ClassDef1.classDefs.get(left, 0)
                    second = subtable.ClassDef2.classDefs.get(right, 0)
                    record = subtable.Class1Record[first].Class2Record[second]
                    value = getattr(record.Value1, "XAdvance", 0) or 0
                if value is not None:
                    total += value
                    break
        self.kerning[key] = total
        return total

    def pen_positions(self, text: str) -> list[float]:
        """Pen x of each character in font units: advances plus GPOS kerning."""
        metrics = self.ttf["hmtx"]
        positions = []
        pen = 0.0
        previous = None
        for char in text:
            glyph = self.glyph_name(char)
            if previous is not None:
                pen += self.kern(previous, glyph)
            positions.append(pen)
            pen += metrics[glyph][0]
            previous = glyph
        return positions

    def advance(self, text: str) -> float:
        if not text:
            return 0.0
        positions = self.pen_positions(text)
        return positions[-1] + self.ttf["hmtx"][self.glyph_name(text[-1])][0]

    def dot(self) -> tuple[float, float, float, float]:
        """Bounding box of the dot on the font's 'i' (the topmost contour)."""
        glyphs = self.ttf.getGlyphSet()
        pen = SegmentPen(glyphs)
        glyphs[self.glyph_name("i")].draw(pen)
        best = None
        for contour in pen.path.contours:
            ys = [segment[-1] for segment in contour]
            xs = [segment[-2] for segment in contour]
            box = (min(xs), min(ys), max(xs), max(ys))
            if best is None or box[3] > best[3]:
                best = box
        return best


def transform(path: Path2D, scale: float, ox: float, baseline: float) -> Path2D:
    """Font units (y up) to artwork units (y down)."""
    out = Path2D()
    for contour in path.contours:
        segments = []
        for segment in contour:
            values = list(segment[1:])
            mapped = []
            for i in range(0, len(values), 2):
                mapped.extend([ox + values[i] * scale, baseline - values[i + 1] * scale])
            segments.append((segment[0], *mapped))
        out.contours.append(segments)
    return out


def text_path(font: BrandFont, text: str, x: float, baseline: float, size: float, tracking: float = 0.0) -> tuple[Path2D, float]:
    """Outline ``text`` at ``size`` units per em; returns the path and its advance width."""
    scale = size / font.upem
    path = Path2D()
    positions = font.pen_positions(text)
    for index, (char, pen_x) in enumerate(zip(text, positions)):
        if char.isspace():
            continue
        path.extend(transform(font.outline(char), scale, x + pen_x * scale + index * tracking, baseline))
    return path, font.advance(text) * scale + tracking * max(0, len(text) - 1)


# ---------------------------------------------------------------------------
# The mark: an orange tile with a white V whose right arm sheds three pixels.
# Designed on a 1024-unit square.
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class MarkStyle:
    plate: bool = True
    plate_fill: str | Linear | None = None   # default: orange gradient
    glyph: str = WHITE
    inset: float = 64.0                       # tile margin inside the 1024 square
    shadow: bool = False


def mark_items(x: float, y: float, size: float, style: MarkStyle = MarkStyle()) -> list[Item]:
    """Mark placed with its 1024-unit square at (x, y) scaled to ``size``."""
    s = size / 1024.0
    items: list[Item] = []
    inset = style.inset
    plate_size = 1024 - inset * 2
    # The glyph scales with the tile so the macOS inset tile keeps its proportions.
    g = plate_size / 896.0

    def gx(v: float) -> float:
        return x + (inset + (v - 64) * g) * s

    def gy(v: float) -> float:
        return y + (inset + (v - 64) * g) * s

    if style.plate:
        fill = style.plate_fill or Linear(0, y + inset * s, 0, y + (1024 - inset) * s, ((0.0, FLARE), (1.0, EMBER)))
        shadow = (0, 12 * s * g, 28 * s * g, 0.32) if style.shadow else None
        items.append(Item(rounded_rect(x + inset * s, y + inset * s, plate_size * s, plate_size * s, 216 * g * s), fill, 1.0, shadow))
    # V: arms from the top corners to the vertex, then three shrinking pixels
    # continuing the right arm toward the tile's top-right corner.
    cx, top, bottom, half, stroke = 476.0, 384.0, 738.0, 176.0, 128.0
    w = stroke * g * s
    glyph = Path2D()
    glyph.extend(capsule(gx(cx - half), gy(top), gx(cx), gy(bottom), w))
    items.append(Item(glyph, style.glyph))
    items.append(Item(capsule(gx(cx), gy(bottom), gx(cx + half), gy(top), w), style.glyph))
    ux, uy = half, -(bottom - top)
    length = math.hypot(ux, uy)
    ux, uy = ux / length, uy / length
    distance = stroke / 2
    for side, gap, opacity in ((72.0, 34.0, 1.0), (54.0, 30.0, 0.8), (38.0, 28.0, 0.55)):
        distance += gap + side / 2
        px = round(((cx + half) + ux * distance) / 2) * 2
        py = round((top + uy * distance) / 2) * 2
        items.append(Item(rect(gx(px - side / 2), gy(py - side / 2), side * g * s, side * g * s), style.glyph, opacity))
        distance += side / 2
    return items


def mark_scene(style: MarkStyle = MarkStyle(), title: str = "VibeStudio") -> Scene:
    scene = Scene(1024, 1024, title)
    scene.items.extend(mark_items(0, 0, 1024, style))
    return scene


def pixel_v(left: int, top: int, arm: int, step_rows: list[int], trail: list[tuple[int, int, int, float]]):
    """Pixel cells of a V with ``arm``-wide arms stepping one column per entry of ``step_rows``.

    The arms close into a tip that narrows by a pixel per side per row, then the
    trail squares (x, y, side, opacity) are added.
    """
    cells: dict[tuple[int, int], float] = {}
    steps = len(step_rows)
    width = arm * 2 + (steps - 1) * 2  # outer width of the V
    right = left + width - arm
    row = top
    for step, rows in enumerate(step_rows):
        for _ in range(rows):
            for c in range(arm):
                cells[(left + step + c, row)] = 1.0
                cells[(right - step + c, row)] = 1.0
            row += 1
    first, last = left + steps - 1, right - steps + 1 + arm - 1
    while last - first > 1:
        first, last = first + 1, last - 1
        for c in range(first, last + 1):
            cells[(c, row)] = 1.0
        row += 1
    for x, y, side, alpha in trail:
        for dy in range(side):
            for dx in range(side):
                cells[(x + dx, y + dy)] = alpha
    return cells


# Hand-placed glyphs for the smallest sizes, where a soft diagonal turns to mush.
SMALL_GLYPHS = {
    16: pixel_v(left=3, top=4, arm=2, step_rows=[2, 2, 2], trail=[(11, 2, 1, 0.8)]),
    20: pixel_v(left=4, top=5, arm=2, step_rows=[2, 2, 2, 2], trail=[(14, 3, 1, 0.85)]),
    24: pixel_v(left=5, top=6, arm=3, step_rows=[3, 3, 2, 2], trail=[(17, 3, 2, 0.9)]),
}


def small_icon(size: int) -> Image.Image:
    """Tile rendered as vectors, glyph placed pixel by pixel."""
    tile = rasterise(Scene(1024, 1024, "VibeStudio", mark_items(0, 0, 1024, MarkStyle())[:1]), size, 8)
    pixels = tile.load()
    for (column, row), alpha in SMALL_GLYPHS[size].items():
        if 0 <= column < size and 0 <= row < size:
            r, g, b, a = pixels[column, row]
            pixels[column, row] = tuple(round(c + (255 - c) * alpha) for c in (r, g, b)) + (a,)
    return tile


def icon_png(size: int, style: MarkStyle = MarkStyle()) -> Image.Image:
    if size in SMALL_GLYPHS and style == MarkStyle():
        return small_icon(size)
    return rasterise(mark_scene(style), size, 4 if size >= 128 else 8)


# ---------------------------------------------------------------------------
# Wordmark, lockup and banner.
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class WordmarkStyle:
    vibe: str
    studio: str
    dots: str


ON_DARK = WordmarkStyle(ORANGE, WHITE, ORANGE)
ON_LIGHT = WordmarkStyle(ORANGE, CHARCOAL, ORANGE)
ON_ORANGE = WordmarkStyle(WHITE, WHITE, CHARCOAL)


def wordmark_items(font: BrandFont, x: float, baseline: float, size: float, style: WordmarkStyle) -> tuple[list[Item], float]:
    """'VibeStudio' with square pixel dots on both i's. Returns items and width."""
    text = "VıbeStudıo"
    scale = size / font.upem
    positions = font.pen_positions(text)
    vibe = Path2D()
    studio = Path2D()
    for index, (char, pen_x) in enumerate(zip(text, positions)):
        outline = transform(font.outline(char), scale, x + pen_x * scale, baseline)
        (vibe if index < 4 else studio).extend(outline)
    items = [Item(vibe, style.vibe), Item(studio, style.studio)]
    # Square "pixel" dots: as wide as the stem, centred where the font's round dot sits.
    stem = font.bounds(font.glyph_name("ı"))
    dot = font.dot()
    side = (stem[2] - stem[0]) * scale
    centre_y = baseline - (dot[1] + dot[3]) / 2 * scale
    for index, char in enumerate(text):
        if char != "ı":
            continue
        centre_x = x + (positions[index] + (stem[0] + stem[2]) / 2) * scale
        items.append(Item(rect(centre_x - side / 2, centre_y - side / 2, side, side), style.dots))
    return items, font.advance(text) * scale


def cap_height(font: BrandFont) -> float:
    return font.ttf["OS/2"].sCapHeight


def lockup_scene(fonts: dict[str, BrandFont], style: WordmarkStyle, title: str, with_tagline: bool,
                 tagline_colour: str = ASH) -> Scene:
    """Mark beside the wordmark; optionally the tagline under the wordmark."""
    heavy, medium = fonts["heavy"], fonts["medium"]
    mark = 256.0
    text_size = 168.0
    cap = cap_height(heavy) * text_size / heavy.upem
    gap = 48.0
    left = 24.0
    text_x = left + mark + gap
    # The tagline is set to the wordmark's width so the two read as one block.
    word_width = heavy.advance("VıbeStudıo") * text_size / heavy.upem
    tag_size = min(56.0, max(36.0, (word_width - 8) / (medium.advance(TAGLINE) / medium.upem)))
    tag_cap = cap_height(medium) * tag_size / medium.upem
    if with_tagline:
        spacing = tag_size * 0.9
        block = cap + spacing + tag_cap
        baseline = 32 + (mark - block) / 2 + cap
    else:
        baseline = 32 + mark / 2 + cap / 2
    items, width = wordmark_items(heavy, text_x, baseline, text_size, style)
    total_width = text_x + width + left
    if with_tagline:
        tag, tag_width = text_path(medium, TAGLINE, text_x + 6, baseline + spacing + tag_cap, tag_size)
        items.append(Item(tag, tagline_colour))
        total_width = max(total_width, text_x + 6 + tag_width + left)
    scene = Scene(round(total_width), mark + 64, title)
    scene.items.extend(mark_items(left, 32, mark))
    scene.items.extend(items)
    return scene


def wordmark_scene(fonts: dict[str, BrandFont], style: WordmarkStyle, title: str) -> Scene:
    heavy = fonts["heavy"]
    size = 200.0
    cap = cap_height(heavy) * size / heavy.upem
    pad = 24.0
    items, width = wordmark_items(heavy, pad, pad + cap + 8, size, style)
    scene = Scene(round(width + pad * 2), round(cap + pad * 2 + 16), title)
    scene.items.extend(items)
    return scene


# ---------------------------------------------------------------------------
# Social preview (1280x640, GitHub's recommended size).
# ---------------------------------------------------------------------------
def hash01(*values: int) -> float:
    """Deterministic, well-mixed noise in [0, 1) (splitmix64 finaliser)."""
    h = 0x9E3779B97F4A7C15
    for v in values:
        h = (h ^ (v & 0xFFFFFFFFFFFFFFFF)) & 0xFFFFFFFFFFFFFFFF
        h = (h + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
        h = ((h ^ (h >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
        h = ((h ^ (h >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
        h ^= h >> 31
    return (h >> 11) / float(1 << 53)


def pixel_field(x0: float, y0: float, x1: float, y1: float, cell: float, gap: float, strength,
                avoid: list[tuple[float, float, float, float]] = (), colour: str = WHITE,
                ceiling: float = 0.3) -> list[Item]:
    """A grid of squares fading in by ``strength(u, v)``; cells inside ``avoid`` boxes are skipped."""
    items = []
    columns = int((x1 - x0) // cell)
    rows = int((y1 - y0) // cell)
    for row in range(rows):
        for column in range(columns):
            u = (column + 0.5) / columns
            v = (row + 0.5) / rows
            level = min(1.0, strength(u, v))
            if level <= 0:
                continue
            noise = hash01(row, column)
            if noise > level:
                continue
            x = x0 + column * cell + gap / 2
            y = y0 + row * cell + gap / 2
            size = cell - gap
            if any(x < ax1 and x + size > ax0 and y < ay1 and y + size > ay0 for ax0, ay0, ax1, ay1 in avoid):
                continue
            opacity = ceiling * (0.25 + 0.75 * level) * (0.55 + 0.45 * hash01(column, row, 7))
            items.append(Item(rect(x, y, size, size), colour, round(opacity, 3)))
    return items


def social_scene(fonts: dict[str, BrandFont]) -> Scene:
    heavy, medium = fonts["heavy"], fonts["medium"]
    width, height = 1280.0, 640.0
    background = Linear(0, 0, width, height, ((0.0, FLARE), (0.55, ORANGE), (1.0, EMBER)))
    scene = Scene(width, height, "VibeStudio: the all-in-one development studio for classic idTech games", background=background)
    left = 96.0
    content: list[Item] = []
    avoid: list[tuple[float, float, float, float]] = []

    def text(line: str, x: float, baseline: float, size: float, opacity: float = 1.0) -> float:
        path, advance = text_path(medium, line, x, baseline, size)
        content.append(Item(path, WHITE, opacity))
        avoid.append((x - 24, baseline - size, x + advance + 24, baseline + size * 0.35))
        return advance

    # Small platform line above the logo.
    text("Open source  ·  Windows  ·  macOS  ·  Linux", left + 4, 108, 24, 0.86)
    # Inverted mark (white tile, orange glyph) beside the white wordmark.
    mark_size = 168.0
    top = 150.0
    content.extend(mark_items(left, top, mark_size, MarkStyle(plate_fill=WHITE, glyph=ORANGE)))
    text_size = 132.0
    cap = cap_height(heavy) * text_size / heavy.upem
    baseline = top + mark_size / 2 + cap / 2
    word_x = left + mark_size + 34
    items, word_width = wordmark_items(heavy, word_x, baseline, text_size, ON_ORANGE)
    content.extend(items)
    avoid.append((left - 24, top - 24, word_x + word_width + 32, top + mark_size + 24))
    line_y = top + mark_size + 84
    text(DESCRIPTOR[0], left + 8, line_y, 48)
    text(DESCRIPTOR[1], left + 8, line_y + 60, 48)
    # Workflow chips.
    chip_x = left + 8
    chip_y = height - 112
    for label in ("Levels", "Models", "Textures", "Audio", "Packages", "Code", "Build"):
        size = 25.0
        chip_w = medium.advance(label) * size / medium.upem + 36
        content.append(Item(rounded_rect(chip_x, chip_y, chip_w, 46, 23), WHITE, 0.18))
        label_path, _ = text_path(medium, label, chip_x + 18, chip_y + 31.5, size)
        content.append(Item(label_path, WHITE))
        avoid.append((chip_x - 16, chip_y - 16, chip_x + chip_w + 16, chip_y + 62))
        chip_x += chip_w + 12
    # Pixels dissolving in from the right edge, kept clear of the text.
    scene.items.extend(pixel_field(560, 0, width, height, 40, 8,
                                   lambda u, v: (u - 0.12) * 1.35 + (0.5 - v) * 0.25, avoid))
    scene.items.extend(content)
    return scene


# ---------------------------------------------------------------------------
# Installer art.
# ---------------------------------------------------------------------------
def wizard_scene(width: float, height: float) -> Scene:
    """Inno Setup's tall side image: orange, mark and dissolving pixels."""
    scene = Scene(width, height, "VibeStudio setup", background=Linear(0, 0, width * 0.4, height, ((0.0, FLARE), (1.0, EMBER))))
    scene.items.extend(pixel_field(0, height * 0.52, width, height, width / 7, width / 7 * 0.16,
                                   lambda u, v: max(0.0, v * 1.1 - 0.15)))
    size = width * 0.62
    scene.items.extend(mark_items((width - size) / 2, height * 0.16, size, MarkStyle(plate_fill=WHITE, glyph=ORANGE)))
    return scene


def wizard_small_scene(size: float) -> Scene:
    scene = Scene(size, size, "VibeStudio")
    scene.items.extend(mark_items(0, 0, size))
    return scene


def dmg_background_scene(fonts: dict[str, BrandFont]) -> Scene:
    """660x400 Finder window: app on the left, Applications on the right."""
    width, height = 660.0, 400.0
    scene = Scene(width, height, "Install VibeStudio", background=Linear(0, 0, 0, height, ((0.0, "#FBFBFA"), (1.0, "#EFEFED"))))
    scene.items.extend(pixel_field(0, height - 96, width, height, 24, 5, lambda u, v: max(0.0, v - 0.1) * 0.9,
                                   colour=ORANGE, ceiling=0.16))
    # Arrow between the two icon slots (icons are placed at x=165 and x=495, y=190).
    shaft = rounded_rect(262, 186, 120, 10, 5)
    scene.add(shaft, ORANGE)
    head = Path2D().move(382, 172).line(408, 191).line(382, 210)
    scene.add(head, ORANGE)
    medium = fonts["medium"]
    caption, caption_width = text_path(medium, "Drag VibeStudio to Applications", 0, 0, 17)
    caption, _ = text_path(medium, "Drag VibeStudio to Applications", (width - caption_width) / 2, 318, 17)
    scene.add(caption, GRAPHITE)
    return scene


# ---------------------------------------------------------------------------
# Output.
# ---------------------------------------------------------------------------
PNG_SIZES = [16, 20, 22, 24, 32, 40, 48, 64, 96, 128, 256, 512, 1024]
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
ICNS_SIZES = [16, 32, 64, 128, 256, 512, 1024]
QRC_SIZES = [16, 24, 32, 48, 64, 128, 256]
HICOLOR_SIZES = [16, 22, 24, 32, 48, 64, 128, 256, 512]


class Writer:
    """Writes the assets, or in check mode compares the SVG/text masters and
    confirms every binary file exists (raster output is never rebuilt there)."""

    def __init__(self, check: bool):
        self.check = check
        self.stale: list[str] = []
        self.written: list[str] = []

    def text(self, relative: str, content: str) -> None:
        path = BRANDING / relative
        if self.check:
            current = path.read_text(encoding="utf-8") if path.is_file() else None
            if current != content:
                self.stale.append(relative)
            return
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", encoding="utf-8", newline="\n") as handle:
            handle.write(content)
        self.written.append(relative)

    def binary(self, relative: str, produce) -> None:
        """``produce`` is called only when writing."""
        path = BRANDING / relative
        if self.check:
            if not path.is_file():
                self.stale.append(relative)
            return
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(produce())
        self.written.append(relative)

    def png(self, relative: str, produce) -> None:
        def encode() -> bytes:
            buffer = io.BytesIO()
            produce().save(buffer, "PNG", optimize=True)
            return buffer.getvalue()
        self.binary(relative, encode)

    def svg(self, relative: str, scene: Scene) -> None:
        self.text(relative, svg_document(scene))


def ico_bytes(images: dict[int, Image.Image]) -> bytes:
    largest = images[max(images)]
    buffer = io.BytesIO()
    largest.save(buffer, "ICO", sizes=[(s, s) for s in sorted(images)],
                 append_images=[images[s] for s in sorted(images) if s != max(images)])
    return buffer.getvalue()


def icns_bytes(images: dict[int, Image.Image]) -> bytes:
    largest = images[max(images)]
    buffer = io.BytesIO()
    largest.save(buffer, "ICNS", append_images=[images[s] for s in sorted(images) if s != max(images)])
    return buffer.getvalue()


WEBFONT_UNICODES = (list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) +
                    [0x131, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x203A, 0x2190, 0x2192])


def webfont_bytes(font_file: str) -> bytes:
    """A Latin subset of a brand font as WOFF2, for the HTML documentation."""
    from fontTools import subset

    options = subset.Options()
    options.flavor = "woff2"
    options.layout_features = ["kern", "liga", "calt", "ccmp", "locl", "mark", "mkmk"]
    options.name_IDs = ["*"]  # keep the copyright and licence records
    font = subset.load_font(str(FONT_DIR / font_file), options)
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=WEBFONT_UNICODES)
    subsetter.subset(font)
    buffer = io.BytesIO()
    subset.save_font(font, buffer, options)
    return buffer.getvalue()


def qrc_text() -> str:
    lines = ['<!DOCTYPE RCC>', '<RCC version="1.0">', '<qresource prefix="/branding">']
    for size in QRC_SIZES:
        lines.append(f'\t<file alias="vibestudio-{size}.png">icons/png/vibestudio-{size}.png</file>')
    lines += ['</qresource>', '</RCC>']
    return "\n".join(lines) + "\n"


def generate(check: bool) -> int:
    fonts = {
        "heavy": BrandFont.load("Manrope-ExtraBold.ttf"),
        "medium": BrandFont.load("Manrope-SemiBold.ttf"),
    }
    out = Writer(check)
    cache: dict = {}

    def icon(size: int, style: MarkStyle = MarkStyle()):
        key = (size, style)
        if key not in cache:
            cache[key] = icon_png(size, style)
        return cache[key]

    # Mark and app icons.
    mark = mark_scene()
    out.svg("logo/vibestudio-mark.svg", mark)
    out.svg("icons/vibestudio.svg", mark)
    macos_style = MarkStyle(inset=100.0, shadow=True)
    out.svg("icons/vibestudio-macos.svg", mark_scene(macos_style))
    for size in sorted(set(PNG_SIZES + QRC_SIZES + HICOLOR_SIZES)):
        out.png(f"icons/png/vibestudio-{size}.png", lambda size=size: icon(size))
    out.png("logo/vibestudio-mark.png", lambda: icon(1024))
    out.binary("icons/vibestudio.ico", lambda: ico_bytes({s: icon(s) for s in ICO_SIZES}))
    macos = lambda s: icon(s, macos_style) if s >= 32 else icon(s)
    out.binary("icons/vibestudio.icns", lambda: icns_bytes({s: macos(s) for s in ICNS_SIZES}))
    out.png("icons/vibestudio-macos-1024.png", lambda: macos(1024))
    out.text("vibestudio.qrc", qrc_text())

    # Wordmarks, lockups and banners (transparent backgrounds).
    variants = [("on-dark", ON_DARK, "#CFCFCF"), ("on-light", ON_LIGHT, "#5A5A5A")]
    for suffix, style, tagline_colour in variants:
        scenes = {
            "wordmark": wordmark_scene(fonts, style, "VibeStudio"),
            "logo": lockup_scene(fonts, style, "VibeStudio", with_tagline=False),
            "banner": lockup_scene(fonts, style, "VibeStudio: " + TAGLINE, with_tagline=True, tagline_colour=tagline_colour),
        }
        for kind, scene in scenes.items():
            out.svg(f"logo/vibestudio-{kind}-{suffix}.svg", scene)
            out.png(f"logo/vibestudio-{kind}-{suffix}.png", lambda scene=scene: rasterise(scene, round(scene.width * 2)))

    # Social preview.
    social = social_scene(fonts)
    out.svg("social/vibestudio-social-preview.svg", social)
    out.png("social/vibestudio-social-preview.png", lambda: rasterise(social, 1280, 3))

    # Web: favicons and fonts for the HTML documentation.
    out.svg("web/favicon.svg", mark)
    out.png("web/favicon-32.png", lambda: icon(32))
    out.png("web/apple-touch-icon.png", lambda: rasterise(mark_scene(MarkStyle(inset=0.0)), 180, 8))
    out.binary("web/fonts/manrope-800.woff2", lambda: webfont_bytes("Manrope-ExtraBold.ttf"))
    out.binary("web/fonts/manrope-600.woff2", lambda: webfont_bytes("Manrope-SemiBold.ttf"))

    # Installer art. Inno Setup wants BMPs; scripts/package_windows_installer.py
    # converts these PNGs (flattened on white) when it builds the installer.
    for scale in (100, 200):
        factor = scale / 100
        out.png(f"installer/windows-wizard-{scale}.png",
                lambda factor=factor: rasterise(wizard_scene(164, 314), round(164 * factor), 4))
        out.png(f"installer/windows-wizard-small-{scale}.png",
                lambda factor=factor: rasterise(wizard_small_scene(55), round(55 * factor), 8))
    dmg = dmg_background_scene(fonts)
    out.svg("installer/macos-dmg-background.svg", dmg)
    out.png("installer/macos-dmg-background.png", lambda: rasterise(dmg, 660, 4))
    out.png("installer/macos-dmg-background@2x.png", lambda: rasterise(dmg, 1320, 3))

    if check:
        if out.stale:
            print("Branding assets are missing or out of date; run python scripts/generate_branding.py:", file=sys.stderr)
            for name in out.stale:
                print(f"  assets/branding/{name}", file=sys.stderr)
            return 1
        print("Branding assets are current.")
        return 0
    for name in out.written:
        print(f"assets/branding/{name}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate VibeStudio's brand artwork.")
    parser.add_argument("--check", action="store_true", help="Verify committed SVG masters and asset presence; write nothing.")
    parser.add_argument("--skip-missing-tools", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if MISSING_TOOLS is not None:
        print(f"generate_branding needs fontTools (pip install fonttools; Pillow and numpy to write): {MISSING_TOOLS}",
              file=sys.stderr)
        return 77 if args.skip_missing_tools else 2
    try:
        return generate(args.check)
    except FileNotFoundError as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
