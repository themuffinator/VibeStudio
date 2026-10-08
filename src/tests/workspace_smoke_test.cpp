#include "core/workspace_document.h"
#include "core/asset_formats.h"
#include "cli/workspace.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QSet>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool ok, const char* message, const QString& detail = {}) { if (!ok) { std::cerr << message << ": " << detail.toStdString() << '\n'; } return ok; }
bool put(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temp; QString error;
	if (!temp.isValid()) { return 1; }
	const auto original = temp.filePath(QStringLiteral("original")), moved = temp.filePath(QStringLiteral("moved"));
	QDir().mkpath(original + QStringLiteral("/project")); QDir().mkpath(moved + QStringLiteral("/project"));
	const auto path = original + QStringLiteral("/work.vibeworkspace");
	WorkspaceDocument source; source.projectPath = original + QStringLiteral("/project"); source.packagePath = source.projectPath;
	source.mapPath = source.projectPath + QStringLiteral("/missing.map"); source.mapName = QStringLiteral("MAP01");
	source.codeFiles = {source.projectPath + QStringLiteral("/shader Ω.shader")}; source.currentCodeFile = source.codeFiles[0];
	source.activeModule = QStringLiteral("textures"); source.assetSelections = {{"textures", "textures/wall.dds"}};
	source.extensions = {{"custom", QJsonObject{{"nested", QJsonArray{1, true, "retained"}}}}};
	QByteArray first;
	bool ok = expect(writeWorkspaceDocument(path, source, {}, &first, &error), "save workspace", error);
	QFile file(path); if (!file.open(QIODevice::ReadOnly)) { return 1; } const auto bytes = file.readAll(); file.close();
	ok &= expect(!bytes.contains(original.toUtf8()) && bytes.contains("project/missing.map"), "references stored relative to workspace");
	WorkspaceDocument restored;
	ok &= expect(parseWorkspaceDocument(bytes, moved + QStringLiteral("/work.vibeworkspace"), &restored, &error) && restored.projectPath == moved + QStringLiteral("/project") &&
		restored.assetSelections == source.assetSelections && restored.extensions == source.extensions, "relocated workspace preserves context", error);
	ok &= expect(workspaceMissingReferences(restored).size() == 2, "missing references reported without opening files");
	ok &= expect(!writeWorkspaceDocument(path, source, {}, nullptr, &error), "existing workspace not overwritten by a new save");
	source.activeModule = QStringLiteral("models"); QByteArray second;
	ok &= expect(writeWorkspaceDocument(path, source, first, &second, &error) && first != second, "reviewed workspace replacement", error);
	ok &= expect(!writeWorkspaceDocument(path, source, first, nullptr, &error), "stale revision refused");
	const auto retained = restored.projectPath;
	for (const auto& bad : QVector<QJsonObject>{
		{{"version", 2}}, {{"version", 1.5}}, {{"format", "other"}}, {{"codeFiles", "wrong"}}, {{"activeModule", "execute"}},
		{{"project", "https://example.com"}}, {{"project", "C:relative"}}, {{"package", "\\\\?\\C:\\raw"}},
		{{"currentCodeFile", "not-in-tabs.cpp"}}, {{"assetSelections", QJsonObject{{"textures", "../escape"}}}},
		{{"extensions", QJsonArray{}}}, {{"unknown", true}}}) {
		auto object = workspaceDocumentJson(source, path);
		for (auto it = bad.begin(); it != bad.end(); ++it) { object.insert(it.key(), it.value()); }
		ok &= expect(!parseWorkspaceDocument(QJsonDocument(object).toJson(), path, &restored, &error) && restored.projectPath == retained,
			"malformed workspace leaves output untouched", error);
	}
	QJsonArray tooMany; for (int i = 0; i < 129; ++i) { tooMany.append(QStringLiteral("file%1.cpp").arg(i)); }
	auto oversized = workspaceDocumentJson(source, path); oversized.insert("codeFiles", tooMany);
	ok &= expect(!parseWorkspaceDocument(QJsonDocument(oversized).toJson(), path, &restored, &error), "tab count bound");
	ok &= expect(!parseWorkspaceDocument(QByteArray(1024 * 1024 + 1, ' '), path, &restored, &error), "payload size bound");
	const auto occupied = temp.filePath(QStringLiteral("occupied.vibeworkspace")); put(occupied, "user document");
	ok &= expect(!writeWorkspaceDocument(occupied, source, second, nullptr, &error), "non-workspace file protected");
	const auto absent = temp.filePath(QStringLiteral("dry-run.vibeworkspace"));
	auto cli = cli::runWorkspaceCommand({"vibestudio", "--cli", "workspace", "create", absent, "--project", moved, "--code", "test.cpp", "--dry-run", "--json"});
	ok &= expect(cli.exitCode == 0 && !QFileInfo::exists(absent), "CLI dry run has no writes", cli.error);
	cli = cli::runWorkspaceCommand({"vibestudio", "workspace", "inspect", path, "--overwrite"});
	ok &= expect(cli.exitCode == 2, "inspection rejects mutation options");
	cli = cli::runWorkspaceCommand({"vibestudio", "workspace", "inspect", path, "--json"});
	ok &= expect(cli.exitCode == 0 && cli.payload.value("workspace").toObject().value("activeModule") == QStringLiteral("models"), "CLI sees same workspace", cli.error);
	WorkspaceDocument large; large.extensions = {{"large", QString(1024 * 1024 - 4096, 'x')}};
	const auto largePath = temp.filePath(QStringLiteral("large.vibeworkspace"));
	ok &= expect(writeWorkspaceDocument(largePath, large, {}, nullptr, &error), "bounded large extension fixture", error);
	QStringList enlarged{"vibestudio", "workspace", "create", largePath, "--overwrite", "--dry-run"};
	for (const auto ch : {'a', 'b', 'c'}) { enlarged << "--code" << temp.filePath(QString(2000, QLatin1Char(ch))); }
	cli = cli::runWorkspaceCommand(enlarged);
	ok &= expect(cli.exitCode == 2 && readWorkspaceDocument(largePath, &restored, nullptr, &error) && restored.extensions == large.extensions,
		"dry run enforces total size after preserving extensions", cli.error);
	QSet<QString> ids, suffixes;
	for (const auto& format : assetFormats()) {
		ok &= expect(!ids.contains(format.id), "unique format id"); ids.insert(format.id);
		for (const auto& suffix : format.suffixes) { ok &= expect(!suffixes.contains(suffix), "unique format suffix"); suffixes.insert(suffix); }
	}
	ok &= expect(assetFormatForPath(QStringLiteral("MODELS/ROBOT.MESH.JSON"))->id == QStringLiteral("mesh-project"), "longest case-insensitive suffix");
	ok &= expect(assetPreviewKindForPath(QStringLiteral("image.TIFF")) == AssetPreviewKind::Image && assetPreviewKindForPath(QStringLiteral("image.DDS")) == AssetPreviewKind::Image, "shared image routing");
	ok &= expect(assetPreviewKindForPath(QStringLiteral("script.qc")) == AssetPreviewKind::Text, "code fallback retained");
	ok &= expect(assetPackageOpenFilter().contains(QStringLiteral("*.pk4")) && assetPackageOpenFilter().contains(QStringLiteral("*.pkz")), "shared package filter includes ZIP aliases");
	cli = cli::runWorkspaceCommand({"vibestudio", "asset", "formats", "--module", "models"});
	ok &= expect(cli.exitCode == 0 && cli.payload.value("formats").toArray().size() >= 7, "module-filtered format catalog", cli.error);
	ok &= expect(assetFormatForPath(QStringLiteral("test.iqm"))->readCapability == QStringLiteral("geometry") && assetFormatForPath(QStringLiteral("test.iqm"))->runtimeWrite,
		"IQM is advertised as decoded geometry with export");
	return ok ? 0 : 1;
}
