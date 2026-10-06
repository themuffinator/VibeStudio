#include "core/ericw_map_preflight.h"
#include "core/map_geometry_cache.h"
#include "core/map_preview_mesh.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) { std::cerr << message << ": " << error.toStdString() << '\n'; }
	return value;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
void put32(QByteArray& bytes, qsizetype at, qint32 value)
{
	qToLittleEndian<qint32>(value, bytes.data() + at);
}
QByteArray udmfFixture()
{
	QByteArray bytes(12 + 3 * 16, '\0'); bytes.replace(0, 4, "PWAD"); put32(bytes, 4, 3); put32(bytes, 8, 12);
	for (int i = 0; i < 3; ++i) { put32(bytes, 12 + i * 16, 12); }
	bytes.replace(20, 5, "MAP01"); bytes.replace(36, 7, "TEXTMAP"); bytes.replace(52, 6, "ENDMAP");
	return bytes;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temp;
	LevelMapDocument fixture; QString error;
	if (!temp.isValid() || !tests::createGeometryFixture(96, &fixture, &error)) { return 1; }
	const auto bytes = serializeLevelMap(fixture).bytes;
	const auto path = temp.filePath(QStringLiteral("load.map"));
	bool ok = expect(write(path, bytes), "write original map");
	LevelMapDocument baseline, loaded;
	ok &= expect(loadLevelMap({path, {}, {}}, &baseline, &error), "load without callbacks", error);
	MapBrushGeometryCache prepared;
	LevelMapLoadRequest request {path, {}, {}};
	request.brushGeometryCache = &prepared;
	QSet<int> phases;
	request.progress = [&](LevelMapLoadPhase phase, qint64 done, qint64 total) {
		phases.insert(int(phase));
		ok &= expect(done >= 0 && total >= 0 && (total == 0 || done <= total), "valid per-phase progress bounds");
	};
	ok &= expect(loadLevelMap(request, &loaded, &error), "load with progress and geometry", error);
	ok &= expect(serializeLevelMap(loaded).bytes == serializeLevelMap(baseline).bytes
		&& loaded.sourceContentHash == QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
		&& loaded.issues.size() == baseline.issues.size(), "callbacks preserve parsed output, diagnostics and fingerprint");
	ok &= expect(phases.size() == 8 && prepared.statistics().solved == 96 && prepared.statistics().retainedBrushes == 96,
		"every text loading phase reported and every valid brush prepared");
	prepared.build(loaded);
	ok &= expect(prepared.statistics().solved == 0 && prepared.statistics().reused == 96, "prepared geometry requires no second solve");

	LevelMapDocument kept = fixture;
	ok &= setLevelMapSelection(&kept, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error) && moveLevelMapSelection(&kept, 8, 0, 0, &error);
	const auto keptBytes = serializeLevelMap(kept).bytes;
	const auto keptRevision = kept.revision;
	const auto unchanged = [&] {
		return kept.revision == keptRevision && kept.undoStack.size() == 1 && kept.selection.size() == 1
			&& serializeLevelMap(kept).bytes == keptBytes && kept.sourcePath == fixture.sourcePath;
	};
	for (const auto phase : {LevelMapLoadPhase::Reading, LevelMapLoadPhase::Indexing, LevelMapLoadPhase::Tokenizing,
		LevelMapLoadPhase::Parsing, LevelMapLoadPhase::Solving, LevelMapLoadPhase::Validating, LevelMapLoadPhase::Hashing, LevelMapLoadPhase::Complete}) {
		bool stop = false;
		request.isCancelled = [&] { return stop; };
		request.progress = [&](LevelMapLoadPhase current, qint64 done, qint64) {
			if (current == phase && (done > 0 || phase == LevelMapLoadPhase::Hashing)) { stop = true; }
		};
		ok &= expect(!loadLevelMap(request, &kept, &error) && stop && error.contains(QStringLiteral("cancelled")) && unchanged(),
			"cancellation in each phase preserves the prior document and undo", error);
	}

	for (const QByteArray& large : {QByteArray("//") + QByteArray(256 * 1024, 'x') + '\n' + bytes,
		QByteArray("{\n\"classname\" \"worldspawn\"\n{\n( ") + QByteArray("1 ").repeated(12000) + ")\n}\n}\n"}) {
		bool stop = false;
		request.isCancelled = [&] { return stop; };
		request.progress = [&](LevelMapLoadPhase phase, qint64 done, qint64) {
			if ((large.startsWith("//") && phase == LevelMapLoadPhase::Tokenizing && done >= 4096)
				|| (!large.startsWith("//") && phase == LevelMapLoadPhase::Parsing && done >= 1024)) { stop = true; }
		};
		ok &= expect(!loadLevelMapBytes(request, large, &kept, &error) && stop && unchanged(),
			"long comment and malformed numeric group remain cancellable", error);
	}

	// Cancellation must also interrupt a single expensive brush, retaining the
	// previous valid cache entry rather than replacing it with partial geometry.
	auto complex = loaded.brushes.front();
	for (int i = 0; i < 512; ++i) { complex.faces.append(loaded.brushes.front().faces[i % 6]); }
	int checks = 0;
	auto geometry = prepared.resolve(complex, MapGeometryPrecision::CompilerCompatible, [&] { return ++checks >= 20; });
	ok &= expect(geometry.cancelled && !geometry.solved && geometry.faces.isEmpty(), "cancel inside one brush");
	prepared.beginBuild(loaded); prepared.resolve(loaded.brushes.front());
	ok &= expect(prepared.statistics().solved == 0 && prepared.statistics().reused == 1, "cancelled solve preserves prior cached geometry");
	LevelMapDocument complexMap = loaded; complexMap.brushes = {complex}; checks = 0;
	LevelMapPreviewMeshOptions previewOptions; previewOptions.isCancelled = [&] { return ++checks >= 20; };
	ok &= expect(buildLevelMapPreviewMesh(complexMap, previewOptions).cancelled, "camera shares cancellation inside one brush");

	for (const QString& game : {QStringLiteral("doom"), QStringLiteral("hexen"), QStringLiteral("udmf")}) {
		LevelMapDocument doom; LevelMapCreateRequest create; create.game = game;
		QByteArray wad;
		if (game == QStringLiteral("udmf")) { wad = udmfFixture(); }
		else { ok &= createLevelMap(create, &doom, &error); wad = serializeLevelMap(doom).bytes; }
		LevelMapLoadRequest wadRequest {temp.filePath(game + QStringLiteral(".wad")), {}, {}};
		ok &= expect(loadLevelMapBytes(wadRequest, wad, &doom, &error)
			&& levelMapNamesInWadDocument(doom) == QStringList{QStringLiteral("MAP01")}, "loaded WAD lists map names without I/O", error);
		ok &= expect(write(wadRequest.path, wad) && levelMapNamesInWad(wadRequest.path, &error) == levelMapNamesInWadDocument(doom),
			"directory-only WAD listing agrees with loaded records", error);
		if (game != QStringLiteral("udmf")) { ok &= expect(serializeLevelMap(doom).bytes == wad, "Doom and Hexen round trip unchanged"); }
		for (const auto phase : {LevelMapLoadPhase::Indexing, LevelMapLoadPhase::Parsing, LevelMapLoadPhase::Hashing, LevelMapLoadPhase::Complete}) {
			bool stop = false; wadRequest.isCancelled = [&] { return stop; };
			wadRequest.progress = [&](LevelMapLoadPhase current, qint64, qint64) { if (current == phase) { stop = true; } };
			ok &= expect(!loadLevelMapBytes(wadRequest, wad, &kept, &error) && stop && unchanged(), "binary/UDMF cancellation preserves current text document");
		}
	}

	// Overlapping payloads are legal, but their total copied size must be bounded
	// before allocation. This one-MiB file advertises 513 MiB of repeated data.
	QByteArray amplified(12 + 1024 * 1024 + 513 * 16, '\0'); amplified.replace(0, 4, "PWAD");
	put32(amplified, 4, 513); put32(amplified, 8, 12 + 1024 * 1024);
	for (int i = 0; i < 513; ++i) { put32(amplified, 12 + 1024 * 1024 + i * 16, 12); put32(amplified, 16 + 1024 * 1024 + i * 16, 1024 * 1024); }
	ok &= expect(!loadLevelMapBytes({QStringLiteral("amplified.wad"), {}, {}}, amplified, &kept, &error)
		&& error.contains(QStringLiteral("512 MiB")) && unchanged(), "expanded WAD payload bound enforced", error);

	EricwMapPreflightOptions preflight; checks = 0;
	preflight.isCancelled = [&] { return ++checks > 15; };
	const auto report = validateEricwMapPreflightText(QString::fromUtf8(bytes), preflight);
	ok &= expect(report.cancelled && !report.parseComplete && report.warnings.isEmpty(), "cancelled preflight publishes no partial diagnostics");
	checks = 0;
	ok &= expect(validateEricwMapPreflightFile(path, preflight).cancelled, "file preflight propagates cancellation");

	request.isCancelled = {}; request.brushGeometryCache = nullptr;
	bool changed = false;
	request.progress = [&](LevelMapLoadPhase phase, qint64, qint64) {
		if (changed || phase != LevelMapLoadPhase::Solving) { return; }
		changed = true;
		QFile file(path);
		ok &= expect(file.open(QIODevice::ReadWrite) && file.setFileTime(QFileInfo(path).lastModified().addSecs(10), QFileDevice::FileModificationTime),
			"change source timestamp without changing size");
	};
	ok &= expect(!loadLevelMap(request, &kept, &error) && changed && error.contains(QStringLiteral("source changed")) && unchanged(),
		"source metadata captured before parsing detects a concurrent change", error);
	request = {temp.filePath(QStringLiteral("missing.map")), {}, {}};
	ok &= expect(!loadLevelMap(request, &kept, &error) && unchanged(), "read failure preserves the working document");
	return ok ? 0 : 1;
}
