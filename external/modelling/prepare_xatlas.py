"""Build a reviewed adaptation without changing the pinned upstream files."""
from pathlib import Path
import hashlib
import json
import sys

source, header, manifest, output, header_output = map(Path, sys.argv[1:])
pin = json.loads(manifest.read_text(encoding="utf-8"))
data = source.read_bytes()
if hashlib.sha256(data).hexdigest() != pin["files"]["xatlas.cpp"]:
    raise SystemExit("xatlas source differs from its reviewed revision")
text = data.decode("utf-8").replace("\r\n", "\n")
original = "\tvoid createColocals()\n\t{\n\t\tif (m_epsilon <= FLT_EPSILON)"
replacement = """\tvoid createColocals()
\t{
\t\t// VibeStudio, 2026-10-04: retain indexed boundaries, including user seams.
\t\t// Automatic positional welding reconnects intentional coincident splits.
\t\tm_nextColocalVertex.resize(m_positions.size());
\t\tm_firstColocalVertex.resize(m_positions.size());
\t\tfor (uint32_t i = 0; i < m_positions.size(); ++i) {
\t\t\tm_nextColocalVertex[i] = i;
\t\t\tm_firstColocalVertex[i] = i;
\t\t}
\t\treturn;
\t\tif (m_epsilon <= FLT_EPSILON)"""
if text.count(original) != 1:
    raise SystemExit("xatlas indexed-topology adaptation no longer applies")
text = text.replace(original, replacement)
rounding = "if (extents.x > 0.0f && extents.y > 0.0f) {"
if text.count(rounding) != 1:
    raise SystemExit("xatlas shape-preserving packing adaptation no longer applies")
# Lightmap texel rounding stretches axes independently. Texture authoring must
# preserve chart shape and relative scale when block alignment is disabled.
text = text.replace(rounding, "if (options.blockAlign && extents.x > 0.0f && extents.y > 0.0f) {")
# VibeStudio, 2026-10-06: keep the upstream raster-mask packer, with independent
# atlas axis bounds. Chart scaling is uniform; this does not stretch a square
# layout after packing. The original reviewed files and licence notices stay intact.
header_data = header.read_bytes()
if hashlib.sha256(header_data).hexdigest() != pin["files"]["xatlas.h"]:
    raise SystemExit("xatlas header differs from its reviewed revision")
header_text = header_data.decode("utf-8").replace("\r\n", "\n")
field = "\tuint32_t resolution = 0;"
if header_text.count(field) != 1:
    raise SystemExit("xatlas rectangular header adaptation no longer applies")
header_text = header_text.replace(field, field + "\n\n\t// VibeStudio: fixed/estimated atlas height; 0 uses resolution (square).\n\tuint32_t resolutionHeight = 0;")

def replace(old, new, count=1):
    global text
    if text.count(old) != count:
        raise SystemExit("xatlas rectangular packing adaptation no longer applies: " + old[:100])
    text = text.replace(old, new)

replace("\t\tconst uint32_t maxResolution = m_texelsPerUnit > 0.0f ? resolution : 0;",
        "\t\tuint32_t resolutionHeight = options.resolution > 0 && options.resolutionHeight > 0 ? options.resolutionHeight + options.padding * 2 : resolution;\n"
        "\t\tconst uint32_t maxResolution = m_texelsPerUnit > 0.0f ? resolution : 0;\n"
        "\t\tconst uint32_t maxResolutionHeight = m_texelsPerUnit > 0.0f ? resolutionHeight : 0;")
replace("sqrtf((resolution * resolution) / texelCount)",
        "sqrtf((resolution * (resolutionHeight > 0 ? resolutionHeight : resolution)) / texelCount)")
replace("\t\tArray<float> chartOrderArray;", "\t\tif (resolutionHeight == 0)\n\t\t\tresolutionHeight = resolution;\n\t\tArray<float> chartOrderArray;")
start = text.index("\t\t\tuint32_t maxChartSize = options.maxChartSize;")
end = text.index("\t\t\t// Align to texel centers and add padding offset.", start)
text = text[:start] + """\t\t\t// VibeStudio: fit both axis limits with one scale, retaining chart shape.
\t\t\tuint32_t maxWidth = options.maxChartSize, maxHeight = options.maxChartSize;
\t\t\tif (maxResolution > 0) {
\t\t\t\tconst uint32_t widthLimit = maxResolution - options.padding * 2;
\t\t\t\tconst uint32_t heightLimit = maxResolutionHeight - options.padding * 2;
\t\t\t\tmaxWidth = maxWidth == 0 ? widthLimit : min(maxWidth, widthLimit);
\t\t\t\tmaxHeight = maxHeight == 0 ? heightLimit : min(maxHeight, heightLimit);
\t\t\t}
\t\t\tif (maxWidth > 0 && maxHeight > 0) {
\t\t\t\tconst Vector2 limit((float)maxWidth - 1.0f, (float)maxHeight - 1.0f);
\t\t\t\tif (extents.x > limit.x || extents.y > limit.y) {
\t\t\t\t\tscale = min(extents.x > 0 ? limit.x / extents.x : FLT_MAX, extents.y > 0 ? limit.y / extents.y : FLT_MAX);
\t\t\t\t\tfor (uint32_t i = 0; i < chart->uniqueVertexCount(); i++) {
\t\t\t\t\t\tVector2 &texcoord = chart->uniqueVertexAt(i);
\t\t\t\t\t\ttexcoord = min(texcoord * scale, limit);
\t\t\t\t\t}
\t\t\t\t}
\t\t\t}
""" + text[end:]
replace("extents.x > resolution || extents.y > resolution", "extents.x > resolution || extents.y > resolutionHeight")
replace("BitImage, resolution, resolution)", "BitImage, resolution, resolutionHeight)")
replace("AtlasImage, resolution, resolution)", "AtlasImage, resolution, resolutionHeight)")
replace("atlasSizes[currentAtlas].y <= (int)maxResolution", "atlasSizes[currentAtlas].y <= (int)maxResolutionHeight")
replace("m_width = m_height = maxResolution - (int)options.padding * 2;",
        "m_width = maxResolution - (int)options.padding * 2;\n\t\t\tm_height = maxResolutionHeight - (int)options.padding * 2;")
replace("uint32_t maxResolution)", "uint32_t maxResolution, uint32_t maxResolutionHeight)", 3)
replace("best_h, best_r, maxResolution);", "best_h, best_r, maxResolution, maxResolutionHeight);")
replace("best_r, attempts, maxResolution);", "best_r, attempts, maxResolution, maxResolutionHeight);")
replace("&best_r, maxResolution);", "&best_r, maxResolution, maxResolutionHeight);")
replace("y > (int)maxResolution - ch", "y > (int)maxResolutionHeight - ch", 2)
replace("yRange = min(yRange, (int)maxResolution - ch);", "yRange = min(yRange, (int)maxResolutionHeight - ch);")
# A rotated chart can exceed one rectangular axis even if its original fits.
# Avoid negative random ranges; the studio currently disables chart rotation.
replace("\t\t\tint x = m_rand.getRange(xRange);", "\t\t\tif (xRange < 0 || yRange < 0)\n\t\t\t\tcontinue;\n\t\t\tint x = m_rand.getRange(xRange);")
output.write_text(text, encoding="utf-8")
header_output.write_text(header_text, encoding="utf-8")
