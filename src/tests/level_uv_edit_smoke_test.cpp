// Core and command-line checks of the UV Editor's service
// (core/level_uv_edit.h), NetRadiant Custom's TexTool: texture-space
// transforms turn clockwise as the view draws them and keep quarter turns
// exact; a face reads its winding in texels in every format; a move changes
// only the offsets; a turn keeps a classic face classic where it can, takes
// the nearest classic mapping (the anchor kept) or converts to Valve 220 when
// asked, keeps a Valve 220 face's scales and turns a brush-primitive face
// exactly in texel space; a patch's points move one by one and its untouched
// points keep their stored numbers; the drag helpers move and turn as the
// view drags; every edit is one exact undo step through the surface plan;
// hidden, locked, out-of-group and changed targets are refused without a
// change. map edit-uv lists the points and writes what the service writes.

#include "core/level_document.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_texture_mapping.h"
#include "core/level_uv_edit.h"
#include "core/level_visibility.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

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

bool load(const QByteArray& bytes, const QString& engine, LevelMapDocument* document)
{
	QString error;
	return expect(loadLevelMapBytes({QStringLiteral("uv.map"), {}, engine}, bytes, document, &error), "load fixture", error);
}

bool near(QPointF a, QPointF b, double tolerance = 1e-6)
{
	return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
}

QString text(QPointF p)
{
	return QStringLiteral("%1,%2").arg(p.x(), 0, 'g', 12).arg(p.y(), 0, 'g', 12);
}

bool allNear(const QVector<QPointF>& a, const QVector<QPointF>& b, double tolerance = 1e-6)
{
	if (a.size() != b.size()) {
		return false;
	}
	for (qsizetype i = 0; i < a.size(); ++i) {
		if (!near(a[i], b[i], tolerance)) {
			return false;
		}
	}
	return true;
}

QVector<QPointF> mapped(const QVector<QPointF>& uv, const LevelUvTransform& transform)
{
	QVector<QPointF> result;
	for (const QPointF& point : uv) {
		result.append(transform.map(point));
	}
	return result;
}

LevelMaterialTarget face(int brush, int index)
{
	return {LevelMaterialKind::BrushFace, brush, index};
}

// Commits `edit` and checks it is one undo step that undo reverses exactly.
bool commitOnce(LevelMapDocument* document, const LevelUvLayout& layout, const LevelUvEdit& edit, LevelUvFit* fit, const char* label)
{
	const QByteArray before = serializeLevelMap(*document).bytes;
	const auto depth = document->undoStack.size();
	LevelSurfaceEditPlan plan;
	QString error;
	if (!expect(prepareLevelUvEdit(*document, layout, edit, &plan, &error, fit) && commitLevelSurfaceEdit(document, plan, &error), label, error)) {
		return false;
	}
	expect(document->undoStack.size() == depth + 1 && serializeLevelMap(*document).bytes != before, "one undo step", QString::fromLatin1(label));
	return true;
}

bool undoExactly(LevelMapDocument* document, const QByteArray& before, const char* label)
{
	QString error;
	return expect(undoLevelMapEdit(document, &error) && serializeLevelMap(*document).bytes == before, label, error);
}

QByteArray patchFixture(int* patchId)
{
	LevelMapDocument created;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	QString error;
	createLevelMap(create, &created, &error);
	LevelPatchCreateRequest request;
	request.shape = QStringLiteral("plane");
	request.texture = QStringLiteral("studio/grid");
	request.size = {256, 128, 128, true};
	LevelMapPatch plane;
	expect(createLevelPatch(request, &plane, &error) && addLevelMapPatch(&created, plane, patchId, &error), "patch fixture", error);
	return serializeLevelMap(created).bytes;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary directory")) {
		return 1;
	}
	QString error;

	phase = QStringLiteral("transforms");
	{
		const LevelUvTransform quarter = LevelUvTransform::rotation({0, 0}, 90);
		expect(quarter.map({1, 0}) == QPointF(0, 1) && quarter.map({0, 1}) == QPointF(-1, 0), "a quarter turn is exact and clockwise with t down",
			text(quarter.map({1, 0})));
		const LevelUvTransform half = LevelUvTransform::rotation({10, 5}, 180);
		expect(half.map({12, 5}) == QPointF(8, 5) && half.map({10, 5}) == QPointF(10, 5), "a half turn keeps its pivot", text(half.map({12, 5})));
		const LevelUvTransform both = LevelUvTransform::translation({1, 2}).then(quarter);
		expect(both.map({0, 0}) == QPointF(-2, 1), "then() applies the second transform after the first", text(both.map({0, 0})));
		expect(LevelUvTransform {}.isIdentity() && LevelUvTransform::translation({1, 0}).isTranslation() && !quarter.isTranslation(), "transform kinds");
		expect(std::abs(levelUvTurnAngle({0, 0}, {1, 0}, {0, 1}) - 90) < 1e-12 && std::abs(levelUvTurnAngle({0, 0}, {0, 1}, {1, 0}) + 90) < 1e-12
				&& levelUvTurnAngle({0, 0}, {0, 0}, {1, 0}) == 0,
			"turn angles are clockwise in texture space");
		expect(snapLevelUv({1.4, -2.6}, 1) == QPointF(1, -3) && snapLevelUv({1.4, -2.6}, 0) == QPointF(1.4, -2.6) && snapLevelUv({0.3, 0.2}, 0.25) == QPointF(0.25, 0.25),
			"snapping");
		// The drags.
		const QVector<QPointF> square {{0, 0}, {16, 0}, {16, 16}, {0, 16}};
		const LevelUvTransform move = levelUvFaceDrag(square, 1, -1, {20, 4});
		expect(move.isTranslation() && move.map(square[1]) == QPointF(20, 4), "a drag without an anchor moves the texture");
		expect(levelUvFaceDrag(square, 0, 0, {5, 5}).map(square[0]) == QPointF(5, 5), "dragging the anchor itself moves");
		const LevelUvTransform turn = levelUvFaceDrag(square, 1, 0, {0, 16});
		expect(near(turn.map(square[1]), {0, 16}, 1e-12) && turn.map(square[0]) == QPointF(0, 0), "a drag about the anchor turns", text(turn.map(square[1])));
		const LevelUvTransform stepped = levelUvFaceDrag(square, 1, 0, {16, 5}, 15);
		const double angle = levelUvTurnAngle({0, 0}, square[1], stepped.map(square[1]));
		expect(std::abs(angle - 15) < 1e-9, "a stepped turn snaps to the step", QString::number(angle));
		const QVector<QPointF> dragged = levelUvPatchDrag(square, {1, 2}, 1, {18, 3});
		expect(dragged[0] == square[0] && dragged[1] == QPointF(18, 3) && dragged[2] == QPointF(18, 19) && dragged[3] == square[3], "a patch drag moves the selection");
		const QVector<QPointF> turned = levelUvPatchTransformed(square, {}, quarter);
		expect(turned[1] == QPointF(0, 16), "a patch transform without a selection takes every point");
	}

	phase = QStringLiteral("classic face");
	{
		LevelMapDocument document;
		load(tests::surfaceFixture(QStringLiteral("classic")), QStringLiteral("idtech2"), &document);
		const int brush = document.brushes.first().id;
		LevelUvLayout layout;
		expect(readLevelUvLayout(document, face(brush, 0), {}, &layout, &error) && layout.format == LevelUvFormat::Classic && layout.points.size() >= 3
				&& layout.uv.size() == layout.points.size() && !layout.imageSize.isValid() && layout.material == QStringLiteral("studio/grid"),
			"a classic face reads in texels without an image size", error);
		const LevelTextureProjection projection = levelTextureProjection(layout.face);
		expect(near(layout.uv.first(), projection.at(layout.points.first()), 1e-9), "texel coordinates are the projection's");
		// A move changes only the shift.
		const QByteArray original = serializeLevelMap(document).bytes;
		LevelUvEdit move;
		move.transform = LevelUvTransform::translation({16, -8});
		LevelUvResult result;
		expect(applyLevelUvEdit(layout, move, &result, &error) && result.fit == LevelUvFit::Exact && result.face.shiftX == layout.face.shiftX + 16
				&& result.face.shiftY == layout.face.shiftY - 8 && result.face.rotation == layout.face.rotation && result.face.scaleX == layout.face.scaleX
				&& !result.face.explicitTextureAxes && allNear(result.uv, mapped(layout.uv, move.transform)),
			"a move changes the shift only", error);
		LevelUvFit fit = LevelUvFit::Unchanged;
		if (commitOnce(&document, layout, move, &fit, "commit a move")) {
			const QString saved = QString::fromUtf8(serializeLevelMap(document).bytes);
			expect(saved.contains(QStringLiteral("\"studio/grid\" 23 /* retained */ 1 17 -0.5 2")), "only the shift numbers change, comments kept", saved);
			undoExactly(&document, original, "undo restores the exact map");
		}
		// This fixture scales U and V differently, so a classic face cannot
		// hold every turn: the nearest classic mapping keeps the anchor.
		LevelUvEdit turn;
		turn.transform = LevelUvTransform::rotation(layout.uv[0], 30);
		turn.anchor = 0;
		expect(applyLevelUvEdit(layout, turn, &result, &error) && result.fit == LevelUvFit::Approximated && !result.face.explicitTextureAxes
				&& near(result.uv[0], layout.uv[0], 1e-6),
			"an unequal classic turn is approximated about the anchor", error + QLatin1Char(' ') + text(result.uv[0]) + QLatin1Char(' ') + text(layout.uv[0]));
		turn.allowValve220 = true;
		expect(applyLevelUvEdit(layout, turn, &result, &error) && result.fit == LevelUvFit::Converted && result.face.explicitTextureAxes
				&& allNear(result.uv, mapped(layout.uv, turn.transform)),
			"with Valve 220 allowed, the turn is exact", error);
		if (commitOnce(&document, layout, turn, &fit, "commit a converted turn")) {
			expect(fit == LevelUvFit::Converted && document.brushes.first().faces.first().explicitTextureAxes, "the face is Valve 220 now");
			LevelUvLayout after;
			expect(readLevelUvLayout(document, face(brush, 0), {}, &after, &error) && allNear(after.uv, mapped(layout.uv, turn.transform), 1e-5),
				"the saved face reads the turned coordinates", error);
			undoExactly(&document, original, "undo restores the classic face");
		}
		// An identity edit changes nothing and commits nothing.
		LevelSurfaceEditPlan plan;
		const auto depth = document.undoStack.size();
		expect(prepareLevelUvEdit(document, layout, {}, &plan, &error, &fit) && fit == LevelUvFit::Unchanged && commitLevelSurfaceEdit(&document, plan, &error)
				&& document.undoStack.size() == depth && serializeLevelMap(document).bytes == original,
			"no change commits nothing", error);
	}

	phase = QStringLiteral("uniform classic face");
	{
		LevelMapDocument document;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("quake");
		create.starterRoom = false;
		int brush = -1;
		expect(createLevelMap(create, &document, &error) && addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("wall"), &brush, &error),
			"a Quake box", error);
		LevelUvLayout layout;
		expect(readLevelUvLayout(document, face(brush, 0), QSize(64, 64), &layout, &error), "read", error);
		LevelUvEdit turn;
		turn.transform = LevelUvTransform::rotation(layout.centre(), 90);
		LevelUvResult result;
		expect(applyLevelUvEdit(layout, turn, &result, &error) && result.fit == LevelUvFit::Exact && !result.face.explicitTextureAxes
				&& std::abs(std::remainder(result.face.rotation - layout.face.rotation, 90.0)) < 1e-9
				&& allNear(result.uv, mapped(layout.uv, turn.transform), 1e-6),
			"an equal-scale classic face holds a quarter turn exactly", error + QLatin1Char(' ') + QString::number(result.face.rotation));
	}

	phase = QStringLiteral("Valve 220 face");
	{
		LevelMapDocument document;
		load(tests::surfaceFixture(QStringLiteral("valve220")), QStringLiteral("idtech2"), &document);
		const int brush = document.brushes.first().id;
		LevelUvLayout layout;
		expect(readLevelUvLayout(document, face(brush, 2), {}, &layout, &error) && layout.format == LevelUvFormat::Valve220, "read", error);
		LevelUvEdit turn;
		turn.transform = LevelUvTransform::rotation(layout.uv[1], 45);
		turn.anchor = 1;
		LevelUvResult result;
		expect(applyLevelUvEdit(layout, turn, &result, &error) && result.fit == LevelUvFit::Exact && result.face.scaleX == layout.face.scaleX
				&& result.face.scaleY == layout.face.scaleY && allNear(result.uv, mapped(layout.uv, turn.transform)),
			"a Valve 220 face keeps its scales and turns exactly", error);
		const QByteArray original = serializeLevelMap(document).bytes;
		LevelUvFit fit = LevelUvFit::Unchanged;
		if (commitOnce(&document, layout, turn, &fit, "commit a Valve 220 turn")) {
			undoExactly(&document, original, "undo restores the Valve 220 face");
		}
	}

	phase = QStringLiteral("brush-primitive face");
	{
		LevelMapDocument document;
		load(tests::surfaceFixture(QStringLiteral("brushDef")), QStringLiteral("idtech3"), &document);
		const int brush = document.brushes.first().id;
		LevelUvLayout layout;
		expect(!readLevelUvLayout(document, face(brush, 0), {}, &layout, &error) && error.contains(QStringLiteral("image size")),
			"brush primitives need the image size", error);
		expect(readLevelUvLayout(document, face(brush, 0), QSize(64, 32), &layout, &error) && layout.format == LevelUvFormat::BrushPrimitive
				&& near(layout.uv[0], QPointF(levelTextureProjection(layout.face).at(layout.points[0]).x() * 64,
										 levelTextureProjection(layout.face).at(layout.points[0]).y() * 32), 1e-9),
			"repeats read as texels", error);
		LevelUvEdit move;
		move.transform = LevelUvTransform::translation({16, 8});
		LevelUvResult result;
		expect(applyLevelUvEdit(layout, move, &result, &error) && result.face.textureMatrix[2] == layout.face.textureMatrix[2] + 0.25
				&& result.face.textureMatrix[5] == layout.face.textureMatrix[5] + 0.25 && result.face.textureMatrix[0] == layout.face.textureMatrix[0],
			"a move shifts the matrix by repeats", error);
		LevelUvEdit turn;
		turn.transform = LevelUvTransform::rotation(layout.centre(), 30);
		expect(applyLevelUvEdit(layout, turn, &result, &error) && result.fit == LevelUvFit::Exact && result.face.explicitTextureMatrix
				&& allNear(result.uv, mapped(layout.uv, turn.transform), 1e-6),
			"a brush-primitive turn is exact in texels, whatever the image's proportions", error);
	}

	phase = QStringLiteral("patch");
	{
		int created = -1;
		LevelMapDocument document;
		load(patchFixture(&created), QStringLiteral("idtech3"), &document);
		if (!expect(document.patches.size() == 1, "patch reloaded")) {
			return 1;
		}
		const LevelMapPatch start = document.patches.first();
		const LevelMaterialTarget target {LevelMaterialKind::Patch, start.id, 0};
		LevelUvLayout layout;
		expect(!readLevelUvLayout(document, target, {}, &layout, &error), "patches need the image size");
		expect(readLevelUvLayout(document, target, QSize(128, 64), &layout, &error) && layout.isPatch() && layout.columns == start.width
				&& layout.rows == start.height && layout.uv.size() == start.controlPoints.size()
				&& layout.uv[1] == QPointF(start.controlU[1] * 128, start.controlV[1] * 64),
			"a patch reads its control grid in texels", error);
		const int middle = static_cast<int>(layout.uv.size() / 2);
		LevelUvEdit edit;
		edit.patchUv = levelUvPatchDrag(layout.uv, {middle}, middle, {100, 50});
		LevelUvResult result;
		bool untouched = true;
		expect(applyLevelUvEdit(layout, edit, &result, &error) && result.fit == LevelUvFit::Exact && result.patch.controlU[middle] == 100.0 / 128
				&& result.patch.controlV[middle] == 50.0 / 64,
			"a patch point takes its new coordinates", error);
		for (qsizetype i = 0; i < layout.uv.size(); ++i) {
			untouched &= i == middle || (result.patch.controlU[i] == start.controlU[i] && result.patch.controlV[i] == start.controlV[i]);
		}
		expect(untouched, "untouched points keep their stored numbers exactly");
		const QByteArray original = serializeLevelMap(document).bytes;
		LevelUvFit fit = LevelUvFit::Unchanged;
		if (commitOnce(&document, layout, edit, &fit, "commit a patch point")) {
			expect(document.patches.first().controlU[middle] == 100.0 / 128, "the map holds the new point");
			undoExactly(&document, original, "undo restores the patch");
		}
		LevelUvEdit turn;
		turn.patchUv = levelUvPatchTransformed(layout.uv, {}, LevelUvTransform::rotation(layout.centre(), 90));
		expect(applyLevelUvEdit(layout, turn, &result, &error) && allNear(result.uv, turn.patchUv, 1e-9), "a patch turns all its points", error);
		LevelUvEdit wrong;
		wrong.patchUv = {QPointF(0, 0)};
		expect(!applyLevelUvEdit(layout, wrong, &result, &error), "a point count that does not match is refused");
	}

	phase = QStringLiteral("refusals");
	{
		LevelMapDocument document;
		load(tests::surfaceFixture(QStringLiteral("classic")), QStringLiteral("idtech2"), &document);
		int second = -1;
		expect(addLevelMapBoxBrush(&document, {256, 0, 0, true}, {320, 64, 64, true}, QStringLiteral("studio/grid"), &second, &error), "a second brush", error);
		const int first = document.brushes.first().id;
		LevelUvLayout firstLayout, secondLayout;
		expect(readLevelUvLayout(document, face(first, 0), {}, &firstLayout, &error) && readLevelUvLayout(document, face(second, 0), {}, &secondLayout, &error),
			"read both", error);
		LevelUvEdit move;
		move.transform = LevelUvTransform::translation({8, 0});
		const auto refused = [&](const LevelUvLayout& layout, const char* label, const char* reason) {
			const QByteArray before = serializeLevelMap(document).bytes;
			const auto depth = document.undoStack.size();
			LevelSurfaceEditPlan plan;
			QString problem;
			expect(!prepareLevelUvEdit(document, layout, move, &plan, &problem) && problem.contains(QString::fromLatin1(reason))
					&& serializeLevelMap(document).bytes == before && document.undoStack.size() == depth,
				label, problem);
		};
		expect(!levelUvTargetProblem(document, face(first, 99)).isEmpty() && !levelUvTargetProblem(document, {}).isEmpty(), "a missing face is a problem");
		// Hidden.
		setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, first}});
		LevelVisibilityRequest hide;
		expect(changeLevelMapVisibility(&document, hide, nullptr, &error), "hide the first brush", error);
		refused(firstLayout, "a hidden face is refused", "hidden");
		LevelVisibilityRequest show;
		show.operation = LevelVisibilityOperation::ShowAll;
		expect(changeLevelMapVisibility(&document, show, nullptr, &error), "show all", error);
		// Outside the open group.
		QString group;
		expect(createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Room"), {}, &group, &error)
				&& assignLevelSceneObjects(&document, group, {QStringLiteral("brush:%1").arg(first)}, &error) && openLevelSceneGroup(&document, group, &error),
			"open a group holding the first brush", error);
		refused(secondLayout, "a face outside the open group is refused", "outside the open group");
		expect(closeLevelSceneGroup(&document, true, &error), "close the group", error);
		// Locked.
		QString layer;
		expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
				&& assignLevelSceneObjects(&document, layer, {QStringLiteral("brush:%1").arg(second)}, &error) && setLevelSceneLocked(&document, layer, true, &error),
			"lock the second brush", error);
		refused(secondLayout, "a locked face is refused", "locked");
		// Changed since it was read.
		LevelUvLayout fresh;
		expect(readLevelUvLayout(document, face(first, 0), {}, &fresh, &error), "read again", error);
		LevelUvEdit shift;
		shift.transform = LevelUvTransform::translation({4, 4});
		LevelSurfaceEditPlan plan;
		LevelUvFit fit = LevelUvFit::Unchanged;
		expect(prepareLevelUvEdit(document, fresh, shift, &plan, &error, &fit) && commitLevelSurfaceEdit(&document, plan, &error), "edit the face", error);
		refused(fresh, "a stale layout is refused", "changed");
		expect(readLevelUvLayout(document, face(first, 0), {}, &fresh, &error), "read the new mapping", error);
		// Moving a perpendicular plane changes this face's corners without
		// changing its own plane or attributes: old point numbers are stale.
		LevelMapBrush& brush = document.brushes.first();
		const auto normal = planeFromPoints(brush.faces[0].p0, brush.faces[0].p1, brush.faces[0].p2);
		for (int i = 1; i < brush.faces.size(); ++i) {
			auto& side = brush.faces[i];
			const auto other = planeFromPoints(side.p0, side.p1, side.p2);
			if (std::abs(normal.normalX * other.normalX + normal.normalY * other.normalY + normal.normalZ * other.normalZ) < 0.1) {
				for (auto* point : {&side.p0, &side.p1, &side.p2}) {
					point->x += other.normalX * 8;
					point->y += other.normalY * 8;
					point->z += other.normalZ * 8;
				}
				break;
			}
		}
		refused(fresh, "a layout with changed neighbouring planes is refused", "changed");
	}

	if (argc > 1) {
		phase = QStringLiteral("cli");
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const QString settings = QDir(temp.path()).filePath(QStringLiteral("settings.ini"));
		const auto run = [&](const QStringList& arguments, int* exitCode) {
			QProcess process;
			process.start(binary, QStringList {QStringLiteral("--settings-file"), settings, QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("edit-uv")}
				+ arguments + QStringList {QStringLiteral("--json")});
			const bool ended = process.waitForFinished(60000);
			*exitCode = ended && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
			return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		};
		const auto out = [&](const char* name) { return QDir(temp.path()).filePath(QString::fromLatin1(name)); };
		const auto read = [](const QString& path) {
			QFile file(path);
			return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
		};
		const auto compact = [](const QJsonObject& json) { return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)); };
		const QString classic = out("classic.map"), primitive = out("primitive.map"), curves = out("curves.map");
		int created = -1;
		tests::putMaterialFile(classic, tests::surfaceFixture(QStringLiteral("classic")));
		tests::putMaterialFile(primitive, tests::surfaceFixture(QStringLiteral("brushDef")));
		tests::putMaterialFile(curves, patchFixture(&created));
		const QByteArray original = read(classic);
		LevelMapDocument source;
		load(original, QStringLiteral("idtech2"), &source);
		LevelUvLayout layout;
		expect(readLevelUvLayout(source, face(source.brushes.first().id, 0), {}, &layout, &error), "reference layout", error);
		int code = -1;
		// Listing.
		auto json = run({classic, QStringLiteral("--engine"), QStringLiteral("idtech2"), QStringLiteral("--target"), QStringLiteral("face:0:1")}, &code);
		const QJsonArray points = json.value(QStringLiteral("points")).toArray();
		const QJsonArray first = points.at(0).toObject().value(QStringLiteral("texel")).toArray();
		expect(code == 0 && json.value(QStringLiteral("format")).toString() == QStringLiteral("classic") && points.size() == layout.uv.size()
				&& near(QPointF(first.at(0).toDouble(), first.at(1).toDouble()), layout.uv[0], 1e-9) && !json.value(QStringLiteral("mapWritten")).toBool(),
			"map edit-uv lists the winding in texels", compact(json));
		// Move point 1 to 64,32: the service's own edit.
		LevelMapDocument expected = source;
		LevelUvEdit edit;
		edit.transform = levelUvFaceDrag(layout.uv, 0, -1, {64, 32});
		LevelUvFit fit = LevelUvFit::Unchanged;
		LevelSurfaceEditPlan plan;
		expect(prepareLevelUvEdit(expected, layout, edit, &plan, &error, &fit) && commitLevelSurfaceEdit(&expected, plan, &error), "reference move", error);
		const QStringList move {classic, QStringLiteral("--engine"), QStringLiteral("idtech2"), QStringLiteral("--target"), QStringLiteral("face:0:1"),
			QStringLiteral("--move-point"), QStringLiteral("1"), QStringLiteral("--to"), QStringLiteral("64,32")};
		json = run(move + QStringList {QStringLiteral("--output"), out("dry.map"), QStringLiteral("--dry-run")}, &code);
		expect(code == 0 && !QFile::exists(out("dry.map")) && read(classic) == original && json.value(QStringLiteral("changed")).toBool(), "a dry run writes nothing",
			compact(json));
		json = run(move + QStringList {QStringLiteral("--output"), out("moved.map")}, &code);
		LevelMapDocument written;
		load(read(out("moved.map")), QStringLiteral("idtech2"), &written);
		expect(code == 0 && json.value(QStringLiteral("fit")).toString() == QStringLiteral("exact")
				&& serializeLevelMap(written).bytes == serializeLevelMap(expected).bytes && read(classic) == original,
			"--move-point writes what the service writes", compact(json));
		// A turn about point 2, converted to Valve 220 when allowed.
		json = run({classic, QStringLiteral("--engine"), QStringLiteral("idtech2"), QStringLiteral("--target"), QStringLiteral("face:0:1"), QStringLiteral("--rotate"),
					   QStringLiteral("-30"), QStringLiteral("--anchor"), QStringLiteral("2"), QStringLiteral("--allow-valve220"), QStringLiteral("--output"),
					   out("turned.map")},
			&code);
		LevelMapDocument turned;
		load(read(out("turned.map")), QStringLiteral("idtech2"), &turned);
		LevelUvLayout after;
		expect(code == 0 && json.value(QStringLiteral("fit")).toString() == QStringLiteral("converted")
				&& readLevelUvLayout(turned, face(turned.brushes.first().id, 0), {}, &after, &error)
				&& allNear(after.uv, mapped(layout.uv, LevelUvTransform::rotation(layout.uv[1], -30)), 1e-5),
			"--rotate turns about the anchor", compact(json) + QLatin1Char(' ') + error);
		// A patch point needs the image size, then moves.
		LevelMapDocument patchSource;
		load(read(curves), QStringLiteral("idtech3"), &patchSource);
		const QString patchTarget = QStringLiteral("patch:%1").arg(patchSource.patches.first().id);
		run({curves, QStringLiteral("--engine"), QStringLiteral("idtech3"), QStringLiteral("--target"), patchTarget, QStringLiteral("--move-point"), QStringLiteral("5"),
				QStringLiteral("--to"), QStringLiteral("32,16"), QStringLiteral("--output"), out("nosize.map")},
			&code);
		expect(code == 4 && !QFile::exists(out("nosize.map")), "a patch without an image size is refused");
		json = run({curves, QStringLiteral("--engine"), QStringLiteral("idtech3"), QStringLiteral("--target"), patchTarget, QStringLiteral("--move-point"),
					   QStringLiteral("5"), QStringLiteral("--to"), QStringLiteral("32,16"), QStringLiteral("--texture-size"), QStringLiteral("128,128"),
					   QStringLiteral("--output"), out("curves-uv.map")},
			&code);
		LevelMapDocument patched;
		load(read(out("curves-uv.map")), QStringLiteral("idtech3"), &patched);
		expect(code == 0 && patched.patches.size() == 1 && patched.patches.first().controlU.value(4) == 0.25 && patched.patches.first().controlV.value(4) == 0.125
				&& patched.patches.first().controlU.value(0) == patchSource.patches.first().controlU.value(0),
			"a patch point moves", compact(json));
		// Brush primitives without a size, and usage errors.
		run({primitive, QStringLiteral("--engine"), QStringLiteral("idtech3"), QStringLiteral("--target"), QStringLiteral("face:0:1")}, &code);
		expect(code == 4, "a brush-primitive face needs the image size");
		json = run({primitive, QStringLiteral("--engine"), QStringLiteral("idtech3"), QStringLiteral("--target"), QStringLiteral("face:0:1"), QStringLiteral("--texture-size"),
					   QStringLiteral("64,32")},
			&code);
		expect(code == 0 && json.value(QStringLiteral("imageSizeSource")).toString() == QStringLiteral("explicit"), "with an explicit size it lists", compact(json));
		run(move, &code);
		expect(code == 2, "a change needs --output");
		run({classic, QStringLiteral("--move-point"), QStringLiteral("1"), QStringLiteral("--to"), QStringLiteral("0,0"), QStringLiteral("--output"), out("x.map")}, &code);
		expect(code == 2 && !QFile::exists(out("x.map")), "a change needs --target");
		run({curves, QStringLiteral("--target"), patchTarget, QStringLiteral("--offset"), QStringLiteral("1,1"), QStringLiteral("--allow-valve220"),
				QStringLiteral("--texture-size"), QStringLiteral("64,64"), QStringLiteral("--output"), out("y.map")},
			&code);
		expect(code == 2 && !QFile::exists(out("y.map")), "patches take no Valve 220 conversion");
		run({classic, QStringLiteral("--target"), QStringLiteral("face:0:1"), QStringLiteral("--offset"), QStringLiteral("1,1"), QStringLiteral("--rotate"),
				QStringLiteral("5"), QStringLiteral("--output"), out("z.map")},
			&code);
		expect(code == 2 && !QFile::exists(out("z.map")), "one change at a time");
		for (const QString& points : {QStringLiteral(","), QStringLiteral("1,,2")}) {
			run({curves, QStringLiteral("--target"), patchTarget, QStringLiteral("--offset"), QStringLiteral("1,1"), QStringLiteral("--points"), points,
					QStringLiteral("--texture-size"), QStringLiteral("64,64"), QStringLiteral("--output"), out("bad-points.map")}, &code);
			expect(code == 2 && !QFile::exists(out("bad-points.map")), "an empty point selector never means every patch point");
		}
		run({}, &code);
		expect(code == 2, "no map is a usage error");
	}

	std::cout << (ok ? "UV edit checks passed" : "UV edit checks failed") << std::endl;
	return ok ? 0 : 1;
}
