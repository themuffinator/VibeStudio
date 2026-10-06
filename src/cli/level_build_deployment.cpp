#include "cli/level_build_deployment.h"
#include "core/level_build_deployment.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>
namespace vibestudio::cli {
LevelBuildCliResult runLevelBuildDeploymentCommand(const QStringList& arguments) {
	const auto fail = [](int code, const QString& error) { return LevelBuildCliResult{code, error, {}, {}}; };
	const QSet<QString> common{"--cli", "--json", "--quiet", "--verbose"}, globalValues{"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> flags{"--dry-run", "--overwrite", "--include-source", "--allow-test-assets", "--launch"},
		options{"--installation",
				"--mod",
				"--pak-slot",
				"--compression",
				"--expected-output-sha256",
				"--expected-package-sha256",
				"--expected-deployment-sha256"};
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
	if (positional.size() != 3 || (positional[1] != "deploy-plan" && positional[1] != "deploy-prepared")) {
		return fail(
			2, QCoreApplication::translate("LevelBuildCli", "Use build deploy-plan <workspace> or build deploy-prepared <workspace>."));
	}
	const bool deploy = positional[1] == "deploy-prepared";
	if (!deploy) {
		for (const auto& name : seen) {
			if (!common.contains(name) && !globalValues.contains(name) && name != "--installation" && name != "--mod" &&
				name != "--pak-slot") {
				return fail(2, QCoreApplication::translate("LevelBuildCli", "Option %1 does not apply to this command.").arg(name));
			}
		}
	}
	LevelBuildDeploymentOptions request;
	request.dryRun = seen.contains("--dry-run");
	request.allowOverwrite = seen.contains("--overwrite");
	request.allowReadOnlyWrite = seen.contains("--allow-test-assets");
	request.includeSourceMap = seen.contains("--include-source");
	request.launch = seen.contains("--launch");
	if (seen.contains("--compression") && !deflateLevelFromId(values.value("--compression"), &request.compression)) {
		return fail(2, QCoreApplication::translate("LevelBuildCli", "Use compression store, fast, default or best."));
	}
	int pakSlot = -1;
	if (seen.contains("--pak-slot")) {
		bool valid = false;
		pakSlot = values.value("--pak-slot").toInt(&valid);
		if (!valid || pakSlot < 0 || !QRegularExpression(QStringLiteral("^[0-9]+$")).match(values.value("--pak-slot")).hasMatch()) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "PAK slot must be a nonnegative integer; omit it for Automatic."));
		}
	}
	for (const auto& name : {QStringLiteral("--expected-output-sha256"), QStringLiteral("--expected-package-sha256"),
							 QStringLiteral("--expected-deployment-sha256")}) {
		if (!seen.contains(name)) {
			continue;
		}
		auto value = values.value(name);
		if (name == "--expected-package-sha256" && value == "missing") {
			values[name] = {};
			continue;
		}
		if (!QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")).match(value).hasMatch()) {
			return fail(2, QCoreApplication::translate(
							   "LevelBuildCli", "Expected SHA-256 must contain 64 hexadecimal digits; use missing for an absent package."));
		}
		values[name] = value.toLower();
	}
	StudioSettings settings;
	if (deploy && !request.dryRun && settings.isReadOnly()) {
		return fail(1, QCoreApplication::translate("LevelBuildCli", "The settings store is read-only."));
	}
	const auto installations = settings.gameInstallations();
	const auto selected = seen.contains("--installation") ? values.value("--installation") : settings.selectedGameInstallationId();
	GameInstallationProfile installation;
	bool found = false;
	for (const auto& candidate : installations) {
		if ((!selected.isEmpty() && sameGameInstallationId(candidate.id, selected)) || (selected.isEmpty() && !found)) {
			installation = candidate;
			found = true;
		}
	}
	if (!found) {
		return fail(3, QCoreApplication::translate("LevelBuildCli", "No matching saved game installation was found."));
	}
	const auto workspace = readLevelBuildWorkspace(positional[2]);
	if (!workspace.ready) {
		return fail(4, workspace.error);
	}
	const auto folder = seen.contains("--mod") ? values.value("--mod") : settings.launchGameDirectory(installation.id);
	const auto plan = planLevelBuildDeployment(workspace, installation, folder, {}, pakSlot);
	if (!deploy || !plan.ready) {
		return {
			plan.ready ? 0 : 4, plan.error, {{"deploymentPlan", levelBuildDeploymentPlanJson(plan)}}, {levelBuildDeploymentPlanText(plan)}};
	}
	if ((seen.contains("--expected-deployment-sha256") &&
		 values.value("--expected-deployment-sha256") != levelBuildDeploymentReviewSha256(plan)) ||
		(seen.contains("--expected-output-sha256") &&
		 values.value("--expected-output-sha256") != QString::fromLatin1(plan.artifacts.recordSha256.toHex())) ||
		(seen.contains("--expected-package-sha256") && values.value("--expected-package-sha256") != plan.existingPackageSha256)) {
		return fail(4, QCoreApplication::translate("LevelBuildCli", "The reviewed build or destination changed. Review deployment again."));
	}
	const auto result = deployLevelBuild(workspace, plan, request);
	return {
		result.succeeded() ? 0 : 4,
		result.error,
		{{"deployment", levelBuildDeploymentJson(result)}},
		{levelBuildDeploymentPlanText(result.plan), packageWriteReportText(result.publication.write),
		 result.launched ? QCoreApplication::translate("LevelBuildCli", "Game started; process %1.").arg(result.processId) : QString()}};
}
} // namespace vibestudio::cli
