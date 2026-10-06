#include "core/package_staging.h"
#include "tests/level_prefab_test_helpers.h"
#include "tests/level_transform_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
bool expect(bool value, const char *message, const QString &error = {})
{
	if (!value) {
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
QByteArray read(const QString &path)
{
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir tmp;
	bool ok = expect(tmp.isValid(), "temporary directory");
	QString error;
	LevelMapDocument source;
	ok &= expect(loadPrefabFixture(&source, &error), "assembly fixture", error);
	const auto original = serializeLevelMap(source).bytes;
	const auto originalSelection = source.selection;
	LevelPrefab prefab;
	LevelPrefabReport capture;
	ok &= expect(createLevelPrefab(source, {QStringLiteral("Door assembly"), QStringLiteral("Reusable door with attached assets"), {}},
								   &prefab, &capture, &error),
				 "capture", error);
	ok &= expect(serializeLevelMap(source).bytes == original && source.selection == originalSelection, "capture is read-only");
	ok &= expect(capture.statistics.brushCount == 2 && capture.statistics.patchCount == 1 && capture.statistics.entityCount == 5 &&
					 !capture.warnings.isEmpty() && capture.externalTargets == QStringList{QStringLiteral("outside")},
				 "ownership and external-link report");
	ok &= expect(!prefab.mapText.contains(QStringLiteral("Do not export")) && prefab.mapText.contains(QStringLiteral("models/test.md3")) &&
					 prefab.mapText.contains(QStringLiteral("sound/test.wav")),
				 "world settings excluded; asset references preserved");
	const auto bytes = serializeLevelPrefab(prefab, &error);
	LevelPrefab parsed;
	ok &= expect(!bytes.isEmpty() && parseLevelPrefab(bytes, &parsed, &error) && serializeLevelPrefab(parsed) == bytes,
				 "deterministic versioned roundtrip", error);
	const auto path = tmp.filePath(QStringLiteral("door.vprefab"));
	LevelPrefabWriteReport saved;
	ok &= expect(writeLevelPrefab(prefab, path, false, true, &saved, &error) && saved.dryRun && !QFile::exists(path),
				 "dry-run writes nothing", error);
	ok &= expect(writeLevelPrefab(prefab, path, false, false, &saved, &error) && saved.committed && read(path) == bytes, "publish prefab",
				 error);
	ok &= expect(!writeLevelPrefab(prefab, path, false, false, &saved, &error) && read(path) == bytes, "overwrite requires intent");
	prefab.description = QStringLiteral("Updated");
	ok &= expect(writeLevelPrefab(prefab, path, true, false, &saved, &error) && saved.committed && !saved.backupPath.isEmpty() &&
					 read(saved.backupPath) == bytes,
				 "overwrite preserves independent backup", error);
	LevelMapDocument target;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &target);
	int entityId = -1;
	addLevelMapEntity(&target, QStringLiteral("target_relay"), {0, 0, 0, true}, {}, &entityId);
	setLevelMapEntityProperty(&target, entityId, QStringLiteral("target"), QStringLiteral("prefab1_gate"));
	const auto before = serializeLevelMap(target).bytes;
	const auto selectionBefore = target.selection;
	const auto depth = target.undoStack.size();
	LevelPrefabPlacement placement;
	placement.position = {256, 128, 32, true};
	placement.rotation = {0, 0, 90, true};
	LevelPrefabReport inserted;
	ok &= expect(insertLevelPrefab(&target, parsed, placement, &inserted, &error), "insert rotated assembly", error);
	ok &= expect(inserted.prefix == QStringLiteral("prefab2_") &&
					 inserted.renamedTargets.value(QStringLiteral("gate")) == QStringLiteral("prefab2_gate"),
				 "avoid capturing existing unresolved references");
	ok &= expect(target.undoStack.size() == depth + 1 && target.brushes.size() == 2 && target.patches.size() == 1,
				 "one complete insertion undo step");
	bool link = false, external = false, assets = false;
	for (const auto &e : target.entities) {
		if (prefabProperty(e, QStringLiteral("targetname")) == QStringLiteral("prefab2_relay")) {
			link = prefabProperty(e, QStringLiteral("target")) == QStringLiteral("prefab2_gate");
			external = prefabProperty(e, QStringLiteral("killtarget")) == QStringLiteral("outside");
		}
		assets |= prefabProperty(e, QStringLiteral("model")) == QStringLiteral("models/test.md3");
	}
	ok &= expect(link && external && assets, "internal links renamed; external links and model reference retained");
	const auto after = serializeLevelMap(target).bytes;
	ok &= expect(undoLevelMapEdit(&target, &error) && serializeLevelMap(target).bytes == before && target.selection == selectionBefore &&
					 redoLevelMapEdit(&target, &error) && serializeLevelMap(target).bytes == after && target.selection == inserted.inserted,
				 "exact atomic undo/redo", error);
	ok &= expect(insertLevelPrefab(&target, parsed, placement, &inserted, &error) && inserted.prefix == QStringLiteral("prefab3_"),
				 "repeated placements remain separate", error);
	const auto stable = serializeLevelMap(target).bytes;
	const auto stableRevision = target.revision;
	placement.targetPrefix = QStringLiteral("prefab2_");
	ok &= expect(!insertLevelPrefab(&target, parsed, placement, &inserted, &error) && serializeLevelMap(target).bytes == stable &&
					 target.revision == stableRevision,
				 "collision failure is atomic");
	placement.targetPrefix.clear();
	placement.position.x = std::numeric_limits<double>::infinity();
	ok &= expect(!insertLevelPrefab(&target, parsed, placement, nullptr, &error) && serializeLevelMap(target).bytes == stable,
				 "nonfinite placement rejected");
	placement.position.x = 32768;
	ok &= expect(!insertLevelPrefab(&target, parsed, placement, nullptr, &error) && serializeLevelMap(target).bytes == stable,
				 "out-of-bounds result rejected");
	placement.position.x = 128;
	ok &= expect(!insertLevelPrefab(&target, parsed, placement, nullptr, &error, [] { return true; }) &&
					 serializeLevelMap(target).bytes == stable,
				 "cancellation is atomic");
	for (const auto &kind :
		 {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		LevelMapDocument fixture;
		loadTransformFixture(transformFixture(kind), &fixture, &error);
		setLevelMapSelection(&fixture, {{LevelMapSelectionKind::QuakeBrush, 0}});
		LevelPrefab p;
		ok &= expect(createLevelPrefab(fixture, {kind, {}, {}}, &p, nullptr, &error), "capture texture dialect", error);
		LevelMapDocument empty;
		createLevelMap(create, &empty);
		placement = {};
		placement.position = {256, 128, 32, true};
		placement.rotation.z = 90;
		ok &= expect(insertLevelPrefab(&empty, p, placement, nullptr, &error), "place texture dialect", error);
		if (!empty.brushes.isEmpty()) {
			const auto &a = fixture.brushes.first(), &b = empty.brushes.last();
			for (qsizetype f = 0; f < a.faces.size(); ++f) {
				const auto old = a.faces[f].p0;
				const LevelMapVec3 placed{placement.position.x - (old.y - p.anchor.y), placement.position.y + (old.x - p.anchor.x),
										  placement.position.z + old.z - p.anchor.z, true};
				const auto uv0 = levelTextureProjection(a.faces[f]).at(old), uv1 = levelTextureProjection(b.faces[f]).at(placed);
				ok &= expect(std::abs(uv0.x() - uv1.x()) < 0.002 && std::abs(uv0.y() - uv1.y()) < 0.002, "UVs follow prefab placement");
			}
		}
	}
	auto malformed = QJsonDocument::fromJson(bytes).object();
	malformed.insert(QStringLiteral("version"), 2);
	ok &= expect(!parseLevelPrefab(QJsonDocument(malformed).toJson(), &prefab, &error), "future schema rejected");
	malformed.insert(QStringLiteral("version"), 1);
	malformed.insert(QStringLiteral("description"), 17);
	ok &= expect(!parseLevelPrefab(QJsonDocument(malformed).toJson(), &prefab, &error), "schema property types validated");
	auto invalid = parsed;
	invalid.mapText.replace(QStringLiteral("\"targetname\" \"gate\""),
							QStringLiteral("\"targetname\" \"gate\"\n\"TARGETNAME\" \"conflict\""));
	ok &= expect(!inspectLevelPrefab(invalid, nullptr, nullptr, &error), "ambiguous duplicate keys rejected");
	invalid = parsed;
	invalid.mapText += QString(10000, QLatin1Char('{'));
	ok &= expect(!inspectLevelPrefab(invalid, nullptr, nullptr, &error), "bounded parser rejects pathological nesting");
	invalid = parsed;
	invalid.engineFamily = QStringLiteral("idTech1");
	ok &= expect(!insertLevelPrefab(&target, invalid, {}, nullptr, &error), "cross-engine insertion rejected");
	std::cout << (ok ? "Prefab capture, publication, placement, UVs, links and failure atomicity passed.\n" : "Prefab checks failed.\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
