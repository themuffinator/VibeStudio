#include "core/localization.h"

#include <QCoreApplication>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QTimeZone>
#include <QXmlStreamReader>

#include <algorithm>

namespace vibestudio {

namespace {

QString localizationText(const char* source)
{
	return QString::fromUtf8(source);
}

QString normalizedId(const QString& localeName)
{
	QString normalized = localeName.trimmed().replace('_', '-');
	if (normalized.isEmpty()) {
		return QStringLiteral("en");
	}
	return normalized;
}

QString pseudoMap(QChar character)
{
	const ushort code = character.unicode();
	switch (code) {
	case 'a':
		return QStringLiteral("à");
	case 'A':
		return QStringLiteral("À");
	case 'e':
		return QStringLiteral("ë");
	case 'E':
		return QStringLiteral("Ë");
	case 'i':
		return QStringLiteral("ï");
	case 'I':
		return QStringLiteral("Ï");
	case 'o':
		return QStringLiteral("ö");
	case 'O':
		return QStringLiteral("Ö");
	case 'u':
		return QStringLiteral("ü");
	case 'U':
		return QStringLiteral("Ü");
	case 'c':
		return QStringLiteral("ç");
	case 'C':
		return QStringLiteral("Ç");
	case 'n':
		return QStringLiteral("ñ");
	case 'N':
		return QStringLiteral("Ñ");
	default:
		break;
	}
	return QString(character);
}

QString catalogFileName(const QString& localeName)
{
	QString id = localeName;
	id.replace('-', '_');
	return QStringLiteral("vibestudio_%1.ts").arg(id);
}

QString quantityLabel(int count, const QString& singular, const QString& plural)
{
	return count == 1 ? singular : plural;
}

const char* pluralSmokeSource()
{
	return QT_TRANSLATE_NOOP("VibeStudioLocalization", "%n package(s) ready");
}

QString paddedLocaleNumber(const QLocale& locale, int value)
{
	const QString number = locale.toString(value);
	if (number.size() >= 2) {
		return number;
	}
	return locale.toString(0) + number;
}

TranslationCatalogStatus inspectTranslationCatalog(const QDir& catalogRoot, const QString& fileName)
{
	TranslationCatalogStatus status;
	status.fileName = fileName;
	status.localeName = fileName.mid(QStringLiteral("vibestudio_").size());
	status.localeName.chop(QStringLiteral(".ts").size());
	status.localeName.replace('_', '-');
	status.path = catalogRoot.filePath(fileName);
	const QFileInfo info(status.path);
	status.present = info.exists() && info.isFile();
	if (!status.present) {
		status.status = localizationText("missing");
		status.issues.push_back(localizationText("catalog file is missing"));
		return status;
	}

	QFile file(status.path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		status.status = localizationText("unreadable");
		status.stale = true;
		status.issues.push_back(localizationText("catalog file could not be read"));
		return status;
	}

	QXmlStreamReader xml(&file);
	bool inMessage = false;
	bool messageHasTranslation = false;
	while (!xml.atEnd()) {
		xml.readNext();
		if (xml.isStartElement()) {
			if (xml.name() == QLatin1String("message")) {
				inMessage = true;
				messageHasTranslation = false;
				++status.messageCount;
			} else if (inMessage && xml.name() == QLatin1String("translation")) {
				messageHasTranslation = true;
				const QString translationType = xml.attributes().value(QStringLiteral("type")).toString();
				if (translationType == QLatin1String("unfinished")) {
					++status.unfinishedCount;
				} else if (translationType == QLatin1String("obsolete")) {
					++status.obsoleteCount;
				} else if (translationType == QLatin1String("vanished")) {
					++status.vanishedCount;
				} else {
					++status.translatedCount;
				}
			}
		} else if (xml.isEndElement() && xml.name() == QLatin1String("message")) {
			if (!messageHasTranslation) {
				++status.unfinishedCount;
			}
			inMessage = false;
		}
	}

	if (xml.hasError()) {
		status.status = localizationText("invalid");
		status.stale = true;
		status.issues.push_back(localizationText("XML parse error: %1").arg(xml.errorString()));
		return status;
	}

	status.stale = status.unfinishedCount > 0 || status.obsoleteCount > 0 || status.vanishedCount > 0;
	if (status.unfinishedCount > 0) {
		status.issues.push_back(localizationText("%1 %2")
			.arg(status.unfinishedCount)
			.arg(quantityLabel(status.unfinishedCount, localizationText("unfinished translation"), localizationText("unfinished translations"))));
	}
	if (status.obsoleteCount > 0) {
		status.issues.push_back(localizationText("%1 %2")
			.arg(status.obsoleteCount)
			.arg(quantityLabel(status.obsoleteCount, localizationText("obsolete translation"), localizationText("obsolete translations"))));
	}
	if (status.vanishedCount > 0) {
		status.issues.push_back(localizationText("%1 %2")
			.arg(status.vanishedCount)
			.arg(quantityLabel(status.vanishedCount, localizationText("vanished translation"), localizationText("vanished translations"))));
	}

	if (status.stale) {
		status.status = localizationText("needs-translation");
	} else if (status.messageCount == 0) {
		status.status = localizationText("empty");
		status.issues.push_back(localizationText("catalog has no messages"));
	} else {
		status.status = localizationText("complete");
	}
	return status;
}

PluralizationSmokeSample buildPluralizationSample(const QString& localeName, int count)
{
	const QLocale locale(normalizedLocalizationTargetId(localeName));
	PluralizationSmokeSample sample;
	sample.localeName = normalizedLocalizationTargetId(localeName);
	sample.count = count;
	sample.localizedNumber = locale.toString(count);
	sample.singular = count == 1;
	QString translated = QCoreApplication::translate("VibeStudioLocalization", pluralSmokeSource(), nullptr, count);
	if (translated.contains(QStringLiteral("package(s)"))) {
		translated.replace(QStringLiteral("package(s)"), sample.singular ? localizationText("package") : localizationText("packages"));
	}
	if (!translated.contains(sample.localizedNumber)) {
		translated.replace(QString::number(count), sample.localizedNumber);
		translated.replace(QStringLiteral("%n"), sample.localizedNumber);
	}
	sample.text = translated;
	sample.localizedNumberVisible = sample.text.contains(sample.localizedNumber);
	return sample;
}

TranslationExpansionLayoutCheck buildLayoutCheck(const QString& surfaceId, const QString& label, const QString& sourceText, int maxRecommendedCharacters, const QString& recommendation)
{
	TranslationExpansionLayoutCheck check;
	check.surfaceId = surfaceId;
	check.label = label;
	check.sourceText = sourceText;
	check.expandedText = translationExpansionText(sourceText);
	check.sourceLength = sourceText.size();
	check.expandedLength = check.expandedText.size();
	check.maxRecommendedCharacters = maxRecommendedCharacters;
	check.expansionRatio = check.sourceLength > 0 ? static_cast<double>(check.expandedLength) / static_cast<double>(check.sourceLength) : 0.0;
	check.passed = check.sourceLength > 0 && check.expansionRatio >= 1.30 && check.expandedLength <= check.maxRecommendedCharacters;
	check.recommendation = recommendation;
	return check;
}

} // namespace

QVector<LocalizationTarget> localizationTargets()
{
	return {
		{QStringLiteral("en"), localizationText("English"), localizationText("English"), false},
		{QStringLiteral("zh-Hans"), localizationText("Chinese (Simplified)"), QString::fromUtf8("简体中文"), false},
		{QStringLiteral("hi"), localizationText("Hindi"), QString::fromUtf8("हिन्दी"), false},
		{QStringLiteral("es"), localizationText("Spanish"), QString::fromUtf8("Español"), false},
		{QStringLiteral("fr"), localizationText("French"), QString::fromUtf8("Français"), false},
		{QStringLiteral("ar"), localizationText("Arabic"), QString::fromUtf8("العربية"), true},
		{QStringLiteral("bn"), localizationText("Bengali"), QString::fromUtf8("বাংলা"), false},
		{QStringLiteral("pt-BR"), localizationText("Portuguese (Brazil)"), QString::fromUtf8("Português (Brasil)"), false},
		{QStringLiteral("ru"), localizationText("Russian"), QString::fromUtf8("Русский"), false},
		{QStringLiteral("ur"), localizationText("Urdu"), QString::fromUtf8("اردو"), true},
		{QStringLiteral("id"), localizationText("Indonesian"), localizationText("Bahasa Indonesia"), false},
		{QStringLiteral("de"), localizationText("German"), localizationText("Deutsch"), false},
		{QStringLiteral("ja"), localizationText("Japanese"), QString::fromUtf8("日本語"), false},
		{QStringLiteral("pcm"), localizationText("Nigerian Pidgin"), localizationText("Naija"), false},
		{QStringLiteral("mr"), localizationText("Marathi"), QString::fromUtf8("मराठी"), false},
		{QStringLiteral("te"), localizationText("Telugu"), QString::fromUtf8("తెలుగు"), false},
		{QStringLiteral("tr"), localizationText("Turkish"), QString::fromUtf8("Türkçe"), false},
		{QStringLiteral("ta"), localizationText("Tamil"), QString::fromUtf8("தமிழ்"), false},
		{QStringLiteral("vi"), localizationText("Vietnamese"), QString::fromUtf8("Tiếng Việt"), false},
		{QStringLiteral("ko"), localizationText("Korean"), QString::fromUtf8("한국어"), false},
	};
}

QStringList localizationTargetIds()
{
	QStringList ids;
	for (const LocalizationTarget& target : localizationTargets()) {
		ids.push_back(target.localeName);
	}
	return ids;
}

bool localizationTargetForId(const QString& localeName, LocalizationTarget* out)
{
	const QString requested = normalizedId(localeName);
	for (const LocalizationTarget& target : localizationTargets()) {
		if (QString::compare(target.localeName, requested, Qt::CaseInsensitive) == 0) {
			if (out) {
				*out = target;
			}
			return true;
		}
	}
	const QString languageOnly = requested.section('-', 0, 0);
	for (const LocalizationTarget& target : localizationTargets()) {
		if (QString::compare(target.localeName.section('-', 0, 0), languageOnly, Qt::CaseInsensitive) == 0) {
			if (out) {
				*out = target;
			}
			return true;
		}
	}
	return false;
}

bool isRightToLeftLocale(const QString& localeName)
{
	LocalizationTarget target;
	return localizationTargetForId(localeName, &target) && target.rightToLeft;
}

QString normalizedLocalizationTargetId(const QString& localeName)
{
	LocalizationTarget target;
	if (localizationTargetForId(localeName, &target)) {
		return target.localeName;
	}
	return QStringLiteral("en");
}

QString pseudoLocalizeText(const QString& text)
{
	QString mapped;
	mapped.reserve(text.size() * 2);
	for (QChar character : text) {
		mapped += pseudoMap(character);
		if (character.isLetter() && character.isLower()) {
			mapped += character;
		}
	}
	return QString::fromUtf8("[!! %1 !!]").arg(mapped);
}

QString translationExpansionText(const QString& text)
{
	const QString expanded = QStringLiteral("%1 %2").arg(text, pseudoLocalizeText(text));
	return expanded.left(std::max(text.size() + 8, expanded.size()));
}

QStringList expectedTranslationCatalogFileNames()
{
	QStringList names;
	for (const QString& localeName : localizationTargetIds()) {
		names.push_back(catalogFileName(localeName));
	}
	names.push_back(QStringLiteral("vibestudio_pseudo.ts"));
	names.removeDuplicates();
	names.sort(Qt::CaseInsensitive);
	return names;
}

LocaleFormattingSample localeFormattingSample(const QString& localeName)
{
	const QString normalized = normalizedLocalizationTargetId(localeName);
	const QLocale locale(normalized);
	const QDateTime sampleDateTime(QDate(2026, 5, 3), QTime(14, 35, 12), QTimeZone::UTC);
	LocaleFormattingSample sample;
	sample.localeName = normalized;
	sample.decimalNumber = locale.toString(12345.678, 'f', 2);
	sample.integerNumber = locale.toString(1234567);
	sample.date = locale.toString(sampleDateTime.date(), QLocale::LongFormat);
	sample.time = locale.toString(sampleDateTime.time(), QLocale::ShortFormat);
	sample.dateTime = locale.toString(sampleDateTime, QLocale::ShortFormat);
	sample.size = QStringLiteral("%1 KiB").arg(locale.toString(1536.0 / 1024.0, 'f', 1));
	sample.duration = QStringLiteral("%1:%2:%3")
		.arg(paddedLocaleNumber(locale, 1))
		.arg(paddedLocaleNumber(locale, 2))
		.arg(paddedLocaleNumber(locale, 9));
	sample.sortedLabels = {localizationText("Package"), localizationText("Compiler"), localizationText("Asset"), localizationText("Map")};
	QCollator collator(locale);
	std::sort(sample.sortedLabels.begin(), sample.sortedLabels.end(), [&collator](const QString& left, const QString& right) {
		return collator.compare(left, right) < 0;
	});
	return sample;
}

QVector<PluralizationSmokeSample> pluralizationSmokeSamples(const QString& localeName)
{
	const QString normalized = normalizedLocalizationTargetId(localeName);
	return {
		buildPluralizationSample(normalized, 0),
		buildPluralizationSample(normalized, 1),
		buildPluralizationSample(normalized, 2),
		buildPluralizationSample(normalized, 12),
	};
}

QVector<TranslationExpansionLayoutCheck> translationExpansionLayoutChecks()
{
	return {
		buildLayoutCheck(
			QStringLiteral("toolbar-button"),
			localizationText("Toolbar Button"),
			localizationText("Open Project"),
			64,
			localizationText("Tool buttons should allow icon-plus-text labels to wrap or elide cleanly.")),
		buildLayoutCheck(
			QStringLiteral("mode-rail-label"),
			localizationText("Mode Rail Label"),
			localizationText("Package Manager"),
			72,
			localizationText("Mode rail labels should keep icons visible and provide full text through tooltips.")),
		buildLayoutCheck(
			QStringLiteral("status-chip"),
			localizationText("Status Chip"),
			localizationText("Validation warning"),
			76,
			localizationText("Status chips should keep their icon and non-color cue visible under translation expansion.")),
		buildLayoutCheck(
			QStringLiteral("detail-drawer-title"),
			localizationText("Detail Drawer Title"),
			localizationText("Compiler output details"),
			96,
			localizationText("Detail drawer headings should reserve space for expanded translated labels.")),
		buildLayoutCheck(
			QStringLiteral("setup-step-title"),
			localizationText("Setup Step Title"),
			localizationText("Choose language and accessibility"),
			128,
			localizationText("Setup step headings should tolerate longer translated text at large UI scales.")),
		buildLayoutCheck(
			QStringLiteral("command-palette-row"),
			localizationText("Command Palette Row"),
			localizationText("Create diagnostic bundle"),
			104,
			localizationText("Command palette rows should keep command names, shortcuts, and status cues readable.")),
	};
}

LocalizationSmokeReport buildLocalizationSmokeReport(const QString& localeName, const QString& catalogRootPath)
{
	LocalizationSmokeReport report;
	report.localeName = normalizedLocalizationTargetId(localeName);
	report.targets = localizationTargets();
	report.pseudoSample = pseudoLocalizeText(localizationText("Open package and run compiler"));
	const QString expansionSource = localizationText("Compiler finished");
	report.expansionSourceLength = expansionSource.size();
	report.expansionSample = translationExpansionText(expansionSource);
	report.expansionSampleLength = report.expansionSample.size();
	report.expansionRatio = report.expansionSourceLength > 0 ? static_cast<double>(report.expansionSampleLength) / static_cast<double>(report.expansionSourceLength) : 0.0;
	report.expansionSmokeOk = report.expansionRatio >= 1.30;
	report.pluralization = pluralizationSmokeSamples(report.localeName);
	report.pluralizationSmokeOk = std::all_of(report.pluralization.cbegin(), report.pluralization.cend(), [](const PluralizationSmokeSample& sample) {
		return !sample.text.trimmed().isEmpty() && sample.localizedNumberVisible;
	});
	report.layoutChecks = translationExpansionLayoutChecks();
	report.expansionLayoutSmokeOk = std::all_of(report.layoutChecks.cbegin(), report.layoutChecks.cend(), [](const TranslationExpansionLayoutCheck& check) {
		return check.passed;
	});
	report.formatting = localeFormattingSample(report.localeName);
	for (const LocalizationTarget& target : report.targets) {
		if (target.rightToLeft) {
			report.rightToLeftLocales.push_back(target.localeName);
		}
	}

	const QDir catalogRoot(catalogRootPath.trimmed().isEmpty() ? QStringLiteral("i18n") : catalogRootPath);
	for (const QString& fileName : expectedTranslationCatalogFileNames()) {
		TranslationCatalogStatus status = inspectTranslationCatalog(catalogRoot, fileName);
		if (!status.present) {
			report.ok = false;
			report.warnings.push_back(localizationText("Missing translation catalog: %1").arg(fileName));
		}
		if (status.status == QLatin1String("invalid") || status.status == QLatin1String("unreadable")) {
			report.ok = false;
			report.warnings.push_back(localizationText("Invalid translation catalog: %1").arg(fileName));
		}
		if (status.stale) {
			++report.staleCatalogCount;
		}
		report.untranslatedMessageCount += status.unfinishedCount;
		report.obsoleteMessageCount += status.obsoleteCount + status.vanishedCount;
		report.catalogs.push_back(status);
	}
	report.catalogCount = report.catalogs.size();

	if (report.targets.size() < 20) {
		report.ok = false;
		report.warnings.push_back(localizationText("Localization target set is smaller than the documented 20-language target."));
	}
	if (!report.rightToLeftLocales.contains(QStringLiteral("ar")) || !report.rightToLeftLocales.contains(QStringLiteral("ur"))) {
		report.ok = false;
		report.warnings.push_back(localizationText("Right-to-left smoke set must include Arabic and Urdu."));
	}
	if (!report.expansionSmokeOk) {
		report.ok = false;
		report.warnings.push_back(localizationText("Translation expansion smoke sample did not grow enough to stress layouts."));
	}
	if (!report.pluralizationSmokeOk) {
		report.ok = false;
		report.warnings.push_back(localizationText("Pluralization smoke samples did not include visible localized counts."));
	}
	if (!report.expansionLayoutSmokeOk) {
		report.ok = false;
		report.warnings.push_back(localizationText("Translation expansion layout smoke checks exceeded a recommended text budget."));
	}
	return report;
}

QString localizationSmokeReportText(const LocalizationSmokeReport& report)
{
	QStringList lines;
	lines << localizationText("Localization smoke report");
	lines << localizationText("Locale: %1").arg(report.localeName);
	lines << localizationText("Targets: %1").arg(report.targets.size());
	lines << localizationText("Right-to-left: %1").arg(report.rightToLeftLocales.join(QStringLiteral(", ")));
	lines << localizationText("Pseudo: %1").arg(report.pseudoSample);
	lines << localizationText("Expansion: %1").arg(report.expansionSample);
	lines << localizationText("Expansion ratio: %1").arg(QLocale::c().toString(report.expansionRatio, 'f', 2));
	lines << localizationText("Expansion layout checks: %1").arg(report.expansionLayoutSmokeOk ? localizationText("passed") : localizationText("failed"));
	lines << localizationText("Pluralization: %1").arg(report.pluralizationSmokeOk ? localizationText("passed") : localizationText("failed"));
	lines << localizationText("Number: %1").arg(report.formatting.decimalNumber);
	lines << localizationText("Date: %1").arg(report.formatting.date);
	lines << localizationText("Duration: %1").arg(report.formatting.duration);
	for (const PluralizationSmokeSample& sample : report.pluralization) {
		lines << localizationText("- plural %1: %2").arg(sample.count).arg(sample.text);
	}
	for (const TranslationExpansionLayoutCheck& check : report.layoutChecks) {
		lines << localizationText("- layout %1: %2/%3 chars (%4)")
			.arg(check.surfaceId)
			.arg(check.expandedLength)
			.arg(check.maxRecommendedCharacters)
			.arg(check.passed ? localizationText("ok") : localizationText("over budget"));
	}
	lines << localizationText("Catalogs: %1 total, %2 needing translation, %3 unfinished messages, %4 obsolete/vanished messages")
		.arg(report.catalogCount)
		.arg(report.staleCatalogCount)
		.arg(report.untranslatedMessageCount)
		.arg(report.obsoleteMessageCount);
	for (const TranslationCatalogStatus& catalog : report.catalogs) {
		QString detail = localizationText("- %1: %2 (%3 messages, %4 translated, %5 unfinished)")
			.arg(catalog.fileName)
			.arg(catalog.status)
			.arg(catalog.messageCount)
			.arg(catalog.translatedCount)
			.arg(catalog.unfinishedCount);
		if (!catalog.issues.isEmpty()) {
			detail += localizationText(" - %1").arg(catalog.issues.join(QStringLiteral("; ")));
		}
		lines << detail;
	}
	if (!report.warnings.isEmpty()) {
		lines << localizationText("Warnings:");
		for (const QString& warning : report.warnings) {
			lines << localizationText("- %1").arg(warning);
		}
	}
	return lines.join('\n');
}

} // namespace vibestudio
