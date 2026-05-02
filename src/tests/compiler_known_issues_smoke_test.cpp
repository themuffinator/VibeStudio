#include "core/compiler_known_issues.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

} // namespace

int main()
{
	const QVector<vibestudio::CompilerKnownIssueDescriptor> issues = vibestudio::compilerKnownIssueDescriptors();
	const QStringList auditIssueIds = {
		QStringLiteral("485"),
		QStringLiteral("484"),
		QStringLiteral("483"),
		QStringLiteral("482"),
		QStringLiteral("480"),
		QStringLiteral("475"),
		QStringLiteral("470"),
		QStringLiteral("464"),
		QStringLiteral("463"),
		QStringLiteral("456"),
		QStringLiteral("455"),
		QStringLiteral("451"),
		QStringLiteral("450"),
		QStringLiteral("449"),
		QStringLiteral("444"),
		QStringLiteral("443"),
		QStringLiteral("441"),
		QStringLiteral("439"),
		QStringLiteral("437"),
		QStringLiteral("436"),
		QStringLiteral("435"),
		QStringLiteral("425"),
		QStringLiteral("422"),
		QStringLiteral("417"),
		QStringLiteral("416"),
		QStringLiteral("415"),
		QStringLiteral("413"),
		QStringLiteral("411"),
		QStringLiteral("405"),
		QStringLiteral("401"),
		QStringLiteral("400"),
		QStringLiteral("399"),
		QStringLiteral("390"),
		QStringLiteral("389"),
		QStringLiteral("377"),
		QStringLiteral("375"),
		QStringLiteral("374"),
		QStringLiteral("364"),
		QStringLiteral("351"),
		QStringLiteral("350"),
		QStringLiteral("349"),
		QStringLiteral("346"),
		QStringLiteral("344"),
		QStringLiteral("343"),
		QStringLiteral("342"),
		QStringLiteral("335"),
		QStringLiteral("333"),
		QStringLiteral("327"),
		QStringLiteral("310"),
		QStringLiteral("309"),
		QStringLiteral("308"),
		QStringLiteral("301"),
		QStringLiteral("298"),
		QStringLiteral("297"),
		QStringLiteral("293"),
		QStringLiteral("291"),
		QStringLiteral("289"),
		QStringLiteral("288"),
		QStringLiteral("287"),
		QStringLiteral("285"),
		QStringLiteral("281"),
		QStringLiteral("278"),
		QStringLiteral("277"),
		QStringLiteral("270"),
		QStringLiteral("268"),
		QStringLiteral("264"),
		QStringLiteral("262"),
		QStringLiteral("257"),
		QStringLiteral("256"),
		QStringLiteral("255"),
		QStringLiteral("254"),
		QStringLiteral("249"),
		QStringLiteral("247"),
		QStringLiteral("246"),
		QStringLiteral("245"),
		QStringLiteral("241"),
		QStringLiteral("238"),
		QStringLiteral("234"),
		QStringLiteral("233"),
		QStringLiteral("231"),
		QStringLiteral("230"),
		QStringLiteral("225"),
		QStringLiteral("223"),
		QStringLiteral("222"),
		QStringLiteral("221"),
		QStringLiteral("217"),
		QStringLiteral("215"),
		QStringLiteral("214"),
		QStringLiteral("213"),
		QStringLiteral("209"),
		QStringLiteral("207"),
		QStringLiteral("205"),
		QStringLiteral("203"),
		QStringLiteral("201"),
		QStringLiteral("200"),
		QStringLiteral("199"),
		QStringLiteral("195"),
		QStringLiteral("194"),
		QStringLiteral("193"),
		QStringLiteral("192"),
		QStringLiteral("190"),
		QStringLiteral("189"),
		QStringLiteral("186"),
		QStringLiteral("185"),
		QStringLiteral("173"),
		QStringLiteral("172"),
		QStringLiteral("167"),
		QStringLiteral("145"),
		QStringLiteral("140"),
		QStringLiteral("136"),
		QStringLiteral("135"),
		QStringLiteral("134"),
		QStringLiteral("131"),
		QStringLiteral("129"),
		QStringLiteral("122"),
		QStringLiteral("121"),
		QStringLiteral("114"),
		QStringLiteral("109"),
		QStringLiteral("106"),
		QStringLiteral("105"),
		QStringLiteral("103"),
		QStringLiteral("91"),
		QStringLiteral("87"),
		QStringLiteral("81"),
		QStringLiteral("77"),
	};

	const QStringList knownIssueIds = vibestudio::compilerKnownIssueIds();
	if (issues.size() < auditIssueIds.size() || knownIssueIds.size() < auditIssueIds.size()) {
		return fail("Expected known-issue catalog coverage for every audited open ericw-tools issue.");
	}
	QStringList deduplicatedIssueIds = knownIssueIds;
	deduplicatedIssueIds.removeDuplicates();
	if (deduplicatedIssueIds.size() != knownIssueIds.size()) {
		return fail("Expected known-issue catalog IDs to be unique.");
	}
	for (const QString& issueId : auditIssueIds) {
		if (!knownIssueIds.contains(issueId)) {
			return fail("Expected every audited open ericw-tools issue ID to be cataloged.");
		}
	}
	if (!knownIssueIds.contains(QStringLiteral("483"))) {
		return fail("Expected provenance issue #483.");
	}

	const QStringList highIssueIds = {
		QStringLiteral("485"),
		QStringLiteral("484"),
		QStringLiteral("483"),
		QStringLiteral("482"),
		QStringLiteral("475"),
		QStringLiteral("470"),
		QStringLiteral("463"),
		QStringLiteral("456"),
		QStringLiteral("451"),
		QStringLiteral("450"),
		QStringLiteral("444"),
		QStringLiteral("441"),
		QStringLiteral("439"),
		QStringLiteral("435"),
		QStringLiteral("422"),
		QStringLiteral("417"),
		QStringLiteral("405"),
		QStringLiteral("399"),
		QStringLiteral("390"),
		QStringLiteral("389"),
		QStringLiteral("375"),
		QStringLiteral("364"),
		QStringLiteral("350"),
		QStringLiteral("346"),
		QStringLiteral("344"),
		QStringLiteral("343"),
		QStringLiteral("333"),
		QStringLiteral("308"),
		QStringLiteral("301"),
		QStringLiteral("293"),
		QStringLiteral("289"),
		QStringLiteral("288"),
		QStringLiteral("287"),
		QStringLiteral("278"),
		QStringLiteral("270"),
		QStringLiteral("264"),
		QStringLiteral("257"),
		QStringLiteral("245"),
		QStringLiteral("231"),
		QStringLiteral("230"),
		QStringLiteral("221"),
		QStringLiteral("207"),
		QStringLiteral("201"),
		QStringLiteral("199"),
		QStringLiteral("194"),
		QStringLiteral("192"),
		QStringLiteral("173"),
		QStringLiteral("167"),
		QStringLiteral("135"),
		QStringLiteral("122"),
		QStringLiteral("114"),
		QStringLiteral("87"),
	};
	for (const QString& issueId : highIssueIds) {
		vibestudio::CompilerKnownIssueDescriptor highIssue;
		if (!vibestudio::compilerKnownIssueForId(issueId, &highIssue) || !highIssue.highValue) {
			return fail("Expected every High-row audit issue to be present and high-value.");
		}
	}

	vibestudio::CompilerKnownIssueDescriptor issue;
	if (!vibestudio::compilerKnownIssueForId(QStringLiteral("#475"), &issue) || issue.clusterId != QStringLiteral("D02") || !issue.highValue) {
		return fail("Expected #475 to resolve as a high-value D02 issue.");
	}
	if (issue.warningText.isEmpty() || issue.actionText.isEmpty() || issue.matchKeywords.isEmpty()) {
		return fail("Expected issue warning, action, and match keywords.");
	}

	const QVector<vibestudio::CompilerKnownIssueDescriptor> lightIssues = vibestudio::compilerKnownIssuesForProfile(QStringLiteral("ericw-light"));
	if (lightIssues.size() < 15) {
		return fail("Expected light profile to expose known lighting issues.");
	}
	const QVector<vibestudio::CompilerKnownIssueDescriptor> prefabIssues = vibestudio::compilerKnownIssuesForCluster(QStringLiteral("D05"));
	if (prefabIssues.size() < 5) {
		return fail("Expected D05 prefab cluster issues.");
	}

	const QVector<vibestudio::CompilerKnownIssueMatch> matches = vibestudio::matchCompilerKnownIssues(
		QStringLiteral("warning: _external_map_classname was missing on misc_external_map"),
		QStringLiteral("ericw-qbsp"),
		QStringLiteral("ericw-qbsp"));
	bool foundExternalMapClassname = false;
	for (const vibestudio::CompilerKnownIssueMatch& match : matches) {
		if (match.issue.issueId == QStringLiteral("194")) {
			foundExternalMapClassname = true;
		}
	}
	if (!foundExternalMapClassname) {
		return fail("Expected keyword matching for external-map classname issue.");
	}

	const QString text = vibestudio::compilerKnownIssueText(issue);
	if (!text.contains(QStringLiteral("D02")) || !text.contains(QStringLiteral("https://github.com/ericwa/ericw-tools/issues/475"))) {
		return fail("Expected formatted known-issue text to include cluster and upstream URL.");
	}

	const QStringList lightWarnings = vibestudio::ericwKnownIssuePlanWarnings(
		QStringLiteral("ericw-light"),
		QStringLiteral("maps/test.1.bsp"),
		{QStringLiteral("-bspxhdr"), QStringLiteral("BSPX")});
	int lightingDirWarningCount = 0;
	int dottedNameWarningCount = 0;
	for (const QString& warning : lightWarnings) {
		if (warning.contains(QStringLiteral("#484"))) {
			++lightingDirWarningCount;
		}
		if (warning.contains(QStringLiteral("#230"))) {
			++dottedNameWarningCount;
		}
	}
	if (lightingDirWarningCount != 1 || dottedNameWarningCount != 1) {
		return fail("Expected useful deduplicated light known-issue plan warnings.");
	}

	const QStringList qbspWarnings = vibestudio::ericwKnownIssuePlanWarnings(
		QStringLiteral("ericw-qbsp"),
		QStringLiteral("maps/csg_fail.map"),
		{QStringLiteral("-notex"), QStringLiteral("missing texture")});
	int noTextureWarningCount = 0;
	int csgWarningCount = 0;
	for (const QString& warning : qbspWarnings) {
		if (warning.contains(QStringLiteral("#450"))) {
			++noTextureWarningCount;
		}
		if (warning.contains(QStringLiteral("#114"))) {
			++csgWarningCount;
		}
	}
	if (noTextureWarningCount != 1 || csgWarningCount != 1) {
		return fail("Expected useful deduplicated qbsp known-issue plan warnings.");
	}
	if (!vibestudio::ericwKnownIssuePlanWarnings(QStringLiteral("q3map2-bsp"), QString(), {}).isEmpty()) {
		return fail("Expected ericw known-issue plan warnings to stay scoped to ericw profiles.");
	}

	return EXIT_SUCCESS;
}
