#include "cli/level_build_artifacts.h"
#include "core/level_build_artifacts.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>
namespace vibestudio::cli {
LevelBuildCliResult runLevelBuildArtifactsCommand(const QStringList& arguments) {
	const auto fail = [](int code, const QString& error) { return LevelBuildCliResult{code, error, {}, {}}; };
	const QSet<QString> common{"--cli", "--json", "--quiet", "--verbose"}, globalValues{"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> flags{"--dry-run", "--overwrite", "--include-source"},
		options{"--output", "--compression", "--expected-output-sha256"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (qsizetype at = 1; at < arguments.size(); ++at) {
		const auto word = arguments[at];
		if (!word.startsWith('-')) {
			positional << word;
			continue;
		}
		const auto equal = word.indexOf('=');
		const auto name = equal < 0 ? word : word.left(equal);
		if (seen.contains(name)) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "Repeated option: %1").arg(name));
		}
		seen.insert(name);
		if ((flags.contains(name) || common.contains(name)) && equal < 0) {
			continue;
		}
		if (!globalValues.contains(name) && !options.contains(name)) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "Unexpected option: %1").arg(word));
		}
		QString value;
		if (equal >= 0) {
			value = word.mid(equal + 1);
		} else if (at + 1 < arguments.size() && !arguments[at + 1].startsWith('-')) {
			value = arguments[++at];
		}
		if (value.trimmed().isEmpty()) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "Missing value for %1.").arg(name));
		}
		values[name] = value;
	}
	if (positional.size() != 3 || (positional[1] != "artifacts" && positional[1] != "publish-prepared")) {
		return fail(2,
					QCoreApplication::translate(
						"LevelBuildCli", "Use build artifacts <workspace>, or build publish-prepared <workspace> --output <package.pak-or-pk3>."));
	}
	const bool publish = positional[1] == "publish-prepared";
	if (!publish) {
		for (const auto& name : seen) {
			if (!common.contains(name) && !globalValues.contains(name)) {
				return fail(2, QCoreApplication::translate("LevelBuildCli", "Option %1 does not apply to this command.").arg(name));
			}
		}
	}
	LevelBuildPackageRequest request;
	request.outputPath = values.value("--output");
	request.dryRun = seen.contains("--dry-run");
	request.allowOverwrite = seen.contains("--overwrite");
	request.includeSourceMap = seen.contains("--include-source");
	if (publish) {
		if (request.outputPath.isEmpty()) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "Publishing requires --output <package.pak-or-pk3>."));
		}
		if (seen.contains("--compression") && !deflateLevelFromId(values.value("--compression"), &request.compression)) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "Use compression store, fast, default or best."));
		}
		if (seen.contains("--expected-output-sha256")) {
			const auto digest = values.value("--expected-output-sha256");
			if (!QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")).match(digest).hasMatch()) {
				return fail(2, QCoreApplication::translate("LevelBuildCli", "Expected output SHA-256 must contain 64 hexadecimal digits."));
			}
			request.expectedRecordSha256 = QByteArray::fromHex(digest.toLatin1());
		}
		if (!request.dryRun && StudioSettings().isReadOnly()) {
			return fail(1, QCoreApplication::translate("LevelBuildCli", "The settings store is read-only."));
		}
	}
	const auto workspace = readLevelBuildWorkspace(positional[2]);
	if (!workspace.ready) {
		return fail(4, workspace.error);
	}
	if (!publish) {
		const auto artifacts = inspectLevelBuildArtifacts(workspace);
		return {artifacts.verified ? 0 : 4,
				artifacts.error,
				{{"artifacts", levelBuildArtifactsJson(artifacts)}},
				{levelBuildArtifactsText(artifacts)}};
	}
	const auto result = publishLevelBuildPackage(workspace, request);
	return {
		result.succeeded() ? 0 : 4, result.error, {{"publication", levelBuildPackageJson(result)}}, {packageWriteReportText(result.write)}};
}
} // namespace vibestudio::cli
