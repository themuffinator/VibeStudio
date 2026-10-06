#include "app/code_index_worker.h"
#include "core/text_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>
#include <filesystem>

using namespace vibestudio;
namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}

bool write(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool contains(const CodeWorkspaceIndex& index, const QString& name)
{
	for (const auto& symbol : index.symbols) { if (symbol.name == name) { return true; } }
	return false;
}

bool wait(CodeIndexWorker& worker)
{
	QEventLoop loop;
	QTimer poll, timeout;
	timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (!worker.busy()) { loop.quit(); } });
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(5);
	timeout.start(15000);
	if (worker.busy()) { loop.exec(); }
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
	const QString first = QDir(root).filePath(QStringLiteral("source.qc"));
	ok &= expect(write(first, "  void() saved_name = {\r\n};\r\n"), "write source fixture");
	for (const QString& folder : {QStringLiteral(".agents/tmp/deep"), QStringLiteral("builddir-debug/nested"), QStringLiteral("external/vendor"), QStringLiteral("node_modules/deep")}) {
		ok &= expect(write(QDir(root).filePath(folder + QStringLiteral("/hidden.qc")), "void() hidden_symbol = {\n};\n"), "write excluded source fixture");
	}
	TextFileDocument utf16 = decodeTextFile(QByteArray::fromHex("fffe"));
	QByteArray encoded;
	ok &= expect(encodeTextFile(utf16, QStringLiteral("void() unicode_source = {\n};\n"), &encoded), "encode UTF-16 fixture");
	ok &= expect(write(QDir(root).filePath(QStringLiteral("unicode.qc")), encoded), "write UTF-16 fixture");
	CodeWorkspaceIndexRequest request;
	request.rootPath = root;
	auto index = indexCodeWorkspace(request);
	ok &= expect(index.complete && index.files.size() == 2 && contains(index, QStringLiteral("unicode_source")) && !contains(index, QStringLiteral("hidden_symbol")), "index supported Unicode and prune whole generated subtrees");
	for (const auto& symbol : index.symbols) {
		if (symbol.name == QStringLiteral("saved_name")) { ok &= expect(symbol.column == 10, "definition columns must include source indentation"); }
	}
	const QString linked = QDir(root).filePath(QStringLiteral("linked.qc"));
	std::error_code linkError;
	const auto fsPath = [](const QString& path) {
#ifdef Q_OS_WIN
		return std::filesystem::path(path.toStdWString());
#else
		return std::filesystem::path(path.toUtf8().constData());
#endif
	};
	std::filesystem::create_symlink(fsPath(first), fsPath(linked), linkError);
	if (!linkError) {
		index = indexCodeWorkspace(request);
		ok &= expect(index.complete && index.files.size() == 2, "linked source files are excluded from the scan");
		ok &= expect(QFile::remove(linked), "remove only the test link before temporary-directory cleanup");
	} else { std::cout << "Symlink fixture unavailable on this host.\n"; }
	request.buffers = {{first, QStringLiteral("\nvoid() live_name = {\n};\n"), {}}};
	index = indexCodeWorkspace(request);
	ok &= expect(index.complete && contains(index, QStringLiteral("live_name")) && !contains(index, QStringLiteral("saved_name")), "editor snapshots replace disk definitions");
	bool fromBuffer = false;
	for (const auto& file : index.files) { fromBuffer |= file.fromBuffer; }
	ok &= expect(fromBuffer, "report source provenance for editor snapshots");
	ok &= expect(QFile::remove(first), "delete saved source while keeping the editor snapshot");
	index = indexCodeWorkspace(request);
	ok &= expect(contains(index, QStringLiteral("live_name")), "deleted open files retain their live definitions");
	ok &= expect(write(first, "void() saved_name = {\n};\n"), "restore source fixture");
	request.buffers = {{first, {}, QStringLiteral("Snapshot unavailable")}};
	index = indexCodeWorkspace(request);
	ok &= expect(!index.complete && index.filesSkipped == 1 && !contains(index, QStringLiteral("saved_name")), "unavailable live snapshots must suppress stale disk symbols");
	request.buffers = {{temp.filePath(QStringLiteral("outside.qc")), QStringLiteral("void() outside = {\n};\n"), {}}};
	index = indexCodeWorkspace(request);
	ok &= expect(!index.complete && !contains(index, QStringLiteral("outside")), "buffer paths cannot escape the source root");
	request.buffers.clear();
	request.maxFiles = 1;
	index = indexCodeWorkspace(request);
	ok &= expect(!index.complete && index.files.size() == 1, "candidate file bounds produce an explicit partial index");
	request.maxFiles = 4096;
	request.maxFileBytes = 8;
	index = indexCodeWorkspace(request);
	ok &= expect(!index.complete && index.files.isEmpty() && index.filesSkipped == 2, "oversized files are bounded and explained");
	request.maxFileBytes = textDocumentByteLimit;
	request.maxTotalBytes = 40;
	index = indexCodeWorkspace(request);
	ok &= expect(!index.complete && index.bytesRead <= 40, "total read budget is enforced");
	request.maxTotalBytes = 64ll * 1024 * 1024;
	request.maxEntries = 1;
	index = indexCodeWorkspace(request);
	ok &= expect(!index.complete && index.entriesVisited <= 2, "directory visits are bounded");
	request.maxEntries = 100000;
	request.maxSymbols = 1;
	index = indexCodeWorkspace(request);
	ok &= expect(!index.complete && index.symbols.size() <= 1, "symbol output is bounded");
	request.maxSymbols = 20000;
	request.isCancelled = []() { return true; };
	index = indexCodeWorkspace(request);
	ok &= expect(index.cancelled && !index.complete && index.files.isEmpty(), "pre-cancelled scans do not read source files");
	bool stop = false;
	request.isCancelled = [&]() { return stop; };
	request.progress = [&](int, int) { stop = true; };
	index = indexCodeWorkspace(request);
	ok &= expect(index.cancelled && index.files.size() == 1, "cancellation between files retains only explicitly partial results");
	request.isCancelled = {};
	request.progress = {};
	ok &= expect(write(QDir(root).filePath(QStringLiteral("invalid.qc")), QByteArray::fromHex("fffe00")), "write malformed Unicode");
	index = indexCodeWorkspace(request);
	ok &= expect(!index.complete && index.filesSkipped == 1 && !index.diagnostics.isEmpty(), "invalid Unicode must be reported rather than decoded lossily");
	ok &= expect(QFile::remove(QDir(root).filePath(QStringLiteral("invalid.qc"))), "remove invalid test fixture");
	ok &= expect(write(QDir(root).filePath(QStringLiteral("long.qc")), QByteArray(100000, 'a') + "() {\n}\n"), "write pathological source line");
	index = indexCodeWorkspace(request);
	ok &= expect(!index.complete, "pathological long lines are bounded and reported");
	ok &= expect(QFile::remove(QDir(root).filePath(QStringLiteral("long.qc"))), "remove pathological test fixture");
	const QString secondRoot = temp.filePath(QStringLiteral("second"));
	ok &= expect(write(QDir(secondRoot).filePath(QStringLiteral("fresh.qc")), "void() fresh_project = {\n};\n"), "write second project");
	CodeIndexWorker worker;
	int completions = 0;
	worker.completed = [&](const auto& result) { ++completions; index = result; };
	worker.start(request);
	ok &= expect(worker.busy() && completions == 0, "indexing returns before scanning or publishing");
	worker.cancel();
	ok &= expect(wait(worker) && completions == 1 && index.cancelled, "worker cancellation is terminal even if the scan raced with cancel");
	worker.start(request);
	worker.reset();
	CodeWorkspaceIndexRequest second;
	second.rootPath = secondRoot;
	worker.start(second);
	ok &= expect(wait(worker) && completions == 2 && contains(index, QStringLiteral("fresh_project")) && !contains(index, QStringLiteral("saved_name")), "a superseded scan cannot publish into another project");
	worker.start(request);
	worker.start(second);
	worker.cancel();
	ok &= expect(wait(worker) && completions == 3 && index.cancelled && index.rootPath == secondRoot, "cancelling a queued request reports that request, not obsolete work");
	worker.start(request);
	worker.reset();
	ok &= expect(wait(worker) && completions == 3, "reset discards in-flight results");
	if (argc > 1) {
		QProcess cli;
		cli.start(QString::fromLocal8Bit(argv[1]), {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("code"), QStringLiteral("index"), root});
		ok &= expect(cli.waitForFinished(15000) && cli.exitCode() == 0, "CLI complete Unicode indexes succeed");
		const QJsonObject completeReport = QJsonDocument::fromJson(cli.readAllStandardOutput()).object().value(QStringLiteral("code")).toObject();
		ok &= expect(completeReport.value(QStringLiteral("complete")).toBool(), "CLI complete state matches the scanner");
		cli.start(QString::fromLocal8Bit(argv[1]), {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("code"), QStringLiteral("index"), root, QStringLiteral("--max-files"), QStringLiteral("1")});
		ok &= expect(cli.waitForFinished(15000) && cli.exitCode() == 4, "CLI incomplete indexes return validation status");
		const QJsonObject report = QJsonDocument::fromJson(cli.readAllStandardOutput()).object().value(QStringLiteral("code")).toObject();
		ok &= expect(report.contains(QStringLiteral("bytesRead")) && !report.value(QStringLiteral("complete")).toBool(true), "CLI reports the same bounds and completion state");
		cli.start(QString::fromLocal8Bit(argv[1]), {QStringLiteral("--cli"), QStringLiteral("code"), QStringLiteral("index"), root, QStringLiteral("--max-files"), QStringLiteral("0")});
		ok &= expect(cli.waitForFinished(15000) && cli.exitCode() == 2, "invalid CLI bounds produce usage errors");
		cli.start(QString::fromLocal8Bit(argv[1]), {QStringLiteral("--cli"), QStringLiteral("code"), QStringLiteral("index"), temp.filePath(QStringLiteral("missing"))});
		ok &= expect(cli.waitForFinished(15000) && cli.exitCode() == 1, "unreadable roots produce a failed index exit");
	}
	return ok ? 0 : 1;
}
