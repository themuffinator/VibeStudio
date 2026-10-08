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
// and a falloff image, coloured 2 x the light stage's RGB.
struct PreviewLight {
	Vec3 origin;
	Vec3 radius {256, 256, 256};
	Color color {2, 2, 2, 1};
	const MaterialTexture* projection = nullptr;
	TextureMatrix projectionMatrix;
	Wrap projectionWrap = Wrap::ZeroClamp;
	const MaterialTexture* falloff = nullptr;
	bool ambient = false;

	[[nodiscard]] double attenuation(const Vec3& position) const
	{
		const Vec3 local = position - origin;
		double s = 0.5 + local.x / (2 * radius.x);
		double t = 0.5 + local.y / (2 * radius.y);
		const double f = 0.5 + local.z / (2 * radius.z);
		double projected = 0.0;
		if (projection) {
			projectionMatrix.apply(&s, &t);
			const Color sample = sampleTexture(*projection, s, t, true, projectionWrap);
			projected = (sample.r + sample.g + sample.b) / 3.0;
		} else {
			// A soft round spot when no light material is chosen.
			const double dx = s - 0.5;
			const double dy = t - 0.5;
			const double d2 = (dx * dx + dy * dy) * 4.0;
			projected = (s < 0 || s > 1 || t < 0 || t > 1) ? 0.0 : std::clamp(1.0 - d2, 0.0, 1.0);
		}
		double fall = 0.0;
		if (falloff) {
			fall = sampleTexture(*falloff, f, 0.5, true, Wrap::ZeroClamp).r;
		} else {
			// _quadratic: brightest in the middle of the light's depth.
			const double x = f * 32.0;
			double d = std::abs(x - 15.5) - 0.5;
			d = std::max(0.0, d);
			fall = (f < 0 || f > 1) ? 0.0 : std::pow(std::max(0.0, 1.0 - d / 16.0), 2.0);
		}
		return projected * fall;
	}
	[[nodiscard]] Color tint(const Vec3& position) const
	{
		if (!projection) {
			return color;
		}
		const Vec3 local = position - origin;
		double s = 0.5 + local.x / (2 * radius.x);
		double t = 0.5 + local.y / (2 * radius.y);
		projectionMatrix.apply(&s, &t);
		const Color sample = sampleTexture(*projection, s, t, true, projectionWrap);
		const double mean = std::max(1.0e-6, (sample.r + sample.g + sample.b) / 3.0);
		return {color.r * sample.r / mean, color.g * sample.g / mean, color.b * sample.b / mean, 1.0};
	}
};

Vec3 tangentLight(const Vec3& direction, const Vec3& tangent, const Vec3& bitangent, const Vec3& normal)
{
	return Vec3 {direction.dot(tangent), direction.dot(bitangent), direction.dot(normal)}.normalized();
}

Vec3 decodeNormal(const Color& sample)
{
	return Vec3 {sample.r * 2 - 1, sample.g * 2 - 1, sample.b * 2 - 1}.normalized();
}

struct SurfaceFrame {
	Vec3 position;
	Vec3 normal;
	Vec3 tangent;
	Vec3 bitangent;
	double s = 0;
	double t = 0;
};

SurfaceFrame surfaceAt(const Attributes& a)
{
	SurfaceFrame frame;
	frame.position = {a[0], a[1], a[2]};
	frame.normal = Vec3 {a[3], a[4], a[5]}.normalized();
	frame.tangent = Vec3 {a[6], a[7], a[8]}.normalized();
	frame.bitangent = Vec3 {a[9], a[10], a[11]}.normalized();
	frame.s = a[12];
	frame.t = a[13];
	return frame;
}

void fillAttributes(const Vertex& vertex, Attributes* a)
{
	(*a)[0] = vertex.position.x;
	(*a)[1] = vertex.position.y;
	(*a)[2] = vertex.position.z;
	(*a)[3] = vertex.normal.x;
	(*a)[4] = vertex.normal.y;
	(*a)[5] = vertex.normal.z;
	(*a)[6] = vertex.tangent.x;
	(*a)[7] = vertex.tangent.y;
	(*a)[8] = vertex.tangent.z;
	(*a)[9] = vertex.bitangent.x;
	(*a)[10] = vertex.bitangent.y;
	(*a)[11] = vertex.bitangent.z;
	(*a)[12] = vertex.s;
	(*a)[13] = vertex.t;
}

void forEachFragment(const Mesh& mesh, const Camera& camera, CullMode cull, const std::function<void(const FragmentInput&)>& fragment)
{
	for (const Triangle& triangle : mesh.triangles) {
		const Vertex& a = mesh.vertices.at(triangle.a);
		const Vertex& b = mesh.vertices.at(triangle.b);
		const Vertex& c = mesh.vertices.at(triangle.c);
		std::array<Attributes, 3> attributes {};
		fillAttributes(a, &attributes[0]);
		fillAttributes(b, &attributes[1]);
		fillAttributes(c, &attributes[2]);
		rasterizeTriangle(camera, {a.position, b.position, c.position}, attributes, 14, cull, fragment);
	}
}

// Shades one interaction for one light at a surface point.
Color shadeInteraction(const MaterialDefinition& definition, const Interaction& interaction, const PreviewLight& light, const SurfaceFrame& surface,
	const Camera& camera, const MaterialImageSet& images, const Registers& registers, const MaterialTableSet& tables, const MaterialRenderOptions& options)
{
	const double attenuation = light.attenuation(surface.position);
	if (attenuation <= 0.0) {
		return {0, 0, 0, 0};
	}
	const Color lightColor = light.tint(surface.position);
	// Normal from the bump stage (or _flat).
	Vec3 normal {0, 0, 1};
	if (interaction.bump) {
		const MaterialTexturePtr bump = stageTexture(definition, *interaction.bump, images);
		if (bump && bump->usable()) {
			double s = surface.s;
			double t = surface.t;
			stageMatrix(*interaction.bump, registers, tables).apply(&s, &t);
			normal = decodeNormal(sampleTexture(*bump, s, t, true, stageWrap(*interaction.bump, definition)));
		}
	}
	const Vec3 toLight = (light.origin - surface.position).normalized();
	const Vec3 toEye = (camera.eye - surface.position).normalized();
	Vec3 l = tangentLight(toLight, surface.tangent, surface.bitangent, surface.normal);
	if (light.ambient) {
		// Vanilla ambient lights read a fixed direction from _ambient.
		l = Vec3 {0.0, -0.77, 0.785}.normalized();
	}
	const Vec3 v = tangentLight(toEye, surface.tangent, surface.bitangent, surface.normal);
	const Vec3 h = (l + v).normalized();
	const double nDotL = normal.dot(l);
	if (nDotL <= 0.0) {
		return {0, 0, 0, 0};
	}
	const double nDotH = std::max(0.0, normal.dot(h));
	Color diffuse {0, 0, 0, 0};
	if (interaction.diffuse) {
		const MaterialTexturePtr texture = stageTexture(definition, *interaction.diffuse, images);
		if (texture && texture->usable()) {
			double s = surface.s;
			double t = surface.t;
			stageMatrix(*interaction.diffuse, registers, tables).apply(&s, &t);
			diffuse = sampleTexture(*texture, s, t, true, stageWrap(*interaction.diffuse, definition)) * interaction.diffuseColor;
		}
	}
	Color specular {0, 0, 0, 0};
	if (interaction.specular && options.lighting.doom3Specular && !light.ambient) {
		const MaterialTexturePtr texture = stageTexture(definition, *interaction.specular, images);
		if (texture && texture->usable()) {
			double s = surface.s;
			double t = surface.t;
			stageMatrix(*interaction.specular, registers, tables).apply(&s, &t);
			const Color map = sampleTexture(*texture, s, t, true, stageWrap(*interaction.specular, definition));
			double term = 0.0;
			if (options.lighting.doom3Shading == MaterialDoom3Shading::Bfg) {
				term = std::pow(nDotH, 10.0) * 2.0;
			} else {
				// _specularTable, then the specular map doubled.
				const double f = std::max(0.0, 4.0 * nDotH - 3.0);
				term = f * f * 2.0;
			}
			specular = map * interaction.specularColor * term;
		}
	}
	double vertex = 1.0;
	if (interaction.vertexColor == MaterialVertexColor::InverseVertex) {
		vertex = 0.0;
	}
	const double scale = nDotL * attenuation * vertex;
	return {(diffuse.r + specular.r) * scale * lightColor.r, (diffuse.g + specular.g) * scale * lightColor.g,
		(diffuse.b + specular.b) * scale * lightColor.b, 0.0};
}

Vec3 wobbleSky(const Vec3& direction, double degrees, double wobbleRpm, double rotateRpm, double time)
{
	// The "very ad-hoc wobble transform" of the skybox texgen.
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
	return Vec3 {direction.dot(Vec3 {row0.x, row1.x, row2.x}), direction.dot(Vec3 {row0.y, row1.y, row2.y}),
		direction.dot(Vec3 {row0.z, row1.z, row2.z})};
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

	Framebuffer framebuffer;
	framebuffer.reset(camera.width, camera.height);
	framebuffer.clear(options.background, options.checker, 1.0);
	const CullMode cull = cullModeFor(definition.cull);

	if (light) {
		// A light material lights a neutral test surface: each light stage
		// that passes its condition is a pass, coloured 2 x its RGB,
		// projected through its image and faded by the falloff image.
		forEachFragment(mesh, camera, CullMode::Front, [&](const FragmentInput& input) {
			float& depth = framebuffer.depthAt(input.x, input.y);
			if (input.depth >= depth) {
				return;
			}
			depth = static_cast<float>(input.depth);
			float* pixel = framebuffer.pixel(input.x, input.y);
			pixel[0] = pixel[1] = pixel[2] = 0.0f;
			pixel[3] = 1.0f;
		});
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
			preview.projection = projection && projection->usable() ? projection.get() : nullptr;
			preview.projectionMatrix = stageMatrix(stage, registers, tables);
			preview.projectionWrap = stage.zeroClamp || stage.alphaZeroClamp ? Wrap::ZeroClamp : (stage.clamp ? Wrap::Clamp : Wrap::Repeat);
			preview.falloff = falloff && falloff->usable() ? falloff.get() : nullptr;
			preview.ambient = definition.ambientLight;
			forEachFragment(mesh, camera, CullMode::Front, [&](const FragmentInput& input) {
				const float depth = framebuffer.depthAt(input.x, input.y);
				if (std::abs(input.depth - depth) > 1.0e-4 * std::max(1.0, input.depth)) {
					return;
				}
				const SurfaceFrame surface = surfaceAt(*input.attributes);
				const double attenuation = preview.attenuation(surface.position);
				if (attenuation <= 0.0) {
					return;
				}
				const Color tint = preview.tint(surface.position);
				const Vec3 toLight = (preview.origin - surface.position).normalized();
				const double nDotL = definition.ambientLight ? 1.0 : std::max(0.0, surface.normal.dot(toLight));
				// The test surface: mid grey, a faint checker so motion reads.
				const bool checker = (static_cast<int>(std::floor(surface.s * 4)) + static_cast<int>(std::floor(surface.t * 4))) % 2 == 0;
				const double albedo = checker ? 0.55 : 0.45;
				float* pixel = framebuffer.pixel(input.x, input.y);
				if (definition.blendLight) {
					blendInto(pixel, Color {tint.r / 2 * attenuation, tint.g / 2 * attenuation, tint.b / 2 * attenuation, 1.0}, stage.blend.source,
						stage.blend.destination);
					return;
				}
				const double scale = albedo * nDotL * attenuation;
				pixel[0] = static_cast<float>(std::min(1.0, pixel[0] + tint.r * scale));
				pixel[1] = static_cast<float>(std::min(1.0, pixel[1] + tint.g * scale));
				pixel[2] = static_cast<float>(std::min(1.0, pixel[2] + tint.b * scale));
			});
			++result.stagesDrawn;
		}
		if (definition.fogLight) {
			result.notes << Text::tr("Fog lights are shown as projected light on the test surface, without the fog volume.");
		}
		if (falloff == nullptr) {
			result.notes << Text::tr("No falloff image: the preview uses _quadratic.");
		}
		result.image = framebuffer.toImage(1.0);
		return result;
	}

	const QString coverage = definition.coverage.isEmpty() ? QStringLiteral("opaque") : definition.coverage;
	const bool translucent = coverage == QStringLiteral("translucent");
	const QVector<int> order = sortedStageOrder(definition);

	// 1. The depth fill: opaque surfaces become black; perforated ones only
	// where an alpha-tested stage passes.
	if (!translucent) {
		forEachFragment(mesh, camera, cull, [&](const FragmentInput& input) {
			float& depth = framebuffer.depthAt(input.x, input.y);
			if (input.depth >= depth) {
				return;
			}
			if (coverage == QStringLiteral("perforated")) {
				const SurfaceFrame surface = surfaceAt(*input.attributes);
				bool passed = false;
				for (int index : order) {
					const MaterialStage& stage = definition.stages.at(index);
					if (stage.alphaTest != MaterialAlphaTest::Expression || !stageActive(stage, registers)) {
						continue;
					}
					const MaterialTexturePtr texture = stageTexture(definition, stage, images);
					if (!texture || !texture->usable()) {
						continue;
					}
					double s = surface.s;
					double t = surface.t;
					stageMatrix(stage, registers, tables).apply(&s, &t);
					const double alpha = sampleTexture(*texture, s, t, true, stageWrap(stage, definition)).a * stageColor(stage, registers).a;
					if (alpha > registers.at(stage.alphaTestExpression, 0.5)) {
						passed = true;
						break;
					}
				}
				if (!passed) {
					return;
				}
			}
			depth = static_cast<float>(input.depth);
			float* pixel = framebuffer.pixel(input.x, input.y);
			pixel[0] = pixel[1] = pixel[2] = 0.0f;
			pixel[3] = 1.0f;
		});
	}

	// 2. Interactions with the preview light, added where depth matches.
	bool implicitBump = false;
	const QVector<Interaction> interactions = translucent ? QVector<Interaction>() : pairInteractions(definition, order, registers, &implicitBump);
	if (!interactions.isEmpty()) {
		const PreviewLight light = defaultLight(mesh, options);
		PreviewLight ambient = light;
		ambient.ambient = true;
		ambient.projection = nullptr;
		forEachFragment(mesh, camera, cull, [&](const FragmentInput& input) {
			const float depth = framebuffer.depthAt(input.x, input.y);
			if (std::abs(input.depth - depth) > 1.0e-4 * std::max(1.0, input.depth)) {
				return;
			}
			const SurfaceFrame surface = surfaceAt(*input.attributes);
			float* pixel = framebuffer.pixel(input.x, input.y);
			for (const Interaction& interaction : interactions) {
				Color added = shadeInteraction(definition, interaction, light, surface, camera, images, registers, tables, options);
				if (options.lighting.doom3Ambient > 0.0) {
					Color base = shadeInteraction(definition, interaction, ambient, surface, camera, images, registers, tables, options);
					const double level = options.lighting.doom3Ambient / std::max(1.0e-6, ambient.attenuation(surface.position));
					added = added + base * std::min(4.0, level);
				}
				pixel[0] = static_cast<float>(std::clamp(pixel[0] + added.r, 0.0, 1.0));
				pixel[1] = static_cast<float>(std::clamp(pixel[1] + added.g, 0.0, 1.0));
				pixel[2] = static_cast<float>(std::clamp(pixel[2] + added.b, 0.0, 1.0));
			}
		});
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
		// _currentRender reads what is already on screen.
		const QVector<float> snapshot = renderTarget ? framebuffer.color : QVector<float>();
		const bool bilinear = options.filtering != MaterialFiltering::Nearest && !stage.nearest;
		const Wrap wrap = stageWrap(stage, definition);
		const double wobble[3] = {registers.at(stage.tcGen.expressions[0], 0.0), registers.at(stage.tcGen.expressions[1], 0.0),
			registers.at(stage.tcGen.expressions[2], 0.0)};
		forEachFragment(mesh, camera, cull, [&](const FragmentInput& input) {
			float& depth = framebuffer.depthAt(input.x, input.y);
			const double tolerance = 1.0e-4 * std::max(1.0, input.depth);
			if (translucent || stage.ignoreAlphaTest) {
				if (input.depth > depth + tolerance) {
					return;
				}
			} else if (std::abs(input.depth - depth) > tolerance) {
				return;
			}
			const SurfaceFrame surface = surfaceAt(*input.attributes);
			Color fragment;
			const Vec3 view = (surface.position - camera.eye).normalized();
			switch (stage.tcGen.source) {
			case MaterialTexCoordSource::Normal:
			case MaterialTexCoordSource::Reflect:
			case MaterialTexCoordSource::Skybox:
			case MaterialTexCoordSource::WobbleSky: {
				Vec3 direction = surface.normal;
				if (stage.tcGen.source == MaterialTexCoordSource::Reflect) {
					direction = view - surface.normal * (2 * surface.normal.dot(view));
				} else if (stage.tcGen.source == MaterialTexCoordSource::Skybox) {
					direction = surface.position - camera.eye;
				} else if (stage.tcGen.source == MaterialTexCoordSource::WobbleSky) {
					direction = wobbleSky(surface.position - camera.eye, wobble[0], wobble[1], wobble[2], options.time);
				}
				if (cube) {
					// Camera cube faces were turned into GL faces when loaded.
					fragment = sampleCubeMap(*cube, direction);
				} else if (texture && texture->usable()) {
					fragment = sampleTexture(*texture, 0.5 + direction.normalized().y * 0.5, 0.5 - direction.normalized().z * 0.5, bilinear, wrap);
				}
				break;
			}
			case MaterialTexCoordSource::Screen:
			case MaterialTexCoordSource::Screen2:
			case MaterialTexCoordSource::GlassWarp:
			default: {
				double s = surface.s;
				double t = surface.t;
				const bool screen = stage.tcGen.source == MaterialTexCoordSource::Screen || stage.tcGen.source == MaterialTexCoordSource::Screen2
					|| stage.tcGen.source == MaterialTexCoordSource::GlassWarp || renderTarget;
				if (screen) {
					s = (input.x + 0.5) / framebuffer.width;
					t = 1.0 - (input.y + 0.5) / framebuffer.height;
				}
				matrix.apply(&s, &t);
				if (renderTarget) {
					const int x = std::clamp(static_cast<int>(s * framebuffer.width), 0, framebuffer.width - 1);
					const int y = std::clamp(static_cast<int>((1.0 - t) * framebuffer.height), 0, framebuffer.height - 1);
					const float* p = snapshot.constData() + (static_cast<qsizetype>(y) * framebuffer.width + x) * 4;
					fragment = {p[0], p[1], p[2], 1.0};
				} else if (cube) {
					fragment = sampleCubeMap(*cube, surface.normal);
				} else {
					const double obliquity = std::abs(surface.normal.dot(view));
					const double lod = bilinear ? mipLevel(camera, input.depth, obliquity, texture->width / std::max(1.0, mesh.repeatS)) : 0.0;
					fragment = sampleTexture(*texture, s, t, bilinear, wrap, lod);
				}
				break;
			}
			}
			fragment = fragment * color;
			if (stage.vertexColor == MaterialVertexColor::InverseVertex) {
				fragment = fragment * Color {0, 0, 0, 0};
			}
			blendInto(framebuffer.pixel(input.x, input.y), fragment, stage.blend.source, stage.blend.destination, stage.maskRed, stage.maskGreen,
				stage.maskBlue, stage.maskAlpha);
			if (translucent && stage.blend.isOpaqueReplace() && !stage.maskDepth) {
				depth = static_cast<float>(input.depth);
			}
		});
		++result.stagesDrawn;
	}
	if (implicitBump && !interactions.isEmpty()) {
		result.notes << Text::tr("No bump stage: lit with _flat, as Doom 3 does.");
	}
	if (!translucent && interactions.isEmpty() && result.stagesDrawn == 0) {
		result.notes << Text::tr("Nothing draws over the depth fill, so the surface is black, as in game.");
	}
	result.image = framebuffer.toImage(1.0);
	return result;
}

} // namespace vibestudio::material_render
