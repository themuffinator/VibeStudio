#include "core/language_references.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool ok, const char* text) { if (!ok) { std::cerr << text << '\n'; } return ok; }
bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray utf16(const QString& text)
{
	QByteArray bytes("\xff\xfe", 2);
	for (const auto character : text) { bytes += char(character.unicode() & 0xff); bytes += char(character.unicode() >> 8); }
	return bytes;
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	bool ok = true;
	const auto saved = temp.filePath(QStringLiteral("a.cpp")), live = temp.filePath(QStringLiteral("b.cpp")), missing = temp.filePath(QStringLiteral("missing.cpp"));
	const QString source = QStringLiteral("int target;\nvoid f() { target; }\n// 🌍 target\n");
	ok &= expect(write(saved, utf16(source)) && write(live, "old saved source\n"), "write saved source fixtures");
	LanguageReferenceRequest request; request.rootPath = temp.path(); request.symbol = QStringLiteral("target"); request.provider = QStringLiteral("Fixture");
	request.buffers = {{live, QStringLiteral("live-id"), 7, QStringLiteral("target();\n"), QStringLiteral("UTF-8"), {}},
		{missing, QStringLiteral("missing-id"), 9, QStringLiteral("target();\n"), QStringLiteral("UTF-8"), {}}};
	request.references.items = {{saved, 0, 4, 0, 10}, {saved, 0, 4, 0, 10}, {live, 0, 0, 0, 6}, {missing, 0, 0, 0, 6}};
	auto report = prepareLanguageReferences(request);
	ok &= expect(report.succeeded() && report.matchCount == 3 && report.filesScanned == 3 && report.buffersScanned == 2 && !report.canApply() && report.changes.isEmpty(), "reference preparation deduplicates locations and never creates replacements");
	if (report.matches.size() != 3) { return 1; }
	ok &= expect(report.matches[0].textSha256 == assetTextSnapshotHash(source) && report.matches[0].endColumn == 11
		&& report.matches[1].bufferId == QStringLiteral("live-id") && report.matches[1].bufferRevision == 7
		&& report.matches[2].bufferId == QStringLiteral("missing-id"), "UTF-16 decoding and live/deleted snapshots preserve hash, version and range provenance");
	request.references.items = {{saved, 2, 4, 2, 5}, {saved, 0, 4, 0, 999}, {saved, 0, 4, 0, 4}};
	report = prepareLanguageReferences(request);
	ok &= expect(!report.complete && report.matchCount == 0 && report.referenceLocationsSkipped == 3, "invalid, empty and split-surrogate ranges are omitted explicitly");
	request.references.items = {{live, 0, 0, 0, 3}}; request.buffers[0].error = QStringLiteral("snapshot unavailable");
	report = prepareLanguageReferences(request);
	ok &= expect(!report.complete && report.matchCount == 0 && report.filesSkipped == 1 && report.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("snapshot unavailable")), "unavailable live snapshots cannot fall back to saved bytes");
	request.buffers[0].error.clear(); request.buffers << request.buffers[0];
	ok &= expect(!prepareLanguageReferences(request).complete, "ambiguous duplicate live snapshots are rejected"); request.buffers.removeLast();
	request.references.items = {{QDir(temp.path()).absoluteFilePath(QStringLiteral("../outside.cpp")), 0, 0, 0, 2}};
	report = prepareLanguageReferences(request);
	ok &= expect(!report.complete && report.filesScanned == 0 && report.referenceLocationsSkipped == 1, "reference previews never read outside the project");
	request.references.items = {{saved, 0, 4, 0, 10}}; request.references.limited = true; request.references.skipped = 2;
	report = prepareLanguageReferences(request);
	ok &= expect(!report.complete && report.matchCount == 1 && report.referenceLocationsSkipped == 2, "partial server provenance remains partial after snapshot preparation");
	request.references.limited = false; request.references.skipped = 0;
	request.references.items.fill({saved, 0, 4, 0, 10}, languageReferenceLimit + 1);
	report = prepareLanguageReferences(request);
	ok &= expect(!report.complete && report.matchCount == 1, "direct callers cannot bypass the reference location limit");
	request.references.items = {{saved, 0, 4, 0, 10}}; request.isCancelled = []() { return true; };
	report = prepareLanguageReferences(request);
	ok &= expect(report.cancelled && report.filesScanned == 0 && report.matches.isEmpty(), "pre-cancelled preparation does no file work"); request.isCancelled = {};
	ok &= expect(write(saved, QByteArray(textDocumentByteLimit + 1, 'a')), "write oversized source fixture");
	report = prepareLanguageReferences(request);
	ok &= expect(!report.complete && report.filesScanned == 0 && report.matchCount == 0, "oversized reference sources are rejected before reading");
	ok &= expect(write(saved, QByteArray(10000, 'x')), "write a source with a long preview line");
	request.references.items.clear();
	for (int at = 0; at < 300; ++at) { request.references.items << LanguageLocation {saved, 0, at, 0, at + 1}; }
	report = prepareLanguageReferences(request);
	ok &= expect(!report.complete && report.matchCount > 0 && report.matchCount < 300 && report.referenceLocationsSkipped == 300 - report.matchCount
		&& report.matches.first().rawLine.size() == 8193 && report.matches.first().textSha256 == assetTextSnapshotHash(QString(10000, QLatin1Char('x'))), "preview memory limits retain full-source hashes and report omitted locations");
	return ok ? 0 : 1;
}
