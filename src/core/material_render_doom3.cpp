#include "core/material_render_p.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

// The Doom 3 path follows id Software's Doom 3 GPL source (GPL-3.0 with
// additional terms): Material.cpp for stage defaults and texture matrices,
// draw_common.cpp for the depth fill and the ambient passes, tr_render.cpp
// RB_CreateSingleDrawInteractions for pairing bump, diffuse and specular
// stages, tr_deform.cpp for deforms, and the BFG edition's
// interaction.pixel for the BFG shading. Details: docs/MATERIALS.md.

namespace vibestudio::material_render {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

constexpr double kPi = 3.14159265358979323846;

struct Registers {
	QVector<double> values;

	[[nodiscard]] double at(int index, double fallback = 1.0) const
	{
		if (index < 0 || index >= values.size()) {
			return fallback;
		}
		const double value = values.at(index);
		return std::isfinite(value) ? value : 0.0;
	}
};

// The 2x3 texture matrix of a stage: each transform multiplies on the
// right (MultiplyTextureMatrix), so the newest keyword acts on the raw
// coordinates first.
struct TextureMatrix {
	double m[2][3] = {{1, 0, 0}, {0, 1, 0}};

	void multiply(const double n[2][3])
	{
		double r[2][3];
		for (int row = 0; row < 2; ++row) {
			r[row][0] = m[row][0] * n[0][0] + m[row][1] * n[1][0];
			r[row][1] = m[row][0] * n[0][1] + m[row][1] * n[1][1];
			r[row][2] = m[row][0] * n[0][2] + m[row][1] * n[1][2] + m[row][2];
		}
		std::copy(&r[0][0], &r[0][0] + 6, &m[0][0]);
	}
	void apply(double* s, double* t) const
	{
		const double s0 = *s;
		const double t0 = *t;
		*s = m[0][0] * s0 + m[0][1] * t0 + m[0][2];
		*t = m[1][0] * s0 + m[1][1] * t0 + m[1][2];
	}
};

TextureMatrix stageMatrix(const MaterialStage& stage, const Registers& registers, const MaterialTableSet& tables)
{
	TextureMatrix matrix;
	for (const MaterialTexMod& mod : stage.tcMods) {
		const double a = registers.at(mod.expressions[0], 0.0);
		const double b = registers.at(mod.expressions[1], 0.0);
		double n[2][3] = {{1, 0, 0}, {0, 1, 0}};
		switch (mod.kind) {
		case MaterialTexModKind::Translate:
			n[0][2] = a;
			n[1][2] = b;
			break;
		case MaterialTexModKind::ScaleExpression:
			n[0][0] = a;
			n[1][1] = b;
			break;
		case MaterialTexModKind::CenterScale:
			n[0][0] = a;
			n[0][2] = 0.5 - 0.5 * a;
			n[1][1] = b;
			n[1][2] = 0.5 - 0.5 * b;
			break;
		case MaterialTexModKind::Shear:
			n[0][1] = a;
			n[0][2] = -0.5 * a;
			n[1][0] = b;
			n[1][2] = -0.5 * b;
			break;
		case MaterialTexModKind::RotateExpression: {
			// rotate counts whole turns through sinTable and cosTable.
			const MaterialTable* sinTable = tables.find(QStringLiteral("sinTable"));
			const MaterialTable* cosTable = tables.find(QStringLiteral("cosTable"));
			const double sn = sinTable ? lookupMaterialTable(*sinTable, a) : std::sin(2 * kPi * a);
			const double cs = cosTable ? lookupMaterialTable(*cosTable, a) : std::cos(2 * kPi * a);
			n[0][0] = cs;
			n[0][1] = -sn;
			n[0][2] = -0.5 * cs + 0.5 * sn + 0.5;
			n[1][0] = sn;
			n[1][1] = cs;
			n[1][2] = -0.5 * sn - 0.5 * cs + 0.5;
			break;
		}
		default:
			continue;
		}
		matrix.multiply(n);
	}
	// RB_LoadShaderTextureMatrix keeps scrolls from growing without bound.
	for (int row = 0; row < 2; ++row) {
		if (matrix.m[row][2] < -40 || matrix.m[row][2] > 40) {
			matrix.m[row][2] -= static_cast<int>(matrix.m[row][2]);
		}
	}
	return matrix;
}

Color stageColor(const MaterialStage& stage, const Registers& registers)
{
	Color color {registers.at(stage.colorExpressions[0]), registers.at(stage.colorExpressions[1]), registers.at(stage.colorExpressions[2]),
		registers.at(stage.colorExpressions[3])};
	return color.clamped();
}

bool stageActive(const MaterialStage& stage, const Registers& registers)
{
	return stage.conditionExpression < 0 || registers.at(stage.conditionExpression) != 0.0;
}

Wrap stageWrap(const MaterialStage& stage, const MaterialDefinition& definition)
{
	if (stage.zeroClamp || stage.alphaZeroClamp) {
		return Wrap::ZeroClamp;
	}
	if (stage.clamp || stage.imageKind == MaterialImageKind::Video) {
		return Wrap::Clamp;
	}
	// The global clamp keywords set the material's default repeat.
	if (definition.hasFlag(QStringLiteral("zeroclamp")) || definition.hasFlag(QStringLiteral("alphazeroclamp"))) {
		return stage.role == MaterialStageRole::Regular ? Wrap::ZeroClamp : Wrap::Repeat;
	}
	if (definition.hasFlag(QStringLiteral("clamp"))) {
		return stage.role == MaterialStageRole::Regular ? Wrap::Clamp : Wrap::Repeat;
	}
	return Wrap::Repeat;
}

MaterialTexturePtr stageTexture(const MaterialDefinition& definition, const MaterialStage& stage, const MaterialImageSet& images)
{
	switch (stage.imageKind) {
	case MaterialImageKind::White:
		return materialBuiltinTexture(QStringLiteral("_white"));
	case MaterialImageKind::Video:
		return materialVideoPlaceholder();
	case MaterialImageKind::None:
		return images.find(QStringLiteral("_default"));
	default:
		break;
	}
	if (stage.imageProgram >= 0) {
		const MaterialTexturePtr texture = images.find(materialImageProgramText(definition.imagePrograms, stage.imageProgram));
		if (texture && texture->usable()) {
			return texture;
		}
		// Couldn't load: the engine draws _default.
		return images.find(QStringLiteral("_default"));
	}
	if (!stage.imagePath.isEmpty()) {
		return images.find(stage.imagePath);
	}
	return {};
}

// SortInteractionStages: a stable sort by lighting kind inside each group
// that starts at a bump stage; a first group that does not start with a bump
// runs on to the next one.
QVector<int> sortedStageOrder(const MaterialDefinition& definition)
{
	QVector<int> order;
	for (int index = 0; index < definition.stages.size(); ++index) {
		order << index;
	}
	bool lighting = false;
	for (const MaterialStage& stage : definition.stages) {
		lighting |= stage.role != MaterialStageRole::Regular;
	}
	if (!lighting) {
		return order;
	}
	const auto rank = [&](int index) {
		switch (definition.stages.at(index).role) {
		case MaterialStageRole::Regular:
			return 0;
		case MaterialStageRole::Bump:
			return 1;
		case MaterialStageRole::Diffuse:
			return 2;
		case MaterialStageRole::Specular:
			return 3;
		}
		return 0;
	};
	const int count = static_cast<int>(order.size());
	for (int i = 0; i < count;) {
		int j = i + 1;
		for (; j < count; ++j) {
			if (definition.stages.at(order.at(j)).role == MaterialStageRole::Bump) {
				if (definition.stages.at(order.at(i)).role != MaterialStageRole::Bump) {
					continue;
				}
				break;
			}
		}
		std::stable_sort(order.begin() + i, order.begin() + j, [&](int a, int b) { return rank(a) < rank(b); });
		i = j;
	}
	return order;
}

struct Interaction {
	const MaterialStage* bump = nullptr;
	const MaterialStage* diffuse = nullptr;
	const MaterialStage* specular = nullptr;
	Color diffuseColor;
	Color specularColor;
	MaterialVertexColor vertexColor = MaterialVertexColor::None;
};

// RB_CreateSingleDrawInteractions: a bump flushes and clears; a second
// diffuse or specular flushes and replaces just that slot.
QVector<Interaction> pairInteractions(const MaterialDefinition& definition, const QVector<int>& order, const Registers& registers, bool* implicitBump)
{
	QVector<Interaction> interactions;
	Interaction current;
	bool haveBump = false;
	const auto flush = [&]() {
		if (haveBump && (current.diffuse || current.specular)) {
			interactions.push_back(current);
		}
	};
	// _flat stands in when diffuse or specular stages have no bump.
	bool anyBump = false;
	for (int index : order) {
		anyBump |= definition.stages.at(index).role == MaterialStageRole::Bump;
	}
	*implicitBump = !anyBump;
	haveBump = !anyBump;
	for (int index : order) {
		const MaterialStage& stage = definition.stages.at(index);
		if (stage.role == MaterialStageRole::Regular || !stageActive(stage, registers)) {
			continue;
		}
		switch (stage.role) {
		case MaterialStageRole::Bump:
			flush();
			current = Interaction();
			current.bump = &stage;
			haveBump = true;
			break;
		case MaterialStageRole::Diffuse:
			if (current.diffuse) {
				flush();
			}
			current.diffuse = &stage;
			current.diffuseColor = stageColor(stage, registers);
			current.vertexColor = stage.vertexColor;
			break;
		case MaterialStageRole::Specular:
			if (current.specular) {
				flush();
			}
			current.specular = &stage;
			current.specularColor = stageColor(stage, registers);
			current.vertexColor = stage.vertexColor;
			break;
		default:
			break;
		}
	}
	flush();
	return interactions;
}

void applyDoom3Deforms(const MaterialDefinition& definition, Mesh* mesh, const Camera& camera, const Registers& registers, const MaterialTableSet& tables,
	QStringList* notes)
{
	for (const MaterialDeform& deform : definition.deforms) {
		switch (deform.kind) {
		case MaterialDeformKind::Expand: {
			const double amount = registers.at(deform.expressions[0], 0.0);
			for (Vertex& vertex : mesh->vertices) {
				vertex.position = vertex.position + vertex.normal * amount;
			}
			break;
		}
		case MaterialDeformKind::MoveExpression: {
			const double amount = registers.at(deform.expressions[0], 0.0);
			for (Vertex& vertex : mesh->vertices) {
				vertex.position.x += amount;
			}
			break;
		}
		case MaterialDeformKind::Turbulent: {
			// R_TurbulentDeform, including the time offset that ends up
			// added twice.
			const MaterialTable* table = tables.find(deform.name);
			if (!table) {
				notes->push_back(Text::tr("deform turbulent reads table %1, which is not loaded.").arg(deform.name));
				break;
			}
			const double range = registers.at(deform.expressions[0], 0.0);
			const double timeOffset = registers.at(deform.expressions[1], 0.0);
			const double domain = registers.at(deform.expressions[2], 0.0);
			for (Vertex& vertex : mesh->vertices) {
				double f = vertex.position.x * 0.003 + vertex.position.y * 0.007 + vertex.position.z * 0.011;
				f = 2 * timeOffset + domain * f;
				vertex.s += range * lookupMaterialTable(*table, f);
				vertex.t += range * lookupMaterialTable(*table, f + 0.5);
			}
			break;
		}
		case MaterialDeformKind::Sprite:
		case MaterialDeformKind::Tube:
		case MaterialDeformKind::Flare: {
			// The quad turns to face the viewer (tube about its long axis).
			Vec3 forward = camera.eye - mesh->center;
			if (deform.kind == MaterialDeformKind::Tube) {
				forward.z = 0;
			}
			forward = forward.normalized();
			const Vec3 up = deform.kind == MaterialDeformKind::Tube || std::abs(forward.z) > 0.95 ? Vec3 {0, 0, 1} : camera.up;
			const Vec3 right = up.cross(forward).normalized() * -1.0;
			const Vec3 trueUp = forward.cross(right).normalized() * -1.0;
			for (Vertex& vertex : mesh->vertices) {
				const Vec3 local = vertex.position - mesh->center;
				vertex.position = mesh->center + right * local.y + trueUp * local.z;
				vertex.normal = forward;
			}
			if (deform.kind == MaterialDeformKind::Flare) {
				notes->push_back(Text::tr("deform flare is shown as a camera-facing quad without its glow ring."));
			}
			break;
		}
		case MaterialDeformKind::EyeBall:
		case MaterialDeformKind::Particle:
		case MaterialDeformKind::Particle2:
			notes->push_back(Text::tr("deform %1 needs model geometry or particle decls; the preview leaves the shape as it is.")
								 .arg(materialDeformKindId(deform.kind)));
			break;
		default:
			break;
		}
	}
}

// One preview light: box attenuation (R_DeriveLightData) with a projection
// and a falloff image, coloured 2 x the light stage's RGB. The shaders
// sample it (material_doom3.frag).
struct PreviewLight {
	Vec3 origin;
	Vec3 radius {256, 256, 256};
	Color color {2, 2, 2, 1};
};

// The "very ad-hoc wobble transform" of the skybox texgen, as three rows the
// shader dots a direction with.
std::array<Vec3, 3> wobbleSkyRows(double degrees, double wobbleRpm, double rotateRpm, double time)
{
	const double wobble = degrees * kPi / 180.0;
	const double wobbleSpeed = wobbleRpm * 2 * kPi / 60.0;
	const double rotateSpeed = rotateRpm * 2 * kPi / 60.0;
	const double a = time * wobbleSpeed;
	Vec3 axis2 {std::cos(a) * std::sin(wobble), std::sin(a) * std::sin(wobble), std::cos(wobble)};
	Vec3 axis1;
	axis1.x = -std::sin(a * 2) * std::sin(wobble);
	axis1.z = -axis2.y * axis2.x;
	axis1.y = std::sqrt(std::max(0.0, 1.0 - (axis1.x * axis1.x + axis1.z * axis1.z)));
	axis1 = (axis1 - axis2 * axis2.dot(axis1)).normalized();
	const Vec3 axis0 = axis1.cross(axis2);
	const double s = std::sin(rotateSpeed * time);
	const double c = std::cos(rotateSpeed * time);
	const Vec3 row0 {axis0.x * c + axis1.x * s, axis1.x * c - axis0.x * s, axis2.x};
	const Vec3 row1 {axis0.y * c + axis1.y * s, axis1.y * c - axis0.y * s, axis2.y};
	const Vec3 row2 {axis0.z * c + axis1.z * s, axis1.z * c - axis0.z * s, axis2.z};
	return {Vec3 {row0.x, row1.x, row2.x}, Vec3 {row0.y, row1.y, row2.y}, Vec3 {row0.z, row1.z, row2.z}};
}

void setLight(MaterialUniforms* u, const PreviewLight& light)
{
	u->light[0] = static_cast<float>(light.origin.x);
	u->light[1] = static_cast<float>(light.origin.y);
	u->light[2] = static_cast<float>(light.origin.z);
	u->setParam(0, light.radius.x, light.radius.y, light.radius.z);
	u->setParam(1, light.color.r, light.color.g, light.color.b);
}

void setMatrix(MaterialUniforms* u, int slot, const TextureMatrix& matrix)
{
	u->setMatrix(slot, matrix.m);
}

PreviewLight defaultLight(const Mesh& mesh, const MaterialRenderOptions& options)
{
	PreviewLight light;
	light.origin = previewLightPosition(mesh, options.lighting, options.time);
	const double radius = options.lighting.lightRadius > 0 ? options.lighting.lightRadius : mesh.radius * 2.2;
	light.radius = {radius, radius, radius};
	const QColor& c = options.lighting.lightColor;
	// r_lightScale 2.
	light.color = {2.0 * c.redF(), 2.0 * c.greenF(), 2.0 * c.blueF(), 1.0};
	return light;
}

} // namespace

MaterialRenderResult renderDoom3Material(const MaterialDefinition& source, const MaterialImageSet& images, const MaterialTableSet& tables,
	const MaterialRenderOptions& options, const std::function<bool()>& cancelled)
{
	MaterialRenderResult result;
	MaterialEvalContext eval = options.eval;
	eval.time = options.time;

	MaterialDefinition definition = source;
	if (options.editorImage && !source.editorImage.isEmpty()) {
		definition = implicitDoom3Material(source.editorImage);
		definition.name = source.name;
	} else if (options.honourRejection && !source.engineRejection.isEmpty()) {
		// Parse errors leave { { blend blend; map _default } }.
		definition = MaterialDefinition();
		definition.name = source.name;
		definition.engine = MaterialEngine::Doom3;
		MaterialStage stage;
		stage.imageKind = MaterialImageKind::Builtin;
		stage.imagePath = QStringLiteral("_default");
		stage.blend.source = MaterialBlendFactor::SourceAlpha;
		stage.blend.destination = MaterialBlendFactor::OneMinusSourceAlpha;
		definition.stages.push_back(stage);
		definition.coverage = QStringLiteral("translucent");
		result.fallback = true;
		result.notes << source.engineRejection;
		if (!options.developerDefault) {
			result.notes << Text::tr("Release builds draw _default as transparent black, so the surface disappears.");
		}
	}
	QStringList missingTables;
	Registers registers;
	registers.values = evaluateMaterialExpressions(definition, tables, eval, &missingTables);
	for (const QString& table : missingTables) {
		result.notes << Text::tr("Table %1 is not loaded; its lookups read 0.").arg(table);
	}
	for (const QString& name : tables.names()) {
		const MaterialTable* table = tables.find(name);
		if (table && table->generated) {
			for (const MaterialExpressionNode& node : definition.expressions) {
				if (node.op == MaterialExpressionOp::Table && node.name.compare(name, Qt::CaseInsensitive) == 0) {
					result.notes << Text::tr("%1 is a generated stand-in; mount the game's tables.mtr for its real values.").arg(name);
					break;
				}
			}
		}
	}

	const QSize imageSize = mainImageSize(definition, images);
	const double repeatS = std::max(16.0, imageSize.width() / 2.0);
	const double repeatT = std::max(16.0, imageSize.height() / 2.0);
	const bool light = definition.isLight() && !result.fallback;
	MaterialPreviewShape shape = options.shape;
	const double tiling = options.tiling > 0 ? options.tiling : 2.0;
	Mesh mesh = buildMesh(shape, light ? 128.0 : repeatS, light ? 128.0 : repeatT, tiling, 32.0);
	MaterialRenderOptions cameraOptions = options;
	const Camera camera = makeCamera(mesh, cameraOptions);
	applyDoom3Deforms(definition, &mesh, camera, registers, tables, &result.notes);

	MaterialGpuFrame gpu(camera, mesh, options);
	gpu.background(options.background, options.checker, 1.0);
	const CullMode cull = cullModeFor(definition.cull);
	const int vertices = gpu.vertices();
	GpuState fillState;
	fillState.depthTest = true;
	fillState.depthWrite = true;
	fillState.depthCompare = GpuCompare::Greater;
	// Light added over what is there, clamped; alpha left alone.
	GpuState addState;
	addState.blend = true;
	addState.sourceColor = GpuBlend::One;
	addState.destinationColor = GpuBlend::One;
	addState.sourceAlpha = GpuBlend::Zero;
	addState.destinationAlpha = GpuBlend::One;
	addState.depthTest = true;
	addState.depthCompare = GpuCompare::Equal;

	if (light) {
		// A light material lights a neutral test surface: each light stage
		// that passes its condition is a pass, coloured 2 x its RGB,
		// projected through its image and faded by the falloff image.
		{
			MaterialUniforms u = gpu.uniforms();
			u.mode[0] = 0;
			GpuState state = fillState;
			state.cull = gpuCull(CullMode::Front);
			gpu.draw(GpuProgram::MaterialDoom3, u, state, vertices, GpuDraw());
		}
		const MaterialTexturePtr falloff = definition.lightFalloffImage.isEmpty() ? MaterialTexturePtr() : images.find(definition.lightFalloffImage);
		for (const MaterialStage& stage : definition.stages) {
			if (cancelled && cancelled()) {
				result.cancelled = true;
				return result;
			}
			if (!stageActive(stage, registers)) {
				continue;
			}
			PreviewLight preview = defaultLight(mesh, options);
			const Color rgb = Color {registers.at(stage.colorExpressions[0]), registers.at(stage.colorExpressions[1]),
				registers.at(stage.colorExpressions[2]), 1.0};
			if (rgb.r <= 0.001 && rgb.g <= 0.001 && rgb.b <= 0.001) {
				continue;
			}
			preview.color = {2.0 * rgb.r, 2.0 * rgb.g, 2.0 * rgb.b, 1.0};
			const MaterialTexturePtr projection = stageTexture(definition, stage, images);
			const bool projected = projection && projection->usable();
			const bool fades = falloff && falloff->usable();
			MaterialUniforms u = gpu.uniforms();
			u.mode[0] = 3;
			u.mode[2] = (projected ? 1 : 0) | (fades ? 2 : 0) | (definition.ambientLight ? 4 : 0) | (definition.blendLight ? 8 : 0);
			setLight(&u, preview);
			setMatrix(&u, 3, stageMatrix(stage, registers, tables));
			GpuDraw textures;
			const Wrap projectionWrap = stage.zeroClamp || stage.alphaZeroClamp ? Wrap::ZeroClamp : (stage.clamp ? Wrap::Clamp : Wrap::Repeat);
			gpu.bind(&textures, &u, 3, projected ? gpu.texture(projection.get()) : -1, projectionWrap);
			gpu.bind(&textures, &u, 4, fades ? gpu.texture(falloff.get()) : -1, Wrap::ZeroClamp);
			GpuState state = addState;
			state.cull = gpuCull(CullMode::Front);
			if (definition.blendLight) {
				state = stageState(stage.blend, GpuCompare::Equal, false, CullMode::Front);
			}
			gpu.draw(GpuProgram::MaterialDoom3, u, state, vertices, textures);
			++result.stagesDrawn;
		}
		if (definition.fogLight) {
			result.notes << Text::tr("Fog lights are shown as projected light on the test surface, without the fog volume.");
		}
		if (falloff == nullptr) {
			result.notes << Text::tr("No falloff image: the preview uses _quadratic.");
		}
		gpu.finish(1.0, &result, cancelled);
		return result;
	}

	const QString coverage = definition.coverage.isEmpty() ? QStringLiteral("opaque") : definition.coverage;
	const bool translucent = coverage == QStringLiteral("translucent");
	const QVector<int> order = sortedStageOrder(definition);

	// 1. The depth fill: opaque surfaces become black; perforated ones only
	// where an alpha-tested stage passes.
	if (!translucent) {
		MaterialUniforms u = gpu.uniforms();
		u.mode[0] = 0;
		GpuDraw textures;
		if (coverage == QStringLiteral("perforated")) {
			int tested = 0;
			for (int index : order) {
				const MaterialStage& stage = definition.stages.at(index);
				if (tested >= 4 || stage.alphaTest != MaterialAlphaTest::Expression || !stageActive(stage, registers)) {
					continue;
				}
				const MaterialTexturePtr texture = stageTexture(definition, stage, images);
				if (!texture || !texture->usable()) {
					continue;
				}
				gpu.bind(&textures, &u, tested, gpu.texture(texture.get()), stageWrap(stage, definition));
				setMatrix(&u, tested, stageMatrix(stage, registers, tables));
				u.setParam(tested, registers.at(stage.alphaTestExpression, 0.5), stageColor(stage, registers).a);
				++tested;
			}
			u.mode[2] = 1;
			u.mode[3] = tested;
		}
		GpuState state = fillState;
		state.cull = gpuCull(cull);
		gpu.draw(GpuProgram::MaterialDoom3, u, state, vertices, textures);
	}

	// 2. Interactions with the preview light, added where depth matches.
	bool implicitBump = false;
	const QVector<Interaction> interactions = translucent ? QVector<Interaction>() : pairInteractions(definition, order, registers, &implicitBump);
	if (!interactions.isEmpty()) {
		const PreviewLight preview = defaultLight(mesh, options);
		for (const Interaction& interaction : interactions) {
			MaterialUniforms u = gpu.uniforms();
			u.mode[0] = 1;
			setLight(&u, preview);
			GpuDraw textures;
			int flags = 0;
			const auto bindStage = [&](const MaterialStage* stage, int slot, int bit) {
				if (!stage) {
					return;
				}
				const MaterialTexturePtr texture = stageTexture(definition, *stage, images);
				if (!texture || !texture->usable()) {
					return;
				}
				gpu.bind(&textures, &u, slot, gpu.texture(texture.get()), stageWrap(*stage, definition));
				setMatrix(&u, slot, stageMatrix(*stage, registers, tables));
				flags |= bit;
			};
			bindStage(interaction.bump, 0, 4);
			bindStage(interaction.diffuse, 1, 8);
			bindStage(interaction.specular, 2, 16);
			flags |= options.lighting.doom3Specular ? 32 : 0;
			flags |= options.lighting.doom3Shading == MaterialDoom3Shading::Bfg ? 64 : 0;
			flags |= interaction.vertexColor == MaterialVertexColor::InverseVertex ? 128 : 0;
			flags |= options.lighting.doom3Ambient > 0.0 ? 256 : 0;
			u.mode[2] = flags;
			u.setParam(2, interaction.diffuseColor.r, interaction.diffuseColor.g, interaction.diffuseColor.b, interaction.diffuseColor.a);
			u.setParam(3, interaction.specularColor.r, interaction.specularColor.g, interaction.specularColor.b, interaction.specularColor.a);
			u.setParam(4, options.lighting.doom3Ambient);
			GpuState state = addState;
			state.cull = gpuCull(cull);
			gpu.draw(GpuProgram::MaterialDoom3, u, state, vertices, textures);
		}
		result.stagesDrawn += static_cast<int>(interactions.size());
	}

	// 3. The remaining stages in order, over what was drawn.
	for (int index : order) {
		if (cancelled && cancelled()) {
			result.cancelled = true;
			return result;
		}
		const MaterialStage& stage = definition.stages.at(index);
		if (stage.role != MaterialStageRole::Regular || !stageActive(stage, registers) || stage.blend.writesNothing()) {
			continue;
		}
		const Color color = stageColor(stage, registers);
		// RB_STD_T_RenderShaderPasses skips an add that would be black.
		if (stage.blend.source == MaterialBlendFactor::One && stage.blend.destination == MaterialBlendFactor::One && color.r <= 0 && color.g <= 0
			&& color.b <= 0) {
			continue;
		}
		const TextureMatrix matrix = stageMatrix(stage, registers, tables);
		const MaterialTexturePtr texture = stageTexture(definition, stage, images);
		const MaterialCubeTexturePtr cube = stage.imageKind == MaterialImageKind::CubeMap ? images.findCube(stage.imagePath) : MaterialCubeTexturePtr();
		const bool renderTarget = stage.imageKind == MaterialImageKind::RenderTarget;
		if (!renderTarget && !cube && (!texture || !texture->usable())) {
			if (stage.imageKind == MaterialImageKind::CubeMap) {
				result.notes << Text::tr("Cube map %1 was not found.").arg(stage.imagePath);
			}
			continue;
		}
		if (stage.vertexProgram.size() > 0 || stage.fragmentProgram.size() > 0) {
			result.notes << Text::tr("Stage %1 runs an ARB program in game; the preview draws its image plainly.").arg(index + 1);
		}
		MaterialUniforms u = gpu.uniforms();
		u.mode[0] = 2;
		GpuDraw textures;
		int source = 0;
		switch (stage.tcGen.source) {
		case MaterialTexCoordSource::Normal:
			source = 1;
			break;
		case MaterialTexCoordSource::Reflect:
			source = 2;
			break;
		case MaterialTexCoordSource::Skybox:
			source = 3;
			break;
		case MaterialTexCoordSource::WobbleSky: {
			source = 4;
			const std::array<Vec3, 3> rows = wobbleSkyRows(registers.at(stage.tcGen.expressions[0], 0.0), registers.at(stage.tcGen.expressions[1], 0.0),
				registers.at(stage.tcGen.expressions[2], 0.0), options.time);
			for (int row = 0; row < 3; ++row) {
				u.setParam(row, rows[size_t(row)].x, rows[size_t(row)].y, rows[size_t(row)].z);
			}
			break;
		}
		case MaterialTexCoordSource::Screen:
		case MaterialTexCoordSource::Screen2:
		case MaterialTexCoordSource::GlassWarp:
			source = 5;
			break;
		default:
			break;
		}
		const bool bilinear = options.filtering != MaterialFiltering::Nearest && !stage.nearest;
		int flags = bilinear ? 1 : 0;
		if (renderTarget) {
			// _currentRender reads what is already on screen.
			gpu.bindSnapshot(&textures, &u, 7);
			flags |= 4;
		} else if (cube) {
			for (int face = 0; face < 6; ++face) {
				const MaterialTexturePtr& faceTexture = cube->faces[size_t(face)];
				gpu.bind(&textures, &u, face, faceTexture && faceTexture->usable() ? gpu.texture(faceTexture.get()) : -1, Wrap::Clamp);
			}
			flags |= 2;
		} else {
			gpu.bind(&textures, &u, 0, gpu.texture(texture.get()), stageWrap(stage, definition));
			flags |= 16;
			u.lighting[3] = static_cast<float>(texture->width / std::max(1.0, mesh.repeatS));
		}
		flags |= stage.vertexColor == MaterialVertexColor::InverseVertex ? 8 : 0;
		u.mode[2] = flags;
		u.mode[3] = source;
		u.setColor(color);
		setMatrix(&u, 6, matrix);
		const GpuCompare depth = translucent || stage.ignoreAlphaTest ? GpuCompare::GreaterOrEqual : GpuCompare::Equal;
		GpuState state = stageState(stage.blend, depth, translucent && stage.blend.isOpaqueReplace() && !stage.maskDepth, cull);
		state.colorMask = quint8((stage.maskRed ? 0 : 1) | (stage.maskGreen ? 0 : 2) | (stage.maskBlue ? 0 : 4) | (stage.maskAlpha ? 0 : 8));
		gpu.draw(GpuProgram::MaterialDoom3, u, state, vertices, textures);
		++result.stagesDrawn;
	}
	if (implicitBump && !interactions.isEmpty()) {
		result.notes << Text::tr("No bump stage: lit with _flat, as Doom 3 does.");
	}
	if (!translucent && interactions.isEmpty() && result.stagesDrawn == 0) {
		result.notes << Text::tr("Nothing draws over the depth fill, so the surface is black, as in game.");
	}
	gpu.finish(1.0, &result, cancelled);
	return result;
}

} // namespace vibestudio::material_render
