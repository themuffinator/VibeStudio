// Core and command-line checks of the texture resets (core/level_surface.h):
// TrenchBroom's Reset Texture Alignment (Reset), which keeps each face's
// projection kind, and Radiant's Reset Texture (RadiantReset), which gives
// Valve 220 and brush-primitive faces their plane's own basis and caps
// patches, on classic, Valve 220 and brush-primitive faces, a sloped classic
// face, a mixed-dialect selection and a patch, each one exact undo step; and
// bobToolz's Reset Textures (prepareLevelTextureReset): absolute scale, shift
// and rotation per ticked field in each face format, a texture filter, a
// rename that keeps the numbers as written, the whole map leaving hidden,
// filtered and locked objects alone, patches taking only the name, and its
// refusals. map align-textures --reset-alignment, --radiant-reset and its
// Reset Textures mode write what the shared services write.

#include "core/level_document.h"
#include "core/level_patch.h"
#include "core/level_patch_tools.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_surface.h"
#include "core/level_texture_mapping.h"
#include "core/level_visibility.h"
#include "core/map_geometry.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}

bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool same(const LevelMapVec3& a, const LevelMapVec3& b, double tolerance = 1e-6)
{
	return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}
double dot(const LevelMapVec3& a, const LevelMapVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
QString firstDifference(const QByteArray& a, const QByteArray& b)
{
	const auto left = a.split('\n'), right = b.split('\n');
	for (int i = 0; i < std::max(left.size(), right.size()); ++i) {
		if (left.value(i) != right.value(i)) {
			return QStringLiteral("line %1: %2 | %3").arg(i + 1).arg(QString::fromUtf8(left.value(i)).trimmed(), QString::fromUtf8(right.value(i)).trimmed());
		}
	}
	return {};
}

bool load(const QByteArray& bytes, LevelMapDocument* document)
{
	QString error;
	return expect(loadLevelMapBytes({QStringLiteral("reset.map"), {}, QStringLiteral("idtech3")}, bytes, document, &error), "load fixture", error);
}

LevelSurfaceRequest reset(LevelSurfaceOperation operation, double scale)
{
	LevelSurfaceRequest request;
	request.operation = operation;
	request.x = request.y = scale;
	return request;
}

// The bytes `operation` leaves on brush 0's faces and the given patches.
QByteArray edited(const QByteArray& source, const LevelSurfaceRequest& request, const QHash<QString, QSize>& sizes, const QVector<int>& patches = {},
	bool brush = true)
{
	LevelMapDocument map;
	load(source, &map);
	QVector<LevelSurfaceFace> faces;
	if (brush) {
		for (const auto& candidate : map.brushes) {
			for (int i = 0; i < candidate.faces.size(); ++i) {
				faces.append({candidate.id, i});
			}
		}
	}
	LevelSurfaceEditPlan plan;
	QString error;
	if (!prepareLevelSurfaceEdits(map, faces, patches, {request}, sizes, &plan, &error) || !commitLevelSurfaceEdit(&map, plan, &error)) {
		return QByteArrayLiteral("refused: ") + error.toUtf8();
	}
	return serializeLevelMap(map).bytes;
}

// The fixture's one brush block, from its opening brace to its closing one.
QStringList brushBlock(const QByteArray& fixture)
{
	const QStringList lines = QString::fromUtf8(fixture).split(QStringLiteral("\r\n"));
	const qsizetype end = lines.indexOf(QStringLiteral("// untouched point entity"));
	return lines.mid(3, end - 4);
}

// A worldspawn with a classic, a Valve 220 and a brush-primitive box.
QByteArray mixedFixture()
{
	QStringList lines {QStringLiteral("// mixed dialects"), QStringLiteral("{"), QStringLiteral("\"classname\" \"worldspawn\"")};
	for (const auto* dialect : {"classic", "valve220", "brushDef"}) {
		lines += brushBlock(tests::surfaceFixture(QString::fromLatin1(dialect)));
	}
	lines << QStringLiteral("}") << QString();
	return lines.join(QStringLiteral("\r\n")).toUtf8();
}

// A classic box whose top slopes down along +X and +Y, which no classic
// mapping can lay in its plane.
QByteArray slopedFixture()
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &document);
	addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/grid"));
	for (auto& face : document.brushes.first().faces) {
		const auto plane = planeFromPoints(face.p0, face.p1, face.p2);
		if (plane.normalZ > 0.9) {
			for (LevelMapVec3* point : {&face.p0, &face.p1, &face.p2}) {
				point->z = 64 - point->x / 2 - point->y / 4;
			}
		}
	}
	document.brushes.first().geometryDirty = true;
	return serializeLevelMap(document).bytes;
}

// Radiant's reading of a brush-primitive face in texels: a scale from each
// row's length (negative where its own axis runs backwards), a turn from the
// first row and a shift from the offsets (NRC TexMatToFakeTexCoords).
struct Fake {
	double scaleU = 0, scaleV = 0, shiftU = 0, shiftV = 0, rotation = 0;
};
Fake fake(const LevelMapBrushFace& face, QSize size)
{
	const auto& m = face.textureMatrix;
	Fake result;
	const double lengthU = std::hypot(m[0], m[1]) * size.width(), lengthV = std::hypot(m[3], m[4]) * size.height();
	result.scaleU = (m[0] < 0 ? -1 : 1) / lengthU;
	result.scaleV = (m[4] < 0 ? -1 : 1) / lengthV;
	result.rotation = std::acos(std::clamp(m[0] * size.width() / lengthU, -1.0, 1.0)) * 180 / std::acos(-1.0);
	if (m[1] > 0) {
		result.rotation = -result.rotation;
	}
	result.shiftU = -m[2] * size.width();
	result.shiftV = m[5] * size.height();
	return result;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	expect(temp.isValid(), "temporary directory");
	QString error;
	const QSize imageSize(128, 64);
	const QHash<QString, QSize> sizes {{QStringLiteral("studio/grid"), imageSize}};

	phase = QStringLiteral("plane basis");
	{
		LevelMapDocument map;
		if (!load(tests::surfaceFixture(QStringLiteral("valve220")), &map)) {
			return 1;
		}
		for (const auto& face : map.brushes.first().faces) {
			const auto plane = planeFromPoints(face.p0, face.p1, face.p2);
			const LevelMapVec3 normal {plane.normalX, plane.normalY, plane.normalZ, true};
			const auto [s, t] = levelTexturePrimitiveBasis(normal);
			const auto projection = levelPlaneBasisTextureProjection(face, 0.5, 0.25);
			expect(projection.valid && same(projection.u, {s.x * 2, s.y * 2, s.z * 2, true}) && same(projection.v, {t.x * 4, t.y * 4, t.z * 4, true})
					&& projection.offsetU == 0 && projection.offsetV == 0 && near(dot(projection.u, normal), 0) && near(dot(projection.v, normal), 0),
				"the plane's own basis at 0.5 and 0.25 units per texel");
			if (plane.normalZ > 0.9) {
				// The floor: U along world Y and V along X, a quarter turn from
				// the face-aligned projection's U along X.
				const auto aligned = levelFaceAlignedTextureProjection(face, 0.5, 0.25);
				expect(same(projection.u, {0, 2, 0, true}) && same(projection.v, {4, 0, 0, true}) && near(dot(projection.u, aligned.u), 0),
					"on a floor the basis is a quarter turn from Align to Face");
			}
		}
		LevelMapDocument primitive;
		load(tests::surfaceFixture(QStringLiteral("brushDef")), &primitive);
		const auto& face = primitive.brushes.first().faces.first();
		expect(!levelPlaneBasisTextureProjection(face, 0.5, 0.5).valid, "a brush-primitive basis needs the image size");
		const auto projection = levelPlaneBasisTextureProjection(face, 0.5, 0.5, imageSize);
		expect(projection.valid && projection.normalizedCoordinates && near(std::sqrt(dot(projection.u, projection.u)), 1.0 / 64)
				&& near(std::sqrt(dot(projection.v, projection.v)), 1.0 / 32),
			"brush primitives count repeats of the image");
		expect(!levelPlaneBasisTextureProjection(face, 0, 0.5, imageSize).valid, "a zero scale has no projection");
	}

	for (const auto* dialect : {"classic", "valve220", "brushDef", "brushDef3"}) {
		phase = QStringLiteral("resets ") + QString::fromLatin1(dialect);
		const QByteArray fixture = tests::surfaceFixture(QString::fromLatin1(dialect));
		LevelMapDocument map;
		if (!load(fixture, &map)) {
			return 1;
		}
		const auto source = serializeLevelMap(map).bytes;
		const bool classic = !map.brushes.first().faces.first().explicitTextureAxes && !map.brushes.first().faces.first().explicitTextureMatrix;
		const bool matrix = map.brushes.first().faces.first().explicitTextureMatrix;
		QVector<LevelSurfaceFace> faces;
		for (int i = 0; i < map.brushes.first().faces.size(); ++i) {
			faces.append({map.brushes.first().id, i});
		}
		LevelSurfaceEditPlan plan;
		if (matrix) {
			expect(!prepareLevelSurfaceEdits(map, faces, {}, {reset(LevelSurfaceOperation::RadiantReset, 0.5)}, {}, &plan, &error)
					&& error.contains(QStringLiteral("image size")),
				"a brush-primitive reset needs the image size", error);
		}
		// Radiant: classic faces natural, the others their plane's own basis.
		expect(prepareLevelSurfaceEdits(map, faces, {}, {reset(LevelSurfaceOperation::RadiantReset, 0.5)}, sizes, &plan, &error)
				&& plan.faceCount() == 6 && commitLevelSurfaceEdit(&map, plan, &error) && map.undoStack.size() == 1,
			"Radiant Texture Reset changes every face in one undo step", error);
		for (const auto& face : map.brushes.first().faces) {
			const auto plane = planeFromPoints(face.p0, face.p1, face.p2);
			const auto [s, t] = levelTexturePrimitiveBasis({plane.normalX, plane.normalY, plane.normalZ, true});
			if (classic) {
				expect(face.shiftX == 0 && face.shiftY == 0 && face.rotation == 0 && face.scaleX == 0.5 && face.scaleY == 0.5, "a classic face takes Natural");
			} else if (matrix) {
				expect(near(face.textureMatrix[0], 1.0 / 64) && near(face.textureMatrix[1], 0) && near(face.textureMatrix[2], 0)
						&& near(face.textureMatrix[3], 0) && near(face.textureMatrix[4], 1.0 / 32) && near(face.textureMatrix[5], 0),
					"a brush-primitive face takes a unit matrix over its basis at the default scale",
					QStringLiteral("%1 %2 %3 %4 %5 %6").arg(face.textureMatrix[0]).arg(face.textureMatrix[1]).arg(face.textureMatrix[2])
						.arg(face.textureMatrix[3]).arg(face.textureMatrix[4]).arg(face.textureMatrix[5]));
			} else {
				expect(same(face.uAxis, s) && same(face.vAxis, t) && face.uOffset == 0 && face.vOffset == 0 && face.rotation == 0 && face.scaleX == 0.5
						&& face.scaleY == 0.5,
					"a Valve 220 face takes its plane's basis with no offset or rotation");
			}
		}
		const auto radiant = serializeLevelMap(map).bytes;
		expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == source, "undo restores the exact source", error);
		expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == radiant, "redo repeats the reset", error);
		undoLevelMapEdit(&map, &error);
		// TrenchBroom: each face keeps its kind: Natural for classic faces,
		// Align to Face for the others.
		const auto tb = edited(source, reset(LevelSurfaceOperation::Reset, 0.5), sizes);
		const auto expected = edited(source, reset(classic ? LevelSurfaceOperation::Natural : LevelSurfaceOperation::FaceAligned, 0.5), sizes);
		expect(tb == expected && !tb.startsWith("refused"), "Reset Texture Alignment keeps each face's projection kind", firstDifference(tb, expected));
		if (!classic) {
			expect(tb != radiant, "the two resets differ on Valve 220 and brush-primitive faces");
		} else {
			expect(tb == radiant, "the two resets agree on classic faces");
		}
		// Twice is no further change, and no undo step.
		load(radiant, &map);
		const auto revision = map.revision;
		expect(prepareLevelSurfaceEdits(map, faces, {}, {reset(LevelSurfaceOperation::RadiantReset, 0.5)}, sizes, &plan, &error) && plan.faceCount() == 0
				&& commitLevelSurfaceEdit(&map, plan, &error) && map.revision == revision,
			"resetting a reset face is a no-op without an undo step", error);
		// A locked brush refuses the reset.
		load(source, &map);
		QString layer;
		expect(createLevelSceneNode(&map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
				&& assignLevelSceneObjects(&map, layer, {QStringLiteral("brush:0")}, &error) && setLevelSceneLocked(&map, layer, true, &error),
			"lock the brush", error);
		const auto locked = serializeLevelMap(map).bytes;
		expect(prepareLevelSurfaceEdits(map, faces, {}, {reset(LevelSurfaceOperation::Reset, 0.5)}, sizes, &plan, &error)
				&& !commitLevelSurfaceEdit(&map, plan, &error) && serializeLevelMap(map).bytes == locked,
			"a locked brush refuses the reset", error);
	}

	phase = QStringLiteral("sloped classic face");
	{
		const QByteArray sloped = slopedFixture();
		LevelMapDocument map;
		if (!load(sloped, &map)) {
			return 1;
		}
		expect(edited(sloped, reset(LevelSurfaceOperation::FaceAligned, 1), {}).startsWith("refused"), "Align to Face refuses a sloped classic face");
		const auto natural = edited(sloped, reset(LevelSurfaceOperation::Natural, 1), {});
		const auto tb = edited(sloped, reset(LevelSurfaceOperation::Reset, 1), {}), radiant = edited(sloped, reset(LevelSurfaceOperation::RadiantReset, 1), {});
		expect(!natural.startsWith("refused") && tb == natural && radiant == natural, "both resets give a sloped classic face its natural numbers",
			firstDifference(tb, natural));
	}

	phase = QStringLiteral("mixed dialects");
	{
		const QByteArray mixed = mixedFixture();
		LevelMapDocument map;
		if (!load(mixed, &map) || !expect(map.brushes.size() == 3 && levelMapBrushDialect(map) == QStringLiteral("mixed"), "three brushes in three dialects",
				levelMapBrushDialect(map))) {
			return 1;
		}
		QVector<LevelSurfaceFace> faces;
		for (const auto& brush : map.brushes) {
			for (int i = 0; i < brush.faces.size(); ++i) {
				faces.append({brush.id, i});
			}
		}
		const auto source = serializeLevelMap(map).bytes;
		LevelSurfaceEditPlan plan;
		expect(prepareLevelSurfaceEdits(map, faces, {}, {reset(LevelSurfaceOperation::RadiantReset, 0.5)}, sizes, &plan, &error) && plan.faceCount() == 18
				&& plan.brushCount() == 3 && commitLevelSurfaceEdit(&map, plan, &error) && map.undoStack.size() == 1,
			"one Radiant reset covers all three dialects in one undo step", error);
		for (const auto& brush : map.brushes) {
			for (const auto& face : brush.faces) {
				const bool classic = !face.explicitTextureAxes && !face.explicitTextureMatrix;
				expect(classic ? face.shiftX == 0 && face.rotation == 0 && face.scaleX == 0.5
							   : face.explicitTextureAxes ? face.uOffset == 0 && face.scaleX == 0.5 : near(face.textureMatrix[2], 0),
					"each face keeps its dialect and is reset", QString::number(brush.id));
			}
		}
		expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == source, "the mixed reset undoes exactly", error);
		expect(prepareLevelSurfaceEdits(map, faces, {}, {reset(LevelSurfaceOperation::Reset, 0.5)}, sizes, &plan, &error) && plan.faceCount() == 18,
			"one TrenchBroom reset covers all three dialects", error);
		expect(!prepareLevelSurfaceEdits(map, faces, {}, {reset(LevelSurfaceOperation::Reset, 0.5)}, {}, &plan, &error) && error.contains(QStringLiteral("image size")),
			"the brush-primitive brush still needs its image size", error);
	}

	phase = QStringLiteral("patch");
	{
		LevelMapDocument created;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("quake3");
		create.starterRoom = false;
		createLevelMap(create, &created, &error);
		LevelPatchCreateRequest patchRequest;
		patchRequest.shape = QStringLiteral("cylinder");
		patchRequest.texture = QStringLiteral("studio/grid");
		LevelMapPatch cylinder;
		int patchId = -1;
		expect(createLevelPatch(patchRequest, &cylinder, &error) && addLevelMapPatch(&created, cylinder, &patchId, &error), "patch fixture", error);
		const auto bytes = serializeLevelMap(created).bytes;
		LevelMapDocument map;
		load(bytes, &map);
		patchId = map.patches.first().id;
		const auto source = serializeLevelMap(map).bytes;
		const auto natural = edited(source, reset(LevelSurfaceOperation::Natural, 0.5), sizes, {patchId}, false);
		const auto cap = edited(source, reset(LevelSurfaceOperation::FaceAligned, 0.5), sizes, {patchId}, false);
		expect(!natural.startsWith("refused") && edited(source, reset(LevelSurfaceOperation::Reset, 0.5), sizes, {patchId}, false) == natural,
			"Reset Texture Alignment gives a patch its natural spread");
		expect(!cap.startsWith("refused") && edited(source, reset(LevelSurfaceOperation::RadiantReset, 0.5), sizes, {patchId}, false) == cap && cap != natural,
			"Radiant Texture Reset caps a patch");
		expect(edited(source, reset(LevelSurfaceOperation::RadiantReset, 0.5), {}, {patchId}, false).startsWith("refused"), "a patch reset needs the image size");
		LevelSurfaceEditPlan plan;
		expect(prepareLevelSurfaceEdits(map, {}, {patchId}, {reset(LevelSurfaceOperation::RadiantReset, 0.5)}, sizes, &plan, &error) && plan.patchCount() == 1
				&& commitLevelSurfaceEdit(&map, plan, &error) && undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == source,
			"the patch reset undoes exactly", error);
	}

	phase = QStringLiteral("reset textures classic");
	{
		LevelMapDocument map;
		load(tests::surfaceFixture(QStringLiteral("classic")), &map);
		const auto source = serializeLevelMap(map).bytes;
		LevelTextureResetRequest request;
		request.faces = levelMapSelectedSurfaces(map);
		expect(request.faces.isEmpty(), "nothing selected yet");
		setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
		request.faces = levelMapSelectedSurfaces(map);
		LevelSurfaceEditPlan plan;
		expect(!prepareLevelTextureReset(map, request, {}, &plan, &error) && !error.isEmpty(), "a reset with nothing ticked is refused", error);
		request.setScaleU = true;
		request.scaleU = 0;
		expect(!prepareLevelTextureReset(map, request, {}, &plan, &error) && error.contains(QStringLiteral("0")), "a zero scale is refused", error);
		request.scaleU = 0.25;
		request.setShiftV = true;
		request.shiftV = 4;
		request.setRotation = true;
		request.rotation = 30;
		expect(prepareLevelTextureReset(map, request, {}, &plan, &error) && plan.faceCount() == 6 && plan.renamedCount() == 0
				&& commitLevelSurfaceEdit(&map, plan, &error) && map.undoStack.size() == 1,
			"absolute values on every face in one undo step", error);
		for (const auto& face : map.brushes.first().faces) {
			// The fixture's faces hold shift 7 9, rotation 17 and scale -0.5 2.
			expect(face.scaleX == 0.25 && face.scaleY == 2 && face.shiftX == 7 && face.shiftY == 4 && face.rotation == 30,
				"ticked fields take their values; the others keep theirs");
		}
		expect(map.undoStack.last().description.contains(QStringLiteral("Reset textures")), "the undo step names the reset", map.undoStack.last().description);
		expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == source, "undo restores the exact source", error);

		// Only faces using one texture, ignoring case and textures/.
		map.brushes.first().faces[2].textureName = QStringLiteral("studio/other");
		map.brushes.first().faces[2].textureDirty = true;
		map.brushes.first().texturesDirty = true;
		load(serializeLevelMap(map).bytes, &map);
		setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
		const auto renamedSource = serializeLevelMap(map).bytes;
		LevelTextureResetRequest filtered;
		filtered.faces = levelMapSelectedSurfaces(map);
		filtered.onlyTexture = QStringLiteral("Textures\\STUDIO/Other");
		filtered.setShiftU = true;
		filtered.shiftU = 5;
		QVector<LevelSurfaceFace> targets;
		QVector<int> patches;
		expect(levelTextureResetTargets(map, filtered, &targets, &patches, nullptr, &error) && targets.size() == 1 && targets.first().faceIndex == 2,
			"the filter keeps only the face using that texture", error);
		expect(prepareLevelTextureReset(map, filtered, {}, &plan, &error) && plan.faceCount() == 1 && commitLevelSurfaceEdit(&map, plan, &error)
				&& map.brushes.first().faces[2].shiftX == 5 && map.brushes.first().faces[1].shiftX == 7,
			"only the filtered face changes", error);
		undoLevelMapEdit(&map, &error);

		// A rename keeps each face's numbers exactly as written.
		LevelTextureResetRequest rename;
		rename.faces = levelMapSelectedSurfaces(map);
		rename.onlyTexture = QStringLiteral("studio/grid");
		rename.renameTo = QStringLiteral("studio/new");
		expect(prepareLevelTextureReset(map, rename, {}, &plan, &error) && plan.faceCount() == 5 && plan.renamedCount() == 5
				&& commitLevelSurfaceEdit(&map, plan, &error) && map.undoStack.size() == 1,
			"rename the five grid faces in one undo step", error);
		const auto renamedBytes = serializeLevelMap(map).bytes;
		expect(QString::fromUtf8(renamedBytes).replace(QStringLiteral("studio/new"), QStringLiteral("studio/grid")).toUtf8() == renamedSource,
			"only the names changed in the saved text", firstDifference(renamedBytes, renamedSource));
		expect(map.textureReferences.contains(QStringLiteral("studio/new")) && map.brushes.first().textureNames.contains(QStringLiteral("studio/new"))
				&& !map.brushes.first().textureNames.contains(QStringLiteral("studio/grid")),
			"the map's texture lists follow the rename");
		expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == renamedSource && !map.textureReferences.contains(QStringLiteral("studio/new")),
			"undo restores the names", error);
		rename.renameTo = QStringLiteral("bad name");
		expect(!prepareLevelTextureReset(map, rename, {}, &plan, &error) && !error.isEmpty(), "a name with a space is refused", error);
	}

	phase = QStringLiteral("reset textures valve220");
	{
		LevelMapDocument map;
		load(tests::surfaceFixture(QStringLiteral("valve220")), &map);
		setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
		const auto before = map.brushes.first().faces;
		LevelTextureResetRequest request;
		request.faces = levelMapSelectedSurfaces(map);
		request.setRotation = true;
		request.rotation = 90;
		request.setShiftU = true;
		request.shiftU = 3;
		request.setScaleV = true;
		request.scaleV = 0.5;
		LevelSurfaceEditPlan plan;
		expect(prepareLevelTextureReset(map, request, {}, &plan, &error) && plan.faceCount() == 6 && commitLevelSurfaceEdit(&map, plan, &error),
			"absolute values on Valve 220 faces", error);
		for (int i = 0; i < before.size(); ++i) {
			const auto& face = map.brushes.first().faces[i];
			// The fixture's faces hold rotation 0, so the axes turn a quarter
			// about their normal, keeping their lengths.
			const double length = std::sqrt(dot(before[i].uAxis, before[i].uAxis));
			expect(face.rotation == 90 && face.uOffset == 3 && face.vOffset == 9 && face.scaleY == 0.5 && face.scaleX == -0.5
					&& near(dot(face.uAxis, before[i].uAxis), 0, 1e-5) && near(std::sqrt(dot(face.uAxis, face.uAxis)), length, 1e-5),
				"the rotation turns the axes, the shift and scale are the face's own", QString::number(i));
		}
	}

	phase = QStringLiteral("reset textures brush primitives");
	{
		LevelMapDocument map;
		load(tests::surfaceFixture(QStringLiteral("brushDef")), &map);
		setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
		const auto faces = levelMapSelectedSurfaces(map);
		// From a Radiant reset, where Radiant reads scale 0.5, no shift and no turn.
		LevelSurfaceEditPlan plan;
		expect(prepareLevelSurfaceEdits(map, faces, {}, {reset(LevelSurfaceOperation::RadiantReset, 0.5)}, sizes, &plan, &error)
				&& commitLevelSurfaceEdit(&map, plan, &error),
			"start from the plane basis", error);
		LevelTextureResetRequest request;
		request.faces = faces;
		request.setScaleU = request.setScaleV = request.setShiftU = request.setShiftV = request.setRotation = true;
		request.scaleU = 0.25;
		request.scaleV = 0.5;
		request.shiftU = 8;
		request.shiftV = -4;
		request.rotation = 30;
		QStringList sized;
		QVector<LevelSurfaceFace> targets;
		QVector<int> patches;
		expect(levelTextureResetTargets(map, request, &targets, &patches, &sized, &error) && sized == QStringList {QStringLiteral("studio/grid")},
			"brush-primitive faces need their image size", sized.join(QLatin1Char(',')));
		expect(!prepareLevelTextureReset(map, request, {}, &plan, &error) && error.contains(QStringLiteral("image size")), "and are refused without it", error);
		expect(prepareLevelTextureReset(map, request, sizes, &plan, &error) && plan.faceCount() == 6 && commitLevelSurfaceEdit(&map, plan, &error),
			"absolute values on brush-primitive faces", error);
		for (const auto& face : map.brushes.first().faces) {
			const auto read = fake(face, imageSize);
			expect(near(read.scaleU, 0.25, 1e-4) && near(read.scaleV, 0.5, 1e-4) && near(read.shiftU, 8, 1e-3) && near(read.shiftV, -4, 1e-3)
					&& near(read.rotation, 30, 0.01),
				"Radiant reads back the values set",
				QStringLiteral("%1 %2 %3 %4 %5").arg(read.scaleU).arg(read.scaleV).arg(read.shiftU).arg(read.shiftV).arg(read.rotation));
		}
	}

	phase = QStringLiteral("reset textures whole map");
	{
		LevelMapDocument created;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("quake3");
		create.starterRoom = false;
		createLevelMap(create, &created, &error);
		addLevelMapBoxBrush(&created, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/grid"));
		addLevelMapBoxBrush(&created, {128, 0, 0, true}, {192, 64, 64, true}, QStringLiteral("studio/grid"));
		addLevelMapBoxBrush(&created, {256, 0, 0, true}, {320, 64, 64, true}, QStringLiteral("studio/grid"));
		addLevelMapBoxBrush(&created, {384, 0, 0, true}, {448, 64, 64, true}, QStringLiteral("studio/other"));
		LevelPatchCreateRequest patchRequest;
		patchRequest.texture = QStringLiteral("textures/studio/grid");
		LevelMapPatch plane;
		expect(createLevelPatch(patchRequest, &plane, &error) && addLevelMapPatch(&created, plane, nullptr, &error), "patch", error);
		LevelMapDocument map;
		load(serializeLevelMap(created).bytes, &map);
		if (!expect(map.brushes.size() == 4 && map.patches.size() == 1, "four brushes and a patch")) {
			return 1;
		}
		const int hidden = map.brushes[1].id, locked = map.brushes[2].id;
		// Hide one brush and lock another.
		setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, hidden}});
		LevelVisibilityRequest hide;
		expect(changeLevelMapVisibility(&map, hide, nullptr, &error), "hide a brush", error);
		QString layer;
		expect(createLevelSceneNode(&map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
				&& assignLevelSceneObjects(&map, layer, {levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, locked})}, &error)
				&& setLevelSceneLocked(&map, layer, true, &error),
			"lock a brush", error);
		setLevelMapSelection(&map, {});
		LevelTextureResetRequest request;
		request.wholeMap = true;
		request.onlyTexture = QStringLiteral("studio/grid");
		request.renameTo = QStringLiteral("textures/studio/new");
		request.setRotation = true;
		request.rotation = 45;
		QVector<LevelSurfaceFace> targets;
		QVector<int> patches;
		expect(levelTextureResetTargets(map, request, &targets, &patches, nullptr, &error) && targets.size() == 6 && patches.size() == 1
				&& std::all_of(targets.cbegin(), targets.cend(), [&](const LevelSurfaceFace& face) { return face.brushId == map.brushes[0].id; }),
			"the whole map leaves the hidden, the locked and the other texture's brushes out", QString::number(targets.size()));
		request.excludedObjects = {levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch, map.patches.first().id})};
		expect(levelTextureResetTargets(map, request, &targets, &patches, nullptr, &error) && patches.isEmpty(), "view-filtered objects stay as they are");
		request.excludedObjects.clear();
		const auto source = serializeLevelMap(map).bytes;
		LevelSurfaceEditPlan plan;
		expect(prepareLevelTextureReset(map, request, {}, &plan, &error) && plan.faceCount() == 6 && plan.patchCount() == 1 && plan.renamedCount() == 7
				&& commitLevelSurfaceEdit(&map, plan, &error),
			"reset and rename the whole map in one undo step", error);
		const auto& patch = map.patches.first();
		expect(patch.textureName == levelPatchMaterialToken(QStringLiteral("textures/studio/new"), patch.fixedSubdivisions) && map.brushes[0].faces[0].rotation == 45
				&& map.brushes[1].faces[0].textureName == QStringLiteral("studio/grid") && map.brushes[2].faces[0].textureName == QStringLiteral("studio/grid")
				&& map.brushes[3].faces[0].textureName == QStringLiteral("studio/other"),
			"the patch takes only the name; hidden, locked and other brushes keep theirs", patch.textureName);
		expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == source, "the whole-map reset undoes exactly", error);
		// Selected locked objects are refused whole.
		LevelTextureResetRequest selected;
		selected.faces = {{locked, 0}};
		selected.setShiftU = true;
		selected.shiftU = 3;
		expect(prepareLevelTextureReset(map, selected, {}, &plan, &error) && !commitLevelSurfaceEdit(&map, plan, &error)
				&& serializeLevelMap(map).bytes == source,
			"a locked target refuses the reset", error);
		selected.faces = {{locked, 99}};
		expect(!prepareLevelTextureReset(map, selected, {}, &plan, &error), "a missing face is refused", error);
	}

	if (argc > 1) {
		phase = QStringLiteral("cli");
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const auto run = [&](const QString& map, const QStringList& arguments, int* exitCode) {
			QProcess process;
			process.start(binary, QStringList {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("align-textures"), map} + arguments
				+ QStringList {QStringLiteral("--json")});
			const bool ended = process.waitForFinished(60000);
			*exitCode = ended && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
			const auto bytes = process.readAllStandardOutput();
			if (*exitCode != 0) {
				std::cerr << bytes.constData() << process.readAllStandardError().constData() << std::endl;
			}
			return QJsonDocument::fromJson(bytes).object();
		};
		const auto savedBytes = [](const QString& path) {
			LevelMapDocument saved;
			QString problem;
			expect(loadLevelMap({path, {}, QStringLiteral("idtech3")}, &saved, &problem), "saved map loads", problem);
			return serializeLevelMap(saved).bytes;
		};
		const auto out = [&](const char* name) { return QDir(temp.path()).filePath(QString::fromLatin1(name)); };
		const auto valve = out("valve.map"), classic = out("classic.map"), primitive = out("primitive.map");
		tests::putMaterialFile(valve, tests::surfaceFixture(QStringLiteral("valve220")));
		tests::putMaterialFile(classic, tests::surfaceFixture(QStringLiteral("classic")));
		tests::putMaterialFile(primitive, tests::surfaceFixture(QStringLiteral("brushDef")));
		const auto loaded = [](const QString& path) {
			LevelMapDocument document;
			QString problem;
			expect(loadLevelMap({path, {}, QStringLiteral("idtech3")}, &document, &problem), "reference loads", problem);
			return document;
		};
		const QStringList brush {QStringLiteral("--engine"), QStringLiteral("idTech3"), QStringLiteral("--object"), QStringLiteral("brush:0")};
		int code = -1;
		auto json = run(valve, brush + QStringList {QStringLiteral("--radiant-reset"), QStringLiteral("0.5,0.5"), QStringLiteral("--output"), out("radiant.map")}, &code);
		expect(code == 0 && json.value(QStringLiteral("operation")).toString() == QStringLiteral("radiant-reset") && json.value(QStringLiteral("facesChanged")).toInt() == 6,
			"map align-textures --radiant-reset", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		{
			auto expected = loaded(valve);
			LevelSurfaceEditPlan plan;
			setLevelMapSelection(&expected, {{LevelMapSelectionKind::QuakeBrush, 0}});
			expect(prepareLevelSurfaceEdits(expected, levelMapSelectedSurfaces(expected), {}, {reset(LevelSurfaceOperation::RadiantReset, 0.5)}, {}, &plan, &error)
					&& commitLevelSurfaceEdit(&expected, plan, &error) && savedBytes(out("radiant.map")) == serializeLevelMap(expected).bytes,
				"--radiant-reset writes what the shared reset writes", error);
		}
		json = run(valve, brush + QStringList {QStringLiteral("--reset-alignment"), QStringLiteral("1,1"), QStringLiteral("--output"), out("tb.map")}, &code);
		{
			auto expected = loaded(valve);
			LevelSurfaceEditPlan plan;
			setLevelMapSelection(&expected, {{LevelMapSelectionKind::QuakeBrush, 0}});
			expect(code == 0 && prepareLevelSurfaceEdits(expected, levelMapSelectedSurfaces(expected), {}, {reset(LevelSurfaceOperation::Reset, 1)}, {}, &plan, &error)
					&& commitLevelSurfaceEdit(&expected, plan, &error) && savedBytes(out("tb.map")) == serializeLevelMap(expected).bytes,
				"--reset-alignment writes what the shared reset writes", error);
		}
		run(primitive, brush + QStringList {QStringLiteral("--radiant-reset"), QStringLiteral("0.5,0.5"), QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "a brush-primitive reset needs a texture size");
		run(primitive, brush + QStringList {QStringLiteral("--radiant-reset"), QStringLiteral("0.5,0.5"), QStringLiteral("--texture-size"), QStringLiteral("128,64"),
			QStringLiteral("--output"), out("primitive-reset.map")}, &code);
		expect(code == 0, "a brush-primitive reset with --texture-size");

		// Reset Textures mode.
		json = run(classic, QStringList {QStringLiteral("--engine"), QStringLiteral("idTech3"), QStringLiteral("--whole-map"), QStringLiteral("--only-texture"),
			QStringLiteral("studio/grid"), QStringLiteral("--rename"), QStringLiteral("studio/new"), QStringLiteral("--set-scale-u"), QStringLiteral("0.25"),
			QStringLiteral("--set-rotation"), QStringLiteral("30"), QStringLiteral("--output"), out("retextured.map")}, &code);
		expect(code == 0 && json.value(QStringLiteral("operation")).toString() == QStringLiteral("reset-textures")
				&& json.value(QStringLiteral("scope")).toString() == QStringLiteral("map") && json.value(QStringLiteral("facesChanged")).toInt() == 6
				&& json.value(QStringLiteral("renamed")).toInt() == 6 && near(json.value(QStringLiteral("set")).toObject().value(QStringLiteral("scale-u")).toDouble(), 0.25),
			"map align-textures Reset Textures mode reports the reset", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		{
			auto expected = loaded(classic);
			LevelTextureResetRequest request;
			request.wholeMap = true;
			request.onlyTexture = QStringLiteral("studio/grid");
			request.renameTo = QStringLiteral("studio/new");
			request.setScaleU = request.setRotation = true;
			request.scaleU = 0.25;
			request.rotation = 30;
			LevelSurfaceEditPlan plan;
			expect(prepareLevelTextureReset(expected, request, {}, &plan, &error) && commitLevelSurfaceEdit(&expected, plan, &error)
					&& savedBytes(out("retextured.map")) == serializeLevelMap(expected).bytes,
				"Reset Textures mode writes what the shared reset writes", error);
		}
		json = run(classic, brush + QStringList {QStringLiteral("--set-shift-u"), QStringLiteral("2"), QStringLiteral("--output"), out("shifted.map")}, &code);
		expect(code == 0 && json.value(QStringLiteral("scope")).toString() == QStringLiteral("selection") && json.value(QStringLiteral("facesChanged")).toInt() == 6,
			"Reset Textures on the --object selection");
		run(primitive, brush + QStringList {QStringLiteral("--set-shift-u"), QStringLiteral("2"), QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "brush-primitive values need a texture size");
		run(primitive, brush + QStringList {QStringLiteral("--set-shift-u"), QStringLiteral("2"), QStringLiteral("--texture-size"), QStringLiteral("128,64"),
			QStringLiteral("--output"), out("primitive-shifted.map")}, &code);
		expect(code == 0, "brush-primitive values with --texture-size");
		const struct {
			QStringList arguments;
			const char* label;
		} refusals[] {
			{brush + QStringList {QStringLiteral("--whole-map"), QStringLiteral("--set-shift-u"), QStringLiteral("2")}, "--whole-map with --object"},
			{brush + QStringList {QStringLiteral("--set-scale-u"), QStringLiteral("0")}, "a zero scale"},
			{brush + QStringList {QStringLiteral("--set-scale-u"), QStringLiteral("wide")}, "a scale that is not a number"},
			{brush + QStringList {QStringLiteral("--rename"), QStringLiteral("studio/new"), QStringLiteral("--shift"), QStringLiteral("1,1")}, "Reset Textures with an operation"},
			{brush + QStringList {QStringLiteral("--only-texture"), QStringLiteral("studio/grid")}, "a filter with nothing to set"},
			{brush + QStringList {QStringLiteral("--rename"), QStringLiteral("bad name")}, "a name with a space"},
			{QStringList {QStringLiteral("--set-shift-u"), QStringLiteral("2")}, "no selection and no --whole-map"},
		};
		for (const auto& refusal : refusals) {
			run(classic, refusal.arguments + QStringList {QStringLiteral("--output"), out("refused.map")}, &code);
			expect(code != 0 && !QFile::exists(out("refused.map")), "refused", QString::fromLatin1(refusal.label));
		}
	}
	std::cout << (ok ? "Texture reset checks passed" : "Texture reset checks failed") << std::endl;
	return ok ? 0 : 1;
}
