#include "core/localization.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QDir>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	const QVector<vibestudio::LocalizationTarget> targets = vibestudio::localizationTargets();
	if (targets.size() != 20 || vibestudio::localizationTargetIds() != vibestudio::supportedLocaleNames()) {
		return fail("Expected documented 20-language localization target set to drive settings.");
	}
	if (!vibestudio::isRightToLeftLocale(QStringLiteral("ar")) || !vibestudio::isRightToLeftLocale(QStringLiteral("ur")) || vibestudio::isRightToLeftLocale(QStringLiteral("en"))) {
		return fail("Expected Arabic and Urdu right-to-left smoke coverage.");
	}
	if (vibestudio::normalizedLocalizationTargetId(QStringLiteral("pt_BR")) != QStringLiteral("pt-BR") || vibestudio::normalizedLocalizationTargetId(QStringLiteral("missing")) != QStringLiteral("en")) {
		return fail("Expected locale target normalization.");
	}

	const QString pseudo = vibestudio::pseudoLocalizeText(QStringLiteral("Compiler finished"));
	if (!pseudo.startsWith(QStringLiteral("[!! ")) || !pseudo.endsWith(QStringLiteral(" !!]")) || pseudo == QStringLiteral("Compiler finished")) {
		return fail("Expected pseudo-localized text expansion.");
	}
	if (vibestudio::translationExpansionText(QStringLiteral("Package")).size() <= QStringLiteral("Package").size()) {
		return fail("Expected translation expansion sample to grow.");
	}
	const QVector<vibestudio::PluralizationSmokeSample> plurals = vibestudio::pluralizationSmokeSamples(QStringLiteral("de"));
	if (plurals.size() < 4 || plurals.at(1).singular != true || plurals.at(2).singular != false) {
		return fail("Expected pluralization smoke samples for zero, one, and many counts.");
	}
	for (const vibestudio::PluralizationSmokeSample& sample : plurals) {
		if (sample.text.isEmpty() || !sample.localizedNumberVisible) {
			return fail("Expected pluralization smoke text to include visible localized counts.");
		}
	}
	const QVector<vibestudio::TranslationExpansionLayoutCheck> layoutChecks = vibestudio::translationExpansionLayoutChecks();
	if (layoutChecks.size() < 5) {
		return fail("Expected multiple translation expansion layout checks.");
	}
	for (const vibestudio::TranslationExpansionLayoutCheck& check : layoutChecks) {
		if (!check.passed || check.expansionRatio < 1.30 || check.expandedLength <= check.sourceLength) {
			return fail("Expected translation expansion layout smoke checks to pass with expanded text.");
		}
	}

	const vibestudio::LocaleFormattingSample german = vibestudio::localeFormattingSample(QStringLiteral("de"));
	if (german.decimalNumber.isEmpty() || german.date.isEmpty() || german.duration.isEmpty() || german.sortedLabels.size() != 4) {
		return fail("Expected locale formatting samples for numbers, dates, durations, and sorting.");
	}

	const QString catalogRoot = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../i18n"));
	const vibestudio::LocalizationSmokeReport report = vibestudio::buildLocalizationSmokeReport(QStringLiteral("ar"), catalogRoot);
	if (!report.ok || report.catalogs.size() < 21 || !report.rightToLeftLocales.contains(QStringLiteral("ar")) || !report.rightToLeftLocales.contains(QStringLiteral("ur"))) {
		return fail("Expected localization smoke report to include catalogs and RTL targets.");
	}
	if (!report.expansionSmokeOk || report.expansionRatio < 1.30) {
		return fail("Expected translation expansion smoke coverage.");
	}
	if (!report.pluralizationSmokeOk || report.pluralization.size() < 4) {
		return fail("Expected pluralization smoke coverage in localization report.");
	}
	if (!report.expansionLayoutSmokeOk || report.layoutChecks.size() < 5) {
		return fail("Expected translation expansion layout smoke coverage in localization report.");
	}
	if (report.staleCatalogCount <= 0 || report.untranslatedMessageCount <= 0) {
		return fail("Expected stale/untranslated translation catalog reporting.");
	}
	if (!vibestudio::localizationSmokeReportText(report).contains(QStringLiteral("Localization smoke report"))) {
		return fail("Expected text localization smoke report.");
	}

	return EXIT_SUCCESS;
}
