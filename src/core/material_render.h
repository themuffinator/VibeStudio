#pragma once

// A software renderer that draws a material the way its engine draws it, on
// a preview shape, at a moment in time.
//
// Each engine keeps its own pipeline (details and sources in
// docs/MATERIALS.md and docs/CREDITS.md, "Materials"):
// - Quake III draws stage after stage over the whole surface with the
//   engine's blend, depth, alpha-test and colour rules, per-vertex texture
//   coordinate modifiers and deforms, overbright lightmaps and display, sky
//   boxes with cloud layers, and fog volumes. A shader the engine would drop
//   is drawn as its default shader.
// - Doom 3 fills depth (with alpha test), adds one interaction per
//   bump/diffuse/specular group for the preview light, then draws the other
//   stages with their expressions, texture matrices and texgens.
// - Doom samples point-wise with vanilla wrapping and shades through COLORMAP
//   by sector light, distance and fake contrast; Quake and Quake II shade
//   with lightmaps, light styles, fullbrights, turbulent liquids, two-layer
//   skies, flowing and translucent surfaces.
//
// Rendering is CPU only, deterministic for the same inputs, bounded and
// cancellable, so the GUI, the CLI and tests share it.

#include "core/material_eval.h"
#include "core/material_images.h"
#include "core/material_model.h"
#include "core/material_script.h"

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>

namespace vibestudio {

enum class MaterialPreviewShape {
	Wall,
	Floor,
	Cube,
	Sphere,
	Cylinder,
	// Inside a box: skies, fog and seeing a surface all around.
	Room,
};

QString materialPreviewShapeId(MaterialPreviewShape shape);
QString materialPreviewShapeDisplayName(MaterialPreviewShape shape);
bool materialPreviewShapeFromId(const QString& id, MaterialPreviewShape* shape);
QVector<MaterialPreviewShape> materialPreviewShapes();

enum class MaterialQuakeRenderer {
	// GLQuake: bilinear, lightmap without overbright, no fullbrights.
	GLQuake,
	// A modern GL port (QuakeSpasm and friends): overbright lightmaps,
	// fullbright colours and _glow/_luma companions.
	ModernPort,
	// The software renderer: point sampling and colormap lighting.
	Software,
};

QString materialQuakeRendererId(MaterialQuakeRenderer renderer);
bool materialQuakeRendererFromId(const QString& id, MaterialQuakeRenderer* renderer);

enum class MaterialDoom3Shading {
	Vanilla, // the ARB2 interaction: specular table and specular map x 2
	Bfg,     // the BFG interaction: pow(N.H, 10) x 2
};

enum class MaterialFiltering {
	Engine, // what the engine does by default
	Nearest,
	Bilinear,
};

QString materialFilteringId(MaterialFiltering filtering);
bool materialFilteringFromId(const QString& id, MaterialFiltering* filtering);

struct MaterialPreviewLighting {
	// Quake III and Quake II lightmaps: 1.0 shows the texture at full
	// brightness; above 1 is overbright.
	double lightmap = 1.0;
	// A brighter spot falling off over the surface rather than one level.
	bool lightmapSpot = true;
	// Quake III fullscreen overbright (r_overBrightBits 1). Off draws like a
	// window: stages at half brightness and no overbright lightmaps.
	bool overbright = true;
	// Quake III light grid for rgbGen lightingDiffuse, 0..255.
	int gridAmbient = 54;
	int gridDirected = 128;
	// Doom sector light (0..255), extralight (0..2), distance shading and
	// fake contrast.
	int doomLight = 160;
	int doomExtraLight = 0;
	bool doomDistance = true;
	bool doomFakeContrast = true;
	bool doomBoomWrapping = false;
	// Quake light style pattern and lightmap sample (0..255).
	QString quakeStyle = QStringLiteral("m");
	int quakeLightmap = 160;
	MaterialQuakeRenderer quakeRenderer = MaterialQuakeRenderer::ModernPort;
	// Quake II's intensity cvar (2 by default).
	double quake2Intensity = 2.0;
	// Doom 3: a point light orbiting the shape.
	QColor lightColor = QColor(255, 255, 255);
	double lightRadius = 0.0; // 0: sized to the shape
	bool lightOrbit = true;
	double lightAngle = 35.0; // degrees, when not orbiting
	double lightOrbitSeconds = 8.0;
	// A Doom 3 ambient light added to the interaction, 0..1.
	double doom3Ambient = 0.0;
	bool doom3Specular = true;
	MaterialDoom3Shading doom3Shading = MaterialDoom3Shading::Vanilla;
};

struct MaterialRenderOptions {
	QSize size {320, 240};
	double time = 0.0;
	MaterialPreviewShape shape = MaterialPreviewShape::Wall;
	// Orbit camera, degrees; zoom 1 frames the shape.
	double yaw = 25.0;
	double pitch = 12.0;
	double zoom = 1.0;
	double fieldOfView = 70.0;
	// Face-on with no perspective: thumbnails and swatches.
	bool orthographic = false;
	// Texture repeats across the shape; 0 picks per shape.
	double tiling = 0.0;
	MaterialSurfaceContext context = MaterialSurfaceContext::World;
	MaterialPreviewLighting lighting;
	// Doom 3 shader parms, globals and sound; Quake III entity colour uses
	// parm0-3 as RGBA.
	MaterialEvalContext eval;
	// Quake +a..+j, Doom switch on, Doom 3 parm-driven states.
	bool alternate = false;
	MaterialFiltering filtering = MaterialFiltering::Engine;
	QColor background = QColor(34, 36, 40);
	bool checker = true;
	// Draw the editor image instead of the stages.
	bool editorImage = false;
	// Draw what the engine draws when it drops the material (default shader,
	// _default), rather than the stages.
	bool honourRejection = true;
	// Doom 3 _default as the developer grid rather than transparent black.
	bool developerDefault = false;
	quint32 seed = 1;
};

struct MaterialRenderResult {
	QImage image;
	// The classic frame showing, when the material animates by frames.
	int frameIndex = -1;
	QString frameName;
	int stagesDrawn = 0;
	// What this frame approximated or skipped, untranslated ids and
	// translated text.
	QStringList notes;
	double milliseconds = 0.0;
	bool cancelled = false;
	// True when the engine's fallback (default shader, _default) was drawn.
	bool fallback = false;
};

MaterialRenderResult renderMaterial(const MaterialDefinition& definition, const MaterialImageSet& images, const MaterialTableSet& tables,
	const MaterialRenderOptions& options, const std::function<bool()>& cancelled = {});

// A square, face-on swatch: one texture repeat, orthographic, for browser
// thumbnails.
MaterialRenderResult renderMaterialSwatch(const MaterialDefinition& definition, const MaterialImageSet& images, const MaterialTableSet& tables,
	int side, double time, const MaterialPreviewLighting& lighting = {});

// The shape a material is best seen on: skies and fog get the room, Doom
// flats and floors the floor, the rest a wall.
MaterialPreviewShape defaultMaterialPreviewShape(const MaterialDefinition& definition);
// The camera pitch a shape reads best at: floors seen from above, walls
// nearly face-on.
double defaultMaterialPreviewPitch(MaterialPreviewShape shape);

} // namespace vibestudio
