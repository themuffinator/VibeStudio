// Core and command-line checks of NetRadiant Custom's texture step
// operations (core/level_surface.h): the additive Radiant scale step with its
// sign flip at zero and the legacy zero scale, brush-primitive rows read in
// texels, patches scaled by factors without an image, the Radiant turn about
// the texture origin in every face format, the centre turn sharing its
// direction, one exact undo step, scene locks, and map align-textures
// --scale-step and --degrees with --anchor.

#include "core/level_document.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_surface.h"
#include "core/level_texture_mapping.h"
#include "core/map_geometry.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;

namespace {

bool ok = true;
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << phase.toStdString() << ": " << label << ": " << detail.toStdString() << std::endl;
	}
	return value;
}

bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool near(const QPointF& a, const QPointF& b, double tolerance = 1e-6) { return near(a.x(), b.x(), tolerance) && near(a.y(), b.y(), tolerance); }
bool near(const LevelMapVec3& a, const LevelMapVec3& b, double tolerance = 1e-6)
{
	return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}
LevelMapVec3 cross(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}
double dot(const LevelMapVec3& a, const LevelMapVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double length(const LevelMapVec3& a) { return std::sqrt(dot(a, a)); }

bool load(const QByteArray& bytes, LevelMapDocument* document)
{
	QString error;
	return expect(loadLevelMapBytes({QStringLiteral("steps.map"), {}, QStringLiteral("idtech3")}, bytes, document, &error), "load fixture", error);
}

bool apply(LevelMapDocument* map, const QVector<LevelSurfaceFace>& faces, const QVector<int>& patches, const LevelSurfaceRequest& request,
	const QHash<QString, QSize>& sizes, LevelSurfaceEditPlan* plan, QString* error)
{
	return prepareLevelSurfaceEdits(*map, faces, patches, {request}, sizes, plan, error) && commitLevelSurfaceEdit(map, *plan, error);
}

// NetRadiant Custom's signed scale of one brush-primitive row, in texels:
// one over the row's length, negative where its first coefficient is.
double rowScale(const LevelMapBrushFace& face, int first, double texels)
{
	const double scale = 1 / (std::hypot(face.textureMatrix[first], face.textureMatrix[first + 1]) * texels);
	return face.textureMatrix[first == 0 ? 0 : 4] < 0 ? -scale : scale;
}

// The face's U gradient within its own plane.
LevelMapVec3 inPlaneU(const LevelMapBrushFace& face, const LevelMapVec3& normal)
{
	const auto u = levelTextureProjection(face).u;
	const double along = dot(u, normal);
	return {u.x - normal.x * along, u.y - normal.y * along, u.z - normal.z * along, true};
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	expect(temp.isValid(), "temporary directory");
	QString error;
	const QHash<QString, QSize> sizes{{QStringLiteral("studio/grid"), QSize(128, 64)}};
	const LevelMapVec3 origin{0, 0, 0, true};

	phase = QStringLiteral("arithmetic");
	expect(levelSurfaceRadiantScaleStep(0.5, 0.5) == 1.0 && levelSurfaceRadiantScaleStep(1, -0.5) == 0.5 && levelSurfaceRadiantScaleStep(2, 0) == 2,
		"a step adds its increment");
	expect(levelSurfaceRadiantScaleStep(0.5, -0.5) == -0.5 && levelSurfaceRadiantScaleStep(-0.5, 0.5) == 0.5
			&& levelSurfaceRadiantScaleStep(0.25, -0.2500001) == -0.25,
		"a step reaching zero flips the sign instead");
	expect(levelSurfaceRadiantScaleStep(0, 0.5) == 1.5 && levelSurfaceRadiantScaleStep(0, 0) == 0, "a legacy zero scale counts as one");

	for (const auto* dialect : {"classic", "valve220", "brushDef", "brushDef3"}) {
		phase = QString::fromLatin1(dialect);
		LevelMapDocument map;
		if (!load(tests::surfaceFixture(phase), &map)) {
			return 1;
		}
		setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
		const auto faces = levelMapSelectedSurfaces(map);
		const auto source = serializeLevelMap(map).bytes;
		const auto before = map.brushes.first();
		const auto geometry = solveBrushGeometry(before.faces);
		const bool matrix = before.faces.first().explicitTextureMatrix;
		LevelSurfaceEditPlan plan;

		// Scale V + 0.5: one undo step, offsets and the texture origin kept.
		expect(apply(&map, faces, {}, {LevelSurfaceOperation::RadiantScale, 0, 0.5}, sizes, &plan, &error), "scale step V", error);
		expect(plan.faceCount() == 6 && plan.brushCount() == 1 && map.undoStack.size() == 1, "one undo step for all six faces");
		const auto scaled = map.brushes.first();
		for (int i = 0; i < 6; ++i) {
			const auto& a = before.faces[i];
			const auto& b = scaled.faces[i];
			expect(near(levelTextureProjection(a).at(origin), levelTextureProjection(b).at(origin)), "the texture origin keeps its UV");
			if (matrix) {
				expect(near(rowScale(b, 3, 64), rowScale(a, 3, 64) + 0.5, 1e-4) && near(b.textureMatrix[0], a.textureMatrix[0])
						&& near(b.textureMatrix[1], a.textureMatrix[1]) && b.textureMatrix[2] == a.textureMatrix[2] && b.textureMatrix[5] == a.textureMatrix[5],
					"a brush-primitive V row takes the increment on its texel scale",
					QStringLiteral("%1 -> %2").arg(rowScale(a, 3, 64)).arg(rowScale(b, 3, 64)));
			} else {
				expect(b.scaleY == a.scaleY + 0.5 && b.scaleX == a.scaleX && b.shiftX == a.shiftX && b.shiftY == a.shiftY && b.uOffset == a.uOffset
						&& b.vOffset == a.vOffset && b.rotation == a.rotation && near(b.uAxis, a.uAxis, 0) && near(b.vAxis, a.vAxis, 0),
					"a classic or Valve 220 face adds the increment to its V scale only",
					QStringLiteral("%1 -> %2").arg(a.scaleY).arg(b.scaleY));
			}
		}
		const auto changed = serializeLevelMap(map).bytes;
		expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == source, "undo restores the exact bytes", error);
		expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == changed, "redo repeats the step exactly", error);
		undoLevelMapEdit(&map, &error);

		// A step that would reach zero mirrors the axis instead.
		const double flip = matrix ? -rowScale(before.faces.first(), 0, 128) : -before.faces.first().scaleX;
		expect(apply(&map, {{0, 0}}, {}, {LevelSurfaceOperation::RadiantScale, flip, 0}, sizes, &plan, &error), "scale step to zero", error);
		const auto& flipped = map.brushes.first().faces.first();
		if (matrix) {
			expect(near(flipped.textureMatrix[0], -before.faces.first().textureMatrix[0]) && near(flipped.textureMatrix[1], -before.faces.first().textureMatrix[1]),
				"a brush-primitive row reaching zero is mirrored");
		} else {
			expect(flipped.scaleX == -before.faces.first().scaleX, "a scale reaching zero flips its sign", QString::number(flipped.scaleX));
		}
		undoLevelMapEdit(&map, &error);

		// The Radiant turn about the texture origin, per face format.
		expect(apply(&map, faces, {}, {LevelSurfaceOperation::RadiantRotate, 1, 1, 30}, sizes, &plan, &error), "turn about the texture origin", error);
		const auto turned = map.brushes.first();
		const auto trig = rotateLevelVector({1, 0, 0, true}, 2, 30);
		for (int i = 0; i < 6; ++i) {
			const auto& a = before.faces[i];
			const auto& b = turned.faces[i];
			expect(near(levelTextureProjection(a).at(origin), levelTextureProjection(b).at(origin)), "the turn keeps the texture origin's UV");
			if (matrix) {
				// NRC BPTexdef_Rotate: each row turns the plane coordinates it maps.
				expect(near(b.textureMatrix[0], a.textureMatrix[0] * trig.x + a.textureMatrix[1] * trig.y)
						&& near(b.textureMatrix[1], a.textureMatrix[1] * trig.x - a.textureMatrix[0] * trig.y)
						&& near(b.textureMatrix[3], a.textureMatrix[3] * trig.x + a.textureMatrix[4] * trig.y)
						&& near(b.textureMatrix[4], a.textureMatrix[4] * trig.x - a.textureMatrix[3] * trig.y)
						&& b.textureMatrix[2] == a.textureMatrix[2] && b.textureMatrix[5] == a.textureMatrix[5],
					"the matrix rows turn their plane coordinates and keep their offsets");
			} else {
				expect(near(b.rotation, std::fmod(a.rotation + 30, 360.0)) && b.shiftX == a.shiftX && b.shiftY == a.shiftY && b.scaleX == a.scaleX
						&& b.scaleY == a.scaleY && b.uOffset == a.uOffset && b.vOffset == a.vOffset,
					"the stored rotation takes the angle; offsets and scales stay", QString::number(b.rotation));
				if (a.explicitTextureAxes) {
					// NRC Valve220_rotate: about the axes' own normal, by minus the angle.
					const auto k = cross(a.uAxis, a.vAxis);
					const auto turnedU = cross(a.uAxis, b.uAxis);
					expect(near(length(b.uAxis), length(a.uAxis)) && near(length(cross(cross(b.uAxis, b.vAxis), k)), 0, 1e-6)
							&& near(dot(a.uAxis, b.uAxis), dot(a.uAxis, a.uAxis) * trig.x, 1e-6) && dot(turnedU, k) < 0,
						"Valve 220 axes turn about their own normal by minus the angle");
				}
			}
		}
		const auto originTurn = map.brushes.first();
		undoLevelMapEdit(&map, &error);
		expect(serializeLevelMap(map).bytes == source, "undo after the turn is exact");

		// The centre turn is the same turn with the face centre's UV kept.
		expect(apply(&map, faces, {}, {LevelSurfaceOperation::Rotate, 1, 1, 30}, sizes, &plan, &error), "turn about each face centre", error);
		for (int i = 0; i < 6; ++i) {
			const auto a = levelTextureProjection(originTurn.faces[i]), b = levelTextureProjection(map.brushes.first().faces[i]);
			LevelMapVec3 centre{0, 0, 0, true};
			for (const auto& point : geometry.faces[i].points) {
				centre = {centre.x + point.x / geometry.faces[i].points.size(), centre.y + point.y / geometry.faces[i].points.size(),
					centre.z + point.z / geometry.faces[i].points.size(), true};
			}
			expect(near(a.u, b.u) && near(a.v, b.v), "both anchors turn every face format the same way round");
			// The map writer keeps six decimals, so allow its rounding.
			const auto kept = levelTextureProjection(before.faces[i]).at(centre), moved = b.at(centre);
			expect(near(kept, moved, matrix ? 1e-4 : 1e-3), "the centre turn keeps the face centre's UV",
				QStringLiteral("(%1 %2) -> (%3 %4)").arg(kept.x(), 0, 'g', 12).arg(kept.y(), 0, 'g', 12).arg(moved.x(), 0, 'g', 12).arg(moved.y(), 0, 'g', 12));
		}
		undoLevelMapEdit(&map, &error);

		// The direction NetRadiant Custom turns each format. A classic face's
		// rotation turns about the positive world axis it projects along
		// (about -Y on Y walls), so a positive angle is anticlockwise seen from
		// +X, -Y and +Z and clockwise from the opposite sides; Valve 220 axes
		// turn the same way. A brush-primitive face always turns anticlockwise
		// seen from the front.
		for (int i = 0; i < 6; ++i) {
			const auto& plane = geometry.faces[i].plane;
			const LevelMapVec3 normal{plane.normalX, plane.normalY, plane.normalZ, true};
			const auto a = inPlaneU(before.faces[i], normal), b = inPlaneU(originTurn.faces[i], normal);
			const double turnSign = dot(cross(a, b), normal) > 0 ? 1 : -1;
			const int axis = std::abs(normal.x) > 0.9 ? 0 : std::abs(normal.y) > 0.9 ? 1 : 2;
			const double facing = axis == 0 ? normal.x : axis == 1 ? normal.y : normal.z;
			const double expected = matrix ? 1 : (axis == 1 ? -1 : 1) * (facing > 0 ? 1 : -1);
			expect(turnSign == expected, "the turn's direction matches NetRadiant Custom's for this face format",
				QStringLiteral("face %1 normal (%2 %3 %4)").arg(i + 1).arg(normal.x).arg(normal.y).arg(normal.z));
		}

		// Matrix faces need the image for a scale step, never for a turn.
		if (matrix) {
			expect(!prepareLevelSurfaceEdits(map, faces, {}, {{LevelSurfaceOperation::RadiantScale, 0.5, 0}}, {}, &plan, &error)
					&& error.contains(QStringLiteral("image size")),
				"a brush-primitive scale step refuses without the image size", error);
			expect(prepareLevelSurfaceEdits(map, faces, {}, {{LevelSurfaceOperation::RadiantRotate, 1, 1, 45}}, {}, &plan, &error) && plan.faceCount() == 6,
				"a brush-primitive turn needs no image size", error);
		} else {
			expect(prepareLevelSurfaceEdits(map, faces, {}, {{LevelSurfaceOperation::RadiantScale, 0.5, 0}}, {}, &plan, &error) && plan.faceCount() == 6,
				"a classic or Valve 220 scale step needs no image size", error);
		}
		expect(!prepareLevelSurfaceEdits(map, faces, {}, {{LevelSurfaceOperation::RadiantScale, std::numeric_limits<double>::quiet_NaN(), 0}}, sizes, &plan, &error)
				&& !plan.ready(),
			"a non-finite step is refused");
		const auto revision = map.revision;
		expect(prepareLevelSurfaceEdits(map, faces, {}, {{LevelSurfaceOperation::RadiantScale, 0, 0}}, sizes, &plan, &error) && plan.faceCount() == 0
				&& commitLevelSurfaceEdit(&map, plan, &error) && map.revision == revision,
			"an empty step is a no-op with no undo step", error);

		// A locked layer refuses the whole step.
		QString layer;
		expect(createLevelSceneNode(&map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
				&& assignLevelSceneObjects(&map, layer, {QStringLiteral("brush:0")}, &error) && setLevelSceneLocked(&map, layer, true, &error),
			"lock the brush", error);
		const auto locked = serializeLevelMap(map).bytes;
		const auto lockedRevision = map.revision;
		expect(prepareLevelSurfaceEdits(map, faces, {}, {{LevelSurfaceOperation::RadiantScale, 0, 0.5}}, sizes, &plan, &error)
				&& !commitLevelSurfaceEdit(&map, plan, &error) && map.revision == lockedRevision && serializeLevelMap(map).bytes == locked,
			"a locked brush refuses the step", error);
	}

	// Patches: a scale step multiplies or divides their coordinates without
	// an image; a turn about the origin rotates them in texels about 0,0.
	phase = QStringLiteral("patch");
	{
		LevelMapDocument map;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("quake3");
		create.starterRoom = false;
		createLevelMap(create, &map, &error);
		LevelPatchCreateRequest request;
		request.shape = QStringLiteral("plane");
		request.texture = QStringLiteral("studio/grid");
		request.size = {256, 128, 128, true};
		LevelMapPatch plane;
		int patchId = -1;
		expect(createLevelPatch(request, &plane, &error) && addLevelMapPatch(&map, plane, &patchId, &error), "patch fixture", error);
		LevelMapDocument document;
		load(serializeLevelMap(map).bytes, &document);
		if (!expect(document.patches.size() == 1, "patch reloaded")) {
			return 1;
		}
		patchId = document.patches.first().id;
		const auto start = document.patches.first();
		const auto source = serializeLevelMap(document).bytes;
		LevelSurfaceEditPlan plan;
		expect(apply(&document, {}, {patchId}, {LevelSurfaceOperation::RadiantScale, 0, 0.5}, {}, &plan, &error) && plan.patchCount() == 1, "patch scale V +", error);
		bool same = true;
		for (int i = 0; i < start.controlU.size(); ++i) {
			same &= near(document.patches.first().controlV[i], start.controlV[i] * 0.5) && document.patches.first().controlU[i] == start.controlU[i];
		}
		expect(same && document.undoStack.size() == 1, "a positive step multiplies the patch's V coordinates by the step");
		expect(apply(&document, {}, {patchId}, {LevelSurfaceOperation::RadiantScale, -0.5, 0}, {}, &plan, &error), "patch scale U −", error);
		same = true;
		for (int i = 0; i < start.controlU.size(); ++i) { same &= near(document.patches.first().controlU[i], start.controlU[i] * 2); }
		expect(same, "a negative step divides the patch's U coordinates by the step's size");
		undoLevelMapEdit(&document, &error);
		undoLevelMapEdit(&document, &error);
		expect(serializeLevelMap(document).bytes == source, "patch undo is exact");
		expect(!prepareLevelSurfaceEdits(document, {}, {patchId}, {{LevelSurfaceOperation::RadiantRotate, 1, 1, 90}}, {}, &plan, &error),
			"a patch turn needs the image size");
		expect(apply(&document, {}, {patchId}, {LevelSurfaceOperation::RadiantRotate, 1, 1, 90}, sizes, &plan, &error), "patch turn about 0,0", error);
		same = true;
		for (int i = 0; i < start.controlU.size(); ++i) {
			// Texels: u' = -v, v' = u, with a 128 × 64 image.
			same &= near(document.patches.first().controlU[i], -start.controlV[i] * 64 / 128) && near(document.patches.first().controlV[i], start.controlU[i] * 128 / 64);
		}
		expect(same, "a patch turns in texels about coordinate 0,0");
	}

	if (argc > 1) {
		phase = QStringLiteral("cli");
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const auto run = [&](const QString& map, const QStringList& arguments, int* exitCode) {
			QProcess process;
			process.start(binary, QStringList{QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("align-textures"), map} + arguments
				+ QStringList{QStringLiteral("--json")});
			const bool ended = process.waitForFinished(60000);
			*exitCode = ended && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
			const auto bytes = process.readAllStandardOutput();
			if (*exitCode != 0) {
				std::cerr << bytes.toStdString() << process.readAllStandardError().toStdString() << std::endl;
			}
			return QJsonDocument::fromJson(bytes).object();
		};
		const auto classic = QDir(temp.path()).filePath(QStringLiteral("classic.map"));
		const auto primitive = QDir(temp.path()).filePath(QStringLiteral("primitive.map"));
		tests::putMaterialFile(classic, tests::surfaceFixture(QStringLiteral("classic")));
		tests::putMaterialFile(primitive, tests::surfaceFixture(QStringLiteral("brushDef")));
		const auto out = [&](const char* name) { return QDir(temp.path()).filePath(QString::fromLatin1(name)); };
		const QStringList brush{QStringLiteral("--engine"), QStringLiteral("idTech3"), QStringLiteral("--object"), QStringLiteral("brush:0")};
		int code = -1;
		auto json = run(classic, brush + QStringList{QStringLiteral("--scale-step"), QStringLiteral("0,0.5"), QStringLiteral("--output"), out("scaled.map")}, &code);
		LevelMapDocument written;
		expect(code == 0 && json.value(QStringLiteral("operation")).toString() == QStringLiteral("scale-step") && json.value(QStringLiteral("facesChanged")).toInt() == 6
				&& loadLevelMap({out("scaled.map"), {}, QStringLiteral("idtech3")}, &written, &error) && written.brushes.first().faces.first().scaleY == 2.5,
			"map align-textures --scale-step adds to the V scale", QString::fromUtf8(QJsonDocument(json).toJson()));
		json = run(classic, brush + QStringList{QStringLiteral("--degrees"), QStringLiteral("30"), QStringLiteral("--anchor"), QStringLiteral("origin"),
			QStringLiteral("--output"), out("turned.map")}, &code);
		expect(code == 0 && json.value(QStringLiteral("anchor")).toString() == QStringLiteral("origin")
				&& loadLevelMap({out("turned.map"), {}, QStringLiteral("idtech3")}, &written, &error) && near(written.brushes.first().faces.first().rotation, 47)
				&& written.brushes.first().faces.first().shiftX == 7,
			"--degrees with --anchor origin adds to the rotation and keeps the shift", QString::fromUtf8(QJsonDocument(json).toJson()));
		json = run(classic, brush + QStringList{QStringLiteral("--degrees"), QStringLiteral("30"), QStringLiteral("--output"), out("centre.map")}, &code);
		expect(code == 0 && json.value(QStringLiteral("anchor")).toString() == QStringLiteral("centre"), "--degrees turns about each face centre by default");
		run(classic, brush + QStringList{QStringLiteral("--scale-step"), QStringLiteral("0,0.5"), QStringLiteral("--anchor"), QStringLiteral("origin"),
			QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "--anchor goes only with --degrees");
		run(classic, brush + QStringList{QStringLiteral("--degrees"), QStringLiteral("30"), QStringLiteral("--anchor"), QStringLiteral("sideways"),
			QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "--anchor takes centre or origin");
		run(classic, brush + QStringList{QStringLiteral("--scale-step"), QStringLiteral("0.5"), QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "--scale-step takes two values");
		run(primitive, brush + QStringList{QStringLiteral("--scale-step"), QStringLiteral("0.5,0"), QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "a brush-primitive scale step needs a texture size");
		run(primitive, brush + QStringList{QStringLiteral("--scale-step"), QStringLiteral("0.5,0"), QStringLiteral("--texture-size"), QStringLiteral("128,64"),
			QStringLiteral("--output"), out("primitive-scaled.map")}, &code);
		expect(code == 0 && QFile::exists(out("primitive-scaled.map")), "a brush-primitive scale step with --texture-size");
		run(primitive, brush + QStringList{QStringLiteral("--degrees"), QStringLiteral("90"), QStringLiteral("--anchor"), QStringLiteral("origin"),
			QStringLiteral("--output"), out("primitive-turned.map")}, &code);
		expect(code == 0 && QFile::exists(out("primitive-turned.map")), "a brush-primitive turn needs no texture size");
	}
	return ok ? 0 : 1;
}
