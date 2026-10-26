// Core and command-line checks of per-face content and surface flags and
// value (core/level_face_flags.h), NetRadiant Custom's Surface Inspector flag
// boxes and Unset button: the game is Quake II for Quake-format maps written
// with flags and Quake III for Quake III maps, with each game's flag names,
// ids and parsing; the summary marks bits set on every face and on some;
// an edit sets or clears single bits and keeps every other bit, the value and
// the detail flag, rewrites only the three numbers (adding them to a line
// with none, also on a Quake II map whose faces write none), writes a changed
// word signed so bit 31 reads -2147483648 in every compiler, and is one exact
// undo step; it refuses the detail flag, a value
// outside 32 bits, a no-op, a Quake map, hidden faces, faces outside the open
// group and a locked layer, changing nothing. map face-flags reads the flags
// by name and writes what the shared service writes.

#include "core/level_document.h"
#include "core/level_face_flags.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
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
	return expect(loadLevelMapBytes({QStringLiteral("flags.map"), {}, engine}, bytes, document, &error), "load fixture", error);
}

QVector<LevelSurfaceFace> brushFaces(const LevelMapDocument& document, int brushIndex = 0)
{
	QVector<LevelSurfaceFace> faces;
	const LevelMapBrush& brush = document.brushes[brushIndex];
	for (int i = 0; i < brush.faces.size(); ++i) {
		faces.append({brush.id, i});
	}
	return faces;
}

// The Quake II fixture: classic faces written with "2 4 8" (window, sky, 8).
QByteArray quake2Fixture() { return tests::surfaceFixture(QStringLiteral("classic")); }
// Quake III brushDef faces written with "2 4 8".
QByteArray quake3Fixture() { return tests::surfaceFixture(QStringLiteral("brushDef")); }
// Quake III classic faces with no flag numbers on their lines.
QByteArray quake3NoFlags()
{
	QByteArray bytes = tests::surfaceFixture(QStringLiteral("classic"));
	bytes.replace(" 2 4 8 // flags", " // flags");
	return bytes;
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

	phase = QStringLiteral("games and names");
	{
		LevelMapDocument quake2, quake3, bare;
		load(quake2Fixture(), QStringLiteral("idtech2"), &quake2);
		load(quake3Fixture(), QStringLiteral("idtech3"), &quake3);
		LevelMapCreateRequest create;
		create.game = QStringLiteral("quake");
		create.starterRoom = false;
		expect(createLevelMap(create, &bare, &error) && addLevelMapBoxBrush(&bare, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("wall"), nullptr, &error),
			"a Quake map", error);
		expect(levelFaceFlagGame(quake2) == LevelFaceFlagGame::Quake2, "classic faces with flags are Quake II");
		expect(levelFaceFlagGame(quake3) == LevelFaceFlagGame::Quake3, "Quake III map");
		expect(levelFaceFlagGame(bare) == LevelFaceFlagGame::None, "Quake faces carry no flags");
		expect(levelFaceFlagNames(LevelFaceFlagGame::None, LevelFaceFlagWord::Content).isEmpty(), "no names without flags");
		const auto q2Surface = levelFaceFlagNames(LevelFaceFlagGame::Quake2, LevelFaceFlagWord::Surface);
		const auto q3Content = levelFaceFlagNames(LevelFaceFlagGame::Quake3, LevelFaceFlagWord::Content);
		const auto q3Surface = levelFaceFlagNames(LevelFaceFlagGame::Quake3, LevelFaceFlagWord::Surface);
		expect(!q2Surface.isEmpty() && q2Surface.first().bit == 0x1 && q2Surface.first().id == QStringLiteral("light"), "Quake II surface bit 1 is light");
		bool playerclip = false, detail = false, labelled = true;
		for (const auto& flag : q3Content) {
			playerclip |= flag.id == QStringLiteral("playerclip") && flag.bit == 0x10000;
			detail |= flag.id == QStringLiteral("detail") && flag.bit == kLevelFaceDetailContent;
			labelled &= !flag.displayName.isEmpty() && !flag.description.isEmpty();
		}
		expect(playerclip && detail && labelled && q3Content.size() == 25, "Quake III contents are named and described");
		expect(q3Surface.size() == 19 && q3Surface.at(3).id == QStringLiteral("ladder") && q3Surface.at(3).bit == 0x8, "Quake III surface flags");
		expect(levelFaceFlagIds(LevelFaceFlagGame::Quake3, LevelFaceFlagWord::Content, 0x10020 | 0x4) == QStringList({QStringLiteral("water"),
			QStringLiteral("playerclip"), QStringLiteral("0x4")}), "ids by name, unnamed bits in hex");
		quint32 bits = 0;
		expect(parseLevelFaceFlagBits(LevelFaceFlagGame::Quake3, LevelFaceFlagWord::Content, QStringLiteral(" playerclip , WATER,0x4,16"), &bits, &error)
				&& bits == (0x10000u | 0x20u | 0x4u | 0x10u),
			"names, hex and decimal parse together", error);
		expect(!parseLevelFaceFlagBits(LevelFaceFlagGame::Quake2, LevelFaceFlagWord::Surface, QStringLiteral("light,bogus"), &bits, &error)
				&& error.contains(QStringLiteral("bogus")),
			"an unknown name is refused, with the known ones", error);
		expect(!parseLevelFaceFlagBits(LevelFaceFlagGame::Quake2, LevelFaceFlagWord::Surface, QStringLiteral("0x100000000"), &bits, &error),
			"more than 32 bits is refused");
		expect(parseLevelFaceFlagBits(LevelFaceFlagGame::Quake2, LevelFaceFlagWord::Content, QStringLiteral("playerclip"), &bits) && bits == 0x10000,
			"Quake II player clip");

		// A Quake map refuses face flags.
		const auto bareBytes = serializeLevelMap(bare).bytes;
		LevelFaceFlagsEdit edit;
		edit.setSurface = 0x1;
		expect(!setLevelMapFaceFlags(&bare, brushFaces(bare), edit, nullptr, &error) && error.contains(QStringLiteral("Quake II"))
				&& serializeLevelMap(bare).bytes == bareBytes,
			"a Quake map refuses face flags", error);
	}

	phase = QStringLiteral("summary and masked edits");
	for (const bool quake3 : {false, true}) {
		const QByteArray source = quake3 ? quake3Fixture() : quake2Fixture();
		const QString engine = quake3 ? QStringLiteral("idtech3") : QStringLiteral("idtech2");
		LevelMapDocument document;
		load(source, engine, &document);
		const auto faces = brushFaces(document);
		auto summary = levelFaceFlagsSummary(document, faces);
		expect(summary.faces == 6 && summary.contentAll == 2 && summary.contentAny == 2 && summary.surfaceAll == 4 && summary.surfaceAny == 4 && summary.value == 8
				&& !summary.valueMixed,
			"every face shares 2 4 8");
		const QByteArray start = serializeLevelMap(document).bytes;
		const auto depth = document.undoStack.size();
		// One face gains player clip and a value; the rest keep theirs.
		LevelFaceFlagsEdit edit;
		edit.setContent = 0x10000;
		edit.setValue = true;
		edit.value = -3;
		LevelFaceFlagsReport report;
		expect(setLevelMapFaceFlags(&document, {faces.first()}, edit, &report, &error) && report.changedFaces == 1 && report.brushes == 1
				&& document.undoStack.size() == depth + 1,
			"one face changes as one undo step", error);
		LevelMapDocument reopened;
		load(serializeLevelMap(document).bytes, engine, &reopened);
		const auto& changed = reopened.brushes.first().faces;
		expect(changed[0].contentFlags == 0x10002 && changed[0].surfaceFlags == 4 && changed[0].surfaceValue == -3 && changed[1].contentFlags == 2
				&& changed[1].surfaceValue == 8 && changed[0].textureName == changed[1].textureName,
			"the face keeps its other bits and texture; the others are untouched");
		summary = levelFaceFlagsSummary(document, faces);
		expect(summary.contentAll == 2 && summary.contentAny == 0x10002 && summary.valueMixed, "the summary marks partial bits and a mixed value");
		const QString line = QString::fromUtf8(serializeLevelMap(document).bytes);
		expect(line.contains(QStringLiteral("65538 4 -3 // flags")) && line.count(QStringLiteral("2 4 8 // flags")) == 5,
			"only the three numbers of the changed line are rewritten", line);
		expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == start && document.undoStack.size() == depth,
			"undo restores the exact map", error);
		expect(redoLevelMapEdit(&document, &error) && levelFaceFlagsSummary(document, faces).contentAny == 0x10002, "redo", error);
		expect(undoLevelMapEdit(&document, &error), "undo again", error);

		// Clearing a bit on every face; a bit no face has stays clear.
		edit = {};
		edit.clearSurface = 0x4 | 0x1;
		expect(setLevelMapFaceFlags(&document, faces, edit, &report, &error) && report.changedFaces == 6, "clear sky everywhere", error);
		summary = levelFaceFlagsSummary(document, faces);
		expect(summary.surfaceAny == 0 && summary.contentAll == 2 && summary.value == 8, "only the cleared bit changes");
		// A no-op is refused and adds no undo step.
		const auto noOpDepth = document.undoStack.size();
		expect(!setLevelMapFaceFlags(&document, faces, edit, nullptr, &error) && document.undoStack.size() == noOpDepth && error.contains(QStringLiteral("already")),
			"a no-op is refused", error);

		// Unset keeps the detail flag, as NRC's Surface Inspector does.
		setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, document.brushes.first().id}});
		expect(setLevelMapSelectionDetail(&document, true, nullptr, &error), "make the brush detail", error);
		expect(setLevelMapFaceFlags(&document, faces, LevelFaceFlagsEdit::clearAll(), &report, &error), "clear all", error);
		summary = levelFaceFlagsSummary(document, faces);
		expect(summary.contentAll == kLevelFaceDetailContent && summary.contentAny == kLevelFaceDetailContent && summary.surfaceAny == 0 && summary.value == 0
				&& !summary.valueMixed,
			"Clear Face Flags keeps only detail");

		// Refusals change nothing.
		const QByteArray kept = serializeLevelMap(document).bytes;
		const auto keptDepth = document.undoStack.size();
		const auto refused = [&](const LevelFaceFlagsEdit& bad, const QVector<LevelSurfaceFace>& targets, const char* label, const char* reason) {
			QString problem;
			expect(!setLevelMapFaceFlags(&document, targets, bad, nullptr, &problem) && problem.contains(QString::fromLatin1(reason))
					&& serializeLevelMap(document).bytes == kept && document.undoStack.size() == keptDepth,
				label, problem);
		};
		LevelFaceFlagsEdit bad;
		bad.setContent = kLevelFaceDetailContent;
		refused(bad, faces, "the detail flag belongs to Make Detail", "Make Detail");
		bad = {};
		bad.setValue = true;
		bad.value = 2147483648LL;
		refused(bad, faces, "a value outside 32 bits", "32-bit");
		bad = {};
		bad.setSurface = bad.clearSurface = 0x2;
		refused(bad, faces, "set and clear at once", "at once");
		bad = {};
		refused(bad, faces, "an empty edit", "Choose");
		bad.setSurface = 0x2;
		refused(bad, {{document.brushes.first().id, 99}}, "a face that does not exist", "no face");
		refused(bad, {}, "no faces", "Select");
	}

	phase = QStringLiteral("a line without flags gains them");
	{
		LevelMapDocument document;
		load(quake3NoFlags(), QStringLiteral("idtech3"), &document);
		expect(levelFaceFlagGame(document) == LevelFaceFlagGame::Quake3 && !document.brushes.first().faces.first().flagsWritten, "no flags written");
		LevelFaceFlagsEdit edit;
		edit.setSurface = 0x80;
		expect(setLevelMapFaceFlags(&document, {{document.brushes.first().id, 0}}, edit, nullptr, &error), "nodraw on one face", error);
		const QString text = QString::fromUtf8(serializeLevelMap(document).bytes);
		expect(text.contains(QStringLiteral("-0.5 2 0 128 0 // flags")) && text.count(QStringLiteral("-0.5 2 // flags")) == 5,
			"the face's line gains its three numbers before the comment", text);
		LevelMapDocument reopened;
		load(text.toUtf8(), QStringLiteral("idtech3"), &reopened);
		expect(reopened.brushes.first().faces.first().flagsWritten && reopened.brushes.first().faces.first().surfaceFlags == 0x80
				&& !reopened.brushes.first().faces.at(1).flagsWritten,
			"reopened with the flags on that face alone");
	}

	phase = QStringLiteral("a Quake II map without flag numbers");
	{
		// The three numbers are optional on a Quake II line; its game header
		// supplies the evidence, and an edit adds numbers to the changed line
		// alone. Texture folders also occur in Quake and cannot choose edits.
		QByteArray bytes = quake2Fixture();
		bytes.replace(" 2 4 8 // flags", " // flags");
		LevelMapDocument ambiguous;
		load(bytes, QStringLiteral("idtech2"), &ambiguous);
		expect(levelFaceFlagGame(ambiguous) == LevelFaceFlagGame::None, "texture folders alone do not enable flags");
		LevelFaceFlagsEdit edit;
		edit.setSurface = 0x1;
		const QByteArray unchanged = serializeLevelMap(ambiguous).bytes;
		expect(!setLevelMapFaceFlags(&ambiguous, {{ambiguous.brushes.first().id, 0}}, edit, nullptr, &error)
			&& ambiguous.undoStack.isEmpty() && serializeLevelMap(ambiguous).bytes == unchanged,
			"ambiguous classic maps refuse flag edits without changing bytes or history");
		LevelMapDocument document;
		load(QByteArray("// Game: Quake 2\n") + bytes, QStringLiteral("idtech2"), &document);
		expect(document.format == LevelMapFormat::QuakeMap && !levelMapUsesFaceFlags(document) && levelFaceFlagGame(document) == LevelFaceFlagGame::Quake2,
			"a Quake II map whose faces write no flags is Quake II");
		expect(setLevelMapFaceFlags(&document, {{document.brushes.first().id, 0}}, edit, nullptr, &error), "light on one face", error);
		const QString text = QString::fromUtf8(serializeLevelMap(document).bytes);
		expect(text.contains(QStringLiteral("-0.5 2 0 1 0 // flags")) && text.count(QStringLiteral("-0.5 2 // flags")) == 5,
			"the changed line gains its three numbers", text);
		// The same lines under a Quake header stay Quake: no flags.
		LevelMapDocument quake;
		load(QByteArray("// Game: Quake\n") + bytes, QStringLiteral("idtech2"), &quake);
		expect(levelFaceFlagGame(quake) == LevelFaceFlagGame::None, "a Quake header keeps the map Quake");
	}

	phase = QStringLiteral("bit 31 is written signed");
	for (const bool quake3 : {false, true}) {
		// N64 scroll flip (Quake II surface bit 31) and No drop (Quake III
		// content bit 31) must read back in every compiler: written as a signed
		// 32-bit number, never as 2147483648 or above.
		const QString engine = quake3 ? QStringLiteral("idtech3") : QStringLiteral("idtech2");
		LevelMapDocument document;
		load(quake3 ? quake3Fixture() : quake2Fixture(), engine, &document);
		const int id = document.brushes.first().id;
		LevelFaceFlagsEdit edit;
		(quake3 ? edit.setContent : edit.setSurface) = 0x80000000u;
		expect(setLevelMapFaceFlags(&document, {{id, 0}}, edit, nullptr, &error), "set bit 31", error);
		QString text = QString::fromUtf8(serializeLevelMap(document).bytes);
		expect(text.contains(quake3 ? QStringLiteral(" -2147483646 4 8 // flags") : QStringLiteral(" 2 -2147483644 8 // flags"))
				&& !text.contains(QStringLiteral(" 2147483")),
			"bit 31 is written as a negative number", text);
		LevelMapDocument reopened;
		load(text.toUtf8(), engine, &reopened);
		const auto summary = levelFaceFlagsSummary(reopened, {{id, 0}});
		expect((quake3 ? summary.contentAll : summary.surfaceAll) == (quake3 ? 0x80000002u : 0x80000004u), "and reads back as bit 31");
		edit = {};
		(quake3 ? edit.clearContent : edit.clearSurface) = 0x80000000u;
		expect(setLevelMapFaceFlags(&reopened, {{id, 0}}, edit, nullptr, &error)
				&& QString::fromUtf8(serializeLevelMap(reopened).bytes).count(QStringLiteral(" 2 4 8 // flags")) == 6,
			"clearing it writes the plain number again", error);

		// A word a map already spelt unsigned keeps its text while unchanged,
		// and is written signed once it changes.
		QByteArray unsignedBytes = quake3 ? quake3Fixture() : quake2Fixture();
		const QByteArray plain(" 2 4 8 // flags");
		unsignedBytes.replace(unsignedBytes.indexOf(plain), plain.size(), QByteArray(" 2147483650 4 8 // flags"));
		LevelMapDocument spelt;
		load(unsignedBytes, engine, &spelt);
		edit = {};
		edit.setSurface = 0x2;
		expect(setLevelMapFaceFlags(&spelt, {{id, 0}}, edit, nullptr, &error), "change the surface word only", error);
		text = QString::fromUtf8(serializeLevelMap(spelt).bytes);
		expect(text.contains(QStringLiteral(" 2147483650 6 8 // flags")), "the unchanged content word keeps its text", text);
		edit = {};
		edit.setContent = 0x1;
		expect(setLevelMapFaceFlags(&spelt, {{id, 0}}, edit, nullptr, &error), "change the content word", error);
		text = QString::fromUtf8(serializeLevelMap(spelt).bytes);
		expect(text.contains(QStringLiteral(" -2147483645 6 8 // flags")) && !text.contains(QStringLiteral(" 2147483")), "a changed word is written signed",
			text);
	}

	phase = QStringLiteral("hidden, open groups and locks");
	{
		LevelMapDocument document;
		load(quake2Fixture(), QStringLiteral("idtech2"), &document);
		int second = -1;
		expect(addLevelMapBoxBrush(&document, {256, 0, 0, true}, {320, 64, 64, true}, QStringLiteral("studio/grid"), &second, &error), "a second brush", error);
		const int first = document.brushes.first().id;
		LevelFaceFlagsEdit edit;
		edit.setSurface = 0x2;
		const auto check = [&](const QVector<LevelSurfaceFace>& targets, const char* label, const char* reason) {
			const QByteArray before = serializeLevelMap(document).bytes;
			const auto depth = document.undoStack.size();
			QString problem;
			expect(!setLevelMapFaceFlags(&document, targets, edit, nullptr, &problem) && problem.contains(QString::fromLatin1(reason))
					&& serializeLevelMap(document).bytes == before && document.undoStack.size() == depth,
				label, problem);
		};
		// Hidden.
		setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, first}});
		LevelVisibilityRequest hide;
		expect(changeLevelMapVisibility(&document, hide, nullptr, &error), "hide the first brush", error);
		check({{first, 0}}, "a hidden face is refused", "hidden");
		LevelVisibilityRequest show;
		show.operation = LevelVisibilityOperation::ShowAll;
		expect(changeLevelMapVisibility(&document, show, nullptr, &error), "show all", error);
		// Outside the open group.
		QString group;
		expect(createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Room"), {}, &group, &error)
				&& assignLevelSceneObjects(&document, group, {QStringLiteral("brush:%1").arg(first)}, &error) && openLevelSceneGroup(&document, group, &error),
			"open a group holding the first brush", error);
		check({{second, 0}}, "a face outside the open group is refused", "outside the open group");
		expect(setLevelMapFaceFlags(&document, {{first, 0}}, edit, nullptr, &error), "a face inside the open group changes", error);
		expect(closeLevelSceneGroup(&document, true, &error), "close the group", error);
		// Locked.
		QString layer;
		expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
				&& assignLevelSceneObjects(&document, layer, {QStringLiteral("brush:%1").arg(second)}, &error) && setLevelSceneLocked(&document, layer, true, &error),
			"lock the second brush", error);
		check({{first, 1}, {second, 0}}, "a locked face refuses the whole edit", "locked");
	}

	if (argc > 1) {
		phase = QStringLiteral("cli");
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const QString settings = QDir(temp.path()).filePath(QStringLiteral("settings.ini"));
		const auto run = [&](const QStringList& arguments, int* exitCode) {
			QProcess process;
			process.start(binary, QStringList {QStringLiteral("--settings-file"), settings, QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("face-flags")}
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
		const QString quake2 = out("quake2.map"), quake3 = out("quake3.map");
		tests::putMaterialFile(quake2, quake2Fixture());
		tests::putMaterialFile(quake3, quake3Fixture());
		const QByteArray original = read(quake2);
		int code = -1;
		// Read only, by name.
		auto json = run({quake2, QStringLiteral("--engine"), QStringLiteral("idtech2"), QStringLiteral("--face"), QStringLiteral("0:1")}, &code);
		const auto row = json.value(QStringLiteral("faces")).toArray().at(0).toObject();
		expect(code == 0 && json.value(QStringLiteral("game")).toString() == QStringLiteral("quake2") && !json.value(QStringLiteral("mapWritten")).toBool()
				&& row.value(QStringLiteral("face")).toString() == QStringLiteral("0:1")
				&& row.value(QStringLiteral("contentFlags")).toArray() == QJsonArray({QStringLiteral("window")})
				&& row.value(QStringLiteral("surfaceFlags")).toArray() == QJsonArray({QStringLiteral("sky")}) && row.value(QStringLiteral("value")).toInt() == 8,
			"the face's flags by Quake II name", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		json = run({quake3}, &code);
		bool named = false;
		for (const auto& value : json.value(QStringLiteral("contentNames")).toArray()) {
			named |= value.toObject().value(QStringLiteral("id")).toString() == QStringLiteral("botclip");
		}
		expect(code == 0 && json.value(QStringLiteral("game")).toString() == QStringLiteral("quake3") && named && json.value(QStringLiteral("targetFaces")).toInt() == 0,
			"with no faces, the game's flag names", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		// A change writes what the service writes.
		LevelMapDocument expected;
		load(original, QStringLiteral("idtech2"), &expected);
		LevelFaceFlagsEdit edit;
		edit.setContent = 0x10000;
		edit.clearSurface = 0x4;
		edit.setValue = true;
		edit.value = 300;
		expect(setLevelMapFaceFlags(&expected, brushFaces(expected), edit, nullptr, &error), "reference edit", error);
		const QStringList change {quake2, QStringLiteral("--engine"), QStringLiteral("idtech2"), QStringLiteral("--object"), QStringLiteral("brush:0"),
			QStringLiteral("--set-content"), QStringLiteral("playerclip"), QStringLiteral("--clear-surface"), QStringLiteral("sky"), QStringLiteral("--value"),
			QStringLiteral("300")};
		json = run(change + QStringList {QStringLiteral("--output"), out("dry.map"), QStringLiteral("--dry-run")}, &code);
		expect(code == 0 && !QFile::exists(out("dry.map")) && read(quake2) == original && json.value(QStringLiteral("changedFaces")).toInt() == 6,
			"a dry run writes nothing", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		json = run(change + QStringList {QStringLiteral("--output"), out("clip.map")}, &code);
		LevelMapDocument written;
		load(read(out("clip.map")), QStringLiteral("idtech2"), &written);
		expect(code == 0 && json.value(QStringLiteral("changedFaces")).toInt() == 6 && json.value(QStringLiteral("setContent")).toArray() == QJsonArray({QStringLiteral("playerclip")})
				&& serializeLevelMap(written).bytes == serializeLevelMap(expected).bytes && read(quake2) == original,
			"the change matches the shared service", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		// Clear all on one face of the Quake III map.
		json = run({quake3, QStringLiteral("--engine"), QStringLiteral("idtech3"), QStringLiteral("--face"), QStringLiteral("0:2"), QStringLiteral("--clear-all"),
					   QStringLiteral("--output"), out("cleared.map")},
			&code);
		LevelMapDocument cleared;
		load(read(out("cleared.map")), QStringLiteral("idtech3"), &cleared);
		const auto& face = cleared.brushes.first().faces.at(1);
		expect(code == 0 && face.contentFlags == 0 && face.surfaceFlags == 0 && face.surfaceValue == 0 && cleared.brushes.first().faces.at(0).surfaceValue == 8,
			"--clear-all clears one face", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		// Bit 31 by name is written as a signed number the compilers read.
		json = run({quake2, QStringLiteral("--engine"), QStringLiteral("idtech2"), QStringLiteral("--face"), QStringLiteral("0:1"), QStringLiteral("--set-surface"),
					   QStringLiteral("n64_scroll_flip"), QStringLiteral("--output"), out("flip.map")},
			&code);
		const QString flip = QString::fromUtf8(read(out("flip.map")));
		expect(code == 0 && flip.contains(QStringLiteral(" 2 -2147483644 8 // flags")) && !flip.contains(QStringLiteral(" 2147483")),
			"N64 scroll flip is written signed", flip);
		json = run({quake3, QStringLiteral("--engine"), QStringLiteral("idtech3"), QStringLiteral("--face"), QStringLiteral("0:1"), QStringLiteral("--set-content"),
					   QStringLiteral("nodrop"), QStringLiteral("--output"), out("nodrop.map")},
			&code);
		const QString nodrop = QString::fromUtf8(read(out("nodrop.map")));
		expect(code == 0 && nodrop.contains(QStringLiteral(" -2147483646 4 8 // flags")) && !nodrop.contains(QStringLiteral(" 2147483")),
			"No drop is written signed", nodrop);
		// Refusals write nothing.
		run(change, &code);
		expect(code == 2, "a change needs --output");
		run({quake2, QStringLiteral("--engine"), QStringLiteral("idtech2"), QStringLiteral("--face"), QStringLiteral("0:1"), QStringLiteral("--set-content"),
				QStringLiteral("bogus"), QStringLiteral("--output"), out("bogus.map")},
			&code);
		expect(code == 2 && !QFile::exists(out("bogus.map")), "an unknown flag is a usage error");
		run({quake2, QStringLiteral("--engine"), QStringLiteral("idtech2"), QStringLiteral("--face"), QStringLiteral("0:1"), QStringLiteral("--set-content"),
				QStringLiteral("detail"), QStringLiteral("--output"), out("detail.map")},
			&code);
		expect(code == 4 && !QFile::exists(out("detail.map")), "the detail flag is refused");
		run({quake2, QStringLiteral("--engine"), QStringLiteral("idtech2"), QStringLiteral("--object"), QStringLiteral("patch:0"), QStringLiteral("--clear-all"),
				QStringLiteral("--output"), out("patch.map")},
			&code);
		expect(code == 2 && !QFile::exists(out("patch.map")), "patches carry no face flags");
		run({}, &code);
		expect(code == 2, "no map is a usage error");
	}

	std::cout << (ok ? "Face flags checks passed" : "Face flags checks failed") << std::endl;
	return ok ? 0 : 1;
}
