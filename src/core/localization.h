#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct LocalizationTarget {
	QString localeName;
	QString englishName;
	QString nativeName;
	bool rightToLeft = false;
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
	int obsoleteMessageCount = 0;
	QStringList warnings;
	bool ok = true;
};

QVector<LocalizationTarget> localizationTargets();
QStringList localizationTargetIds();
bool localizationTargetForId(const QString& localeName, LocalizationTarget* out = nullptr);
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
QVector<PluralizationSmokeSample> pluralizationSmokeSamples(const QString& localeName);
QVector<TranslationExpansionLayoutCheck> translationExpansionLayoutChecks();
LocalizationSmokeReport buildLocalizationSmokeReport(const QString& localeName = QString(), const QString& catalogRootPath = QString());
QString localizationSmokeReportText(const LocalizationSmokeReport& report);

} // namespace vibestudio
