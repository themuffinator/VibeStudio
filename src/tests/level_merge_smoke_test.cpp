#include "core/level_brush.h"
#include "core/level_merge.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <random>
#include <set>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *label, const QString &error = {})
{
	if (!value) {
		std::cerr << label << ": " << error.toStdString() << '\n';
	}
	return value;
}
LevelMapDocument map()
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &document);
	return document;
}
bool box(LevelMapDocument *document, double x, double y, double z, double width = 64, double height = 64, double depth = 64)
{
	return addLevelMapBoxBrush(document, {x, y, z, true}, {x + width, y + height, z + depth, true}, QStringLiteral("studio/grid"));
}
void selectAll(LevelMapDocument *document)
{
	QVector<LevelMapSelectionRef> selection;
	for (const auto &brush : document->brushes) {
		selection << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, brush.id};
	}
	setLevelMapSelection(document, selection);
}
bool put(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temporary;
	if (!temporary.isValid()) {
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	LevelMapDocument cliSource;
	for (const auto &dialect :
		 {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		const auto path = QDir(temporary.path()).filePath(dialect + QStringLiteral(".map"));
		ok &= expect(put(path, tests::surfaceFixture(dialect)), "write source");
		LevelMapLoadRequest load;
		load.path = path;
		load.engineHint = QStringLiteral("idtech3");
		LevelMapDocument document;
		ok &= expect(loadLevelMap(load, &document, &error), "load dialect", error);
		ok &= expect(duplicateLevelMapObjects(&document, {{LevelMapSelectionKind::QuakeBrush, 0}}, 128, 0, 0, &error), "adjacent duplicate",
					 error);
		selectAll(&document);
		const auto before = serializeLevelMap(document).bytes;
		const auto selection = document.selection;
		const auto undoSize = document.undoStack.size();
		LevelBrushMergePlan plan;
		if (!expect(prepareLevelBrushMerge(document, {}, &plan, &error), "prepare adjacent union", error)) {
			return EXIT_FAILURE;
		}
		ok &= expect(plan.ready() && plan.brush().faceCount == 6 && std::abs(plan.geometry().volume - 256.0 * 64 * 64) < 0.001,
					 "exact volume and faces");
		ok &= expect(serializeLevelMap(document).bytes == before && document.undoStack.size() == undoSize, "preparation is read only");
		const auto donorBrushes = document.brushes;
		ok &= expect(commitLevelBrushMerge(&document, plan, &error), "commit merge", error);
		const auto after = serializeLevelMap(document).bytes;
		ok &= expect(document.brushes.size() == 1 && document.entities.size() == 2 && document.undoStack.size() == undoSize + 1 &&
						 document.selectedObjectId == plan.brush().id,
					 "one result and undo step, unrelated entity retained");
		ok &=
			expect(after.count("// flags") == before.count("// flags") && after.count("/* retained */") == before.count("/* retained */") &&
					   after.contains("// untouched point entity\r\n{\r\n\"classname\" \"light\""),
				   "comments and unrelated text retained");
		for (int f = 0; f < plan.geometry().faces.size(); ++f) {
			const auto ref = plan.geometry().faces[f].chosen;
			const auto &donor = donorBrushes[ref.brushId].faces[ref.faceIndex];
			const auto &result = document.brushes.first().faces[f];
			const auto a = levelTextureProjection(donor), b = levelTextureProjection(result);
			const auto geometry = solveBrushGeometry(document.brushes.first().faces);
			for (const auto &p : geometry.faces[f].points) {
				const auto d = a.at(p) - b.at(p);
				ok &= expect(std::abs(d.x()) < 1e-6 && std::abs(d.y()) < 1e-6, "source UV preserved across entire merged plane");
			}
			ok &= expect(result.contentFlags == 2 && result.surfaceFlags == 4 && result.surfaceValue == 8, "surface flags retained");
		}
		ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before && document.selection == selection,
					 "exact undo including selection", error);
		ok &= expect(redoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == after, "exact redo", error);
		const auto saved = saveLevelMapAs(document, QDir(temporary.path()).filePath(dialect + QStringLiteral("-merged.map")), false, false);
		ok &= expect(saved.succeeded(), "save merged dialect");
		load.path = QDir(temporary.path()).filePath(dialect + QStringLiteral("-merged.map"));
		LevelMapDocument reopened;
		ok &= expect(loadLevelMap(load, &reopened, &error) && reopened.brushes.size() == 1 && reopened.brushes.first().faceCount == 6 &&
						 reopened.brushes.first().primitiveKind == dialect,
					 "reload dialect and solid", error);
	}
	for (const double offset : {0.0, 32.0, 64.0, 64.00001, 64.001, 80.0}) {
		auto document = map();
		box(&document, 0, 0, 0);
		box(&document, offset, 0, 0);
		selectAll(&document);
		const auto before = serializeLevelMap(document).bytes;
		LevelBrushMergePlan plan;
		const bool prepared = prepareLevelBrushMerge(document, {}, &plan, &error);
		ok &= expect(prepared == (offset <= 64), "overlap, duplicate, contact and gap classification", error);
		ok &= expect(serializeLevelMap(document).bytes == before, "classification leaves map unchanged");
		if (prepared) {
			ok &= expect(std::abs(plan.geometry().volume - (64 + offset) * 4096) < 0.001, "overlap counted once");
		}
	}
	{
		auto document = map();
		box(&document, 0, 0, 0);
		box(&document, 64, 32, 0);
		selectAll(&document);
		LevelBrushMergePlan plan;
		ok &= expect(!prepareLevelBrushMerge(document, {}, &plan, &error), "L shape rejected");
		document = map();
		box(&document, 0, 0, 0);
		box(&document, 64, 64, 64);
		selectAll(&document);
		ok &= expect(!prepareLevelBrushMerge(document, {}, &plan, &error), "point contact rejected");
		// A closed shell shares every exterior plane and corner with a box,
		// so checking only bounds, support planes or vertices would be unsafe.
		document = map();
		box(&document, 0, 0, 0, 128, 128, 16);
		box(&document, 0, 0, 112, 128, 128, 16);
		box(&document, 0, 0, 16, 16, 128, 96);
		box(&document, 112, 0, 16, 16, 128, 96);
		box(&document, 16, 0, 16, 96, 16, 96);
		box(&document, 16, 112, 16, 96, 16, 96);
		selectAll(&document);
		ok &= expect(!prepareLevelBrushMerge(document, {}, &plan, &error) && error.contains(QStringLiteral("cavity")),
					 "hidden cavity rejected", error);
		box(&document, 16, 16, 16, 96, 96, 96);
		selectAll(&document);
		ok &= expect(prepareLevelBrushMerge(document, {}, &plan, &error) && plan.ready(), "filled shell is an exact union", error);
	}
	{
		auto document = map();
		box(&document, 0, 0, 0);
		box(&document, 64, 0, 0);
		selectAll(&document);
		LevelBrushMergePlan plan;
		ok &=
			expect(!prepareLevelBrushMerge(document, {}, &plan, &error, [] { return true; }) && !plan.prepared(), "cancelled preparation");
		LevelMapRotationRequest rotate;
		rotate.axis = 2;
		rotate.degrees = 23;
		rotate.allowValve220 = true;
		rotate.textureLock = true;
		ok &= expect(rotateLevelMapSelection(&document, rotate, &error), "rotate adjacent geometry", error);
		ok &= expect(prepareLevelBrushMerge(document, {}, &plan, &error) && plan.ready(), "oblique union", error);
	}
	{
		auto document = map();
		box(&document, 0, 0, 0);
		box(&document, 64, 0, 0);
		// Different mapping on one shared exterior plane must not be guessed.
		const auto geometry = solveBrushGeometry(document.brushes[1].faces);
		int top = 0;
		for (int f = 0; f < geometry.faces.size(); ++f) {
			if (geometry.faces[f].plane.normalZ > 0.99) {
				top = f;
			}
		}
		ok &= expect(setLevelMapBrushFaceProperty(&document, 1, top, QStringLiteral("shiftX"), QStringLiteral("7"), &error),
					 "change source mapping", error);
		selectAll(&document);
		LevelBrushMergePlan plan;
		ok &=
			expect(prepareLevelBrushMerge(document, {}, &plan, &error) && plan.prepared() && !plan.ready(), "UV conflict reported", error);
		const auto before = serializeLevelMap(document).bytes;
		ok &= expect(!commitLevelBrushMerge(&document, plan, &error) && serializeLevelMap(document).bytes == before,
					 "unresolved commit refused", error);
		LevelBrushMergeRequest request;
		for (int f = 0; f < plan.geometry().faces.size(); ++f) {
			if (plan.geometry().faces[f].conflict) {
				request.faceSources.insert(f, plan.geometry().faces[f].sources.last());
			}
		}
		ok &= expect(prepareLevelBrushMerge(document, request, &plan, &error) && plan.ready(), "explicit face source resolves conflict",
					 error);
		auto stale = document;
		setLevelMapSelection(&stale, {});
		ok &= expect(!commitLevelBrushMerge(&stale, plan, &error), "changed selection rejected");
		stale = document;
		stale.revision++;
		ok &= expect(!commitLevelBrushMerge(&stale, plan, &error), "changed revision rejected");
		stale = document;
		stale.brushes[0].sourceLines[0] += QStringLiteral(" // changed");
		ok &= expect(!commitLevelBrushMerge(&stale, plan, &error), "source identity checked even with same revision");
		cliSource = document;
		request.faceSources.insert(123, {1, 2});
		ok &= expect(!prepareLevelBrushMerge(document, request, &plan, &error), "invalid output source rejected");
	}
	{
		auto document = map();
		box(&document, 0, 0, 0);
		box(&document, 64, 0, 0);
		selectAll(&document);
		LevelBrushMergePlan plan;
		document.brushes[1].entityId = 99;
		ok &= expect(!prepareLevelBrushMerge(document, {}, &plan, &error), "mixed ownership rejected");
		document.brushes[1].entityId = document.brushes[0].entityId;
		document.brushes[1].primitiveKind = QStringLiteral("brushDef3");
		ok &= expect(!prepareLevelBrushMerge(document, {}, &plan, &error), "mixed dialect rejected");
		document.brushes[1].primitiveKind = QStringLiteral("classic");
		setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 0}});
		ok &= expect(prepareLevelBrushMerge(document, {}, &plan, &error) && plan.ready(), "selected owner resolves owned brushes", error);
	}
	{
		// Independent voxel oracle: random unions of integer-grid boxes merge
		// exactly when every cell in their common bounding box is occupied.
		std::mt19937 random(0x726765);
		for (int trial = 0; trial < 40; ++trial) {
			auto document = map();
			std::set<std::array<int, 3>> cells;
			std::array<int, 3> low{4, 4, 4}, high{0, 0, 0};
			const int count = 2 + random() % 6;
			for (int b = 0; b < count; ++b) {
				std::array<int, 3> a{}, z{};
				for (int axis = 0; axis < 3; ++axis) {
					a[axis] = random() % 3;
					z[axis] = a[axis] + 1 + random() % (4 - a[axis]);
					low[axis] = std::min(low[axis], a[axis]);
					high[axis] = std::max(high[axis], z[axis]);
				}
				box(&document, a[0] * 16, a[1] * 16, a[2] * 16, (z[0] - a[0]) * 16, (z[1] - a[1]) * 16, (z[2] - a[2]) * 16);
				for (int x = a[0]; x < z[0]; ++x) {
					for (int y = a[1]; y < z[1]; ++y) {
						for (int v = a[2]; v < z[2]; ++v) {
							cells.insert({x, y, v});
						}
					}
				}
			}
			selectAll(&document);
			LevelBrushMergePlan plan;
			const bool complete = static_cast<int>(cells.size()) == (high[0] - low[0]) * (high[1] - low[1]) * (high[2] - low[2]);
			ok &= expect(prepareLevelBrushMerge(document, {}, &plan, &error) == complete, "voxel oracle for overlapping box unions", error);
		}
	}
	if (argc > 1) {
		const auto path = QDir(temporary.path()).filePath(QStringLiteral("cli-source.map"));
		const auto output = QDir(temporary.path()).filePath(QStringLiteral("cli-merged.map"));
		ok &= expect(put(path, serializeLevelMap(cliSource).bytes), "write CLI conflict source");
		const QStringList base{QStringLiteral("--cli"),			QStringLiteral("map"),
							   QStringLiteral("merge-brushes"), path,
							   QStringLiteral("--object"),		QStringLiteral("brush:0"),
							   QStringLiteral("--object"),		QStringLiteral("brush:1"),
							   QStringLiteral("--output"),		output,
							   QStringLiteral("--json")};
		const auto run = [&](const QStringList &args, QJsonObject *json) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), args);
			if (!process.waitForFinished(30000)) {
				process.kill();
				process.waitForFinished();
				return -1;
			}
			*json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
		};
		QJsonObject json;
		ok &= expect(run(base, &json) != 0 &&
						 json.value(QStringLiteral("merge")).toObject().value(QStringLiteral("unresolved")).toInt() > 0 &&
						 !QFile::exists(output),
					 "CLI conflict report without output");
		QStringList choices;
		for (const auto &item : json.value(QStringLiteral("merge")).toObject().value(QStringLiteral("faces")).toArray()) {
			const auto face = item.toObject();
			if (!face.value(QStringLiteral("conflict")).toBool()) {
				continue;
			}
			const auto source = face.value(QStringLiteral("sources")).toArray().last().toObject();
			choices << QStringLiteral("--face-source")
					<< QStringLiteral("%1=%2:%3")
						   .arg(face.value(QStringLiteral("face")).toInt())
						   .arg(source.value(QStringLiteral("brush")).toInt())
						   .arg(source.value(QStringLiteral("face")).toInt());
		}
		ok &= expect(run(base + choices + QStringList{QStringLiteral("--dry-run")}, &json) == 0 && !QFile::exists(output),
					 "CLI resolved dry run");
		ok &= expect(run(base + choices, &json) == 0 && QFile::exists(output), "CLI resolved save");
		ok &= expect(run(base + choices, &json) != 0, "CLI protects existing output");
		ok &= expect(run(base + choices + QStringList{QStringLiteral("--face-source"), QStringLiteral("999=0:0")}, &json) != 0,
					 "CLI invalid choice refused");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
