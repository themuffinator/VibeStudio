#include "core/material_render_p.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

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

constexpr double kPi = 3.14159265358979323846;

int largestPowerOfTwo(int value)
{
	int power = 1;
	while (power * 2 <= value) {
		power *= 2;
	}
	return power;
}

QRgb paletteColor(const MaterialImageSet& images, const MaterialTexture& texture, int index)
{
	if (images.palette.size() >= 256) {
		return images.palette.at(index & 255) | 0xff000000u;
	}
	return texture.palette.value(index & 255) | 0xff000000u;
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

Color fromRgb(QRgb rgb)
{
	return Color::fromRgb(rgb);
}

// ---------------------------------------------------------------------------
// Doom
// ---------------------------------------------------------------------------

struct DoomLighting {
	int band = 0;
	bool distance = true;
	bool flat = false;
	int contrast = 0;

	// The colormap row for a view depth (r_main.c zlight / scalelight).
	[[nodiscard]] int level(double depth) const
	{
		const int startmap = 4 * (15 - std::clamp(band + contrast, 0, 15));
		const double d = distance ? std::max(1.0, depth) : 128.0;
		if (flat) {
			const int j = std::clamp(static_cast<int>(d / 16.0), 0, 127);
			return std::clamp(startmap - (160 / (j + 1)) / 2, 0, 31);
		}
		const int j = std::clamp(static_cast<int>(2560.0 / d), 0, 47);
		return std::clamp(startmap - j / 2, 0, 31);
	}
};

// Texel coordinates of a Doom wall or flat sample, with vanilla wrapping:
// columns wrap at the largest power of two not above the width, rows at
// 128 whatever the height; Boom wraps properly.
bool doomTexel(const MaterialTexture& texture, double sTexel, double tTexel, bool flat, bool boom, int* x, int* y, bool* garbage)
{
	*garbage = false;
	const int column = static_cast<int>(std::floor(sTexel));
	const int row = static_cast<int>(std::floor(tTexel));
	if (flat) {
		*x = ((column % 64) + 64) % 64 % std::max(1, texture.width);
		*y = ((row % 64) + 64) % 64 % std::max(1, texture.height);
		return true;
	}
	if (boom) {
		*x = ((column % texture.width) + texture.width) % texture.width;
		*y = ((row % texture.height) + texture.height) % texture.height;
		return true;
	}
	*x = column & (largestPowerOfTwo(texture.width) - 1);
	*y = row & 127;
	if (*y >= texture.height) {
		*garbage = true;
	}
	return true;
}

void doomWarp(MaterialWarpStyle warp, double speed, double time, double* u, double* v)
{
	const double t = time * speed;
	if (warp == MaterialWarpStyle::DoomWarp) {
		// func_warp1.fp: one wave per repeat, an eight-second cycle.
		const double du = 0.1 * std::sin(2 * kPi * (*v + t / 8.0));
		const double dv = 0.1 * std::sin(2 * kPi * (*u + t / 8.0));
		*u += du;
		*v += dv;
	} else if (warp == MaterialWarpStyle::DoomWarp2) {
		// func_warp2.fp: two summed waves with Eternity's phase constants.
		const double u0 = *u;
		const double v0 = *v;
		*u += 0.025 * (0.5 + std::sin(2 * kPi * (v0 + 0.49 * t + 700.0 / 8192.0)) + std::sin(2 * kPi * (2 * u0 + 0.49 * t + 1200.0 / 8192.0)));
		*v += 0.025 * (0.5 + std::sin(2 * kPi * (v0 + 0.61 * t + 900.0 / 8192.0)) + std::sin(2 * kPi * (2 * u0 + 0.36 * t + 300.0 / 8192.0)));
	}
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
	Framebuffer framebuffer;
	framebuffer.reset(camera.width, camera.height);
	framebuffer.clear(options.background, options.checker, 1.0);
	if (!texture || !texture->usable()) {
		result.notes << (texture ? texture->note : Text::tr("The texture was not found."));
		result.image = framebuffer.toImage(1.0);
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
	DoomLighting lighting;
	lighting.band = (std::clamp(options.lighting.doomLight, 0, 255) >> 4) + std::clamp(options.lighting.doomExtraLight, 0, 2);
	lighting.distance = options.lighting.doomDistance;
	lighting.flat = flat;
	const bool boom = options.lighting.doomBoomWrapping;
	bool sawGarbage = false;
	for (const Triangle& triangle : mesh.triangles) {
		if (cancelled && cancelled()) {
			result.cancelled = true;
			return result;
		}
		const Vertex& a = mesh.vertices.at(triangle.a);
		const Vertex& b = mesh.vertices.at(triangle.b);
		const Vertex& c = mesh.vertices.at(triangle.c);
		std::array<Attributes, 3> attributes {};
		const Vertex* vertices[3] = {&a, &b, &c};
		for (int k = 0; k < 3; ++k) {
			Attributes& at = attributes[static_cast<size_t>(k)];
			at[0] = vertices[k]->s * (flat ? 64.0 : size.width());
			at[1] = vertices[k]->t * (flat ? 64.0 : size.height());
			at[2] = vertices[k]->normal.x;
			at[3] = vertices[k]->normal.y;
			at[4] = vertices[k]->normal.z;
		}
		rasterizeTriangle(camera, {a.position, b.position, c.position}, attributes, 5, CullMode::Front, [&](const FragmentInput& input) {
			float& depth = framebuffer.depthAt(input.x, input.y);
			if (input.depth >= depth) {
				return;
			}
			float* pixel = framebuffer.pixel(input.x, input.y);
			if (sky) {
				// r_plane.c: 1024 sky columns a turn (a 256-wide sky repeats
				// four times), mirrored; rows from screen height, unlit.
				const Vec3 d = camera.ray(input.x + 0.5, input.y + 0.5);
				const double yaw = std::atan2(d.y, d.x);
				int column = static_cast<int>(std::floor(yaw / (2 * kPi) * 1024.0));
				column &= largestPowerOfTwo(texture->width) - 1;
				const double horizontal = std::max(1.0e-6, std::sqrt(d.x * d.x + d.y * d.y));
				int row = static_cast<int>(std::floor(100.0 - 160.0 * d.z / horizontal)) & 127;
				row = std::min(row, texture->height - 1);
				const qsizetype at = static_cast<qsizetype>(row) * texture->width + std::clamp(column, 0, texture->width - 1);
				const QRgb color = texture->hasIndices() ? paletteColor(images, *texture, texture->indices.at(at)) : texture->pixels.at(at);
				pixel[0] = static_cast<float>(qRed(color) / 255.0);
				pixel[1] = static_cast<float>(qGreen(color) / 255.0);
				pixel[2] = static_cast<float>(qBlue(color) / 255.0);
				pixel[3] = 1.0f;
				depth = static_cast<float>(input.depth);
				return;
			}
			const Attributes& at = *input.attributes;
			double s = at[0];
			double t = at[1];
			if (definition.classic.warp == MaterialWarpStyle::DoomWarp || definition.classic.warp == MaterialWarpStyle::DoomWarp2) {
				const double w = flat ? 64.0 : size.width();
				const double h = flat ? 64.0 : size.height();
				double u = s / w;
				double v = t / h;
				doomWarp(definition.classic.warp, definition.classic.warpSpeed, options.time, &u, &v);
				s = u * w;
				t = v * h;
			}
			int x = 0;
			int y = 0;
			bool garbage = false;
			doomTexel(*texture, s, t, flat, boom, &x, &y, &garbage);
			int index = -1;
			QRgb color = 0;
			if (garbage) {
				// Vanilla reads past the column into other memory.
				sawGarbage = true;
				index = static_cast<int>((static_cast<quint32>(x) * 2654435761u ^ static_cast<quint32>(y) * 40503u) % 256u);
			} else {
				const qsizetype offset = static_cast<qsizetype>(y) * texture->width + x;
				if (qAlpha(texture->pixels.at(offset)) == 0) {
					// Holes in a masked texture show what is behind.
					return;
				}
				index = texture->hasIndices() ? texture->indices.at(offset) : -1;
				color = texture->pixels.at(offset);
			}
			// Fake contrast: walls along X are darker, along Y brighter.
			DoomLighting local = lighting;
			if (!flat && options.lighting.doomFakeContrast) {
				const Vec3 normal {at[2], at[3], at[4]};
				if (std::abs(normal.x) > 0.99) {
					local.contrast = 1;
				} else if (std::abs(normal.y) > 0.99) {
					local.contrast = -1;
				}
			}
			const int level = local.level(input.depth);
			if (useColormap && index >= 0) {
				color = paletteColor(images, *texture, static_cast<quint8>(colormap.at(level * 256 + index)));
			} else if (index >= 0 && images.palette.size() >= 256) {
				const double factor = 1.0 - level / 32.0;
				const QRgb base = images.palette.at(index);
				color = qRgb(static_cast<int>(qRed(base) * factor), static_cast<int>(qGreen(base) * factor), static_cast<int>(qBlue(base) * factor));
			} else {
				const double factor = 1.0 - level / 32.0;
				color = qRgb(static_cast<int>(qRed(color) * factor), static_cast<int>(qGreen(color) * factor), static_cast<int>(qBlue(color) * factor));
			}
			pixel[0] = static_cast<float>(qRed(color) / 255.0);
			pixel[1] = static_cast<float>(qGreen(color) / 255.0);
			pixel[2] = static_cast<float>(qBlue(color) / 255.0);
			pixel[3] = 1.0f;
			depth = static_cast<float>(input.depth);
		});
	}
	if (sawGarbage) {
		result.notes << Text::tr("This texture is shorter than 128 pixels and tiles vertically: vanilla Doom draws garbage there (tutti-frutti). "
								 "Boom and later ports wrap it.");
	}
	if (!definition.classic.switchPartner.isEmpty()) {
		result.notes << Text::tr("Switch: %1 when pressed.").arg(definition.classic.switchPartner);
	}
	result.stagesDrawn = 1;
	result.image = framebuffer.toImage(1.0);
	return result;
}

// ---------------------------------------------------------------------------
// Quake
// ---------------------------------------------------------------------------

Color quakeSkyLayer(const MaterialTexture& texture, const MaterialImageSet& images, double s, double t, bool front, bool bilinear)
{
	// R_InitSky: the right half is the back layer, the left half the front
	// layer with palette index 0 transparent.
	const int half = std::max(1, texture.width / 2);
	const int x0 = front ? 0 : half;
	const auto texel = [&](int x, int y) -> Color {
		const int xx = x0 + ((x % half) + half) % half;
		const int yy = ((y % texture.height) + texture.height) % texture.height;
		const qsizetype at = static_cast<qsizetype>(yy) * texture.width + xx;
		if (front && texture.hasIndices() && texture.indices.at(at) == 0) {
			return {0, 0, 0, 0};
		}
		const QRgb rgb = texture.hasIndices() ? paletteColor(images, texture, texture.indices.at(at)) : texture.pixels.at(at);
		return fromRgb(rgb);
	};
	const double u = s * half;
	const double v = t * texture.height;
	if (!bilinear) {
		return texel(static_cast<int>(std::floor(u)), static_cast<int>(std::floor(v)));
	}
	const int x = static_cast<int>(std::floor(u - 0.5));
	const int y = static_cast<int>(std::floor(v - 0.5));
	const double fx = u - 0.5 - x;
	const double fy = v - 0.5 - y;
	return texel(x, y) * ((1 - fx) * (1 - fy)) + texel(x + 1, y) * (fx * (1 - fy)) + texel(x, y + 1) * ((1 - fx) * fy)
		+ texel(x + 1, y + 1) * (fx * fy);
}

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
	Framebuffer framebuffer;
	framebuffer.reset(camera.width, camera.height);
	framebuffer.clear(options.background, options.checker, 1.0);
	if (quake2 && (flags & kQuake2SurfNoDraw) != 0) {
		result.notes << Text::tr("nodraw surfaces are not drawn in game.");
		result.image = framebuffer.toImage(1.0);
		return result;
	}
	const MaterialCubeTexturePtr skyBox = quake2 && sky && !images.cubes.isEmpty() ? images.cubes.cbegin().value() : MaterialCubeTexturePtr();
	if (!sky && (!texture || !texture->usable())) {
		result.notes << (texture ? texture->note : Text::tr("The texture was not found."));
		result.image = framebuffer.toImage(1.0);
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
	const bool useColormap = software && colormap.size() >= 64 * 256 && texture && texture->hasIndices();
	const double intensity = std::max(1.0, options.lighting.quake2Intensity);
	bool sawFlowingTranslucent = false;
	for (const Triangle& triangle : mesh.triangles) {
		if (cancelled && cancelled()) {
			result.cancelled = true;
			return result;
		}
		const Vertex& a = mesh.vertices.at(triangle.a);
		const Vertex& b = mesh.vertices.at(triangle.b);
		const Vertex& c = mesh.vertices.at(triangle.c);
		std::array<Attributes, 3> attributes {};
		const Vertex* vertices[3] = {&a, &b, &c};
		for (int k = 0; k < 3; ++k) {
			Attributes& at = attributes[static_cast<size_t>(k)];
			// Texel coordinates without the texture offset, as the warps use.
			at[0] = vertices[k]->s * size.width();
			at[1] = vertices[k]->t * size.height();
			at[2] = vertices[k]->position.x;
			at[3] = vertices[k]->position.y;
			at[4] = vertices[k]->position.z;
			at[5] = vertices[k]->normal.x;
			at[6] = vertices[k]->normal.y;
			at[7] = vertices[k]->normal.z;
		}
		rasterizeTriangle(camera, {a.position, b.position, c.position}, attributes, 8, CullMode::Front, [&](const FragmentInput& input) {
			float& depth = framebuffer.depthAt(input.x, input.y);
			if (input.depth >= depth) {
				return;
			}
			const Attributes& at = *input.attributes;
			const Vec3 position {at[2], at[3], at[4]};
			const Vec3 normal = Vec3 {at[5], at[6], at[7]}.normalized();
			float* pixel = framebuffer.pixel(input.x, input.y);
			if (sky) {
				Color color {0, 0, 0, 1};
				if (quake2) {
					if (skyBox) {
						color = sampleSkyBox(*skyBox, position - camera.eye);
					}
				} else if (texture && texture->usable()) {
					// EmitSkyPolys: flatten the dome by 3 in z, project to
					// radius 378, scroll the back layer 8 and the front 16
					// texels a second.
					Vec3 d = position - camera.eye;
					d.z *= 3;
					const double length = std::max(1.0e-6, d.length());
					d.x *= 378.0 / length;
					d.y *= 378.0 / length;
					const auto layer = [&](double speed, bool front) {
						double scroll = options.time * speed;
						scroll -= static_cast<int>(scroll) & ~127;
						return quakeSkyLayer(*texture, images, (scroll + d.x) / 128.0, (scroll + d.y) / 128.0, front, bilinear);
					};
					const Color back = layer(8.0, false);
					const Color front = layer(16.0, true);
					color = Color {back.r * (1 - front.a) + front.r * front.a, back.g * (1 - front.a) + front.g * front.a,
						back.b * (1 - front.a) + front.b * front.a, 1.0};
				}
				pixel[0] = static_cast<float>(color.r);
				pixel[1] = static_cast<float>(color.g);
				pixel[2] = static_cast<float>(color.b);
				pixel[3] = 1.0f;
				depth = static_cast<float>(input.depth);
				return;
			}
			double s = at[0] / size.width();
			double t = at[1] / size.height();
			int index = -1;
			Color sample;
			if (warp) {
				const double os = at[0];
				const double ot = at[1];
				if (software && !quake2) {
					// d_scan.c: a 128-texel cycle at 20 steps a second,
					// wrapping at 64 texels.
					const int k = static_cast<int>(options.time * 20.0) & 127;
					const auto turb = [](double value) { return 8.0 + 8.0 * std::sin(2 * kPi * value / 128.0); };
					const int sx = ((static_cast<int>(std::floor(os + turb(std::floor(ot) + k))) % 64) + 64) % 64;
					const int ty = ((static_cast<int>(std::floor(ot + turb(std::floor(os) + k))) % 64) + 64) % 64;
					s = (sx + 0.5) / size.width();
					t = (ty + 0.5) / size.height();
				} else {
					// EmitWaterPolys: (os + 8 sin(ot / 8 + time)) / 64; Quake
					// II adds the flowing scroll before dividing.
					double scroll = 0.0;
					if (quake2 && flowing) {
						scroll = -64.0 * fract(options.time / 2.0);
					}
					s = (os + 8.0 * std::sin(ot * 0.125 + options.time) + scroll) / 64.0;
					t = (ot + 8.0 * std::sin(os * 0.125 + options.time)) / 64.0;
				}
			} else if (quake2 && flowing) {
				if (translucency < 1.0) {
					// R_DrawAlphaSurfaces never scrolls these.
					sawFlowingTranslucent = true;
				} else {
					double scroll = -64.0 * fract(options.time / 40.0);
					if (scroll == 0.0) {
						scroll = -64.0;
					}
					s += scroll;
				}
			}
			const Vec3 view = (position - camera.eye).normalized();
			const double obliquity = std::abs(normal.dot(view));
			const double lod = mipLevel(camera, input.depth, obliquity, 1.0);
			if (software || useColormap) {
				index = sampleIndex(*texture, s, t, Wrap::Repeat);
				if (index < 0) {
					return;
				}
				sample = fromRgb(paletteColor(images, *texture, index));
			} else {
				sample = sampleTexture(*texture, s, t, bilinear, Wrap::Repeat, std::min(lod, 3.0));
				index = texture->hasIndices() ? sampleIndex(*texture, s, t, Wrap::Repeat) : -1;
				if (sample.a <= 0.0) {
					return;
				}
				if (sample.a < 0.5 && definition.classic.masked) {
					return;
				}
			}
			Color color = sample;
			if (quake2) {
				// gl_image.c: world textures are scaled by intensity and
				// clamped; unlit surfaces are drawn at 1/intensity.
				Color boosted {std::min(1.0, sample.r * intensity), std::min(1.0, sample.g * intensity), std::min(1.0, sample.b * intensity), 1.0};
				if (warp || translucency < 1.0) {
					color = boosted * (1.0 / intensity);
				} else {
					const double level = previewLightAt(position, normal, mesh, options.lighting, options.time) * 0.5;
					const double lm = std::clamp(level, 0.0, 1.0);
					color = boosted * lm;
				}
				color.a = 1.0;
				if (translucency < 1.0) {
					color.a = translucency;
					blendInto(pixel, color, MaterialBlendFactor::SourceAlpha, MaterialBlendFactor::OneMinusSourceAlpha);
					return;
				}
			} else if (!warp) {
				// The lightmap sample under this pixel, from the preview's
				// level and spot, times the light style.
				const double level = previewLightAt(position, normal, mesh, options.lighting, options.time);
				const double sampleValue = std::clamp(options.lighting.quakeLightmap * level, 0.0, 255.0);
				const double bl = sampleValue * styleRaw;
				const bool fullbright = fullbrights && index >= 224;
				if (useColormap && index >= 0) {
					// r_surf.c: rows 0 (bright) to 63 (dark) of colormap.lmp.
					const int light = std::max(64, static_cast<int>((65280.0 - bl)) >> 2);
					const int row = std::clamp(light >> 8, 0, 63);
					color = fromRgb(paletteColor(images, *texture, static_cast<quint8>(colormap.at(row * 256 + index))));
				} else if (!fullbright) {
					// GLQuake stores min(255, bl >> 7): no overbright. Modern
					// ports store min(255, bl >> 8) and double it at draw time.
					const double factor = overbright ? std::min(255.0, bl / 256.0) / 255.0 * 2.0 : std::min(255.0, bl / 128.0) / 255.0;
					color = sample * factor;
				}
				if (renderer == MaterialQuakeRenderer::ModernPort && companionGlow) {
					const Color added = sampleTexture(*companionGlow, s, t, bilinear, Wrap::Repeat, std::min(lod, 3.0));
					color = color + added * added.a;
				}
			}
			pixel[0] = static_cast<float>(std::clamp(color.r, 0.0, 1.0));
			pixel[1] = static_cast<float>(std::clamp(color.g, 0.0, 1.0));
			pixel[2] = static_cast<float>(std::clamp(color.b, 0.0, 1.0));
			pixel[3] = 1.0f;
			depth = static_cast<float>(input.depth);
		});
	}
	if (sawFlowingTranslucent) {
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
	result.image = framebuffer.toImage(1.0);
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
