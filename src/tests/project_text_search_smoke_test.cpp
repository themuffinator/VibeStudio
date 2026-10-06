#include "core/project_text_search.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>

using namespace vibestudio;
namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonObject cli(const QString& binary, const QStringList& args, int exitCode, bool& ok)
{
	QProcess process;
	process.start(binary, QStringList {QStringLiteral("--cli")} + args + QStringList {QStringLiteral("--json")});
	if (!process.waitForFinished(15000)) { process.kill(); process.waitForFinished(); }
	ok &= expect(process.exitStatus() == QProcess::NormalExit && process.exitCode() == exitCode, "CLI exit code must reflect the result");
	const auto document = QJsonDocument::fromJson(process.readAllStandardOutput());
	ok &= expect(document.isObject(), "CLI must return valid JSON");
	return document.object();
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	bool ok = true;
	const QDir root(temp.filePath(QStringLiteral("project with spaces")));
	const QString first = root.filePath(QStringLiteral("scripts/café.cfg"));
	const QString second = root.filePath(QStringLiteral("src/main.cpp"));
	const QByteArray original = QByteArray::fromHex("efbbbf") + QStringLiteral("// café\r\nplayer player_run Player\n  player\rplayer").toUtf8();
	ok &= expect(writeFile(first, original) && writeFile(second, "void player() {}\n"), "write UTF-8 fixtures");
	for (const auto& directory : {QStringLiteral(".git"), QStringLiteral("build"), QStringLiteral("builddir-cl"), QStringLiteral(".agents"), QStringLiteral(".vibestudio"), QStringLiteral("node_modules")}) {
		ok &= expect(writeFile(root.filePath(directory + QStringLiteral("/nested/ignored.cfg")), "player\n"), "write excluded subtree fixture");
	}
	ok &= expect(writeFile(root.filePath(QStringLiteral("invalid.cfg")), QByteArray("player\xc3", 7))
		&& writeFile(root.filePath(QStringLiteral("binary.cfg")), QByteArray("player\0text", 11)), "write unsupported text fixtures");
	AssetTextSearchRequest request;
	request.rootPath = root.path();
	request.findText = QStringLiteral("player");
	request.caseSensitive = true;
	request.wholeWords = true;
	auto search = findReplaceProjectText(request);
	ok &= expect(search.succeeded() && search.complete && search.matchCount == 4 && search.filesWithMatches == 2 && search.filesSkipped == 2,
		"whole identifier search must include C++ and skip binary, malformed UTF-8, and complete excluded subtrees");
	ok &= expect(search.matches.at(0).line == 2 && search.matches.at(0).column == 1
		&& search.matches.at(1).line == 3 && search.matches.at(1).column == 3 && search.matches.at(2).line == 4,
		"BOM, CRLF, LF and lone CR must produce editor-compatible locations");
	request.caseSensitive = false;
	ok &= expect(findReplaceProjectText(request).matchCount == 5, "case-insensitive whole words must include differently cased matches");
	request.wholeWords = false;
	ok &= expect(findReplaceProjectText(request).matchCount == 6, "literal substring search must remain available");
	request.wholeWords = true;
	request.caseSensitive = true;
	request.includeGlobs = {QStringLiteral("*.CPP"), QStringLiteral("scripts/*")};
	request.excludeGlobs = {QStringLiteral("src/")};
	ok &= expect(findReplaceProjectText(request).matchCount == 3, "include and exclude globs must support basename, relative paths, and trailing slashes");
	for (const auto& glob : {QStringLiteral("s*/*.c?g"), QStringLiteral("*/caf[é].cfg"), QStringLiteral("scripts/[a-d]af[!x].cfg")}) {
		request.includeGlobs = {glob};
		ok &= expect(findReplaceProjectText(request).matchCount == 3, "portable globs retain question marks, Unicode, ranges, and negated classes");
	}
	request.includeGlobs = {QStringLiteral("s*fg")};
	ok &= expect(findReplaceProjectText(request).matchCount == 0, "slash-free globs match the basename only");
	request.includeGlobs = {QStringLiteral("*/caf?.cf?")};
	ok &= expect(findReplaceProjectText(request).matchCount == 3, "relative-path wildcards span the complete path");
	{
		const QDir globRoot(temp.filePath(QStringLiteral("glob fixtures")));
		ok &= expect(writeFile(globRoot.filePath(QStringLiteral("nested/deeper/café.cfg")), "player\n"), "write nested wildcard fixture");
		auto globRequest = request; globRequest.rootPath = globRoot.path(); globRequest.excludeGlobs.clear();
		for (const auto& glob : {QStringLiteral("nested/*.cfg"), QStringLiteral("nested/d??per/caf[é].cfg"), QStringLiteral("nested/*/[a-d]af[!x].cfg")}) {
			globRequest.includeGlobs = {glob};
			ok &= expect(findReplaceProjectText(globRequest).matchCount == 1, "non-path wildcard semantics work across nested directories on each Qt version");
		}
		globRequest.includeGlobs = {QStringLiteral("nested/*/caf[!é].cfg")};
		ok &= expect(findReplaceProjectText(globRequest).matchCount == 0, "negated character classes reject the excluded Unicode character");
	}
	request.includeGlobs.clear();
	request.excludeGlobs.clear();
	request.replace = true;
	request.replaceText = QStringLiteral("enemy");
	auto preview = findReplaceProjectText(request);
	ok &= expect(preview.canApply() && preview.changes.size() == 2 && preview.replacementCount == 4 && readFile(first) == original,
		"preview must prepare bytes without changing files");
	ok &= expect(preview.matches[0].replacementLine == QStringLiteral("enemy player_run Player"), "preview must show all replacements on the line with the chosen match options");
	// A same-size edit with a restored timestamp must still block the whole batch.
	const auto timestamp = QFileInfo(second).lastModified();
	ok &= expect(writeFile(second, "void health() {}\n"), "change a reviewed source");
	QFile modified(second);
	if (modified.open(QIODevice::ReadWrite)) { modified.setFileTime(timestamp, QFileDevice::FileModificationTime); modified.close(); }
	auto result = applyProjectTextReplacements(preview);
	ok &= expect(!result.succeeded() && result.writtenFiles.isEmpty() && readFile(first) == original, "preflight must reject any changed file before writing the first file");
	ok &= expect(writeFile(second, "void player() {}\n"), "restore reviewed bytes");
	const QString added = root.filePath(QStringLiteral("added.cfg"));
	ok &= expect(writeFile(added, "player\n"), "add an unreviewed match after preview");
	result = applyProjectTextReplacements(preview);
	QByteArray expected = QByteArray::fromHex("efbbbf") + QStringLiteral("// café\r\nenemy player_run Player\n  enemy\renemy").toUtf8();
	ok &= expect(result.succeeded() && result.writtenFiles.size() == 2 && result.replacementsApplied == 4
		&& readFile(first) == expected && readFile(added) == QByteArray("player\n"), "apply must preserve BOM/newlines and never rescan into an unreviewed file");
	ok &= expect(!applyProjectTextReplacements(preview).succeeded(), "a preview cannot silently be reused after its files change");
	request.includeGlobs = {QStringLiteral("scripts/*")};
	request.findText = QStringLiteral("enemy");
	request.replaceText.clear();
	preview = findReplaceProjectText(request);
	result = applyProjectTextReplacements(preview);
	ok &= expect(result.succeeded() && readFile(first) == QByteArray::fromHex("efbbbf") + QStringLiteral("// café\r\n player_run Player\n  \r").toUtf8(),
		"an explicitly enabled empty replacement must delete matches without normalizing bytes");
	// Unicode combining marks remain part of an identifier.
	ok &= expect(writeFile(first, QStringLiteral("playeŕ player\U00010400 \U00010400player player\n").toUtf8()), "write Unicode identifier fixture");
	request.findText = QStringLiteral("player");
	request.replaceText = QStringLiteral("enemy");
	ok &= expect(findReplaceProjectText(request).matchCount == 1, "whole identifiers must not split combining marks or supplementary-plane letters");
	ok &= expect(writeFile(first, "player player\n"), "restore limit fixture");
	request.maxMatches = 1;
	preview = findReplaceProjectText(request);
	ok &= expect(!preview.complete && !preview.canApply() && preview.matchCount == 1 && !applyProjectTextReplacements(preview).succeeded(),
		"truncated match results must never permit replacement");
	request.maxMatches = 10000;
	request.maxFileBytes = 4;
	preview = findReplaceProjectText(request);
	ok &= expect(!preview.complete && !preview.warnings.isEmpty() && !preview.canApply(), "oversized files must make incompleteness explicit");
	request.maxFileBytes = 4ll * 1024 * 1024;
	request.maxTotalBytes = 4;
	ok &= expect(!findReplaceProjectText(request).complete, "total byte budget must bound a scan");
	request.maxTotalBytes = 64ll * 1024 * 1024;
	request.includeGlobs.clear();
	request.maxFiles = 1;
	ok &= expect(!findReplaceProjectText(request).complete, "file count budget must bound a scan");
	request.maxFiles = 20000;
	bool cancel = false;
	request.progress = [&](int scanned, int) { if (scanned > 0) { cancel = true; } };
	request.isCancelled = [&]() { return cancel; };
	preview = findReplaceProjectText(request);
	ok &= expect(preview.cancelled && !preview.complete && !preview.canApply(), "cancellation must preserve an explicit incomplete result");
	request.progress = {};
	request.isCancelled = {};
	request.includeGlobs = {QStringLiteral("scripts/*"), QStringLiteral("src/*")};
	ok &= expect(writeFile(first, "player\n") && writeFile(second, "player\n"), "write cancellation batch");
	preview = findReplaceProjectText(request);
	cancel = false;
	result = applyProjectTextReplacements(preview, [&]() { return cancel; }, [&](int written, int) { if (written == 1) { cancel = true; } });
	ok &= expect(result.cancelled && result.writtenFiles.size() == 1 && result.replacementsApplied == 1
		&& readFile(first) == QByteArray("enemy\n") && readFile(second) == QByteArray("player\n"), "cancelling a write must identify exactly the files already saved");
	ok &= expect(writeFile(first, "player\n"), "restore partially written batch");
	preview = findReplaceProjectText(request);
	result = applyProjectTextReplacements(preview, {}, [&](int written, int) { if (written == 1) { writeFile(second, "health\n"); } });
	ok &= expect(!result.succeeded() && result.writtenFiles.size() == 1 && readFile(second) == QByteArray("health\n"),
		"per-file checks must detect a change after preflight and leave its new contents intact");
	// Even a malformed in-process proposal cannot escape its project root.
	const QString outside = temp.filePath(QStringLiteral("outside.cfg"));
	ok &= expect(writeFile(first, "player\n") && writeFile(second, "player\n") && writeFile(outside, "player\n"), "write path fixtures");
	preview = findReplaceProjectText(request);
	preview.changes[0].filePath = outside;
	ok &= expect(!applyProjectTextReplacements(preview).succeeded() && readFile(outside) == QByteArray("player\n"), "replacement paths must stay inside the reviewed root");
	std::error_code linkError;
	const auto nativePath = [](const QString& path) {
#ifdef Q_OS_WIN
		return std::filesystem::path(path.toStdWString());
#else
		return std::filesystem::path(path.toStdString());
#endif
	};
	const QString link = root.filePath(QStringLiteral("link.cfg"));
	std::filesystem::create_symlink(nativePath(outside), nativePath(link), linkError);
	if (!linkError) {
		request.includeGlobs = {QStringLiteral("link.cfg")};
		search = findReplaceProjectText(request);
		ok &= expect(search.matchCount == 0 && search.filesSkipped > 0, "file links must not be searched or rewritten");
		std::filesystem::remove(nativePath(link), linkError);
	} else {
		std::cout << "Symlink fixture unavailable on this host: " << linkError.message() << '\n';
	}
	const QDir unicodeRoot(temp.filePath(QStringLiteral("unicode")));
	const QString littlePath = unicodeRoot.filePath(QStringLiteral("little.cfg"));
	const QString bigPath = unicodeRoot.filePath(QStringLiteral("big.cfg"));
	const QByteArray littleBytes = QByteArray::fromHex("fffe61000d000a0061000a006100");
	const QByteArray bigBytes = QByteArray::fromHex("feff0061000d000a0061000a0061");
	ok &= expect(writeFile(littlePath, littleBytes) && writeFile(bigPath, bigBytes), "write BOM-marked UTF-16 fixtures");
	AssetTextSearchRequest unicodeRequest;
	unicodeRequest.rootPath = unicodeRoot.path();
	unicodeRequest.findText = QStringLiteral("a");
	unicodeRequest.replace = true;
	unicodeRequest.replaceText = QStringLiteral("b");
	const auto unicodePreview = findReplaceProjectText(unicodeRequest);
	ok &= expect(unicodePreview.canApply() && unicodePreview.matchCount == 6 && unicodePreview.matches[1].line == 2
		&& !unicodePreview.matches[0].encoding.isEmpty(), "UTF-16 search must share editor line locations and report its encoding");
	result = applyProjectTextReplacements(unicodePreview);
	ok &= expect(result.succeeded() && readFile(littlePath) == QByteArray::fromHex("fffe62000d000a0062000a006200")
		&& readFile(bigPath) == QByteArray::fromHex("feff0062000d000a0062000a0062"), "UTF-16 replacements retain byte order, BOM, mixed endings and repeated-line positions");
	const QDir liveRoot(temp.filePath(QStringLiteral("live")));
	const QString livePath = liveRoot.filePath(QStringLiteral("open.cfg"));
	const QString diskPath = liveRoot.filePath(QStringLiteral("disk.cfg"));
	const QString deletedPath = liveRoot.filePath(QStringLiteral("removed.cfg"));
	ok &= expect(writeFile(livePath, "stale live\n") && writeFile(diskPath, "live\n"), "write live-document fixtures");
	AssetTextSearchRequest liveRequest;
	liveRequest.rootPath = liveRoot.path();
	liveRequest.findText = QStringLiteral("live");
	liveRequest.replace = true;
	liveRequest.replaceText = QStringLiteral("updated");
	liveRequest.buffers = {{livePath, QStringLiteral("open-tab"), 7, QStringLiteral("live\nlive\n"), QStringLiteral("UTF-8"), {}},
		{deletedPath, QStringLiteral("deleted-tab"), 2, QStringLiteral("live\n"), QStringLiteral("UTF-8"), {}},
		{liveRoot.filePath(QStringLiteral("nested/.agents/ignored.cfg")), QStringLiteral("excluded-tab"), 1, QStringLiteral("live"), {}, {}},
		{outside, QStringLiteral("outside-tab"), 1, QStringLiteral("live"), {}, {}}};
	const auto livePreview = findReplaceProjectText(liveRequest);
	ok &= expect(livePreview.canApply() && livePreview.buffersScanned == 2 && livePreview.filesScanned == 3
		&& livePreview.matchCount == 4 && livePreview.changes.size() == 3, "live snapshots override disk exactly once, include deleted files and obey project exclusions");
	ok &= expect(livePreview.changes[1].bufferId == QStringLiteral("open-tab") && livePreview.changes[1].bufferRevision == 7
		&& livePreview.changes[1].edits.size() == 2 && livePreview.changes[1].edits[1].offset == 5
		&& livePreview.changes[1].textSha256 == assetTextSnapshotHash(QStringLiteral("live\nlive\n"))
		&& livePreview.changes[1].replacementBytes.isEmpty(), "buffer changes carry exact edits and immutable snapshot identity without prepared disk bytes");
	ok &= expect(!applyProjectTextReplacements(livePreview).succeeded() && readFile(diskPath) == QByteArray("live\n"),
		"the core default must reject an editor plan before any disk writes");
	result = applyProjectTextReplacements(livePreview, {}, {}, true);
	ok &= expect(result.bufferEditsPending && !result.succeeded() && result.writtenFiles == QStringList {diskPath}
		&& result.replacementsApplied == 1 && readFile(diskPath) == QByteArray("updated\n")
		&& readFile(livePath) == QByteArray("stale live\n") && !QFile::exists(deletedPath),
		"editor-host deferral commits disk files only and leaves buffer application explicitly pending");
	liveRequest.buffers = {liveRequest.buffers.first()};
	liveRequest.buffers[0].error = QStringLiteral("unavailable");
	auto unavailable = findReplaceProjectText(liveRequest);
	ok &= expect(!unavailable.complete && unavailable.matchCount == 0 && !unavailable.canApply(), "unavailable buffers cannot fall back to stale disk");
	liveRequest.buffers[0].error.clear();
	liveRequest.includeGlobs = {QStringLiteral("open.cfg")};
	liveRequest.maxFileBytes = 1;
	ok &= expect(!findReplaceProjectText(liveRequest).complete, "live UTF-16 snapshots obey per-file size bounds");
	liveRequest.maxFileBytes = textDocumentByteLimit;
	liveRequest.maxTotalBytes = 8;
	ok &= expect(!findReplaceProjectText(liveRequest).complete, "live snapshots obey the shared input budget");
	liveRequest.maxTotalBytes = 64ll * 1024 * 1024;
	liveRequest.buffers[0].text = QString(QChar(0xd800));
	ok &= expect(!findReplaceProjectText(liveRequest).complete, "invalid Unicode buffers block replacement");
	liveRequest.buffers.clear();
	liveRequest.includeGlobs = {QStringLiteral("meson.build")};
	ok &= expect(writeFile(liveRoot.filePath(QStringLiteral("meson.build")), "live\n") && findReplaceProjectText(liveRequest).matchCount == 1,
		"project search includes shared special-name Code candidates");
	if (app.arguments().size() > 1) {
		const QString binary = app.arguments()[1];
		const auto unicodeJson = cli(binary, {QStringLiteral("asset"), QStringLiteral("find"), unicodeRoot.path(), QStringLiteral("--find"), QStringLiteral("b")}, 0, ok)
			.value(QStringLiteral("textSearch")).toObject();
		const auto unicodeMatch = unicodeJson.value(QStringLiteral("matches")).toArray().first().toObject();
		ok &= expect(unicodeJson.value(QStringLiteral("matchCount")).toInt() == 6 && unicodeJson.value(QStringLiteral("buffersScanned")).toInt() == 0
			&& unicodeMatch.value(QStringLiteral("source")).toString() == QStringLiteral("disk") && unicodeMatch.value(QStringLiteral("encoding")).toString().startsWith(QStringLiteral("UTF-16"))
			&& unicodeMatch.value(QStringLiteral("textSha256")).toString().size() == 64, "CLI shares UTF-16 search and exposes source, encoding and text snapshot metadata");
		const auto found = cli(binary, {QStringLiteral("asset"), QStringLiteral("find"), root.path(), QStringLiteral("--find"), QStringLiteral("player"),
			QStringLiteral("--whole-word"), QStringLiteral("--include"), QStringLiteral("scripts/*")}, 0, ok).value(QStringLiteral("textSearch")).toObject();
		ok &= expect(found.value(QStringLiteral("matchCount")).toInt() == 1 && found.value(QStringLiteral("complete")).toBool(), "CLI must expose filtered whole-word search and completion");
		const QStringList deletion {QStringLiteral("asset"), QStringLiteral("replace"), root.path(), QStringLiteral("--find"), QStringLiteral("player"),
			QStringLiteral("--delete-matches"), QStringLiteral("--include"), QStringLiteral("scripts/*")};
		const auto dry = cli(binary, deletion, 0, ok).value(QStringLiteral("textSearch")).toObject();
		ok &= expect(dry.value(QStringLiteral("canApply")).toBool() && readFile(first) == QByteArray("player\n"), "CLI replacement must default to review without writing");
		const auto written = cli(binary, deletion + QStringList {QStringLiteral("--write")}, 0, ok).value(QStringLiteral("textSearch")).toObject();
		ok &= expect(written.value(QStringLiteral("replacementsApplied")).toInt() == 1 && readFile(first) == QByteArray("\n"), "CLI must support explicit deletion and report committed replacements");
		cli(binary, deletion + QStringList {QStringLiteral("--replace"), QStringLiteral("enemy")}, 2, ok);
	}
	request.findText = QStringLiteral("player\n");
	ok &= expect(!findReplaceProjectText(request).succeeded(), "multiline needles must be rejected rather than count and replace differently");
	return ok ? 0 : 1;
}
