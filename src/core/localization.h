#pragma once

#include <QDateTime>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct LocalizationTarget {
	// BCP 47 id, such as "pt-BR"; the catalog is vibestudio_pt_BR.ts.
	QString localeName;
	// The language's name in the interface language (translated).
	QString englishName;
	// The language's name in itself, never translated.
	QString nativeName;
	// ISO 15924 script code, such as "Latn", "Arab", "Hant".
	QString script;
	bool rightToLeft = false;
};

// A region format choice: the locale numbers, dates, and sizes are written in.
struct RegionFormatChoice {
	QString localeName;    // BCP 47, such as "en-GB"
	QString displayName;   // "English (United Kingdom)", in the region's own language
};

struct LocaleFormattingSample {
	QString localeName;
	QString decimalNumber;
	QString integerNumber;
	QString date;
	QString time;
	QString dateTime;
	QString size;
	QString duration;
	QStringList sortedLabels;
};

// What the plural smoke sample actually observed. No string surgery is
// performed on the result, so `text` is exactly what the call site returned:
// with no translator installed Qt returns the source text with `%n` replaced by
// the count, which is what `usedUntranslatedFallback` reports.
struct PluralizationSmokeSample {
	QString localeName;
	int count = 0;
	QString localizedNumber;
	QString sourceText;
	QString text;
	bool singular = false;
	bool localizedNumberVisible = false;
	bool countVisible = false;
	bool usedUntranslatedFallback = true;
	bool pluralFormsFromTranslator = false;
};

// Where the .ts/.qm catalogs were looked for, in order, and which directory won.
struct CatalogRootResolution {
	QString rootPath;
	// One of: "argument", "environment", "application-dir", "application-parent",
	// "installed-share", "working-directory", "fallback".
	QString source;
	bool exists = false;
	QStringList candidatesTried;
};

struct TranslationCatalogAvailability {
	QString localeName;
	QString sourceFileName;
	QString compiledFileName;
	QString sourcePath;
	QString compiledPath;
	bool sourcePresent = false;
	bool compiledPresent = false;
};

struct TranslationExpansionLayoutCheck {
	QString surfaceId;
	QString label;
	QString sourceText;
	QString expandedText;
	int sourceLength = 0;
	int expandedLength = 0;
	int maxRecommendedCharacters = 0;
	double expansionRatio = 0.0;
	bool passed = false;
	QString recommendation;
};

struct TranslationCatalogStatus {
	QString localeName;
	QString fileName;
	QString path;
	bool present = false;
	bool stale = false;
	int messageCount = 0;
	int translatedCount = 0;
	int unfinishedCount = 0;
	// Unfinished messages that already carry text: drafts awaiting review.
	// lrelease compiles them, so the interface shows them.
	int draftedCount = 0;
	int obsoleteCount = 0;
	int vanishedCount = 0;
	QString status;
	QStringList issues;
};

struct LocalizationSmokeReport {
	QString localeName;
	QVector<LocalizationTarget> targets;
	QString pseudoSample;
	QString expansionSample;
	int expansionSourceLength = 0;
	int expansionSampleLength = 0;
	double expansionRatio = 0.0;
	bool expansionSmokeOk = false;
	bool expansionLayoutSmokeOk = false;
	// True when the plural call site returned text with the count substituted.
	// It does NOT claim that translated plural forms exist: that is
	// `pluralFormsFromTranslator`.
	bool pluralizationSmokeOk = false;
	bool pluralFormsFromTranslator = false;
	QString pluralizationNote;
	QStringList rightToLeftLocales;
	LocaleFormattingSample formatting;
	QVector<PluralizationSmokeSample> pluralization;
	QVector<TranslationExpansionLayoutCheck> layoutChecks;
	QVector<TranslationCatalogStatus> catalogs;
	CatalogRootResolution catalogRoot;
	QVector<TranslationCatalogAvailability> catalogAvailability;
	int compiledCatalogCount = 0;
	int catalogCount = 0;
	int staleCatalogCount = 0;
	int untranslatedMessageCount = 0;
	int draftedMessageCount = 0;
	int obsoleteMessageCount = 0;
	// What "follow the system" resolves to on this machine, and the region
	// format locale the report's formatting sample would use.
	QString systemTargetName;
	QStringList systemLanguages;
	QStringList warnings;
	bool ok = true;
};

QVector<LocalizationTarget> localizationTargets();
QStringList localizationTargetIds();
// Resolves regional and legacy ids to a target: "zh-TW" and "zh-Hant-HK" give
// Traditional Chinese, "es-MX" Latin American Spanish, "pt-AO" European
// Portuguese, "iw" Hebrew, "tl" Filipino, "no" and "nn" Norwegian Bokmål.
// "system" resolves to systemLocalizationTargetId().
bool localizationTargetForId(const QString& localeName, LocalizationTarget* out = nullptr);
// The stored preference that follows the operating system's language.
QString systemLocalizationPreferenceId();
bool isSystemLocalizationPreference(const QString& localeName);
// The first of `languageTags` (BCP 47, most preferred first) that resolves to
// a target, or "en".
QString preferredLocalizationTargetId(const QStringList& languageTags);
// The target for the operating system's interface languages. The
// VIBESTUDIO_SYSTEM_LANGUAGES environment variable (comma separated tags)
// stands in for the platform list, for tests.
QString systemLocalizationTargetId();
QStringList systemLanguageTags();
bool isRightToLeftLocale(const QString& localeName);
// Qt::RightToLeft for any RTL locale, including ones outside the shipped target
// set. The app layer calls this to set the layout direction.
Qt::LayoutDirection localeLayoutDirection(const QString& localeName);
QStringList rightToLeftLanguageCodes();
QString normalizedLocalizationTargetId(const QString& localeName);
QString pseudoLocalizeText(const QString& text);
QString translationExpansionText(const QString& text);
QStringList expectedTranslationCatalogFileNames();
QString translationCatalogRootEnvironmentVariable();
// Resolution order: explicit argument, VIBESTUDIO_I18N_DIR, <app dir>/i18n,
// <app dir>/../i18n, <app dir>/../share/vibestudio/i18n, <cwd>/i18n, then the
// relative "i18n" fallback. The result records every directory that was tried.
CatalogRootResolution resolveTranslationCatalogRoot(const QString& explicitCatalogRootPath = QString());
// Ordered .qm paths to try for a locale: exact match, base language, then the
// English fallback. Paths are returned whether or not they exist.
QStringList compiledCatalogCandidatePaths(const QString& localeName, const QString& catalogRootPath = QString());
QVector<TranslationCatalogAvailability> translationCatalogAvailability(const QString& catalogRootPath = QString());
LocaleFormattingSample localeFormattingSample(const QString& localeName);

// Region formats: how numbers, dates, times, and sizes are written, chosen
// apart from the interface language. The preference holds "system" (the
// operating system's regional settings, the default), "language" (the
// interface language's own conventions), or a BCP 47 locale such as "de-CH".
QString systemRegionFormatId();
QString languageRegionFormatId();
// The stored form of a region preference: one of the two keywords, or a
// locale Qt knows, written as BCP 47. Anything else becomes "system".
QString normalizedRegionFormatId(const QString& regionFormat);
// The locale a region preference and an interface language resolve to.
QLocale regionFormatLocale(const QString& regionFormat, const QString& languagePreference);
// Every regional locale Qt knows, one per BCP 47 name, sorted by display name.
QVector<RegionFormatChoice> regionFormatChoices();
QString regionFormatSample(const QLocale& locale);
QVector<PluralizationSmokeSample> pluralizationSmokeSamples(const QString& localeName);
QVector<TranslationExpansionLayoutCheck> translationExpansionLayoutChecks();
LocalizationSmokeReport buildLocalizationSmokeReport(const QString& localeName = QString(), const QString& catalogRootPath = QString());
QString localizationSmokeReportText(const LocalizationSmokeReport& report);

} // namespace vibestudio
