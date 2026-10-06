#include "cli/model_material_slots.h"
#include "app/model_material_worker.h"
#include "core/model_material_slots.h"
#include "core/model_recovery.h"
#include "tests/model_surfaces_test_helpers.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0;
bool expect(bool condition, const char *message)
{
	++checks;
	if (!condition)
		std::cerr << "FAIL: " << message << '\n';
	return condition;
}
bool settle(ModelMaterialWorker &worker)
{
	QElapsedTimer timer;
	timer.start();
	while (worker.busy() && timer.elapsed() < 20000)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	return !worker.busy();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("material-slots-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	bool ok = true;
	QString error;
	const QStringList originalPaths{"models/red.png", "models/blue.png", "models/red.png"};
	QStringList paths = originalPaths;
	const auto mutate = [&](ModelMaterialSlotAction action, int slot, int to, const QStringList &materials) {
		return editModelMaterialSlots(paths, {action, slot, to, materials}, &paths, &error);
	};
	ok &= expect(mutate(ModelMaterialSlotAction::Move, 2, 0, {}) &&
					 paths == QStringList{"models/red.png", "models/red.png", "models/blue.png"},
				 "moving a repeated path retains every ordered slot");
	ok &= expect(mutate(ModelMaterialSlotAction::Set, 1, -1, {"models/green.png"}) &&
					 paths == QStringList{"models/red.png", "models/green.png", "models/blue.png"},
				 "set replaces only the selected slot");
	ok &= expect(mutate(ModelMaterialSlotAction::Insert, 3, -1, {"models/last.png"}) && paths.last() == "models/last.png" &&
					 paths.size() == 4,
				 "insert at count appends");
	ok &= expect(mutate(ModelMaterialSlotAction::Insert, 0, -1, {"models/first.png"}) && paths.first() == "models/first.png" &&
					 paths.size() == 5,
				 "insert at zero changes the primary explicitly");
	ok &= expect(mutate(ModelMaterialSlotAction::Remove, 0, -1, {}) && paths.first() == "models/red.png" && paths.size() == 4,
				 "remove shifts indices predictably");
	ok &= expect(mutate(ModelMaterialSlotAction::Clear, -1, -1, {}) && paths.isEmpty(), "clear explicitly unassigns all external slots");
	ok &= expect(mutate(ModelMaterialSlotAction::Replace, -1, -1, originalPaths) && paths == originalPaths,
				 "complete replacement retains duplicates");
	for (const auto &bad : QVector<ModelMaterialSlotEdit>{{ModelMaterialSlotAction::Set, -1, -1, {"ok.png"}},
														  {ModelMaterialSlotAction::Set, 3, -1, {"ok.png"}},
														  {ModelMaterialSlotAction::Insert, 4, -1, {"ok.png"}},
														  {ModelMaterialSlotAction::Remove, 3, -1, {}},
														  {ModelMaterialSlotAction::Move, 0, 3, {}},
														  {ModelMaterialSlotAction::Move, 0, -1, {}},
														  {ModelMaterialSlotAction::Clear, 0, -1, {}},
														  {ModelMaterialSlotAction::Replace, -1, 0, originalPaths},
														  {ModelMaterialSlotAction::Set, 0, -1, originalPaths},
														  {ModelMaterialSlotAction::Remove, 0, -1, {"unused.png"}},
														  {static_cast<ModelMaterialSlotAction>(99), -1, -1, {}}})
	{
		QStringList output{"unchanged"};
		ok &= expect(!editModelMaterialSlots(paths, bad, &output, &error) && !error.isEmpty() && output == QStringList{"unchanged"},
					 "invalid actions or arguments leave output untouched");
	}
	for (const auto &path : QStringList{"", "../escape.png", "/absolute.png", "C:/outside.png", "models/../skin.png", QString(256, 'x')})
	{
		QStringList output{"unchanged"};
		ok &= expect(!editModelMaterialSlots({}, {ModelMaterialSlotAction::Replace, -1, -1, {path}}, &output, &error) &&
						 output == QStringList{"unchanged"},
					 "invalid material paths are rejected atomically");
	}
	QStringList full;
	for (int i = 0; i < modelMaxMaterialSlots; ++i)
		full << "models/repeated.png";
	ok &= expect(editModelMaterialSlots({}, {ModelMaterialSlotAction::Replace, -1, -1, full}, &paths, &error) && paths.size() == 256,
				 "the inclusive 256 slot bound retains repetitions");
	ok &= expect(!editModelMaterialSlots(paths, {ModelMaterialSlotAction::Insert, 256, -1, {"more.png"}}, &paths, &error) && paths == full,
				 "insertion beyond slot capacity preserves source");
	auto original = surfaceFixture();
	original.surfaces[1].skinPaths = originalPaths;
	updateEditableModelMetadata(&original);
	ModelDocument document;
	ModelEdit edit;
	edit.kind = ModelEditKind::SetMaterialSlots;
	edit.selection = {1, {0}, {1}, {{1, 2}}};
	edit.materialSlots = {"models/green.png", "models/red.png", "models/green.png"};
	ok &= expect(document.setMesh(original, &error), "open animated fixture");
	document.setSelection(edit.selection);
	ok &= expect(document.edit(edit, &error) && document.selection() == edit.selection &&
					 document.mesh().surfaces[1].skinPaths == edit.materialSlots,
				 "document replaces all slots and preserves component selection");
	const auto changed = document.mesh();
	auto expected = original;
	expected.surfaces[1].skinPaths = edit.materialSlots;
	updateEditableModelMetadata(&expected);
	ok &= expect(surfaceBytes(changed) == surfaceBytes(expected),
				 "every other pose, surface, seam, UV, tag, clip and collision field stays byte-equivalent");
	ok &= expect(document.undo() && surfaceBytes(document.mesh()) == surfaceBytes(original) && document.selection() == edit.selection &&
					 !document.canUndo() && document.redo() && surfaceBytes(document.mesh()) == surfaceBytes(changed),
				 "one edit has one exact undo and redo");
	const auto revision = document.revisionFingerprint();
	ok &= expect(document.edit(edit, &error) && document.undo() && !document.canUndo() && document.redo() &&
					 document.revisionFingerprint() == revision,
				 "unchanged list creates no extra history entry");
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = changed;
	snapshot.selection = edit.selection;
	snapshot.title = "material slots";
	const auto recovery = writeModelRecovery(snapshot, temporary.path(), "5f9a83aa-0b45-4cf8-8b0d-ae6f237cb0b8", &error);
	ModelRecoverySnapshot restored;
	ok &= expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() &&
					 surfaceBytes(restored.mesh) == surfaceBytes(changed) && restored.selection == edit.selection,
				 "recovery preserves alternate order and selection");
	const auto source = QDir(temporary.path()).filePath("source.mesh.json"), output = QDir(temporary.path()).filePath("output.mesh.json");
	ok &= expect(document.save(source, false, &error), "save editable source with material slots");
	ModelDocument loaded;
	ok &= expect(loaded.load(source, &error) && surfaceBytes(loaded.mesh()) == surfaceBytes(changed), "reopen retains exact authored list");
	ModelMesh native;
	const auto md3 = exportEditableModel(changed, "md3", 0, &error);
	ok &= expect(!md3.isEmpty() && importEditableModel("slots.md3", md3, &native, &error) &&
					 native.surfaces[1].skinPaths == edit.materialSlots,
				 "MD3 roundtrip retains slot order and duplicate paths");
	auto md2Mesh = changed;
	md2Mesh.surfaces = {changed.surfaces[1]};
	md2Mesh.tags.clear();
	md2Mesh.surfaces[0].skinPaths = {"models/first.pcx", "models/second.pcx", "models/first.pcx"};
	updateEditableModelMetadata(&md2Mesh);
	const auto md2 = exportEditableModel(md2Mesh, "md2", 0, &error);
	ok &= expect(!md2.isEmpty() && importEditableModel("slots.md2", md2, &native, &error) &&
					 native.surfaces[0].skinPaths == md2Mesh.surfaces[0].skinPaths,
				 "MD2 roundtrip retains all authored PCX skins");
	md2Mesh.surfaces[0].skinPaths[1] = "models/second.png";
	ok &=
		expect(exportEditableModel(md2Mesh, "md2", 0, &error).isEmpty(), "native validation still rejects an incompatible alternate slot");
	ModelMesh mdl;
	ok &= expect(importEditableModel("grouped.mdl", groupedMdlFixture().bytes, &mdl, &error), "import embedded MDL fixture");
	const auto mdlBytes = surfaceBytes(mdl);
	edit.selection = {};
	ok &= expect(!applyModelEdit(&mdl, edit, nullptr, &error) && surfaceBytes(mdl) == mdlBytes,
				 "external slot authoring cannot silently mask embedded MDL skins");
	for (int failure = 0; failure < 4; ++failure)
	{
		auto candidate = original;
		auto invalid = edit;
		if (failure == 0)
			invalid.frame = 0;
		if (failure == 1)
			invalid.text = "ignored";
		if (failure == 2)
			invalid.skinBindings = "ignored";
		if (failure == 3)
			invalid.selection.surface = 9;
		ok &= expect(!applyModelEdit(&candidate, invalid, nullptr, &error) && surfaceBytes(candidate) == surfaceBytes(original),
					 "invalid scope and unrelated slot arguments are atomic");
	}
	ok &= expect(document.setMesh(original, &error), "reset cancellation fixture");
	bool cancelled = false;
	ModelWorkControl control{[&] { return cancelled; },
							 [&](ModelWorkPhase phase, qint64, qint64) {
								 if (phase == ModelWorkPhase::Editing)
									 cancelled = true;
							 }};
	ok &= expect(!document.edit(edit, &error, control) && cancelled && !document.canUndo() &&
					 surfaceBytes(document.mesh()) == surfaceBytes(original),
				 "cancelled edit leaves source and history intact");
	ModelMesh preview;
	ok &= expect(modelMaterialPreviewSnapshot(original, {{1, 1}}, &preview, &error) &&
					 preview.surfaces[1].skinPaths == QStringList{"models/blue.png"} &&
					 preview.surfaces[0].skinPaths == QStringList{original.surfaces[0].skinPaths[0]} && preview.frames.isEmpty() &&
					 preview.tags.isEmpty() && preview.surfaces[1].frames.isEmpty(),
				 "alternate preview snapshot retains provenance but no animated geometry");
	for (const auto &overrides : QVector<QHash<int, int>>{{{9, 0}}, {{1, -1}}, {{1, 3}}})
	{
		const auto before = preview.surfaces[1].skinPaths;
		ok &= expect(!modelMaterialPreviewSnapshot(original, overrides, &preview, &error) && preview.surfaces[1].skinPaths == before,
					 "invalid preview overrides leave the output intact");
	}
	ok &= expect(modelMaterialPreviewSnapshot(mdl, {}, &preview, &error) && !preview.embeddedSkins[0].image.isNull() &&
					 preview.embeddedSkins[0].indexedFrames.isEmpty() && !modelMaterialPreviewSnapshot(mdl, {{0, 0}}, &preview, &error),
				 "MDL default preview retains its derived image and rejects external override");
	auto reader = std::make_shared<SkinReader>();
	QImage red(16, 16, QImage::Format_RGB32), blue(16, 16, QImage::Format_RGB32);
	red.fill(Qt::red);
	blue.fill(Qt::blue);
	reader->add("models/red.png", skinPng(red));
	reader->add("models/blue.png", skinPng(blue));
	auto visible = original;
	visible.surfaces = {original.surfaces[1]};
	updateEditableModelMetadata(&visible);
	ModelMaterialWorker worker;
	int delivered = 0;
	ModelMaterialResult last;
	worker.completed = [&](const auto &value) {
		++delivered;
		last = value;
	};
	const ModelMaterialSource package{reader, "slots-v1", "auto"};
	worker.request(visible, package, {{0, 0}});
	worker.request(visible, package, {{0, 1}});
	ok &= expect(settle(worker) && delivered == 1 && last.assets.materials.size() == 1 &&
					 last.assets.materials[0].image.pixelColor(0, 0) == QColor(Qt::blue),
				 "coalesced alternate preview delivers only the newest material request");
	worker.request(visible, package, {{0, 1}});
	ok &= expect(!worker.busy() && delivered == 1, "unchanged slot preview reuses material work");
	worker.request(visible, package, {{0, 0}});
	worker.cancel();
	const int cancelledCount = delivered;
	worker.request(visible, package, {{0, 0}});
	ok &= expect(settle(worker) && delivered == cancelledCount && last.assets.cancelled,
				 "ordinary refresh does not restart a cancelled slot preview");
	worker.request(visible, package, {{0, 1}});
	ok &= expect(settle(worker) && delivered == cancelledCount + 1 && !last.assets.cancelled,
				 "changing slots starts new work after cancellation");
	const QStringList base{"vibestudio", "--cli", "model", "slots", source};
	auto listed = cli::runModelMaterialSlots(base);
	ok &=
		expect(listed.exitCode == 0 && listed.payload.value("surfaces").toArray().size() == 3 && !listed.payload.value("written").toBool(),
			   "CLI lists slots without writing");
	const QStringList command =
		base + QStringList{"--surface",		 "1",		   "--operation",	  "replace",  "--material", "models/blue.png", "--material",
						   "models/red.png", "--material", "models/blue.png", "--output", output};
	ok &= expect(cli::runModelMaterialSlots(command + QStringList{"--dry-run"}).exitCode == 0 && !QFileInfo::exists(output),
				 "CLI dry-run validates repeated ordered materials without creating output");
	ok &= expect(cli::runModelMaterialSlots(command).exitCode == 0 && loaded.load(output, &error) &&
					 loaded.mesh().surfaces[1].skinPaths == QStringList{"models/blue.png", "models/red.png", "models/blue.png"},
				 "CLI replacement writes exactly the reviewed slot sequence");
	ok &= expect(cli::runModelMaterialSlots(command).exitCode == 1, "CLI protects an existing output");
	for (const auto &extra : QVector<QStringList>{{"--surface", "0"}, {"--slot", "0"}, {"--to", "0"}, {"--frame", "all"}, {"--unknown"}})
		ok &= expect(cli::runModelMaterialSlots(command + extra).exitCode == 2, "CLI rejects repeated, inapplicable and unknown options");
	for (const auto &operation : QVector<QStringList>{{"set", "--slot", "1", "--material", "models/blue.png"},
													  {"insert", "--slot", "3", "--material", "models/red.png"},
													  {"remove", "--slot", "1"},
													  {"move", "--slot", "2", "--to", "0"},
													  {"clear"}})
		ok &= expect(cli::runModelMaterialSlots(base + QStringList{"--surface", "1", "--operation"} + operation +
												QStringList{"--output", output, "--overwrite", "--dry-run"})
							 .exitCode == 0,
					 "CLI exposes every ordered-list operation");
	if (argc == 2)
	{
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		auto args = command.mid(1) +
					QStringList{"--dry-run", "--overwrite", "--json", "--settings-file", QDir(temporary.path()).filePath("settings.ini")};
		process.start(app.arguments()[1], args);
		const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
		const auto json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		ok &= expect(finished && process.exitCode() == 0 && json.value("surfaces").toArray().size() == 1 && !json.value("written").toBool(),
					 "real CLI dispatch preserves repeated material option values");
		const auto assets = QDir(temporary.path()).filePath("assets/models");
		ok &= expect(QDir().mkpath(assets), "create independent preview package");
		for (const auto &name : QStringList{"red", "blue"})
		{
			QFile file(QDir(assets).filePath(name + ".png"));
			const auto bytes = skinPng(name == "red" ? red : blue);
			ok &= expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write original test texture");
		}
		ok &= expect(document.setMesh(visible, &error) && document.save(output, true, &error), "save CLI preview source");
		args = {"--cli",
				"model",
				"materials",
				output,
				"--package",
				QDir(temporary.path()).filePath("assets"),
				"--surface",
				"0",
				"--material-slot",
				"1",
				"--json",
				"--settings-file",
				QDir(temporary.path()).filePath("settings.ini")};
		process.start(app.arguments()[1], args);
		const bool resolved = process.waitForStarted(10000) && process.waitForFinished(30000);
		const auto report = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		ok &= expect(resolved && process.exitCode() == 0 &&
						 report.value("materials").toObject().value("materials").toArray().first().toObject().value("name") ==
							 "models/blue.png" &&
						 report.value("previewSlots").toArray().first().toObject().value("slot").toInt() == 1,
					 "real CLI materials preview resolves the selected alternate through the shared service");
		for (int invalid = 0; invalid < 5; ++invalid)
		{
			auto rejected = args;
			int code = 2;
			if (invalid == 0)
				rejected.remove(rejected.indexOf("--surface"), 2);
			if (invalid == 1)
				rejected[rejected.indexOf("--material-slot") + 1] = "-1";
			if (invalid == 2)
				rejected.append(QStringList{"--material-slot", "0"});
			if (invalid == 3)
			{
				rejected[rejected.indexOf("--material-slot") + 1] = "3";
				code = 4;
			}
			if (invalid == 4)
			{
				rejected[rejected.indexOf("--surface") + 1] = "99";
				code = 4;
			}
			process.start(app.arguments()[1], rejected);
			const bool completed = process.waitForStarted(10000) && process.waitForFinished(30000);
			ok &= expect(completed && process.exitCode() == code &&
							 !QJsonDocument::fromJson(process.readAllStandardOutput()).object().isEmpty(),
						 "real CLI rejects unpaired, repeated, negative and unavailable preview overrides with structured diagnostics");
		}
		ok &= expect(loaded.load(output, &error) && surfaceBytes(loaded.mesh()) == surfaceBytes(visible),
					 "successful and rejected CLI previews leave the authored source intact");
	}
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " material slot assertions\n";
	return ok ? 0 : 1;
}
