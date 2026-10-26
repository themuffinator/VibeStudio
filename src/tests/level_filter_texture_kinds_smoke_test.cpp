// The Levels view filters' texture kinds (core/level_view_filters.h) on
// Quake, Quake II and Quake III fixtures:
// - Area Portals (any areaportal face or the 0x8000 content bit, and Quake
//   II's func_areaportal), Cluster Portals, Light Grid and Bot Clip (every
//   face, by name or Quake III content bit), Decals (a face under decals/)
//   and Structural (the inverse of Detail; worldspawn with every brush entity
//   but func_detail on Quake maps), with the formats that offer each;
// - Bot Clip split out of Clip on Quake III maps only, Quake's skip counted as
//   caulk rather than hint, and Quake II's current bits never read as
//   Quake III cluster portals or bot clip;
// - Caulk's faces on otherwise textured brushes, left out of the camera mesh
//   while brushes a switched-off kind hides whole keep none;
// - the command ids, palette entries and NetRadiant profile keys;
// - `map filters` reporting the faces as face:brushId:faceNumber selectors.
// With the vibestudio binary as the first argument the CLI is checked too.

#include "core/editor_profiles.h"
#include "core/level_document.h"
#include "core/level_map.h"
#include "core/level_view_filters.h"
#include "core/map_preview_mesh.h"
#include "core/studio_semantics.h"
#include "tests/level_view_filter_test_helpers.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}

QStringList selectors(const QVector<LevelMapSelectionRef>& refs)
{
	QStringList ids;
	for (const LevelMapSelectionRef& ref : refs) {
		ids << levelMapSelectionRefId(ref);
	}
	return ids;
}

QStringList faceIds(const QVector<LevelSurfaceFace>& faces)
{
	QStringList ids;
	for (const LevelSurfaceFace& face : faces) {
		ids << QStringLiteral("%1/%2").arg(face.brushId).arg(face.faceIndex);
	}
	return ids;
}

QStringList offeredIds(const LevelMapDocument& document)
{
	QStringList ids;
	for (const LevelViewFilter& filter : levelViewFilters(document)) {
		ids << filter.id;
	}
	return ids;
}

// A Quake II map (VibeStudio's Quake II target header): worldspawn brushes
// 0 with the area portal bit, 1 with CURRENT_180 (Quake III's cluster portal
// bit), 2 with CURRENT_UP (Quake III's bot clip bit), 3 detail, 4 plain,
// 5 hint with skip faces; a func_areaportal with brush 6.
QByteArray quake2Fixture()
{
	using tests::viewFilterBox;
	QString text = QString::fromLatin1(kQuake2MapTargetHeader) + QStringLiteral("{\n\"classname\" \"worldspawn\"\n");
	const QString floor = QStringLiteral("e1u1/floor1_3");
	text += viewFilterBox(0, 0, 0, 64, 64, 64, {floor}, 0x8000);
	text += viewFilterBox(64, 0, 0, 128, 64, 64, {floor}, 0x100000);
	text += viewFilterBox(128, 0, 0, 192, 64, 64, {floor}, 0x400000);
	text += viewFilterBox(192, 0, 0, 256, 64, 64, {floor}, 0x8000000);
	text += viewFilterBox(256, 0, 0, 320, 64, 64, {floor});
	text += viewFilterBox(320, 0, 0, 384, 64, 64, {QStringLiteral("e1u1/skip"), QStringLiteral("e1u1/hint"), QStringLiteral("e1u1/skip")});
	text += QStringLiteral("}\n{\n\"classname\" \"func_areaportal\"\n\"style\" \"1\"\n");
	text += viewFilterBox(0, 128, 0, 8, 192, 64, {QStringLiteral("e1u1/trigger")});
	text += QStringLiteral("}\n");
	return text.toUtf8();
}

// A Quake brush in the original face syntax, textures as viewFilterBox.
QString quakeBox(int x0, int y0, int z0, int x1, int y1, int z1, const QStringList& textures)
{
	const auto point = [](int x, int y, int z) { return QStringLiteral("( %1 %2 %3 )").arg(x).arg(y).arg(z); };
	const auto face = [&](const QString& a, const QString& b, const QString& c, int index) {
		return QStringLiteral("%1 %2 %3 %4 0 0 0 1 1\n").arg(a, b, c, textures.value(index, textures.value(0)));
	};
	return QStringLiteral("{\n") + face(point(x0, y0, z0), point(x0, y0 + 1, z0), point(x0 + 1, y0, z0), 0)
		+ face(point(x0, y0, z1), point(x0 + 1, y0, z1), point(x0, y0 + 1, z1), 1)
		+ face(point(x0, y0, z0), point(x0 + 1, y0, z0), point(x0, y0, z0 + 1), 2)
		+ face(point(x0, y1, z0), point(x0, y1, z0 + 1), point(x0 + 1, y1, z0), 3)
		+ face(point(x0, y0, z0), point(x0, y0, z0 + 1), point(x0, y0 + 1, z0), 4)
		+ face(point(x1, y0, z0), point(x1, y0 + 1, z0), point(x1, y0, z0 + 1), 5) + QStringLiteral("}\n");
}

// A Quake map (worldspawn names a wad): brushes 0 skip all over, 1 skip but
// its top, 2 hint with hintskip, 3 plain; func_detail with brush 4, func_door
// with brush 5, func_group with brush 6, and a light.
QByteArray quakeFixture()
{
	const QString ground = QStringLiteral("ground1_6");
	QString text = QStringLiteral("{\n\"classname\" \"worldspawn\"\n\"wad\" \"gfx/base.wad\"\n");
	text += quakeBox(0, 0, 0, 64, 64, 64, {QStringLiteral("skip")});
	text += quakeBox(64, 0, 0, 128, 64, 64, {QStringLiteral("skip"), ground, QStringLiteral("skip")});
	text += quakeBox(128, 0, 0, 192, 64, 64, {QStringLiteral("hintskip"), QStringLiteral("hint"), QStringLiteral("hintskip")});
	text += quakeBox(192, 0, 0, 256, 64, 64, {ground});
	text += QStringLiteral("}\n{\n\"classname\" \"func_detail\"\n");
	text += quakeBox(0, 128, 0, 64, 192, 64, {ground});
	text += QStringLiteral("}\n{\n\"classname\" \"func_door\"\n");
	text += quakeBox(64, 128, 0, 128, 192, 64, {ground});
	text += QStringLiteral("}\n{\n\"classname\" \"func_group\"\n");
	text += quakeBox(128, 128, 0, 192, 192, 64, {ground});
	text += QStringLiteral("}\n{\n\"classname\" \"light\"\n\"origin\" \"32 32 96\"\n}\n");
	return text.toUtf8();
}

bool load(const QByteArray& bytes, const QString& engine, LevelMapDocument* document)
{
	QString error;
	return expect(loadLevelMapBytes({QStringLiteral("kinds.map"), {}, engine}, bytes, document, &error), "load a fixture", error);
}

QStringList brushes(std::initializer_list<int> ids)
{
	QStringList list;
	for (const int id : ids) {
		list << QStringLiteral("brush:%1").arg(id);
	}
	return list;
}

QStringList allKinds()
{
	QStringList ids;
	for (const LevelViewFilter& kind : levelViewFilterKinds()) {
		ids << kind.id;
	}
	return ids;
}

const QStringList quake3Only {QStringLiteral("patches"), QStringLiteral("botclip"), QStringLiteral("clusterportals"), QStringLiteral("lightgrid"),
	QStringLiteral("fog"), QStringLiteral("decals")};

void quake3()
{
	LevelMapDocument document;
	if (!load(tests::viewFilterTextureKindsFixture(), QStringLiteral("idTech3"), &document)) {
		return;
	}
	expect(document.format == LevelMapFormat::Quake3Map && document.brushes.size() == 19 && document.entities.size() == 3, "Quake III fixture shape",
		QStringLiteral("%1 brushes").arg(document.brushes.size()));
	const QByteArray before = serializeLevelMap(document).bytes;
	expect(offeredIds(document) == allKinds(), "a Quake III map offers every kind", offeredIds(document).join(QLatin1Char(',')));
	const QHash<QString, QStringList> expected {
		{QStringLiteral("areaportals"), brushes({0, 1})},
		{QStringLiteral("clusterportals"), brushes({2, 3})},
		{QStringLiteral("lightgrid"), brushes({4})},
		{QStringLiteral("botclip"), brushes({6, 7, 9})},
		{QStringLiteral("clip"), brushes({8})},
		{QStringLiteral("decals"), brushes({10, 11})},
		{QStringLiteral("detail"), brushes({13, 17})},
		{QStringLiteral("structural"), brushes({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 15, 16, 18})},
		{QStringLiteral("caulk"), brushes({15})},
		{QStringLiteral("hint"), brushes({16})},
	};
	const QHash<QString, int> counts = levelViewFilterCounts(document);
	for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
		const QStringList matched = selectors(levelViewFilterMatches(document, it.key()));
		expect(matched == it.value(), "Quake III kind matches", it.key() + QLatin1String(": ") + matched.join(QLatin1Char(',')));
		expect(counts.value(it.key()) == it.value().size(), "Quake III count", it.key());
	}
	// Structural is Detail's inverse among brushes: together they name each
	// brush once.
	QSet<QString> structural;
	for (const QString& id : expected.value(QStringLiteral("structural"))) {
		structural.insert(id);
	}
	for (const QString& id : expected.value(QStringLiteral("detail"))) {
		expect(!structural.contains(id), "structural and detail are disjoint", id);
	}
	expect(structural.size() + expected.value(QStringLiteral("detail")).size() == document.brushes.size(), "structural and detail cover every brush");

	// Caulk hides the caulk faces of mixed brushes one at a time: brush 14
	// (every face but its top) and the door's brush 18 (every face but +Y);
	// brush 15 goes whole.
	const QStringList caulkFaces {QStringLiteral("14/0"), QStringLiteral("14/2"), QStringLiteral("14/3"), QStringLiteral("14/4"), QStringLiteral("14/5"),
		QStringLiteral("18/0"), QStringLiteral("18/1"), QStringLiteral("18/2"), QStringLiteral("18/4"), QStringLiteral("18/5")};
	expect(faceIds(levelViewFilterFaceMatches(document, QStringLiteral("caulk"))) == caulkFaces, "Caulk's faces on mixed brushes",
		faceIds(levelViewFilterFaceMatches(document, QStringLiteral("caulk"))).join(QLatin1Char(',')));
	// Skip hides the hint brush's skip sides one by one (level-filter-tags-smoke
	// covers it); no other kind does.
	const QStringList skipFaces {QStringLiteral("16/0"), QStringLiteral("16/2"), QStringLiteral("16/3"), QStringLiteral("16/4"), QStringLiteral("16/5")};
	expect(faceIds(levelViewFilterFaceMatches(document, QStringLiteral("skip"))) == skipFaces, "Skip's faces on the hint brush",
		faceIds(levelViewFilterFaceMatches(document, QStringLiteral("skip"))).join(QLatin1Char(',')));
	for (const QString& id : allKinds()) {
		if (id != QLatin1String("caulk") && id != QLatin1String("skip")) {
			expect(levelViewFilterFaceMatches(document, id).isEmpty(), "only Caulk and Skip hide single faces", id);
		}
	}
	expect(levelViewFilteredFaces(document, {}).isEmpty() && levelViewFilteredFaces(document, {QStringLiteral("clip")}).isEmpty(),
		"no faces hide while Caulk is shown");
	expect(faceIds(levelViewFilteredFaces(document, {QStringLiteral("caulk")})) == caulkFaces, "Caulk off hides its faces");
	expect(faceIds(levelViewFilteredFaces(document, {QStringLiteral("caulk"), QStringLiteral("brush-entities")})) == caulkFaces.mid(0, 5),
		"a brush hidden with its entity keeps no faces to hide");
	expect(levelViewFilteredFaces(document, {QStringLiteral("brushes"), QStringLiteral("caulk")}).isEmpty(), "brushes hidden whole keep no faces");

	// The camera mesh leaves the hidden faces out and keeps every other face.
	LevelMapPreviewMeshOptions options;
	const LevelMapPreviewMesh full = buildLevelMapPreviewMesh(document, options);
	for (const LevelSurfaceFace& face : levelViewFilteredFaces(document, {QStringLiteral("caulk")})) {
		options.hiddenFaces.insert({face.brushId, face.faceIndex});
	}
	const LevelMapPreviewMesh trimmed = buildLevelMapPreviewMesh(document, options);
	bool hiddenGone = trimmed.owners.size() == trimmed.ownerFaces.size();
	bool topKept = false;
	for (int triangle = 0; triangle < trimmed.owners.size() && triangle < trimmed.ownerFaces.size(); ++triangle) {
		const LevelMapSelectionRef owner = trimmed.owners.at(triangle);
		const int face = trimmed.ownerFaces.at(triangle);
		hiddenGone = hiddenGone && !(owner.kind == LevelMapSelectionKind::QuakeBrush && options.hiddenFaces.contains({owner.objectId, face}));
		topKept = topKept || (owner.kind == LevelMapSelectionKind::QuakeBrush && owner.objectId == 14 && face == 1);
	}
	expect(full.brushFaces - trimmed.brushFaces == caulkFaces.size() && hiddenGone && topKept && trimmed.triangles == full.triangles - 2 * caulkFaces.size(),
		"the camera mesh drops exactly the hidden faces",
		QStringLiteral("%1 -> %2 faces, %3 -> %4 triangles").arg(full.brushFaces).arg(trimmed.brushFaces).arg(full.triangles).arg(trimmed.triangles));
	expect(serializeLevelMap(document).bytes == before && document.undoStack.isEmpty(), "classifying never changes the map");
}

void quake2()
{
	LevelMapDocument document;
	if (!load(quake2Fixture(), QStringLiteral("idTech2"), &document)) {
		return;
	}
	expect(document.format == LevelMapFormat::QuakeMap && document.brushes.size() == 7, "Quake II fixture shape");
	QStringList withoutQuake3 = allKinds();
	for (const QString& id : quake3Only) {
		withoutQuake3.removeAll(id);
	}
	expect(offeredIds(document) == withoutQuake3, "a Quake II map offers every kind but the Quake III ones", offeredIds(document).join(QLatin1Char(',')));
	const QHash<QString, QStringList> expected {
		{QStringLiteral("areaportals"), QStringList {QStringLiteral("brush:0"), QStringLiteral("entity:1")}},
		{QStringLiteral("detail"), brushes({3})},
		{QStringLiteral("structural"), brushes({0, 1, 2, 4, 5, 6})},
		{QStringLiteral("hint"), brushes({5})},
		{QStringLiteral("caulk"), {}},
		{QStringLiteral("clusterportals"), {}},
		{QStringLiteral("botclip"), {}},
		{QStringLiteral("lightgrid"), {}},
		{QStringLiteral("decals"), {}},
	};
	for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
		const QStringList matched = selectors(levelViewFilterMatches(document, it.key()));
		expect(matched == it.value(), "Quake II kind matches", it.key() + QLatin1String(": ") + matched.join(QLatin1Char(',')));
	}
	// Quake II's skip guides hint brushes, so it is never caulk there.
	expect(levelViewFilterFaceMatches(document, QStringLiteral("caulk")).isEmpty(), "Quake II skip is not caulk");
}

void quake()
{
	LevelMapDocument document;
	if (!load(quakeFixture(), QStringLiteral("idTech2"), &document)) {
		return;
	}
	expect(document.format == LevelMapFormat::QuakeMap && document.brushes.size() == 7 && document.entities.size() == 5, "Quake fixture shape");
	QStringList withoutQuake3 = allKinds();
	for (const QString& id : quake3Only) {
		withoutQuake3.removeAll(id);
	}
	expect(offeredIds(document) == withoutQuake3, "a Quake map offers every kind but the Quake III ones");
	const QHash<QString, QStringList> expected {
		// Quake's skip is caulk: brush 0 all over, brush 1 face by face.
		{QStringLiteral("caulk"), brushes({0})},
		{QStringLiteral("hint"), brushes({2})},
		{QStringLiteral("detail"), brushes({4})},
		// Worldspawn's brushes, then every brush entity but func_detail.
		{QStringLiteral("structural"), QStringList {QStringLiteral("brush:0"), QStringLiteral("brush:1"), QStringLiteral("brush:2"), QStringLiteral("brush:3"),
										   QStringLiteral("entity:2"), QStringLiteral("entity:3")}},
		{QStringLiteral("areaportals"), {}},
	};
	for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
		const QStringList matched = selectors(levelViewFilterMatches(document, it.key()));
		expect(matched == it.value(), "Quake kind matches", it.key() + QLatin1String(": ") + matched.join(QLatin1Char(',')));
	}
	const QStringList skipFaces {QStringLiteral("1/0"), QStringLiteral("1/2"), QStringLiteral("1/3"), QStringLiteral("1/4"), QStringLiteral("1/5")};
	expect(faceIds(levelViewFilterFaceMatches(document, QStringLiteral("caulk"))) == skipFaces, "Quake's skip faces count as caulk faces",
		faceIds(levelViewFilterFaceMatches(document, QStringLiteral("caulk"))).join(QLatin1Char(',')));
	expect(levelViewFilteredFaces(document, {QStringLiteral("caulk"), QStringLiteral("world")}).isEmpty(), "World Brushes off leaves no skip faces to hide");
}

void commands()
{
	const QHash<QString, QString> ids {{QStringLiteral("areaportals"), QStringLiteral("map.filterAreaportals")},
		{QStringLiteral("clusterportals"), QStringLiteral("map.filterClusterportals")}, {QStringLiteral("structural"), QStringLiteral("map.filterStructural")},
		{QStringLiteral("lightgrid"), QStringLiteral("map.filterLightgrid")}, {QStringLiteral("botclip"), QStringLiteral("map.filterBotclip")},
		{QStringLiteral("decals"), QStringLiteral("map.filterDecals")}};
	for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
		expect(levelViewFilterCommandId(it.key()) == it.value(), "command id", it.key());
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(it.value(), &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry", it.value());
	}
	// NetRadiant Custom's keys (radiant/filters.cpp ConstructFilters), which
	// Xonotic NetRadiant shares; Lightgrid has none upstream.
	const QHash<QString, QString> keys {{QStringLiteral("map.filterAreaportals"), QStringLiteral("Alt+3")},
		{QStringLiteral("map.filterClusterportals"), QStringLiteral("Alt+9")}, {QStringLiteral("map.filterStructural"), QStringLiteral("Ctrl+Shift+D")},
		{QStringLiteral("map.filterBotclip"), QStringLiteral("Alt+M")}, {QStringLiteral("map.filterDecals"), QStringLiteral("Shift+D")},
		{QStringLiteral("map.filterCaulk"), QStringLiteral("Alt+6")}};
	for (const QString& profile : {QStringLiteral("netradiant-custom"), QStringLiteral("netradiant"), QStringLiteral("trenchbroom")}) {
		QHash<QString, QStringList> bound;
		for (const LevelEditorKeyBinding& binding : levelEditorControlsForProfile(profile).keys) {
			bound.insert(binding.commandId, binding.keys);
		}
		const bool radiant = profile != QLatin1String("trenchbroom");
		for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
			expect(radiant ? bound.value(it.key()) == QStringList {it.value()} : bound.value(it.key()).isEmpty(), "profile key", profile + QLatin1Char(' ') + it.key());
		}
		expect(bound.value(QStringLiteral("map.filterLightgrid")).isEmpty(), "Light Grid has no key", profile);
	}
	// The NetRadiant Custom profile's caulk entry is the real command now.
	EditorProfileDescriptor nrc;
	EditorProfileBinding binding;
	expect(editorProfileForId(QStringLiteral("netradiant-custom"), &nrc)
			&& editorProfileBindingForAction(nrc, QStringLiteral("map.filterCaulk"), &binding) && binding.implemented && binding.shortcut == QLatin1String("Alt+6")
			&& !editorProfileBindingForAction(nrc, QStringLiteral("editor.filters.toggle-caulk")),
		"the NetRadiant Custom profile binds Toggle Caulk Filter to the command");
}

void cli(const QString& executable)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "CLI temporary folder")) {
		return;
	}
	const QString path = temp.filePath(QStringLiteral("kinds.map"));
	const QByteArray source = tests::viewFilterTextureKindsFixture();
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "CLI fixture")) {
		return;
	}
	file.close();
	const auto run = [&](const QStringList& args) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(executable, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"),
									  temp.filePath(QStringLiteral("settings.ini")), QStringLiteral("map"), QStringLiteral("filters"), path}
				+ args);
		const bool done = process.waitForStarted() && process.waitForFinished(30000);
		const QByteArray out = process.readAllStandardOutput();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, "CLI exit status",
			args.join(QLatin1Char(' ')) + QLatin1String(" -> ") + QString::fromUtf8(out + process.readAllStandardError()));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return QJsonDocument::fromJson(out).object();
	};
	const auto entry = [](const QJsonObject& report, const QString& id) {
		for (const QJsonValue& value : report.value(QStringLiteral("filters")).toArray()) {
			if (value.toObject().value(QStringLiteral("id")).toString() == id) {
				return value.toObject();
			}
		}
		return QJsonObject();
	};
	QJsonObject report = run({QStringLiteral("--off"), QStringLiteral("caulk"), QStringLiteral("--objects")});
	const QJsonArray faces {QStringLiteral("face:14:1"), QStringLiteral("face:14:3"), QStringLiteral("face:14:4"), QStringLiteral("face:14:5"),
		QStringLiteral("face:14:6"), QStringLiteral("face:18:1"), QStringLiteral("face:18:2"), QStringLiteral("face:18:3"), QStringLiteral("face:18:5"),
		QStringLiteral("face:18:6")};
	const QJsonObject caulkEntry = entry(report, QStringLiteral("caulk"));
	expect(report.value(QStringLiteral("filteredFaces")).toArray() == faces && report.value(QStringLiteral("filteredFaceCount")).toInt() == faces.size()
			&& caulkEntry.value(QStringLiteral("faceCount")).toInt() == faces.size() && caulkEntry.value(QStringLiteral("faces")).toArray() == faces
			&& report.value(QStringLiteral("filteredObjects")).toArray() == QJsonArray {QStringLiteral("brush:15")},
		"CLI reports Caulk's faces as one-based face selectors", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	const QJsonObject decals = entry(report, QStringLiteral("decals"));
	expect(decals.value(QStringLiteral("count")).toInt() == 2 && decals.value(QStringLiteral("command")) == QLatin1String("map.filterDecals")
			&& decals.value(QStringLiteral("faceCount")).toInt() == 0 && decals.value(QStringLiteral("shown")) == true,
		"CLI lists the new kinds with their commands");
	report = run({QStringLiteral("--off"), QStringLiteral("caulk,brushes")});
	expect(report.value(QStringLiteral("filteredFaces")).toArray().isEmpty() && report.value(QStringLiteral("filteredFaceCount")).toInt() == 0,
		"CLI hides no faces of brushes hidden whole");
	report = run({QStringLiteral("--off"), QStringLiteral("botclip,structural")});
	expect(report.value(QStringLiteral("filteredCount")).toInt() == 17 && report.value(QStringLiteral("off")).toArray().size() == 2,
		"CLI combines Bot Clip and Structural", QString::number(report.value(QStringLiteral("filteredCount")).toInt()));
	expect(file.open(QIODevice::ReadOnly) && file.readAll() == source, "the CLI never changes the map");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	quake3();
	quake2();
	quake();
	commands();
	if (argc == 2) {
		cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath());
	}
	if (ok) {
		std::cout << "Filter texture kinds: Quake, Quake II and Quake III classification, caulk faces, keys" << (argc == 2 ? " and CLI" : "") << " passed."
				  << std::endl;
	}
	return ok ? 0 : 1;
}
