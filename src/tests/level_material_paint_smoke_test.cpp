#include "core/level_material_paint.h"
#include "core/level_texture_mapping.h"
#include "core/package_staging.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) { std::cerr << message << ": " << detail.toStdString() << '\n'; }
	return value;
}
bool paint(LevelMapDocument* map, const QVector<LevelMaterialTarget>& targets, const QString& material, QString* error)
{
	LevelMaterialPaintPlan plan;
	return prepareLevelMaterialPaint(*map, targets, material, &plan, error) && commitLevelMaterialPaint(map, plan, error);
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	bool ok = temp.isValid();
	QString error;
	const LevelMaterialTarget face0{LevelMaterialKind::BrushFace, 0, 0}, face2{LevelMaterialKind::BrushFace, 0, 2};
	for (const auto& dialect : {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		LevelMapDocument map;
		const auto original = tests::surfaceFixture(dialect);
		if (!expect(loadLevelMapBytes({QStringLiteral("paint.map"), {}, QStringLiteral("idtech3")}, original, &map, &error), "load dialect", error)) { return 1; }
		setLevelMapSelection(&map, {{LevelMapSelectionKind::Entity, 1}});
		const auto before = map;
		LevelMaterialPaintPlan plan;
		ok &= expect(prepareLevelMaterialPaint(map, {face0, face2, face0}, QStringLiteral("studio/painted"), &plan, &error), "prepare batch", error);
		ok &= expect(plan.ready() && plan.targetCount() == 2 && plan.changedCount() == 2 && map.revision == before.revision, "prepare immutable and deduplicates");
		ok &= expect(commitLevelMaterialPaint(&map, plan, &error), "commit batch", error);
		ok &= expect(map.undoStack.size() == before.undoStack.size() + 1 && map.selection == before.selection && map.selectedObjectId == before.selectedObjectId, "one undo without selecting targets");
		for (int i = 0; i < 6; ++i) {
			const auto a = levelTextureProjection(before.brushes[0].faces[i]), b = levelTextureProjection(map.brushes[0].faces[i]);
			ok &= expect(a.at({17, 25, 31, true}) == b.at({17, 25, 31, true}), "alignment unchanged");
			ok &= expect(map.brushes[0].faces[i].textureName == (i == 0 || i == 2 ? QStringLiteral("studio/painted") : QStringLiteral("studio/grid")), "only named faces painted");
		}
		auto expected = original;
		for (int i : {2, 0}) {
			qsizetype offset = -1;
			for (int occurrence = 0; occurrence <= i; ++occurrence) { offset = expected.indexOf("studio/grid", offset + 1); }
			expected.replace(offset, 11, "studio/painted");
		}
		const auto saved = serializeLevelMap(map);
		ok &= expect(saved.succeeded() && saved.bytes == expected, "only material bytes change; UVs, flags, comments and CRLF exact", dialect);
		LevelMapDocument reopened;
		ok &= expect(loadLevelMapBytes({QStringLiteral("paint.map"), {}, QStringLiteral("idtech3")}, saved.bytes, &reopened, &error) && reopened.brushes[0].faces[2].textureName == QStringLiteral("studio/painted"), "paint survives reload", error);
		ok &= expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == original, "exact byte undo", error);
		const auto revision = map.revision;
		ok &= expect(paint(&map, {face0}, QStringLiteral("studio/grid"), &error) && map.revision == revision && map.redoStack.size() == 1, "no-op preserves redo and revision", error);
		ok &= expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == saved.bytes, "exact redo", error);
		ok &= expect(!commitLevelMaterialPaint(&map, plan, &error), "stale plan rejected");
		const auto bytes = serializeLevelMap(map).bytes;
		ok &= expect(!prepareLevelMaterialPaint(map, {face0, {LevelMaterialKind::BrushFace, 999, 0}}, QStringLiteral("valid"), &plan, &error) && !plan.ready() && serializeLevelMap(map).bytes == bytes, "invalid later target atomic");
		for (const QString& invalid : {QString(), QStringLiteral("bad token"), QStringLiteral("bad//token"), QStringLiteral("bad/*token"), QStringLiteral("bad\\token"), QStringLiteral("bad\"token"), QStringLiteral("bad\0token")}) {
			ok &= expect(!paint(&map, {face0}, invalid, &error), "invalid names refused");
		}
		ok &= expect(!prepareLevelMaterialPaint(map, {}, QStringLiteral("valid"), &plan, &error), "empty batch refused");
		ok &= expect(!prepareLevelMaterialPaint(map, QVector<LevelMaterialTarget>(16385, face0), QStringLiteral("valid"), &plan, &error), "bounded batch");
		ok &= expect(prepareLevelMaterialPaint(map, {face0}, QStringLiteral("other"), &plan, &error), "prepare tamper guard");
		map.brushes[0].faces[0].textureName = QStringLiteral("out-of-band");
		ok &= expect(!commitLevelMaterialPaint(&map, plan, &error), "old-value guard rejects out-of-band material change");
	}
	for (const QString& selector : {QStringLiteral("face:4:1"), QStringLiteral("patch:2"), QStringLiteral("side:9:upper"), QStringLiteral("side:0:lower"), QStringLiteral("side:2:middle"), QStringLiteral("sector:3:floor"), QStringLiteral("sector:0:ceiling")}) {
		LevelMaterialTarget target;
		ok &= expect(parseLevelMaterialTarget(selector, &target, &error) && levelMaterialTargetId(target) == selector, "selector round trip", error);
	}
	for (const QString& selector : {QStringLiteral("face:0:0"), QStringLiteral("face:-1:1"), QStringLiteral("side:0:3"), QStringLiteral("patch:0:1"), QStringLiteral("face:2147483648:1"), QStringLiteral("face:0:2147483648"), QStringLiteral("sector:1:middle")}) {
		LevelMaterialTarget target;
		ok &= expect(!parseLevelMaterialTarget(selector, &target, &error), "strict selector validation", selector);
	}
	LevelMapDocument fixture;
	ok &= expect(tests::createMaterialFixture(temp.path(), &fixture, &error), "mixed fixture", error);
	const auto patchTarget = LevelMaterialTarget{LevelMaterialKind::Patch, fixture.patches[0].id};
	const auto beforeMixed = serializeLevelMap(fixture).bytes;
	ok &= expect(paint(&fixture, {face0, patchTarget}, QStringLiteral("studio/editor"), &error), "mixed patch and face batch", error);
	PackageArchive archive;
	ok &= expect(archive.load(QDir(temp.path()).filePath(QStringLiteral("assets")), &error), "package material source", error);
	const auto assets = resolveLevelPreviewAssets(fixture, archive);
	const auto preview = buildLevelMapPreviewMesh(fixture, {.modelMeshes = assets.models, .textureSizes = levelPreviewTextureSizes(assets)});
	ok &= expect(assets.readyCount() == 4 && preview.modelInstances == 1 && preview.materialTargets.size() == preview.mesh.triangleCount, "paint resolves package images and preview provenance");
	PackageStagingModel staging;
	ok &= expect(staging.loadBaseArchive(archive, &error) && staging.addBytes(tests::materialImage(32, 16), QStringLiteral("textures/studio/editor.png"), &error, PackageStageConflictResolution::ReplaceExisting), "stage replacement material image", error);
	const PackageStagingArchive staged(staging);
	const auto stagedAssets = resolveLevelPreviewAssets(fixture, staged);
	const auto* stagedMaterial = tests::materialNamed(stagedAssets, QStringLiteral("studio/editor"));
	ok &= expect(stagedMaterial && stagedMaterial->sourceSize == QSize(32, 16), "painted material resolves staged bytes");
	const auto* originalMaterial = tests::materialNamed(assets, QStringLiteral("studio/editor"));
	ok &= expect(originalMaterial && originalMaterial->sourceSize == QSize(64, 32), "base package image unchanged by staging and painting");
	int placed = 0, patches = 0;
	for (int i = 0; i < preview.materialTargets.size(); ++i) {
		const auto& target = preview.materialTargets[i];
		if (preview.owners[i].kind == LevelMapSelectionKind::Entity) { ++placed; ok &= expect(target.kind == LevelMaterialKind::None, "placed model cannot be painted as map geometry"); }
		else {
			QString material;
			ok &= expect(sampleLevelMaterial(fixture, target, &material, &error), "all preview map targets sample", error);
			if (target == patchTarget) { ++patches; ok &= expect(material == QStringLiteral("studio/editor"), "patch material shared across triangles"); }
		}
	}
	ok &= expect(placed > 0 && patches > 1 && undoLevelMapEdit(&fixture, &error) && serializeLevelMap(fixture).bytes == beforeMixed, "mixed undo and provenance counts", error);
	for (const auto& game : {QStringLiteral("doom"), QStringLiteral("hexen")}) {
		LevelMapDocument map;
		LevelMapCreateRequest create;
		create.game = game;
		ok &= expect(createLevelMap(create, &map, &error), "Doom-family fixture", error);
		const auto original = serializeLevelMap(map).bytes;
		ok &= expect(loadLevelMapBytes({QStringLiteral("paint.wad"), QStringLiteral("MAP01"), {}}, original, &map, &error), "load clean binary fixture", error);
		const QVector<LevelMaterialTarget> targets{{LevelMaterialKind::WallUpper, 0}, {LevelMaterialKind::WallLower, 0}, {LevelMaterialKind::WallMiddle, 0}, {LevelMaterialKind::SectorFloor, 0}, {LevelMaterialKind::SectorCeiling, 0}};
		ok &= expect(paint(&map, targets, QStringLiteral("newwall"), &error), "all binary wall and flat fields", error);
		for (const auto& target : targets) { QString value; ok &= expect(sampleLevelMaterial(map, target, &value, &error) && value == QStringLiteral("NEWWALL"), "binary names normalized"); }
		ok &= expect(!map.doomGeometryChanged, "material edits do not stale Doom nodes");
		const auto saved = serializeLevelMap(map);
		LevelMapDocument reloaded;
		ok &= expect(loadLevelMapBytes({QStringLiteral("paint.wad"), QStringLiteral("MAP01"), {}}, saved.bytes, &reloaded, &error) && reloaded.doomSidedefs[0].middleTexture == QStringLiteral("NEWWALL"), "binary reload", error);
		ok &= expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == original, "binary undo exact", error);
		ok &= expect(!paint(&map, targets, QStringLiteral("TOO_LONG_NAME"), &error) && !paint(&map, targets, QString::fromUtf8("caf\xc3\xa9"), &error), "binary unrepresentable names refused");
		map.doomFormat = LevelMapDoomFormat::Udmf;
		ok &= expect(!paint(&map, targets, QStringLiteral("VALID"), &error), "UDMF stays read only");
	}
	// A raised front room exposes the BACK lower wall; a taller back room
	// exposes the BACK upper wall. Different names catch front-only mapping.
	LevelMapDocument steps;
	steps.format = LevelMapFormat::DoomWad;
	LevelMapDoomVertex a, b; a.id = 0; b.id = 1; b.x = 128;
	steps.doomVertices = {a, b};
	LevelMapDoomSidedef front, back; front.id = 0; back.id = 1; front.sector = 0; back.sector = 1;
	front.lowerTexture = "FRONTLO"; front.upperTexture = "FRONTHI"; back.lowerTexture = "BACKLO"; back.upperTexture = "BACKHI";
	steps.doomSidedefs = {front, back};
	LevelMapDoomSector low, high; low.id = 0; high.id = 1; low.floorHeight = 32; low.ceilingHeight = 96; high.floorHeight = 0; high.ceilingHeight = 128;
	steps.doomSectors = {low, high};
	LevelMapDoomLinedef line; line.id = 0; line.startVertex = 0; line.endVertex = 1; line.frontSidedef = 0; line.backSidedef = 1;
	steps.doomLinedefs = {line};
	const auto walls = buildLevelMapPreviewMesh(steps);
	ok &= expect(walls.walls == 2 && walls.materialTargets.size() == 4, "Doom step geometry");
	for (const auto& target : walls.materialTargets) { ok &= expect(target.objectId == 1 && (target.kind == LevelMaterialKind::WallLower || target.kind == LevelMaterialKind::WallUpper), "Doom step belongs to correct side"); }
	if (argc > 1) {
		const auto input = QDir(temp.path()).filePath(QStringLiteral("input.map")), output = QDir(temp.path()).filePath(QStringLiteral("output.map"));
		ok &= tests::putMaterialFile(input, tests::surfaceFixture(QStringLiteral("valve220")));
		const auto cli = [&](QStringList args, int expectedCode) {
			QProcess process; args.prepend(QStringLiteral("--cli")); args << QStringLiteral("--json");
			process.start(QString::fromLocal8Bit(argv[1]), args);
			if (!process.waitForFinished(30000)) { process.kill(); process.waitForFinished(); ok = false; return QJsonObject(); }
			const auto stdoutBytes = process.readAllStandardOutput();
			ok &= expect(process.exitStatus() == QProcess::NormalExit && process.exitCode() == expectedCode, "CLI exit", QString::fromUtf8(stdoutBytes + process.readAllStandardError()));
			return QJsonDocument::fromJson(stdoutBytes).object();
		};
		const QStringList args{"map", "paint-material", input, "--target", "face:0:1", "--target", "face:0:3", "--target", "face:0:1", "--texture", "studio/editor", "--output", output};
		const auto dry = cli(args + QStringList{"--dry-run"}, 0);
		ok &= expect(dry.value("changedCount").toInt() == 2 && dry.value("targetCount").toInt() == 2 && !QFileInfo::exists(output), "CLI dry run deduplicates without writing");
		cli(args, 0);
		const auto sampled = cli({"map", "sample-material", output, "--target", "face:0:3"}, 0);
		ok &= expect(sampled.value("material").toString() == QStringLiteral("studio/editor"), "CLI painted output sampled");
		cli(args, 4); cli(args + QStringList{"--overwrite"}, 0);
		cli(args + QStringList{"--unknown"}, 2);
		cli(args + QStringList{"--texture", "another"}, 2);
		cli({"map", "sample-material", input, "--target", "face:0:0"}, 2);
		cli({"map", "sample-material", input, "--target", "face:999:1"}, 4);
		cli({"map", "sample-material", input, "--target", "face:0:1", "--output", output}, 2);
		LevelMapDocument doom;
		LevelMapCreateRequest create; create.game = QStringLiteral("doom");
		ok &= createLevelMap(create, &doom, &error);
		const auto wadInput = QDir(temp.path()).filePath(QStringLiteral("input.wad")), wadOutput = QDir(temp.path()).filePath(QStringLiteral("output.wad"));
		ok &= tests::putMaterialFile(wadInput, serializeLevelMap(doom).bytes);
		cli({"map", "sample-material", wadInput, "--target", "sector:0:floor"}, 2);
		cli({"map", "paint-material", wadInput, "--map-name", "MAP01", "--target", "sector:0:floor", "--target", "side:0:middle", "--texture", "bricks", "--output", wadOutput}, 0);
		const auto flat = cli({"map", "sample-material", wadOutput, "--map-name", "MAP01", "--target", "sector:0:floor"}, 0);
		ok &= expect(flat.value("material").toString() == QStringLiteral("BRICKS"), "binary CLI material roundtrip");
		cli({"map", "sample-material", input, "--map-name", "MAP01", "--target", "face:0:1"}, 2);
		QFile file(input); ok &= file.open(QIODevice::ReadOnly);
		ok &= expect(file.readAll() == tests::surfaceFixture(QStringLiteral("valve220")), "CLI leaves source map intact");
	}
	std::cout << (ok ? "Material paint core, preview and CLI checks passed.\n" : "Material paint checks failed.\n");
	return ok ? 0 : 1;
}
