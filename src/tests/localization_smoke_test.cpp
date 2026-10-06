#include "core/localization.h"
#include "core/studio_settings.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTranslator>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	return file.write(bytes) == bytes.size();
}

QByteArray catalogFixture(const QString& language, bool complete)
{
	QByteArray bytes;
	bytes += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE TS>\n";
	bytes += "<TS version=\"2.1\" language=\"" + language.toUtf8() + "\">\n";
	bytes += "<context>\n\t<name>VibeStudioLocalization</name>\n";
	bytes += "\t<message>\n\t\t<source>Package</source>\n\t\t<translation>Package</translation>\n\t</message>\n";
	if (complete) {
		bytes += "\t<message>\n\t\t<source>Compiler</source>\n\t\t<translation>Compiler</translation>\n\t</message>\n";
	} else {
		bytes += "\t<message>\n\t\t<source>Compiler</source>\n\t\t<translation type=\"unfinished\"></translation>\n\t</message>\n";
	}
	bytes += "</context>\n</TS>\n";
	return bytes;
}

// Supplies a translated plural form so the report can tell "no translator" from
// "plural forms verified".
class PluralTranslator final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr, int n = -1) const override
	{
		Q_UNUSED(disambiguation);
		if (qstrcmp(context, "VibeStudioLocalization") != 0 || qstrcmp(sourceText, "%n package(s) ready") != 0 || n < 0) {
			return {};
		}
		return QStringLiteral("%n Pakete bereit");
	}
};

bool runTargetSmoke()
{
	bool ok = true;
	const QVector<vibestudio::LocalizationTarget> targets = vibestudio::localizationTargets();
	ok &= expect(targets.size() == 20 && vibestudio::localizationTargetIds() == vibestudio::supportedLocaleNames(),
		"Expected documented 20-language localization target set to drive settings.");
	ok &= expect(vibestudio::normalizedLocalizationTargetId(QStringLiteral("pt_BR")) == QStringLiteral("pt-BR") && vibestudio::normalizedLocalizationTargetId(QStringLiteral("missing")) == QStringLiteral("en"),
		"Expected locale target normalization.");

	// Shipped RTL targets plus the general right-to-left script set.
	for (const QString& rtl : {QStringLiteral("ar"), QStringLiteral("ur"), QStringLiteral("ur-PK"), QStringLiteral("he"), QStringLiteral("fa"), QStringLiteral("ps"), QStringLiteral("sd"), QStringLiteral("ug"), QStringLiteral("dv"), QStringLiteral("ckb"), QStringLiteral("yi")}) {
		ok &= expect(vibestudio::isRightToLeftLocale(rtl), "Expected right-to-left detection for a right-to-left locale.");
		ok &= expect(vibestudio::localeLayoutDirection(rtl) == Qt::RightToLeft, "Expected a right-to-left layout direction.");
	}
	for (const QString& ltr : {QStringLiteral("en"), QStringLiteral("de"), QStringLiteral("pt-BR"), QStringLiteral("ja"), QStringLiteral("not-a-locale")}) {
		ok &= expect(!vibestudio::isRightToLeftLocale(ltr), "Expected left-to-right detection for a left-to-right locale.");
		ok &= expect(vibestudio::localeLayoutDirection(ltr) == Qt::LeftToRight, "Expected a left-to-right layout direction.");
	}
	ok &= expect(vibestudio::rightToLeftLanguageCodes().contains(QStringLiteral("ar")) && vibestudio::rightToLeftLanguageCodes().contains(QStringLiteral("ur")),
		"Expected Arabic and Urdu in the right-to-left language set.");

	const QString pseudo = vibestudio::pseudoLocalizeText(QStringLiteral("Compiler finished"));
	ok &= expect(pseudo.startsWith(QStringLiteral("[!! ")) && pseudo.endsWith(QStringLiteral(" !!]")) && pseudo != QStringLiteral("Compiler finished"),
		"Expected pseudo-localized text expansion.");
	ok &= expect(vibestudio::translationExpansionText(QStringLiteral("Package")).size() > QStringLiteral("Package").size(),
		"Expected translation expansion sample to grow.");

	const QVector<vibestudio::TranslationExpansionLayoutCheck> layoutChecks = vibestudio::translationExpansionLayoutChecks();
	ok &= expect(layoutChecks.size() >= 5, "Expected multiple translation expansion layout checks.");
	for (const vibestudio::TranslationExpansionLayoutCheck& check : layoutChecks) {
		ok &= expect(check.passed && check.expansionRatio >= 1.30 && check.expandedLength > check.sourceLength,
			"Expected translation expansion layout smoke checks to pass with expanded text.");
	}

	const vibestudio::LocaleFormattingSample german = vibestudio::localeFormattingSample(QStringLiteral("de"));
	ok &= expect(!german.decimalNumber.isEmpty() && !german.date.isEmpty() && !german.duration.isEmpty() && german.sortedLabels.size() == 4,
		"Expected locale formatting samples for numbers, dates, durations, and sorting.");
	return ok;
}

// The plural smoke must report what it verified, not perform string surgery.
bool runPluralizationSmoke()
{
	bool ok = true;
	const QVector<vibestudio::PluralizationSmokeSample> plurals = vibestudio::pluralizationSmokeSamples(QStringLiteral("de"));
	ok &= expect(plurals.size() >= 4 && plurals.at(1).singular && !plurals.at(2).singular,
		"Expected pluralization smoke samples for zero, one, and many counts.");
	for (const vibestudio::PluralizationSmokeSample& sample : plurals) {
		ok &= expect(!sample.text.isEmpty(), "Expected the plural call site to return text.");
		ok &= expect(sample.sourceText == QStringLiteral("%n package(s) ready"), "Expected the untranslated source to be reported.");
		ok &= expect(sample.usedUntranslatedFallback, "Expected the untranslated fallback with no translator installed.");
		ok &= expect(!sample.pluralFormsFromTranslator, "Expected no claim of translated plural forms without a translator.");
		ok &= expect(sample.text == QStringLiteral("%1 package(s) ready").arg(sample.count), "Expected the fallback to be the source text with the count substituted.");
		ok &= expect(sample.countVisible, "Expected the substituted count to be visible.");
	}

	const vibestudio::LocalizationSmokeReport untranslated = vibestudio::buildLocalizationSmokeReport(QStringLiteral("de"));
	ok &= expect(untranslated.pluralizationSmokeOk, "Expected the plural call site to be verified.");
	ok &= expect(!untranslated.pluralFormsFromTranslator, "Expected the report to distinguish a missing translator from verified plural forms.");
	ok &= expect(untranslated.pluralizationNote.contains(QStringLiteral("No translator")), "Expected the report to say no translator is installed.");

	PluralTranslator translator;
	if (!QCoreApplication::installTranslator(&translator)) {
		return expect(false, "Expected the plural translator to install.");
	}
	const vibestudio::LocalizationSmokeReport translated = vibestudio::buildLocalizationSmokeReport(QStringLiteral("de"));
	ok &= expect(translated.pluralFormsFromTranslator, "Expected translated plural forms to be detected.");
	ok &= expect(translated.pluralizationNote.contains(QStringLiteral("installed translator")), "Expected the report to credit the installed translator.");
	for (const vibestudio::PluralizationSmokeSample& sample : translated.pluralization) {
		ok &= expect(!sample.usedUntranslatedFallback && sample.text.contains(QStringLiteral("Pakete")),
			"Expected the translated plural form to be reported verbatim.");
	}
	QCoreApplication::removeTranslator(&translator);
	return ok;
}

bool runCatalogRootSmoke(const QDir& root)
{
	bool ok = true;
	const QString catalogDir = root.filePath(QStringLiteral("fixture-i18n"));
	const QString otherDir = root.filePath(QStringLiteral("explicit-i18n"));
	if (!QDir().mkpath(catalogDir) || !QDir().mkpath(otherDir)) {
		return expect(false, "Expected catalog fixture directories to be created.");
	}
	if (!writeFile(QDir(catalogDir).filePath(QStringLiteral("vibestudio_en.ts")), catalogFixture(QStringLiteral("en"), true))
		|| !writeFile(QDir(catalogDir).filePath(QStringLiteral("vibestudio_de.ts")), catalogFixture(QStringLiteral("de"), false))
		|| !writeFile(QDir(catalogDir).filePath(QStringLiteral("vibestudio_de.qm")), QByteArray("not-a-real-qm"))) {
		return expect(false, "Expected catalog fixtures to be written.");
	}

	ok &= expect(vibestudio::translationCatalogRootEnvironmentVariable() == QStringLiteral("VIBESTUDIO_I18N_DIR"),
		"Expected the documented catalog root environment variable.");

	// No environment override: the resolver still reports every directory it
	// tried, so an installed binary can explain itself.
	const vibestudio::CatalogRootResolution defaultResolution = vibestudio::resolveTranslationCatalogRoot();
	ok &= expect(!defaultResolution.rootPath.isEmpty() && !defaultResolution.candidatesTried.isEmpty(),
		"Expected catalog root resolution to report the directories it tried.");

	qputenv("VIBESTUDIO_I18N_DIR", QDir::toNativeSeparators(catalogDir).toUtf8());
	const vibestudio::CatalogRootResolution environmentResolution = vibestudio::resolveTranslationCatalogRoot();
	ok &= expect(environmentResolution.exists && environmentResolution.source == QStringLiteral("environment"),
		"Expected the environment override to win when no argument is given.");
	ok &= expect(QDir(environmentResolution.rootPath) == QDir(catalogDir), "Expected the environment override to resolve to its directory.");

	const vibestudio::CatalogRootResolution argumentResolution = vibestudio::resolveTranslationCatalogRoot(otherDir);
	ok &= expect(argumentResolution.exists && argumentResolution.source == QStringLiteral("argument") && QDir(argumentResolution.rootPath) == QDir(otherDir),
		"Expected an explicit argument to win over the environment override.");

	const vibestudio::CatalogRootResolution missingArgument = vibestudio::resolveTranslationCatalogRoot(root.filePath(QStringLiteral("no-such-i18n")));
	ok &= expect(missingArgument.source == QStringLiteral("environment") && QDir(missingArgument.rootPath) == QDir(catalogDir),
		"Expected a missing explicit directory to fall through to the next candidate.");
	ok &= expect(missingArgument.candidatesTried.size() >= 2, "Expected every tried directory to be listed.");

	// Ordered .qm candidates: exact locale, base language, then the fallback.
	const QStringList candidates = vibestudio::compiledCatalogCandidatePaths(QStringLiteral("pt_BR"));
	ok &= expect(candidates.size() == 3, "Expected three compiled catalog candidates for a region locale.");
	if (candidates.size() == 3) {
		ok &= expect(candidates.at(0).endsWith(QStringLiteral("vibestudio_pt_BR.qm")), "Expected the exact locale catalog first.");
		ok &= expect(candidates.at(1).endsWith(QStringLiteral("vibestudio_pt.qm")), "Expected the base language catalog second.");
		ok &= expect(candidates.at(2).endsWith(QStringLiteral("vibestudio_en.qm")), "Expected the fallback catalog last.");
		ok &= expect(QDir(QFileInfo(candidates.at(0)).absolutePath()) == QDir(catalogDir), "Expected candidates to sit in the resolved catalog root.");
	}
	const QStringList englishCandidates = vibestudio::compiledCatalogCandidatePaths(QStringLiteral("en"));
	ok &= expect(englishCandidates.size() == 1 && englishCandidates.at(0).endsWith(QStringLiteral("vibestudio_en.qm")),
		"Expected the fallback locale to produce a single candidate.");

	// Which catalogs exist as .ts and which are compiled to .qm.
	const QVector<vibestudio::TranslationCatalogAvailability> availability = vibestudio::translationCatalogAvailability();
	ok &= expect(availability.size() == vibestudio::localizationTargetIds().size(), "Expected availability for every localization target.");
	int sourcePresent = 0;
	int compiledPresent = 0;
	for (const vibestudio::TranslationCatalogAvailability& entry : availability) {
		if (entry.sourcePresent) {
			++sourcePresent;
		}
		if (entry.compiledPresent) {
			++compiledPresent;
			ok &= expect(entry.localeName == QStringLiteral("de"), "Expected only the German fixture to be compiled.");
		}
	}
	ok &= expect(sourcePresent == 2, "Expected the two fixture source catalogs to be found.");
	ok &= expect(compiledPresent == 1, "Expected exactly one compiled fixture catalog.");

	// Catalogs outside the shape lupdate writes are read by the XML parser
	// instead of the byte scan: a comment must not change the counts, and a
	// truncated file must read as invalid.
	QByteArray commented = catalogFixture(QStringLiteral("fr"), true);
	commented.replace("<context>", "<!-- <message> kept in a comment -->\n<context>");
	QByteArray truncated = catalogFixture(QStringLiteral("es"), true);
	truncated.truncate(truncated.indexOf("</message>"));
	if (!writeFile(QDir(catalogDir).filePath(QStringLiteral("vibestudio_fr.ts")), commented)
		|| !writeFile(QDir(catalogDir).filePath(QStringLiteral("vibestudio_es.ts")), truncated)) {
		return expect(false, "Expected the parser fixtures to be written.");
	}

	const vibestudio::LocalizationSmokeReport report = vibestudio::buildLocalizationSmokeReport(QStringLiteral("ar"));
	const auto catalogFor = [&report](const QString& localeName) -> const vibestudio::TranslationCatalogStatus* {
		for (const vibestudio::TranslationCatalogStatus& catalog : report.catalogs) {
			if (catalog.localeName == localeName) {
				return &catalog;
			}
		}
		return nullptr;
	};
	const vibestudio::TranslationCatalogStatus* english = catalogFor(QStringLiteral("en"));
	const vibestudio::TranslationCatalogStatus* german = catalogFor(QStringLiteral("de"));
	const vibestudio::TranslationCatalogStatus* french = catalogFor(QStringLiteral("fr"));
	const vibestudio::TranslationCatalogStatus* spanish = catalogFor(QStringLiteral("es"));
	ok &= expect(english && english->status == QStringLiteral("complete") && english->messageCount == 2 && english->translatedCount == 2,
		"Expected the complete fixture catalog to count two translated messages.");
	ok &= expect(german && german->status == QStringLiteral("needs-translation") && german->messageCount == 2 && german->unfinishedCount == 1,
		"Expected the fixture catalog with an unfinished message to need translation.");
	ok &= expect(french && french->status == QStringLiteral("complete") && french->messageCount == 2 && french->translatedCount == 2,
		"Expected a catalog with a comment to be parsed as XML with the same counts.");
	ok &= expect(spanish && spanish->status == QStringLiteral("invalid") && spanish->stale,
		"Expected a truncated catalog to be reported as invalid.");
	ok &= expect(QDir(report.catalogRoot.rootPath) == QDir(catalogDir) && report.catalogRoot.source == QStringLiteral("environment"),
		"Expected the report to use the resolved catalog root.");
	ok &= expect(report.compiledCatalogCount == 1, "Expected the report to count compiled catalogs.");
	ok &= expect(report.catalogs.size() >= 21, "Expected the report to inspect every expected catalog file.");
	ok &= expect(report.rightToLeftLocales.contains(QStringLiteral("ar")) && report.rightToLeftLocales.contains(QStringLiteral("ur")),
		"Expected right-to-left targets in the report.");
	ok &= expect(report.expansionSmokeOk && report.expansionRatio >= 1.30, "Expected translation expansion smoke coverage.");
	ok &= expect(report.expansionLayoutSmokeOk && report.layoutChecks.size() >= 5, "Expected translation expansion layout smoke coverage.");
	ok &= expect(report.staleCatalogCount > 0 && report.untranslatedMessageCount > 0, "Expected stale/untranslated translation catalog reporting.");
	ok &= expect(!report.ok, "Expected a fixture root with missing catalogs to be reported as not ok.");
	const QString reportText = vibestudio::localizationSmokeReportText(report);
	ok &= expect(reportText.contains(QStringLiteral("Localization smoke report")), "Expected text localization smoke report.");
	ok &= expect(reportText.contains(QStringLiteral("Catalog root:")), "Expected the report text to name the resolved catalog root.");
	ok &= expect(reportText.contains(QStringLiteral("Plural call site:")), "Expected the report text to say what the plural smoke verified.");

	qunsetenv("VIBESTUDIO_I18N_DIR");
	const vibestudio::CatalogRootResolution clearedResolution = vibestudio::resolveTranslationCatalogRoot();
	ok &= expect(!clearedResolution.candidatesTried.contains(QDir::cleanPath(QDir(catalogDir).absolutePath())),
		"Expected the environment override to stop being tried once it is cleared.");
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected a writable temporary directory.");
	}
	const QDir root(tempDir.path());

	bool ok = true;
	ok &= runTargetSmoke();
	ok &= runPluralizationSmoke();
	ok &= runCatalogRootSmoke(root);
	if (!ok) {
		return fail("localization smoke test failed.");
	}
	return EXIT_SUCCESS;
}
