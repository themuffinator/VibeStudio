#include "app/model_preview_worker.h"
#include "cli/model_appearance.h"
#include "core/model_appearance.h"
#include "core/model_fingerprint.h"
#include "core/package_draft.h"
#include "tests/model_appearance_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0;
bool expect(bool value, const char* message) {
	++checks;
	if (!value) { std::cerr << "FAIL: " << message << '\n'; }
	return value;
}
bool waitFor(const std::function<bool()>& ready) {
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 15000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
	return ready();
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root) || argc != 2) { return 1; }
	QTemporaryDir temporary(QDir(root).filePath("model-appearance-XXXXXX"));
	if (!temporary.isValid()) { return 1; }
	ModelAppearanceFixture fixture;
	QString error;
	bool ok = expect(fixture.create(temporary.path(), &error), "write original native model, material, palette and skin fixtures");
	if (!ok) { std::cerr << error.toStdString() << '\n'; return 1; }
	PackageArchive archive;
	ok &= expect(archive.load(fixture.path("assets"), &error), "open ordinary package asset folder");
	const auto& mesh = fixture.mesh;
	const auto original = modelStateFingerprint(mesh);
	ModelAppearance request;
	ModelAppearanceSnapshot snapshot;
	ok &= expect(prepareModelAppearance(mesh, request, &archive, &snapshot, &error) &&
		snapshot.materials.surfaces[0].skinPaths == QStringList{"models/old_body"} &&
		snapshot.materials.surfaces[1].skinPaths == QStringList{"models/old_head"} &&
		snapshot.materials.surfaces[0].frames.isEmpty(), "default snapshot carries primary bindings without geometry copies");
	request.materialSlots.insert(0, 1);
	ok &= expect(prepareModelAppearance(mesh, request, &archive, &snapshot, &error) &&
		snapshot.materials.surfaces[0].skinPaths == QStringList{"models/alternate"} &&
		snapshot.materials.surfaces[1].skinPaths == QStringList{"models/old_head"}, "alternate material affects only its selected surface");
	const auto images = resolveModelPreviewAssets(snapshot.materials, archive);
	ok &= expect(images.readyCount() == 2 && images.materials[0].image.pixelColor(0, 0) == QColor(Qt::blue) &&
		images.materials[1].image.pixelColor(0, 0) == QColor(Qt::green), "shared material service resolves independently coloured selected bindings");
	request = {}; request.skin = ModelSkinSourceReference{"models/test.skin", -1};
	ok &= expect(prepareModelAppearance(mesh, request, &archive, &snapshot, &error) &&
		snapshot.materials.surfaces[0].skinPaths == QStringList{"models/new_body"} &&
		snapshot.materials.surfaces[1].skinPaths == QStringList{"models/new_head"}, "native .skin preview applies normalized surface mappings");
	const auto source = snapshot.receipt.value("skinSource").toObject();
	ok &= expect(source.value("sha256").toString().toLatin1() == QCryptographicHash::hash(skinBindings(), QCryptographicHash::Sha256).toHex() &&
		source.value("bytes").toInt() == skinBindings().size() && source.value("entryIndex").toInt(-1) >= 0,
		"receipt retains verified exact package identity and content hash");
	const auto kept = snapshot.receipt;
	for (const auto& path : {"missing.skin", "models/broken.skin"}) {
		request.skin = ModelSkinSourceReference{QString::fromLatin1(path), -1};
		ok &= expect(!prepareModelAppearance(mesh, request, &archive, &snapshot, &error) && !error.isEmpty() && snapshot.receipt == kept,
			"missing or incomplete skin cannot publish a partial appearance");
	}
	SkinReader duplicates;
	duplicates.add("same.skin", "body,models/wrong\nhead,models/wrong\n"); duplicates.add("same.skin", skinBindings());
	request.skin = ModelSkinSourceReference{"same.skin", -1};
	ok &= expect(!prepareModelAppearance(mesh, request, &duplicates, &snapshot, &error), "duplicate package paths need an exact occurrence");
	request.skin->entryIndex = 1;
	ok &= expect(prepareModelAppearance(mesh, request, &duplicates, &snapshot, &error) &&
		snapshot.materials.surfaces[0].skinPaths == QStringList{"models/new_body"}, "exact package occurrence determines preview bytes");
	request.skin->path = "other.skin";
	ok &= expect(!prepareModelAppearance(mesh, request, &duplicates, &snapshot, &error), "path guard rejects stale exact indexes");
	request.skin->path = "same.skin"; duplicates.lateFailure = true;
	ok &= expect(!prepareModelAppearance(mesh, request, &duplicates, &snapshot, &error), "late content verification failure is propagated");
	duplicates.lateFailure = false;
	const auto mdl = decodeModelMesh("test.mdl", groupedMdlFixture().bytes, &fixture.palette);
	const auto mdlOriginal = modelStateFingerprint(mdl);
	for (const auto &choice : {QPair{0, 0}, QPair{0, 1}, QPair{1, 0}}) {
		request = {}; request.mdlSkin = choice.first; request.mdlMember = choice.second;
		ok &= expect(prepareModelAppearance(mdl, request, nullptr, &snapshot, &error), "select a native embedded skin or grouped member");
		const auto& image = snapshot.materials.embeddedSkins.first().image;
		const int offset = choice.first == 1 ? 2 : choice.second;
		const int values[]{0, 1, 16, 31, 96, 127, 224, 255};
		for (int pixel = 0; pixel < 32; ++pixel) {
			ok &= expect(image.pixel(pixel % 8, pixel / 8) == fixture.palette.colors[values[(pixel + offset) % 8]],
				"embedded appearance pixels preserve original indices and resolved palette");
		}
		ok &= expect(snapshot.materials.embeddedSkins.size() == 1 && snapshot.materials.embeddedSkins[0].indexedFrames.isEmpty(),
			"prepared material snapshot excludes unused indexed payloads");
	}
	const auto beforeFailure = snapshot.receipt;
	request.mdlMember = 1;
	ok &= expect(!prepareModelAppearance(mdl, request, nullptr, &snapshot, &error) && snapshot.receipt == beforeFailure,
		"member selection cannot cross a single skin's bounds");
	request = {}; request.mdlSkin = 0;
	ok &= expect(!prepareModelAppearance(mesh, request, &archive, &snapshot, &error), "external models reject embedded controls");
	request = {}; request.materialSlots.insert(0, 0);
	ok &= expect(!prepareModelAppearance(mdl, request, &archive, &snapshot, &error), "embedded models reject external slot controls");
	request.skin = ModelSkinSourceReference{"models/test.skin", -1};
	ok &= expect(!prepareModelAppearance(mesh, request, &archive, &snapshot, &error), "skin file and slot modes cannot silently override each other");
	request = {}; request.materialSlots.insert(99, 0);
	ok &= expect(!prepareModelAppearance(mesh, request, &archive, &snapshot, &error), "invalid surface rejected");
	request.materialSlots = {{0, 999}};
	ok &= expect(!prepareModelAppearance(mesh, request, &archive, &snapshot, &error), "invalid slot rejected");
	ModelWorkControl cancellation; cancellation.cancelled = [] { return true; };
	ok &= expect(!prepareModelAppearance(mesh, {}, &archive, &snapshot, &error, cancellation) && snapshot.receipt == beforeFailure,
		"cancellation preserves the prior completed output");
	ok &= expect(modelStateFingerprint(mesh) == original && modelStateFingerprint(mdl) == mdlOriginal, "source geometry, animation, skins and metadata remain unchanged");
	const auto command = QStringList{"vibestudio", "--cli", "model", "materials", fixture.path("source.mesh.json"), "--package", fixture.path("assets"), "--json"};
	auto cli = cli::runModelAppearance(command + QStringList{"--entry", "models/test.skin"});
	ok &= expect(cli.exitCode == 0 && cli.data.value("appearance").toObject().value("skinSource").toObject().value("sha256") == source.value("sha256"),
		"CLI skin receipt matches browser service inputs");
	for (const auto& bad : QList<QStringList>{{"--member", "-1"}, {"--skin", "01"}, {"--entry-index", "2147483648"},
		{"--surface", "0"}, {"--skin", "0", "--skin", "1"}, {"--output", "unused"}, {"--dry-run"}}) {
		ok &= expect(cli::runModelAppearance(command + bad).exitCode == 2, "CLI rejects malformed, repeated and inapplicable options");
	}
	PackageStagingModel staging;
	ok &= expect(staging.loadBaseArchive(archive, &error) && staging.addBytes("body,models/alternate\nhead,models/new_head\n", "models/test.skin", &error,
		PackageStageConflictResolution::ReplaceExisting) && PackageDraft::save(fixture.path("assets.vibepackage"), &staging, false, &error),
		"retain portable draft with staged appearance bindings");
	auto draftCommand = command; draftCommand[draftCommand.indexOf("--package") + 1] = fixture.path("assets.vibepackage");
	cli = cli::runModelAppearance(draftCommand + QStringList{"--entry", "models/test.skin"});
	ok &= expect(cli.exitCode == 0 && cli.data.value("appearance").toObject().value("surfaces").toArray()[0].toObject().value("material") == "models/alternate",
		"appearance review sees staged draft bytes");
	for (bool embedded : {false, true}) {
		auto args = command.mid(1);
		if (embedded) { args[3] = fixture.path("assets/models/test.mdl"); args << "--skin" << "0" << "--member" << "1"; }
		else { args << "--entry" << "models/test.skin"; }
		QProcess process; process.setWorkingDirectory(temporary.path()); process.start(QString::fromLocal8Bit(argv[1]), args);
		const bool finished = process.waitForFinished(30000);
		const auto json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		ok &= expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
			json.value("appearance").toObject().value("previewOnly").toBool(), "actual CLI dispatch publishes appearance receipt");
		if (embedded) {
			const auto selected = json.value("appearance").toObject().value("embeddedSkin").toObject();
			ok &= expect(selected.value("paletteSha256").toString().toLatin1() ==
				QCryptographicHash::hash(fixture.files.value("assets/gfx/palette.lmp"), QCryptographicHash::Sha256).toHex(),
				"CLI native MDL inspection uses the package's original palette bytes");
			ok &= expect(selected.value("indexedSha256").toString().toLatin1() ==
				QCryptographicHash::hash(mdl.embeddedSkins[0].indexedFrames[1], QCryptographicHash::Sha256).toHex(),
				"CLI native MDL receipt identifies the exact selected group member");
		}
	}
	// Queue replacement/cancel while reading slow input, without touching OS input.
	auto slow = std::make_shared<SkinReader>();
	slow->add("test.md3", fixture.files.value("assets/models/test.md3")); slow->delay = true;
	auto fast = std::make_shared<PackageArchive>(archive);
	ModelPreviewWorker worker;
	QStringList completed;
	bool cancelled = false, settled = false;
	worker.completed = [&](const ModelPreviewResult& result) { completed << result.key; cancelled |= result.cancelled; };
	worker.settled = [&] { settled = true; };
	worker.request({slow, "slow", {}}, "test.md3", "retired");
	ok &= expect(waitFor([&] { return slow->entered.load(); }) && !slow->readOnGui, "geometry source reads run off the main thread");
	worker.request({fast, "current", {}}, "models/test.md3", "current");
	ok &= expect(waitFor([&] { return !worker.busy(); }) && completed == QStringList{"current"}, "retired results cannot overwrite a newer appearance request");
	slow->entered = false; completed.clear(); settled = false;
	worker.request({slow, "slow-again", {}}, "test.md3", "cancelled");
	ok &= expect(waitFor([&] { return slow->entered.load(); }), "cancel test observes a live read");
	worker.cancel();
	ok &= expect(cancelled && waitFor([&] { return !worker.busy(); }) && settled && completed == QStringList{"cancelled"},
		"cancel reports once and notifies when retired worker settles");
	ok &= expect(fixture.unchanged(), "all source models, palettes, textures and bindings remain byte-for-byte unchanged");
	std::cout << checks << " model appearance core/CLI checks\n";
	return ok ? 0 : 1;
}
