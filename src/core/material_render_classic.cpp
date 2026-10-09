#include "core/material_render_p.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <limits>

// Classic pipelines, following id Software's GPL releases:
// - Doom (linuxdoom-1.10): r_main.c R_InitLightTables and
//   R_ExecuteSetViewSize for the colormap tables, r_segs.c fake contrast,
//   r_draw.c column wrapping, r_plane.c flats and sky.
// - Quake (WinQuake): gl_warp.c EmitWaterPolys and EmitSkyPolys, d_scan.c
//   turbulence, gl_rsurf.c R_BuildLightMap and R_TextureAnimation,
//   r_surf.c colormap lighting.
// - Quake II (ref_gl): gl_warp.c, gl_rsurf.c DrawGLFlowingPoly and
//   R_DrawAlphaSurfaces, gl_image.c intensity.
// - ZDoom warps: GZDoom's func_warp1.fp and func_warp2.fp.
// Details and line references: docs/MATERIALS.md.

namespace vibestudio::material_render {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

int largestPowerOfTwo(int value)
{
	int power = 1;
	while (power * 2 <= value) {
		power *= 2;
	}
	return power;
}

// The palette paletted images use: the package's, or the image's own.
const QVector<QRgb>& paletteFor(const MaterialImageSet& images, const MaterialTexture& texture)
{
	return images.palette.size() >= 256 ? images.palette : texture.palette;
}

struct ClassicFrame {
	const MaterialDefinition* definition = nullptr;
	const MaterialImageSet* images = nullptr;
	const MaterialRenderOptions* options = nullptr;
	MaterialTexturePtr texture;
	QString frameName;
	int frameIndex = -1;
};

ClassicFrame pickFrame(const MaterialDefinition& definition, const MaterialImageSet& images, const MaterialRenderOptions& options)
{
	ClassicFrame frame;
	frame.definition = &definition;
	frame.images = &images;
	frame.options = &options;
	QString name = definition.name;
	const QVector<MaterialFrame>& frames
		= options.alternate && !definition.classic.alternateFrames.isEmpty() ? definition.classic.alternateFrames : definition.classic.frames;
	if (!frames.isEmpty()) {
		frame.frameIndex = materialFrameIndexAt(frames, options.time, options.seed);
		if (frame.frameIndex >= 0) {
			name = frames.at(frame.frameIndex).name;
		}
	}
	if (options.alternate && !definition.classic.switchPartner.isEmpty()) {
		name = definition.classic.switchPartner;
	}
	frame.frameName = name;
	frame.texture = images.find(name);
	if (!frame.texture || !frame.texture->usable()) {
		frame.texture = images.find(definition.name);
	}
	return frame;
}

// ---------------------------------------------------------------------------
// Doom
// ---------------------------------------------------------------------------

// Whether vanilla Doom's 128-row wrap reads past a short texture anywhere
// on the shape: rows (t & 127) at or beyond the height.
bool doomGarbageRows(const Mesh& mesh, int height)
{
	if (height >= 128 || mesh.vertices.isEmpty()) {
		return false;
	}
	double low = std::numeric_limits<double>::infinity();
	double high = -low;
	for (const Vertex& vertex : mesh.vertices) {
		low = std::min(low, vertex.t * height);
		high = std::max(high, vertex.t * height);
	}
	for (int row = static_cast<int>(std::floor(low)); row <= static_cast<int>(std::floor(high)) && row - static_cast<int>(std::floor(low)) < 256; ++row) {
		if ((row & 127) >= height) {
			return true;
		}
	}
	return false;
}

MaterialRenderResult renderDoom(const MaterialDefinition& definition, const MaterialImageSet& images, const MaterialRenderOptions& options,
	const std::function<bool()>& cancelled)
{
	MaterialRenderResult result;
	const ClassicFrame frame = pickFrame(definition, images, options);
	result.frameIndex = frame.frameIndex;
	result.frameName = frame.frameName;
	const bool flat = definition.kind == QStringLiteral("doom-flat");
	const bool sky = flat && definition.name.compare(QStringLiteral("F_SKY1"), Qt::CaseInsensitive) == 0;
	MaterialTexturePtr texture = sky ? images.find(QStringLiteral("$doomsky")) : frame.texture;
	const QSize size = texture && texture->usable() ? QSize(texture->width, texture->height) : QSize(64, 64);
	// One texel a map unit.
	const double tiling = options.tiling > 0 ? options.tiling : 2.0;
	MaterialPreviewShape shape = options.shape;
	if (sky && shape != MaterialPreviewShape::Room) {
		shape = MaterialPreviewShape::Room;
	}
	Mesh mesh = buildMesh(shape, flat ? 64.0 : size.width(), flat ? 64.0 : size.height(), tiling, 64.0);
	MaterialRenderOptions cameraOptions = options;
	cameraOptions.shape = shape;
	const Camera camera = makeCamera(mesh, cameraOptions);
	MaterialGpuFrame gpu(camera, mesh, options);
	gpu.background(options.background, options.checker, 1.0);
	if (!texture || !texture->usable()) {
		result.notes << (texture ? texture->note : Text::tr("The texture was not found."));
		gpu.finish(1.0, &result, cancelled);
		return result;
	}
	const QByteArray& colormap = images.doomColormap;
	const bool useColormap = colormap.size() >= 34 * 256 && texture->hasIndices();
	if (!texture->hasIndices()) {
		result.notes << Text::tr("This picture has no palette indices, so COLORMAP shading is approximated by darkening.");
	}
	if (images.doomColormapGenerated) {
		result.notes << Text::tr("Using a generated colormap; mount the game's COLORMAP for exact shading.");
	}
	const int band = (std::clamp(options.lighting.doomLight, 0, 255) >> 4) + std::clamp(options.lighting.doomExtraLight, 0, 2);
	const bool boom = options.lighting.doomBoomWrapping;
	// Texel coordinates at each vertex; the shader wraps, warps and shades.
	const double repeatWidth = flat ? 64.0 : size.width();
	const double repeatHeight = flat ? 64.0 : size.height();
	QVector<StageVertex> perVertex(mesh.vertices.size());
	for (int index = 0; index < mesh.vertices.size(); ++index) {
		perVertex[index].s = mesh.vertices.at(index).s * repeatWidth;
		perVertex[index].t = mesh.vertices.at(index).t * repeatHeight;
	}
	MaterialUniforms u = gpu.uniforms();
	u.mode[0] = sky ? 1 : 0;
	u.mode[2] = (flat ? 1 : 0) | (boom ? 2 : 0) | (useColormap ? 4 : 0) | (texture->hasIndices() ? 8 : 0)
		| (images.palette.size() >= 256 ? 16 : 0) | (options.lighting.doomDistance ? 32 : 0)
		| (options.lighting.doomFakeContrast ? 64 : 0);
	u.mode[3] = definition.classic.warp == MaterialWarpStyle::DoomWarp ? 1 : (definition.classic.warp == MaterialWarpStyle::DoomWarp2 ? 2 : 0);
	u.setParam(0, band, definition.classic.warpSpeed, options.time);
	u.setParam(1, repeatWidth, repeatHeight, largestPowerOfTwo(texture->width));
	GpuDraw textures;
	gpu.bind(&textures, &u, 0, gpu.texture(texture.get()), Wrap::Repeat);
	gpu.bind(&textures, &u, 1, gpu.indices(texture.get()), Wrap::Repeat);
	gpu.bind(&textures, &u, 2, gpu.palette(paletteFor(images, *texture)), Wrap::Clamp);
	gpu.bind(&textures, &u, 3, gpu.rows(colormap, 34), Wrap::Clamp);
	GpuState state;
	state.depthTest = true;
	state.depthWrite = true;
	state.depthCompare = GpuCompare::Greater;
	state.cull = gpuCull(CullMode::Front);
	gpu.draw(GpuProgram::MaterialDoom, u, state, gpu.vertices(&perVertex), textures);
	if (!sky && !flat && !boom && definition.classic.warp == MaterialWarpStyle::None && doomGarbageRows(mesh, texture->height)) {
		result.notes << Text::tr("This texture is shorter than 128 pixels and tiles vertically: vanilla Doom draws garbage there (tutti-frutti). "
								 "Boom and later ports wrap it.");
	}
	if (!definition.classic.switchPartner.isEmpty()) {
		result.notes << Text::tr("Switch: %1 when pressed.").arg(definition.classic.switchPartner);
	}
	result.stagesDrawn = 1;
	gpu.finish(1.0, &result, cancelled);
	return result;
}

// ---------------------------------------------------------------------------
// Quake
// ---------------------------------------------------------------------------

MaterialRenderResult renderQuakeFamily(const MaterialDefinition& definition, const MaterialImageSet& images, const MaterialRenderOptions& options,
	const std::function<bool()>& cancelled)
{
	MaterialRenderResult result;
	const bool quake2 = definition.engine == MaterialEngine::Quake2;
	const ClassicFrame frame = pickFrame(definition, images, options);
	result.frameIndex = frame.frameIndex;
	result.frameName = frame.frameName;
	const MaterialTexturePtr texture = frame.texture;
	const quint32 flags = definition.classic.surfaceFlags;
	const bool sky = quake2 ? (flags & kQuake2SurfSky) != 0 : definition.classic.warp == MaterialWarpStyle::QuakeSky;
	const bool warp = quake2 ? (flags & kQuake2SurfWarp) != 0 : definition.classic.warp == MaterialWarpStyle::QuakeTurbulent;
	const bool flowing = quake2 && (flags & kQuake2SurfFlowing) != 0;
	const double translucency = quake2 ? ((flags & kQuake2SurfTrans33) != 0 ? 0.33 : ((flags & kQuake2SurfTrans66) != 0 ? 0.66 : 1.0)) : 1.0;
	const QSize size = texture && texture->usable() ? QSize(texture->width, texture->height) : QSize(64, 64);
	MaterialPreviewShape shape = options.shape;
	if (sky && shape != MaterialPreviewShape::Room) {
		shape = MaterialPreviewShape::Room;
	}
	const double tiling = options.tiling > 0 ? options.tiling : 2.0;
	Mesh mesh = buildMesh(shape, sky ? 128.0 : size.width(), sky ? 128.0 : size.height(), tiling, 64.0);
	MaterialRenderOptions cameraOptions = options;
	cameraOptions.shape = shape;
	const Camera camera = makeCamera(mesh, cameraOptions);
	MaterialGpuFrame gpu(camera, mesh, options);
	gpu.background(options.background, options.checker, 1.0);
	if (quake2 && (flags & kQuake2SurfNoDraw) != 0) {
		result.notes << Text::tr("nodraw surfaces are not drawn in game.");
		gpu.finish(1.0, &result, cancelled);
		return result;
	}
	const MaterialCubeTexturePtr skyBox = quake2 && sky && !images.cubes.isEmpty() ? images.cubes.cbegin().value() : MaterialCubeTexturePtr();
	if (!sky && (!texture || !texture->usable())) {
		result.notes << (texture ? texture->note : Text::tr("The texture was not found."));
		gpu.finish(1.0, &result, cancelled);
		return result;
	}
	const MaterialQuakeRenderer renderer = quake2 ? MaterialQuakeRenderer::GLQuake : options.lighting.quakeRenderer;
	const bool software = renderer == MaterialQuakeRenderer::Software;
	const bool bilinear = options.filtering == MaterialFiltering::Bilinear || (options.filtering == MaterialFiltering::Engine && !software);
	const bool overbright = renderer == MaterialQuakeRenderer::ModernPort;
	const bool fullbrights = renderer != MaterialQuakeRenderer::GLQuake && !quake2;
	const int styleRaw = quakeLightStyleRaw(options.lighting.quakeStyle, options.time);
	const MaterialTexturePtr glow = images.find(definition.name + QStringLiteral("_glow"));
	const MaterialTexturePtr luma = images.find(definition.name + QStringLiteral("_luma"));
	const MaterialTexturePtr companionGlow = glow && glow->usable() ? glow : (luma && luma->usable() ? luma : MaterialTexturePtr());
	if (renderer == MaterialQuakeRenderer::ModernPort && companionGlow) {
		result.notes << Text::tr("Adds the %1 companion as fullbright light.").arg(companionGlow->reference);
	}
	if (images.find(definition.name + QStringLiteral("_norm")) || images.find(definition.name + QStringLiteral("_gloss"))) {
		result.notes << Text::tr("DarkPlaces relief and gloss companions are listed but not lit in the preview.");
	}
	const QByteArray& colormap = images.quakeColormap;
	const bool usable = texture && texture->usable();
	const bool useColormap = software && colormap.size() >= 64 * 256 && usable && texture->hasIndices();
	const double intensity = std::max(1.0, options.lighting.quake2Intensity);
	// Texel coordinates at each vertex, without the texture offset, as the
	// warps use.
	QVector<StageVertex> perVertex(mesh.vertices.size());
	for (int index = 0; index < mesh.vertices.size(); ++index) {
		perVertex[index].s = mesh.vertices.at(index).s * size.width();
		perVertex[index].t = mesh.vertices.at(index).t * size.height();
	}
	MaterialUniforms u = gpu.uniforms();
	u.mode[2] = (bilinear ? 1 : 0) | (quake2 ? 2 : 0) | (sky ? 4 : 0) | (warp ? 8 : 0) | (flowing ? 16 : 0) | (software ? 32 : 0)
		| (useColormap ? 64 : 0) | (overbright ? 128 : 0) | (fullbrights ? 256 : 0) | (usable && texture->hasIndices() ? 512 : 0)
		| (renderer == MaterialQuakeRenderer::ModernPort && companionGlow ? 1024 : 0) | (definition.classic.masked ? 2048 : 0)
		| (skyBox ? 4096 : 0) | (usable ? 8192 : 0);
	u.setParam(0, translucency, intensity, options.time, styleRaw);
	u.setParam(1, options.lighting.quakeLightmap, size.width(), size.height());
	GpuDraw textures;
	if (quake2 && sky) {
		for (int face = 0; face < 6; ++face) {
			const MaterialTexturePtr& faceTexture = skyBox ? skyBox->faces[size_t(face)] : MaterialTexturePtr();
			gpu.bind(&textures, &u, face, faceTexture && faceTexture->usable() ? gpu.texture(faceTexture.get()) : -1, Wrap::Clamp);
		}
	} else if (usable) {
		gpu.bind(&textures, &u, 0, gpu.texture(texture.get()), Wrap::Repeat);
		gpu.bind(&textures, &u, 1, gpu.indices(texture.get()), Wrap::Repeat);
		gpu.bind(&textures, &u, 2, gpu.palette(paletteFor(images, *texture)), Wrap::Clamp);
		gpu.bind(&textures, &u, 3, gpu.rows(colormap, 64), Wrap::Clamp);
		gpu.bind(&textures, &u, 4, companionGlow ? gpu.texture(companionGlow.get()) : -1, Wrap::Repeat);
	}
	GpuState state;
	state.depthTest = true;
	state.depthCompare = GpuCompare::Greater;
	state.cull = gpuCull(CullMode::Front);
	if (quake2 && !sky && translucency < 1.0) {
		// R_DrawAlphaSurfaces: blended over what is behind, depth unchanged.
		state.blend = true;
		state.sourceColor = GpuBlend::SourceAlpha;
		state.destinationColor = GpuBlend::OneMinusSourceAlpha;
		state.sourceAlpha = GpuBlend::SourceAlpha;
		state.destinationAlpha = GpuBlend::OneMinusSourceAlpha;
	} else {
		state.depthWrite = true;
	}
	gpu.draw(GpuProgram::MaterialQuake, u, state, gpu.vertices(&perVertex), textures);
	if (quake2 && flowing && !warp && !sky && translucency < 1.0) {
		result.notes << Text::tr("Quake II does not scroll translucent flowing surfaces that are not warped.");
	}
	if (sky && quake2 && !skyBox) {
		result.notes << Text::tr("No env/ sky box was found to show on this sky surface.");
	}
	if (warp) {
		result.notes << (software && !quake2 ? Text::tr("Liquid warp: the software renderer's turbulence (64-texel tiles).")
											 : Text::tr("Liquid warp: (s + 8 sin(t / 8 + time)) / 64, drawn without a lightmap."));
	}
	result.stagesDrawn = 1;
	gpu.finish(1.0, &result, cancelled);
	return result;
}

} // namespace

MaterialRenderResult renderClassicMaterial(const MaterialDefinition& definition, const MaterialImageSet& images, const MaterialRenderOptions& options,
	const std::function<bool()>& cancelled)
{
	if (definition.engine == MaterialEngine::Doom) {
		return renderDoom(definition, images, options, cancelled);
	}
	return renderQuakeFamily(definition, images, options, cancelled);
}

} // namespace vibestudio::material_render
