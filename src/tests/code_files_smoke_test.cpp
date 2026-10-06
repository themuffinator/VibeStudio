#include "app/code_files_worker.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <filesystem>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}
bool write(const QString& path, const QByteArray& contents = "source\n")
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
bool wait(CodeFilesWorker& worker)
{
	QElapsedTimer timer;
	timer.start();
	while (worker.busy() && timer.elapsed() < 15000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	return !worker.busy();
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	bool ok = true;
	const QString root = temp.filePath(QStringLiteral("project"));
	const QStringList sources {QStringLiteral("src/main.cpp"), QStringLiteral("src/math.hpp"), QStringLiteral("scripts/game.qh"), QStringLiteral("scripts/progs.src"),
		QStringLiteral("README"), QStringLiteral("meson.build"), QStringLiteral("config/default.cfg"), QStringLiteral(".gitignore"), QStringLiteral("notes/日本語.txt")};
	for (const auto& path : sources) { ok &= expect(write(QDir(root).filePath(path)), "write source fixture"); }
	for (const auto& path : {QStringLiteral(".git/deep/file.qc"), QStringLiteral("nested/node_modules/deep/main.cpp"), QStringLiteral(".agents/tmp/copy.cfg"), QStringLiteral("nested/builddir-debug/ignored.txt"), QStringLiteral("external/vendor/source.qc")}) {
		ok &= expect(write(QDir(root).filePath(path)), "write excluded fixture");
	}
	ok &= expect(write(QDir(root).filePath(QStringLiteral("texture.tga")), QByteArray::fromHex("00ff03")), "write a binary asset");
	CodeFilesRequest request;
	request.rootPath = root;
	auto result = listCodeFiles(request);
	ok &= expect(result.complete && result.files.size() == sources.size() && result.directoriesExcluded == 5, "catalog includes Code filenames and prunes excluded subtrees");
	ok &= expect(result.entriesVisited < 30, "excluded directories must be pruned before descent");
	int cppMatches = 0;
	for (const auto& file : result.files) {
		if (studioQueryMatches(parseStudioQuery(QStringLiteral("language=cpp size=7")), codeFileQueryProperties(file), file.relativePath)) { ++cppMatches; }
	}
	ok &= expect(cppMatches == 2, "catalog metadata supplies language and size queries without content reads");
	ok &= expect(codeFileLanguageId(QStringLiteral("GAME.QH")) == QStringLiteral("quakec") && codeFileLanguageId(QStringLiteral("progs.src")) == QStringLiteral("quakec"), "QuakeC headers and source lists use the same language classification");
	request.maxFiles = int(sources.size());
	ok &= expect(listCodeFiles(request).complete, "an exact file limit is complete when no additional candidate exists");
	request.maxFiles = 1;
	result = listCodeFiles(request);
	ok &= expect(!result.complete && result.files.size() == 1 && !result.warnings.isEmpty(), "file limits are explicit partial results");
	request.maxFiles = 4000;
	request.maxEntries = 2;
	result = listCodeFiles(request);
	ok &= expect(!result.complete && result.entriesVisited == 2, "directory visits stop at their hard limit");
	request.maxEntries = 100000;
	request.isCancelled = []() { return true; };
	result = listCodeFiles(request);
	ok &= expect(result.cancelled && result.entriesVisited == 0, "pre-cancellation performs no traversal");
	bool cancel = false;
	request.isCancelled = [&]() { return cancel; };
	request.progress = [&](int, int entries) { cancel = entries >= 3; };
	result = listCodeFiles(request);
	ok &= expect(result.cancelled && result.entriesVisited == 3, "cancellation is observed between directory entries");
	request.isCancelled = {};
	request.progress = {};
	const QString link = QDir(root).filePath(QStringLiteral("linked-source.qc"));
	std::error_code error;
	const auto fs = [](const QString& path) {
#ifdef Q_OS_WIN
		return std::filesystem::path(path.toStdWString());
#else
		return std::filesystem::path(path.toUtf8().constData());
#endif
	};
	std::filesystem::create_symlink(fs(QDir(root).filePath(sources.first())), fs(link), error);
	if (!error) {
		result = listCodeFiles(request);
		ok &= expect(result.linksExcluded == 1 && result.files.size() == sources.size(), "links are excluded rather than traversed");
		ok &= expect(QFile::remove(link), "remove the fixture link before temporary cleanup");
	} else { std::cout << "Symlink fixture unavailable on this host.\n"; }
	const QStringList assets {QStringLiteral("sounds/hit.ogg"), QStringLiteral("models/actor.md3"), QStringLiteral("pak0.pak"), QStringLiteral("sounds/session.vsaudio"), QStringLiteral("textures/layered.vtexture")};
	for (const auto& path : assets) { ok &= expect(write(QDir(root).filePath(path)), "write studio asset candidates"); }
	request.includeAssets = true;
	result = listCodeFiles(request);
	ok &= expect(result.complete && result.files.size() == sources.size() + assets.size() + 1, "studio catalog includes loose media, packages and native documents");
	int images = 0;
	for (const auto& file : result.files) {
		if (studioQueryMatches(parseStudioQuery(QStringLiteral("kind=image")), codeFileQueryProperties(file), file.relativePath)) {
			++images;
			ok &= expect(file.languageId.isEmpty(), "binary candidates do not claim a text language");
		}
	}
	ok &= expect(images == 1 && projectFileKindId(QStringLiteral("compiled.bsp")).isEmpty(), "asset kinds use supported surface routing; unsupported compiled files stay out");
	request.includeAssets = false;
	CodeFilesRequest invalid;
	ok &= expect(listCodeFiles(invalid).state == OperationState::Failed, "empty roots never list the process working directory");
	invalid.rootPath = temp.filePath(QStringLiteral("missing"));
	ok &= expect(listCodeFiles(invalid).state == OperationState::Failed, "missing roots fail explicitly");
	const QString other = temp.filePath(QStringLiteral("other"));
	ok &= expect(write(QDir(other).filePath(QStringLiteral("fresh.qc"))), "write another project");
	CodeFilesRequest second;
	second.rootPath = other;
	CodeFilesWorker worker;
	int completions = 0;
	worker.completed = [&](const auto& value) { ++completions; result = value; };
	worker.start(request);
	ok &= expect(worker.busy() && completions == 0, "catalog work returns immediately to the caller");
	worker.start(second);
	ok &= expect(wait(worker) && completions == 1 && result.rootPath == other && result.files.size() == 1, "replacement requests cannot publish old project files");
	worker.start(request);
	worker.cancel();
	ok &= expect(wait(worker) && completions == 2 && result.cancelled, "cancellation is terminal even when the scan has just finished");
	worker.start(request);
	worker.start(second);
	worker.cancel();
	ok &= expect(wait(worker) && completions == 3 && result.cancelled && result.rootPath == other, "queued cancellation belongs to the current project");
	worker.start(request);
	worker.reset();
	ok &= expect(wait(worker) && completions == 3, "retired results never publish");
	if (argc > 1) {
		QProcess cli;
		const auto run = [&](const QStringList& options) {
			cli.start(QString::fromLocal8Bit(argv[1]), QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("code"), QStringLiteral("files")} + options);
			return cli.waitForFinished(15000) ? cli.exitCode() : -1;
		};
		ok &= expect(run({root, QStringLiteral("--where"), QStringLiteral("language=cpp size=7")}) == 0, "CLI catalog and property query succeed");
		auto json = QJsonDocument::fromJson(cli.readAllStandardOutput()).object().value(QStringLiteral("codeFiles")).toObject();
		ok &= expect(json.value(QStringLiteral("complete")).toBool() && json.value(QStringLiteral("files")).toArray().size() == 2 && json.value(QStringLiteral("filesScanned")).toInt() == sources.size(), "CLI distinguishes query matches from all scanned files");
		ok &= expect(run({root, QStringLiteral("--max-files"), QStringLiteral("1")}) == 4, "CLI partial catalogs have validation status");
		json = QJsonDocument::fromJson(cli.readAllStandardOutput()).object().value(QStringLiteral("codeFiles")).toObject();
		ok &= expect(!json.value(QStringLiteral("complete")).toBool(true) && !json.value(QStringLiteral("warnings")).toArray().isEmpty(), "CLI exposes partial state and warnings");
		ok &= expect(run({root, QStringLiteral("--where"), QStringLiteral("sizze>0")}) == 2, "unknown metadata fields are usage errors");
		ok &= expect(run({root, QStringLiteral("--where")}) == 2 && run({root, QStringLiteral("--where"), QStringLiteral("--json")}) == 2,
			"missing query values are usage errors");
		ok &= expect(run({root, QStringLiteral("--max-files"), QStringLiteral("0")}) == 2 && run({}) == 2, "missing roots and invalid bounds are usage errors");
		ok &= expect(run({invalid.rootPath}) == 1, "missing directories fail without pretending to be empty projects");
		cli.start(QString::fromLocal8Bit(argv[1]), {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("project"), QStringLiteral("files"), root, QStringLiteral("--where"), QStringLiteral("kind=image")});
		ok &= expect(cli.waitForFinished(15000) && cli.exitCode() == 0, "project files CLI uses studio discovery and kind filters");
		json = QJsonDocument::fromJson(cli.readAllStandardOutput()).object().value(QStringLiteral("projectFiles")).toObject();
		ok &= expect(json.value(QStringLiteral("complete")).toBool() && json.value(QStringLiteral("files")).toArray().size() == 1
			&& json.value(QStringLiteral("filesScanned")).toInt() == sources.size() + assets.size() + 1, "project catalog JSON distinguishes the media match from all candidates");
	}
	return ok ? 0 : 1;
}
