#include "cli/level_clipboard.h"
#include "core/level_document.h"
#include "core/level_placement.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>
#include <QSet>
#include <filesystem>

namespace vibestudio::cli {
namespace {
struct Text { Q_DECLARE_TR_FUNCTIONS(LevelClipboardCli) };
struct FileState { bool exists = false; QByteArray hash; };
bool snapshot(const QString& path, FileState* state, QString* error)
{
	const QFileInfo info(path);
	if (info.isSymLink() || info.isJunction() || (info.exists() && !info.isFile())) {
		*error = Text::tr("A clipboard or map output must be a regular file, not a directory or link."); return false;
	}
	state->exists = info.exists(); state->hash.clear();
	if (!state->exists) { return true; }
	QFile file(path); QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file) || file.error() != QFileDevice::NoError) {
		*error = Text::tr("Could not read %1: %2").arg(path, file.errorString()); return false;
	}
	state->hash = hash.result(); return true;
}
bool unchanged(const QString& path, const FileState& before, QString* error)
{
	FileState now;
	if (!snapshot(path, &now, error)) { return false; }
	if (before.exists != now.exists || before.hash != now.hash) {
		*error = Text::tr("The file changed while the clipboard operation was being prepared: %1").arg(path); return false;
	}
	return true;
}
bool safeParent(const QString& path, QString* error)
{
	auto dir = QFileInfo(path).dir();
	if (!dir.exists()) { *error = Text::tr("Create the output directory before copying or cutting: %1").arg(dir.absolutePath()); return false; }
	for (;;) {
		const QFileInfo info(dir.absolutePath());
		if (info.isSymLink() || info.isJunction()) { *error = Text::tr("Output directories cannot pass through symbolic links or junctions."); return false; }
		if (dir.isRoot()) { return true; }
		if (!dir.cdUp()) { *error = Text::tr("Could not check the output directory."); return false; }
	}
}
QString identity(const QString& path)
{
	const QFileInfo info(path);
	auto result = info.exists() ? info.canonicalFilePath() : QDir(info.dir().canonicalPath()).filePath(info.fileName());
	result = QDir::cleanPath(result);
#ifdef Q_OS_WIN
	result = result.toCaseFolded();
#endif
	return result;
}
bool sameFile(const QString& a, const QString& b)
{
	if (identity(a) == identity(b)) { return true; }
	// Filesystem identity also covers hard links and case aliases on volumes
	// whose case behaviour differs from the platform's default.
	const auto native = [](const QString& path) {
		return std::filesystem::path(path.toStdU16String());
	};
	std::error_code error;
	return std::filesystem::equivalent(native(a), native(b), error) && !error;
}
} // namespace

LevelClipboardCliResult runLevelClipboard(const QStringList& arguments)
{
	LevelClipboardCliResult result;
	result.payload = {{"clipboardWritten", false}, {"mapWritten", false}};
	const auto failure = [&](int code, const QString& message) { result.exitCode = code; result.error = message; return result; };
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite"};
	const QSet<QString> options{"--object", "--output", "--clipboard", "--settings-file", "--locale", "--catalog-root"};
	QSet<QString> seen; QHash<QString, QString> values; QStringList positional, objects;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i];
		if (!arg.startsWith('-')) { positional << arg; continue; }
		const auto equal = arg.indexOf('='); const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key) && key != QLatin1String("--object")) { return failure(2, Text::tr("Repeated option: %1").arg(key)); }
		seen.insert(key);
		if (flags.contains(key) && equal < 0) { continue; }
		if (!options.contains(key)) { return failure(2, Text::tr("Unexpected option: %1").arg(arg)); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal + 1); }
		else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith('-')) { value = arguments[++i]; }
		if (value.trimmed().isEmpty()) { return failure(2, Text::tr("Missing value for %1.").arg(key)); }
		if (key == QLatin1String("--object")) { objects << value; } else { values.insert(key, value); }
	}
	if (positional.size() != 3 || !QStringList{"map", "maps", "level"}.contains(positional[0])
		|| (positional[1] != QLatin1String("copy") && positional[1] != QLatin1String("cut"))) {
		return failure(2, Text::tr("Expected map copy|cut <map> with repeatable --object and --output."));
	}
	const bool cut = positional[1] == QLatin1String("cut");
	if (objects.isEmpty() || objects.size() > 16384 || !values.contains("--output") || cut != values.contains("--clipboard")) {
		return failure(2, Text::tr("Copy and cut require 1–16,384 --object selectors and --output. Only cut requires --clipboard for the copied fragment."));
	}
	const auto source = QFileInfo(positional[2]).absoluteFilePath();
	const auto output = QFileInfo(values.value("--output")).absoluteFilePath();
	const auto clipboard = cut ? QFileInfo(values.value("--clipboard")).absoluteFilePath() : output;
	const bool dryRun = seen.contains("--dry-run"), overwrite = seen.contains("--overwrite");
	result.payload.insert("map", source); result.payload.insert("output", output); result.payload.insert("clipboard", clipboard);
	result.payload.insert("dryRun", dryRun); result.payload.insert("objects", QJsonArray::fromStringList(objects));
	QString error;
	if (!QFileInfo(source).isFile()) { return failure(3, Text::tr("The source map does not exist.")); }
	if (!safeParent(clipboard, &error) || (cut && !safeParent(output, &error))) { return failure(4, error); }
	if (sameFile(clipboard, source) || (cut && sameFile(clipboard, output))) {
		return failure(2, Text::tr("The clipboard fragment must use a different file from the source and remaining map."));
	}
	FileState clipboardBefore, outputBefore;
	if (!snapshot(clipboard, &clipboardBefore, &error) || (cut && !snapshot(output, &outputBefore, &error))) { return failure(4, error); }
	if (!overwrite && (clipboardBefore.exists || (cut && outputBefore.exists))) {
		return failure(4, Text::tr("An output exists. Choose another file or explicitly use --overwrite, including for a dry run."));
	}
	LevelMapDocument sourceDocument;
	if (!loadLevelMap({source, {}, {}}, &sourceDocument, &error)) { return failure(4, error); }
	QVector<LevelMapSelectionRef> selection;
	for (const auto& object : objects) {
		if ((!object.startsWith("entity:") && !object.startsWith("brush:") && !object.startsWith("patch:"))
			|| !selectLevelMapObject(&sourceDocument, object, &error)) { return failure(2, Text::tr("Invalid selected object %1: %2").arg(object, error)); }
		for (const auto& ref : sourceDocument.selection) { if (!selection.contains(ref)) { selection << ref; } }
	}
	if (!setLevelMapSelection(&sourceDocument, selection, &error)) { return failure(2, error); }
	LevelPlacementRequest request; request.operation = cut ? LevelPlacementOperation::Cut : LevelPlacementOperation::Copy;
	const auto prepared = prepareLevelPlacement(sourceDocument, request);
	if (!prepared.succeeded) { return failure(4, prepared.error); }
	const auto bytes = prepared.clipboardText.toUtf8();
	result.payload.insert("clipboardBytes", bytes.size()); result.payload.insert("selectedObjects", selection.size());
	LevelDocumentSaveRequest save; save.path = output; save.overwrite = overwrite; save.dryRun = true;
	if (cut) {
		const auto report = writeLevelDocument(prepared.document, save);
		if (!report.succeeded()) { return failure(4, report.errors.join('\n')); }
		result.payload.insert("warnings", QJsonArray::fromStringList(report.warnings));
	}
	if (dryRun) {
		result.lines << (cut ? Text::tr("Would copy selected objects to %1 and save the remaining map to %2.").arg(clipboard, output)
			: Text::tr("Would copy selected objects to %1.").arg(clipboard)); return result;
	}
	if (!unchanged(clipboard, clipboardBefore, &error) || (cut && !unchanged(output, outputBefore, &error))) { return failure(4, error); }
	// Source reads can use a symlink, but outputs cannot. Compare the loaded
	// source content before publication even when saving the remaining map elsewhere.
	FileState sourceNow;
	if (!snapshot(QFileInfo(source).canonicalFilePath(), &sourceNow, &error) || !sourceNow.exists || sourceNow.hash != sourceDocument.sourceContentHash) {
		return failure(4, Text::tr("The source map changed during clipboard preparation. Retry from its current contents."));
	}
	QSaveFile file(clipboard); file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) { return failure(4, file.errorString()); }
	if (!unchanged(clipboard, clipboardBefore, &error)) { return failure(4, error); }
	if (!file.commit()) { return failure(4, file.errorString()); }
	result.payload.insert("clipboardWritten", true);
	if (cut) {
		// Two destinations cannot form one portable filesystem transaction. The
		// fragment is committed first, so a failed map save never loses the cut objects.
		if (!unchanged(output, outputBefore, &error)) {
			return failure(4, Text::tr("The clipboard was written to %1; the map was not saved: %2").arg(clipboard, error));
		}
		save.dryRun = false;
		const auto report = writeLevelDocument(prepared.document, save);
		if (!report.succeeded()) {
			return failure(4, Text::tr("The clipboard was written to %1; the map was not saved: %2").arg(clipboard, report.errors.join('\n')));
		}
		result.payload.insert("mapWritten", true); result.payload.insert("backup", report.backupPath);
		result.payload.insert("warnings", QJsonArray::fromStringList(report.warnings));
		result.lines << Text::tr("Copied selected objects to %1 and saved the remaining map to %2.").arg(clipboard, output);
	} else { result.lines << Text::tr("Copied selected objects to %1.").arg(clipboard); }
	return result;
}
} // namespace vibestudio::cli
