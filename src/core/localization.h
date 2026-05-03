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

struct PluralizationSmokeSample {
	QString localeName;
	int count = 0;
	QString localizedNumber;
	QString text;
	bool singular = false;
	bool localizedNumberVisible = false;
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
	bool pluralizationSmokeOk = false;
	QStringList rightToLeftLocales;
	LocaleFormattingSample formatting;
	QVector<PluralizationSmokeSample> pluralization;
	QVector<TranslationExpansionLayoutCheck> layoutChecks;
	QVector<TranslationCatalogStatus> catalogs;
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
QString normalizedLocalizationTargetId(const QString& localeName);
QString pseudoLocalizeText(const QString& text);
QString translationExpansionText(const QString& text);
QStringList expectedTranslationCatalogFileNames();
LocaleFormattingSample localeFormattingSample(const QString& localeName);
QVector<PluralizationSmokeSample> pluralizationSmokeSamples(const QString& localeName);
QVector<TranslationExpansionLayoutCheck> translationExpansionLayoutChecks();
LocalizationSmokeReport buildLocalizationSmokeReport(const QString& localeName = QString(), const QString& catalogRootPath = QString());
QString localizationSmokeReportText(const LocalizationSmokeReport& report);

} // namespace vibestudio
