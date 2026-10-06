#include "core/map_assets.h"
#include "core/package_draft.h"
#include "core/level_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
LevelMapDocument map()
{
	LevelMapDocument document; document.format = LevelMapFormat::Quake3Map; document.engineFamily = QStringLiteral("idTech3");
	LevelMapBrush brush; LevelMapBrushFace face; face.textureName = QStringLiteral("studio/wall"); brush.faces = {face}; document.brushes = {brush};
	return document;
}
class Reader final : public PackageArchiveReader {
public:
	QByteArray bytes = "textures/studio/wall { { map textures/studio/image } }";
	mutable int reads = 0;
	bool fail = false, oversized = false, unavailable = false, withImage = false, mismatch = false;
	QString path = QStringLiteral("scripts/studio.shader");
	int scriptCount = 1; quint64 declaredBytes = 0;
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Pk3; }
	QString sourcePath() const override { return QStringLiteral("texture-audit-fixture"); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override {
		PackageEntry entry; entry.virtualPath = path; entry.sizeBytes = oversized ? 17 * 1024 * 1024 : bytes.size() + int(mismatch); entry.readable = !unavailable; if (declaredBytes) { entry.sizeBytes = declaredBytes; }
		QVector<PackageEntry> result;
		for (int at = 0; at < scriptCount; ++at) { auto item = entry; if (scriptCount > 1) { item.virtualPath = QStringLiteral("scripts/%1.shader").arg(at); } result << item; }
		if (withImage) { entry.virtualPath = QStringLiteral("textures/studio/wall.png"); entry.readable = false; result << entry; }
		return result;
	}
	bool readEntryBytes(const QString&, QByteArray* out, QString* error, qint64) const override {
		++reads; if (fail) { if (error) { *error = QStringLiteral("fixture read failure"); } return false; } *out = bytes; return true;
	}
	bool streamEntryAt(qsizetype, const std::function<bool(QByteArrayView)>& sink, QString* error, const std::function<bool()>& cancelled) const override {
		++reads;
		for (qsizetype at = 0; at < bytes.size(); at += 65536) {
			if ((cancelled && cancelled()) || !sink(QByteArrayView(bytes).sliced(at, qMin<qsizetype>(65536, bytes.size() - at)))) { return false; }
		}
		if (fail && error) { *error = QStringLiteral("fixture final integrity failure"); }
		return !fail;
	}
};
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); bool ok = true; QString error;
	Reader reader;
	const auto initial = auditLevelMapTextures(map(), reader, false);
	ok &= expect(initial.complete && initial.resolvedCount == 1 && initial.missingCount == 0, "shader declaration resolves the map reference");
	reader.fail = true;
	const auto failed = auditLevelMapTextures(map(), reader, false);
	ok &= expect(!failed.complete && failed.state() == OperationState::Warning && !failed.warnings.isEmpty()
		&& !mapTextureAuditJson(failed).value("complete").toBool(), "failed shader reads explicitly make the audit incomplete");
	reader.fail = false; reader.unavailable = true; reader.reads = 0;
	ok &= expect(!auditLevelMapTextures(map(), reader, false).complete && reader.reads == 0, "known unavailable shader rows are refused before reads");
	reader.path = QStringLiteral("textures/studio/wall.png");
	const auto unavailableImage = auditLevelMapTextures(map(), reader, false);
	ok &= expect(!unavailableImage.complete && unavailableImage.missingCount == 1 && unavailableImage.resolvedCount == 0, "unavailable original image metadata cannot claim successful resolution");
	reader.path = QStringLiteral("scripts/studio.shader"); reader.unavailable = false; reader.withImage = true;
	const auto precedence = auditLevelMapTextures(map(), reader, false);
	ok &= expect(precedence.complete && precedence.references.first().resolution == MapTextureResolution::ResolvedByShader,
		"a valid shader definition precedes a same-named unavailable image");
	reader.withImage = false; reader.mismatch = true;
	ok &= expect(!auditLevelMapTextures(map(), reader, false).complete, "generic full shader reads must match their declared size");
	reader.mismatch = false; reader.oversized = true; reader.reads = 0;
	ok &= expect(!auditLevelMapTextures(map(), reader, false).complete && reader.reads == 0, "oversized shader admission refuses before payload reads");
	reader.oversized = false;
	PackageReadControl control; control.isCancelled = [] { return true; };
	const auto cancelled = auditLevelMapTextures(map(), reader, false, control);
	ok &= expect(cancelled.cancelled && !cancelled.complete && cancelled.state() == OperationState::Cancelled && reader.reads == 0, "pre-cancelled audits read no input and never claim completeness");
	reader.bytes = QByteArray(200000, ' ') + "textures/studio/wall {}";
	bool stop = false; control.isCancelled = [&] { return stop; };
	control.progress = [&](const QString&, qint64 done, qint64) { if (done >= 65536) { stop = true; } };
	const auto interrupted = auditLevelMapTextures(map(), reader, false, control);
	ok &= expect(interrupted.cancelled && !interrupted.complete && interrupted.resolvedCount == 0 && reader.reads == 1, "mid-stream cancellation discards the partial shader");
	stop = false; control.progress = {}; reader.fail = true;
	const auto integrity = auditLevelMapTextures(map(), reader, false, control);
	ok &= expect(!integrity.complete && integrity.resolvedCount == 0, "a final integrity failure cannot publish buffered shader declarations");
	QStringList diagnostics; collectShaderScriptNames(QByteArray(20000, '}'), &diagnostics);
	ok &= expect(diagnostics.size() == 129 && diagnostics.last().contains("truncated"), "hostile shader diagnostics have a fixed retained bound");
	Reader budget; budget.fail = true; budget.declaredBytes = 16 * 1024 * 1024; budget.scriptCount = 5;
	const auto bounded = auditLevelMapTextures(map(), budget, false);
	ok &= expect(!bounded.complete && budget.reads == 4 && bounded.warnings.join(' ').contains("aggregate byte limit"), "aggregate shader admission stops before reading beyond 64 MiB");
	int checks = 0; control.isCancelled = [&] { return ++checks >= 20; };
	ok &= expect(collectShaderScriptNames(QByteArray(100000, 'a') + " {}", nullptr, control).isEmpty() && checks >= 20, "a single long token remains cancellable");

	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3);
	const QString script = QStringLiteral("scripts/studio.shader");
	ok &= expect(plan.addBytes("textures/studio/wall {}", script, &error), "stage a generated shader");
	ok &= expect(auditLevelMapTextures(map(), PackageStagingArchive(plan), false).resolvedCount == 1, "planned generated bytes resolve before archive export");
	ok &= expect(plan.renameEntry(script, QStringLiteral("docs/studio.txt"), &error), "stage shader removal from its namespace");
	ok &= expect(auditLevelMapTextures(map(), PackageStagingArchive(plan), false).missingCount == 1, "staged shader rename changes lookup immediately");
	ok &= expect(plan.undo() && auditLevelMapTextures(map(), PackageStagingArchive(plan), false).resolvedCount == 1, "Undo restores the exact shader content");
	const QString draft = temporary.filePath(QStringLiteral("planned.vibepackage"));
	ok &= expect(PackageDraft::save(draft, &plan, false, &error), "save a portable planned texture source");
	if (argc > 1) {
		LevelMapCreateRequest request; request.game = QStringLiteral("quake3");
		request.wallTexture = request.floorTexture = request.ceilingTexture = QStringLiteral("studio/wall");
		LevelMapDocument document; ok &= expect(createLevelMap(request, &document, &error), "create synthetic CLI map");
		LevelDocumentSaveRequest save; save.path = temporary.filePath(QStringLiteral("context.map"));
		ok &= expect(writeLevelDocument(document, save).succeeded(), "save synthetic CLI map");
		const auto cli = [&](int expected) {
			QProcess process; process.start(QString::fromLocal8Bit(argv[1]), {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("textures"), save.path,
				QStringLiteral("--package"), draft, QStringLiteral("--no-decode"), QStringLiteral("--json")});
			if (!process.waitForFinished(30000)) { process.kill(); process.waitForFinished(); return false; }
			const auto json = QJsonDocument::fromJson(process.readAllStandardOutput()).object().value("textures").toObject();
			return process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && json.value("complete").toBool()
				&& json.value("totals").toObject().value("missing").toInt() == (expected == 0 ? 0 : 1);
		};
		ok &= expect(cli(0), "CLI texture checks read saved planned drafts");
		ok &= expect(plan.redo() && PackageDraft::save(draft, &plan, true, &error) && cli(4), "CLI reports the renamed-away draft shader as missing");
	}
	return ok ? 0 : 1;
}
