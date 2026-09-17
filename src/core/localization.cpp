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
#include <utility>

namespace vibestudio {

namespace {

QString localizationText(const char* source)
{
	return QCoreApplication::translate("VibeStudioLocalization", source);
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

// Catalog file names use the Qt convention: <app>_<locale>.ts / .qm with an
// underscore-separated locale id (vibestudio_pt_BR.ts).
QString catalogStem(const QString& localeName)
{
	QString id = localeName;
	id.replace('-', '_');
	return QStringLiteral("vibestudio_%1").arg(id);
}

QString catalogFileName(const QString& localeName)
{
	return QStringLiteral("%1.ts").arg(catalogStem(localeName));
}

QString compiledCatalogFileName(const QString& localeName)
{
	return QStringLiteral("%1.qm").arg(catalogStem(localeName));
}

void appendCandidate(QStringList* candidates, const QString& path)
{
	if (!candidates || path.trimmed().isEmpty()) {
		return;
	}
	const QString cleaned = QDir::cleanPath(QDir(path).absolutePath());
	if (!candidates->contains(cleaned)) {
		candidates->push_back(cleaned);
	}
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
		status.status = QStringLiteral("missing");
		status.issues.push_back(localizationText("catalog file is missing"));
		return status;
	}

	QFile file(status.path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		status.status = QStringLiteral("unreadable");
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
		status.status = QStringLiteral("invalid");
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
		status.status = QStringLiteral("needs-translation");
	} else if (status.messageCount == 0) {
		status.status = QStringLiteral("empty");
		status.issues.push_back(localizationText("catalog has no messages"));
	} else {
		status.status = QStringLiteral("complete");
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
	sample.sourceText = QString::fromUtf8(pluralSmokeSource());

	// Report what the call site really returns. Qt substitutes %n even when no
	// translator is installed, so the fallback is the source text with the
	// count inserted; anything else means a translator supplied a plural form.
	const QString untranslatedFallback = QString(sample.sourceText).replace(QStringLiteral("%n"), QString::number(count));
	sample.text = QCoreApplication::translate("VibeStudioLocalization", pluralSmokeSource(), nullptr, count);
	sample.usedUntranslatedFallback = sample.text == untranslatedFallback;
	sample.pluralFormsFromTranslator = !sample.usedUntranslatedFallback && !sample.text.trimmed().isEmpty();
	sample.countVisible = sample.text.contains(QString::number(count)) || sample.text.contains(sample.localizedNumber);
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

QStringList rightToLeftLanguageCodes()
{
	// ISO 639-1/639-3 codes written in right-to-left scripts (Arabic, Hebrew,
	// Thaana, Syriac, N'Ko, Adlam). Reference: Unicode CLDR "characterOrder"
	// per language - https://cldr.unicode.org/translation/getting-started/layout
	return {
		QStringLiteral("ar"), // Arabic
		QStringLiteral("arc"), // Aramaic
		QStringLiteral("ckb"), // Central Kurdish
		QStringLiteral("dv"), // Divehi (Thaana)
		QStringLiteral("fa"), // Persian
		QStringLiteral("ff"), // Fulah (Adlam)
		QStringLiteral("he"), // Hebrew
		QStringLiteral("iw"), // Hebrew (legacy code)
		QStringLiteral("ks"), // Kashmiri
		QStringLiteral("nqo"), // N'Ko
		QStringLiteral("pnb"), // Western Punjabi (Shahmukhi)
		QStringLiteral("ps"), // Pashto
		QStringLiteral("sd"), // Sindhi
		QStringLiteral("syr"), // Syriac
		QStringLiteral("ug"), // Uyghur
		QStringLiteral("ur"), // Urdu
		QStringLiteral("yi"), // Yiddish
	};
}

bool isRightToLeftLocale(const QString& localeName)
{
	LocalizationTarget target;
	if (localizationTargetForId(localeName, &target) && target.rightToLeft) {
		return true;
	}

	const QString requested = normalizedId(localeName);
	const QString languageOnly = requested.section('-', 0, 0).toLower();
	for (const QString& code : rightToLeftLanguageCodes()) {
		if (code == languageOnly) {
			return true;
		}
	}

	// Anything Qt itself knows to be right-to-left, including script-tagged ids
	// such as "az-Arab", still counts. An id Qt cannot parse falls back to the
	// default locale, so the resolved language is checked before trusting it.
	const QLocale locale(requested);
	const QString resolvedLanguage = locale.name().section('_', 0, 0).toLower();
	return !languageOnly.isEmpty() && resolvedLanguage == languageOnly && locale.textDirection() == Qt::RightToLeft;
}

Qt::LayoutDirection localeLayoutDirection(const QString& localeName)
{
	return isRightToLeftLocale(localeName) ? Qt::RightToLeft : Qt::LeftToRight;
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

QString translationCatalogRootEnvironmentVariable()
{
	return QStringLiteral("VIBESTUDIO_I18N_DIR");
}

CatalogRootResolution resolveTranslationCatalogRoot(const QString& explicitCatalogRootPath)
{
	struct Candidate {
		QString path;
		QString source;
	};

	QVector<Candidate> candidates;
	const QString explicitPath = explicitCatalogRootPath.trimmed();
	if (!explicitPath.isEmpty()) {
		candidates.push_back({explicitPath, QStringLiteral("argument")});
	}

	const QString environmentPath = qEnvironmentVariable("VIBESTUDIO_I18N_DIR").trimmed();
	if (!environmentPath.isEmpty()) {
		candidates.push_back({environmentPath, QStringLiteral("environment")});
	}

	const QString applicationDir = QCoreApplication::applicationDirPath();
	if (!applicationDir.isEmpty()) {
		// Installed layout: prefix/bin/vibestudio -> prefix/share/vibestudio/i18n.
		// Development layout: builddir/src/vibestudio -> builddir/i18n.
		candidates.push_back({applicationDir + QStringLiteral("/i18n"), QStringLiteral("application-dir")});
		candidates.push_back({applicationDir + QStringLiteral("/../i18n"), QStringLiteral("application-parent")});
		candidates.push_back({applicationDir + QStringLiteral("/../share/vibestudio/i18n"), QStringLiteral("installed-share")});
		candidates.push_back({applicationDir + QStringLiteral("/../../i18n"), QStringLiteral("application-parent")});
	}
	candidates.push_back({QDir::currentPath() + QStringLiteral("/i18n"), QStringLiteral("working-directory")});

	// A candidate only wins if it actually holds catalogs. Merely existing is not
	// enough: a development build writes compiled `.qm` files into
	// <builddir>/i18n, which would otherwise shadow the source tree's `i18n/`
	// and make every catalog look missing to the status report. Source `.ts`
	// catalogs win, then compiled `.qm` ones, then bare existence.
	const QStringList expected = expectedTranslationCatalogFileNames();
	const auto catalogCount = [&expected](const QDir& directory, const QString& suffix) {
		int found = 0;
		for (const QString& fileName : expected) {
			QString candidateName = fileName;
			if (suffix != QStringLiteral("ts")) {
				candidateName.chop(2);
				candidateName.append(suffix);
			}
			if (QFileInfo::exists(directory.filePath(candidateName))) {
				++found;
			}
		}
		return found;
	};

	CatalogRootResolution resolution;
	Candidate sourceMatch;
	Candidate compiledMatch;
	Candidate existingMatch;
	for (const Candidate& candidate : std::as_const(candidates)) {
		appendCandidate(&resolution.candidatesTried, candidate.path);
		const QDir directory(candidate.path);
		if (!directory.exists()) {
			continue;
		}
		// An explicit argument and the environment override are instructions, not
		// guesses: they win on existence alone, even when empty, because silently
		// looking somewhere else would be worse than reporting no catalogs.
		const bool userChosen = candidate.source == QStringLiteral("argument")
			|| candidate.source == QStringLiteral("environment");
		if (userChosen) {
			resolution.rootPath = QDir::cleanPath(directory.absolutePath());
			resolution.source = candidate.source;
			resolution.exists = true;
			return resolution;
		}
		if (existingMatch.path.isEmpty()) {
			existingMatch = candidate;
		}
		if (sourceMatch.path.isEmpty() && catalogCount(directory, QStringLiteral("ts")) > 0) {
			sourceMatch = candidate;
		}
		if (compiledMatch.path.isEmpty() && catalogCount(directory, QStringLiteral("qm")) > 0) {
			compiledMatch = candidate;
		}
	}

	const Candidate winner = !sourceMatch.path.isEmpty()
		? sourceMatch
		: (!compiledMatch.path.isEmpty() ? compiledMatch : existingMatch);
	if (!winner.path.isEmpty()) {
		resolution.rootPath = QDir::cleanPath(QDir(winner.path).absolutePath());
		resolution.source = winner.source;
		resolution.exists = true;
		return resolution;
	}

	// Nothing resolved: keep the historical relative path so the caller still
	// gets a usable (if empty) report, and say where we looked.
	resolution.rootPath = QStringLiteral("i18n");
	resolution.source = QStringLiteral("fallback");
	resolution.exists = false;
	return resolution;
}

QStringList compiledCatalogCandidatePaths(const QString& localeName, const QString& catalogRootPath)
{
	const QDir root(resolveTranslationCatalogRoot(catalogRootPath).rootPath);
	const QString requested = normalizedId(localeName);
	const QString languageOnly = requested.section('-', 0, 0);

	QStringList paths;
	const auto append = [&paths, &root](const QString& locale) {
		if (locale.trimmed().isEmpty()) {
			return;
		}
		const QString path = root.filePath(compiledCatalogFileName(locale));
		if (!paths.contains(path)) {
			paths.push_back(path);
		}
	};

	// Exact locale, then the base language, then the source-language fallback.
	append(requested);
	append(languageOnly);
	append(QStringLiteral("en"));
	return paths;
}

QVector<TranslationCatalogAvailability> translationCatalogAvailability(const QString& catalogRootPath)
{
	const QDir root(resolveTranslationCatalogRoot(catalogRootPath).rootPath);
	QVector<TranslationCatalogAvailability> availability;
	for (const QString& localeName : localizationTargetIds()) {
		TranslationCatalogAvailability entry;
		entry.localeName = localeName;
		entry.sourceFileName = catalogFileName(localeName);
		entry.compiledFileName = compiledCatalogFileName(localeName);
		entry.sourcePath = root.filePath(entry.sourceFileName);
		entry.compiledPath = root.filePath(entry.compiledFileName);
		entry.sourcePresent = QFileInfo(entry.sourcePath).isFile();
		entry.compiledPresent = QFileInfo(entry.compiledPath).isFile();
		availability.push_back(entry);
	}
	return availability;
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
	// What is actually verified: the plural call site exists and returns text
	// with the count substituted. Whether translated plural forms exist is a
	// separate, explicitly reported fact.
	report.pluralizationSmokeOk = std::all_of(report.pluralization.cbegin(), report.pluralization.cend(), [](const PluralizationSmokeSample& sample) {
		return !sample.text.trimmed().isEmpty() && sample.countVisible;
	});
	report.pluralFormsFromTranslator = std::all_of(report.pluralization.cbegin(), report.pluralization.cend(), [](const PluralizationSmokeSample& sample) {
		return sample.pluralFormsFromTranslator;
	});
	report.pluralizationNote = report.pluralFormsFromTranslator
		? localizationText("Plural forms were supplied by an installed translator.")
		: localizationText("No translator is installed for this context: the plural call site was verified and the untranslated fallback was returned.");
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

	report.catalogRoot = resolveTranslationCatalogRoot(catalogRootPath);
	if (!report.catalogRoot.exists) {
		report.warnings.push_back(localizationText("No translation catalog directory was found; tried: %1")
			.arg(report.catalogRoot.candidatesTried.join(QStringLiteral(", "))));
	}
	report.catalogAvailability = translationCatalogAvailability(report.catalogRoot.rootPath);
	for (const TranslationCatalogAvailability& entry : std::as_const(report.catalogAvailability)) {
		if (entry.compiledPresent) {
			++report.compiledCatalogCount;
		}
	}

	const QDir catalogRoot(report.catalogRoot.rootPath);
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
		report.warnings.push_back(localizationText("Pluralization smoke samples did not substitute the count into the plural call site."));
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
	lines << localizationText("Catalog root: %1 (%2)").arg(report.catalogRoot.rootPath, report.catalogRoot.exists ? report.catalogRoot.source : localizationText("not found"));
	if (!report.catalogRoot.exists && !report.catalogRoot.candidatesTried.isEmpty()) {
		lines << localizationText("Catalog root candidates: %1").arg(report.catalogRoot.candidatesTried.join(QStringLiteral(", ")));
	}
	lines << localizationText("Plural call site: %1").arg(report.pluralizationSmokeOk ? localizationText("verified") : localizationText("not verified"));
	lines << localizationText("Plural forms: %1").arg(report.pluralizationNote);
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
	lines << localizationText("Compiled catalogs (.qm): %1 of %2 targets")
		.arg(report.compiledCatalogCount)
		.arg(report.catalogAvailability.size());
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
