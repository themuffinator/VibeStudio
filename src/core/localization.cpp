#include "core/localization.h"

#include <QByteArrayView>
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
	return QT_TRANSLATE_N_NOOP("VibeStudioLocalization", "%n package(s) ready");
}

QString paddedLocaleNumber(const QLocale& locale, int value)
{
	const QString number = locale.toString(value);
	if (number.size() >= 2) {
		return number;
	}
	return locale.toString(0) + number;
}

struct CatalogCounts {
	int messages = 0;
	int translated = 0;
	int unfinished = 0;
	int obsolete = 0;
	int vanished = 0;
};

void countTranslationType(QByteArrayView type, CatalogCounts* counts)
{
	if (type == QByteArrayView("unfinished")) {
		++counts->unfinished;
	} else if (type == QByteArrayView("obsolete")) {
		++counts->obsolete;
	} else if (type == QByteArrayView("vanished")) {
		++counts->vanished;
	} else {
		++counts->translated;
	}
}

bool isXmlSpace(char character)
{
	return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

// The value of the attribute called name in a start tag, or an empty view.
QByteArrayView attributeValue(QByteArrayView tag, QByteArrayView name)
{
	for (qsizetype at = tag.indexOf(name); at > 0; at = tag.indexOf(name, at + 1)) {
		const qsizetype equals = at + name.size();
		if (!isXmlSpace(tag.at(at - 1)) || equals + 1 >= tag.size() || tag.at(equals) != '=') {
			continue;
		}
		const char quote = tag.at(equals + 1);
		const qsizetype end = quote == '"' || quote == '\'' ? tag.indexOf(quote, equals + 2) : -1;
		return end < 0 ? QByteArrayView() : tag.sliced(equals + 2, end - equals - 2);
	}
	return {};
}

// Counts messages and translation states straight from the bytes of a catalog
// written by lupdate or lconvert. Every catalog lists each source string and a
// report reads all 21 of them: through QXmlStreamReader that took 7.5 s in a
// debug build and 0.4 s in a release one, against 0.15 s and 0.03 s for this
// scan. Anything outside the shape those tools write (comments, CDATA, a
// message left open, no <TS> root) returns false, and the caller parses the
// file as XML instead, which also names what is wrong with it.
bool scanCatalogCounts(QByteArrayView bytes, CatalogCounts* counts)
{
	if (!bytes.contains("<TS") || !bytes.contains("</TS>") || bytes.contains("<!--") || bytes.contains("<![CDATA[")) {
		return false;
	}
	CatalogCounts scanned;
	for (qsizetype at = bytes.indexOf("<message"); at >= 0; at = bytes.indexOf("<message", at + 1)) {
		const qsizetype nameEnd = at + qsizetype(sizeof("<message") - 1);
		if (nameEnd >= bytes.size() || (bytes.at(nameEnd) != '>' && !isXmlSpace(bytes.at(nameEnd)))) {
			continue;
		}
		const qsizetype end = bytes.indexOf("</message>", nameEnd);
		const qsizetype next = bytes.indexOf("<message", nameEnd);
		if (end < 0 || (next >= 0 && next < end)) {
			return false;
		}
		++scanned.messages;
		const qsizetype translation = bytes.indexOf("<translation", nameEnd);
		if (translation < 0 || translation > end) {
			++scanned.unfinished;
			continue;
		}
		const qsizetype tagEnd = bytes.indexOf('>', translation);
		if (tagEnd < 0 || tagEnd > end) {
			return false;
		}
		countTranslationType(attributeValue(bytes.sliced(translation, tagEnd - translation), "type"), &scanned);
	}
	if (bytes.count("</message>") != scanned.messages) {
		return false;
	}
	*counts = scanned;
	return true;
}

// The full XML parse, for catalogs the scan does not recognise. Counts are kept
// up to the first error, which is returned in error.
bool parseCatalogCounts(QIODevice* device, CatalogCounts* counts, QString* error)
{
	QXmlStreamReader xml(device);
	bool inMessage = false;
	bool messageHasTranslation = false;
	while (!xml.atEnd()) {
		xml.readNext();
		if (xml.isStartElement()) {
			if (xml.name() == QLatin1String("message")) {
				inMessage = true;
				messageHasTranslation = false;
				++counts->messages;
			} else if (inMessage && xml.name() == QLatin1String("translation")) {
				messageHasTranslation = true;
				countTranslationType(xml.attributes().value(QStringLiteral("type")).toUtf8(), counts);
			}
		} else if (xml.isEndElement() && xml.name() == QLatin1String("message")) {
			if (!messageHasTranslation) {
				++counts->unfinished;
			}
			inMessage = false;
		}
	}
	if (xml.hasError()) {
		*error = xml.errorString();
		return false;
	}
	return true;
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
		status.issues.push_back(QCoreApplication::translate("VibeStudioLocalization", "catalog file is missing"));
		return status;
	}

	QFile file(status.path);
	if (!file.open(QIODevice::ReadOnly)) {
		status.status = QStringLiteral("unreadable");
		status.stale = true;
		status.issues.push_back(QCoreApplication::translate("VibeStudioLocalization", "catalog file could not be read"));
		return status;
	}

	CatalogCounts counts;
	QString parseError;
	bool parsed = scanCatalogCounts(file.readAll(), &counts);
	if (!parsed && file.seek(0)) {
		parsed = parseCatalogCounts(&file, &counts, &parseError);
	}
	status.messageCount = counts.messages;
	status.translatedCount = counts.translated;
	status.unfinishedCount = counts.unfinished;
	status.obsoleteCount = counts.obsolete;
	status.vanishedCount = counts.vanished;

	if (!parsed) {
		status.status = QStringLiteral("invalid");
		status.stale = true;
		status.issues.push_back(QCoreApplication::translate("VibeStudioLocalization", "XML parse error: %1").arg(parseError));
		return status;
	}

	status.stale = status.unfinishedCount > 0 || status.obsoleteCount > 0 || status.vanishedCount > 0;
	if (status.unfinishedCount > 0) {
		status.issues.push_back(QCoreApplication::translate("VibeStudioLocalization", "%1 %2")
			.arg(status.unfinishedCount)
			.arg(quantityLabel(status.unfinishedCount, QCoreApplication::translate("VibeStudioLocalization", "unfinished translation"), QCoreApplication::translate("VibeStudioLocalization", "unfinished translations"))));
	}
	if (status.obsoleteCount > 0) {
		status.issues.push_back(QCoreApplication::translate("VibeStudioLocalization", "%1 %2")
			.arg(status.obsoleteCount)
			.arg(quantityLabel(status.obsoleteCount, QCoreApplication::translate("VibeStudioLocalization", "obsolete translation"), QCoreApplication::translate("VibeStudioLocalization", "obsolete translations"))));
	}
	if (status.vanishedCount > 0) {
		status.issues.push_back(QCoreApplication::translate("VibeStudioLocalization", "%1 %2")
			.arg(status.vanishedCount)
			.arg(quantityLabel(status.vanishedCount, QCoreApplication::translate("VibeStudioLocalization", "vanished translation"), QCoreApplication::translate("VibeStudioLocalization", "vanished translations"))));
	}

	if (status.stale) {
		status.status = QStringLiteral("needs-translation");
	} else if (status.messageCount == 0) {
		status.status = QStringLiteral("empty");
		status.issues.push_back(QCoreApplication::translate("VibeStudioLocalization", "catalog has no messages"));
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
		{QStringLiteral("en"), QCoreApplication::translate("VibeStudioLocalization", "English"), QCoreApplication::translate("VibeStudioLocalization", "English"), false},
		{QStringLiteral("zh-Hans"), QCoreApplication::translate("VibeStudioLocalization", "Chinese (Simplified)"), QString::fromUtf8("简体中文"), false},
		{QStringLiteral("hi"), QCoreApplication::translate("VibeStudioLocalization", "Hindi"), QString::fromUtf8("हिन्दी"), false},
		{QStringLiteral("es"), QCoreApplication::translate("VibeStudioLocalization", "Spanish"), QString::fromUtf8("Español"), false},
		{QStringLiteral("fr"), QCoreApplication::translate("VibeStudioLocalization", "French"), QString::fromUtf8("Français"), false},
		{QStringLiteral("ar"), QCoreApplication::translate("VibeStudioLocalization", "Arabic"), QString::fromUtf8("العربية"), true},
		{QStringLiteral("bn"), QCoreApplication::translate("VibeStudioLocalization", "Bengali"), QString::fromUtf8("বাংলা"), false},
		{QStringLiteral("pt-BR"), QCoreApplication::translate("VibeStudioLocalization", "Portuguese (Brazil)"), QString::fromUtf8("Português (Brasil)"), false},
		{QStringLiteral("ru"), QCoreApplication::translate("VibeStudioLocalization", "Russian"), QString::fromUtf8("Русский"), false},
		{QStringLiteral("ur"), QCoreApplication::translate("VibeStudioLocalization", "Urdu"), QString::fromUtf8("اردو"), true},
		{QStringLiteral("id"), QCoreApplication::translate("VibeStudioLocalization", "Indonesian"), QCoreApplication::translate("VibeStudioLocalization", "Bahasa Indonesia"), false},
		{QStringLiteral("de"), QCoreApplication::translate("VibeStudioLocalization", "German"), QCoreApplication::translate("VibeStudioLocalization", "Deutsch"), false},
		{QStringLiteral("ja"), QCoreApplication::translate("VibeStudioLocalization", "Japanese"), QString::fromUtf8("日本語"), false},
		{QStringLiteral("pcm"), QCoreApplication::translate("VibeStudioLocalization", "Nigerian Pidgin"), QCoreApplication::translate("VibeStudioLocalization", "Naija"), false},
		{QStringLiteral("mr"), QCoreApplication::translate("VibeStudioLocalization", "Marathi"), QString::fromUtf8("मराठी"), false},
		{QStringLiteral("te"), QCoreApplication::translate("VibeStudioLocalization", "Telugu"), QString::fromUtf8("తెలుగు"), false},
		{QStringLiteral("tr"), QCoreApplication::translate("VibeStudioLocalization", "Turkish"), QString::fromUtf8("Türkçe"), false},
		{QStringLiteral("ta"), QCoreApplication::translate("VibeStudioLocalization", "Tamil"), QString::fromUtf8("தமிழ்"), false},
		{QStringLiteral("vi"), QCoreApplication::translate("VibeStudioLocalization", "Vietnamese"), QString::fromUtf8("Tiếng Việt"), false},
		{QStringLiteral("ko"), QCoreApplication::translate("VibeStudioLocalization", "Korean"), QString::fromUtf8("한국어"), false},
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
	const QDateTime sampleDateTime(QDate(2026, 5, 3), QTime(14, 35, 12), QTimeZone::utc());
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
	sample.sortedLabels = {QCoreApplication::translate("VibeStudioLocalization", "Package"), QCoreApplication::translate("VibeStudioLocalization", "Compiler"), QCoreApplication::translate("VibeStudioLocalization", "Asset"), QCoreApplication::translate("VibeStudioLocalization", "Map")};
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
			QCoreApplication::translate("VibeStudioLocalization", "Toolbar Button"),
			QCoreApplication::translate("VibeStudioLocalization", "Open Project"),
			64,
			QCoreApplication::translate("VibeStudioLocalization", "Tool buttons should allow icon-plus-text labels to wrap or elide cleanly.")),
		buildLayoutCheck(
			QStringLiteral("mode-rail-label"),
			QCoreApplication::translate("VibeStudioLocalization", "Mode Rail Label"),
			QCoreApplication::translate("VibeStudioLocalization", "Package Manager"),
			72,
			QCoreApplication::translate("VibeStudioLocalization", "Mode rail labels should keep icons visible and provide full text through tooltips.")),
		buildLayoutCheck(
			QStringLiteral("status-chip"),
			QCoreApplication::translate("VibeStudioLocalization", "Status Chip"),
			QCoreApplication::translate("VibeStudioLocalization", "Validation warning"),
			76,
			QCoreApplication::translate("VibeStudioLocalization", "Status chips should keep their icon and non-color cue visible under translation expansion.")),
		buildLayoutCheck(
			QStringLiteral("detail-drawer-title"),
			QCoreApplication::translate("VibeStudioLocalization", "Detail Drawer Title"),
			QCoreApplication::translate("VibeStudioLocalization", "Compiler output details"),
			96,
			QCoreApplication::translate("VibeStudioLocalization", "Detail drawer headings should reserve space for expanded translated labels.")),
		buildLayoutCheck(
			QStringLiteral("setup-step-title"),
			QCoreApplication::translate("VibeStudioLocalization", "Setup Step Title"),
			QCoreApplication::translate("VibeStudioLocalization", "Choose language and accessibility"),
			128,
			QCoreApplication::translate("VibeStudioLocalization", "Setup step headings should tolerate longer translated text at large UI scales.")),
		buildLayoutCheck(
			QStringLiteral("command-palette-row"),
			QCoreApplication::translate("VibeStudioLocalization", "Command Palette Row"),
			QCoreApplication::translate("VibeStudioLocalization", "Create diagnostic bundle"),
			104,
			QCoreApplication::translate("VibeStudioLocalization", "Command palette rows should keep command names, shortcuts, and status cues readable.")),
	};
}

LocalizationSmokeReport buildLocalizationSmokeReport(const QString& localeName, const QString& catalogRootPath)
{
	LocalizationSmokeReport report;
	report.localeName = normalizedLocalizationTargetId(localeName);
	report.targets = localizationTargets();
	report.pseudoSample = pseudoLocalizeText(QCoreApplication::translate("VibeStudioLocalization", "Open package and run compiler"));
	const QString expansionSource = QCoreApplication::translate("VibeStudioLocalization", "Compiler finished");
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
		? QCoreApplication::translate("VibeStudioLocalization", "Plural forms were supplied by an installed translator.")
		: QCoreApplication::translate("VibeStudioLocalization", "No translator is installed for this context: the plural call site was verified and the untranslated fallback was returned.");
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
		report.warnings.push_back(QCoreApplication::translate("VibeStudioLocalization", "No translation catalog directory was found; tried: %1")
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
			report.warnings.push_back(QCoreApplication::translate("VibeStudioLocalization", "Missing translation catalog: %1").arg(fileName));
		}
		if (status.status == QLatin1String("invalid") || status.status == QLatin1String("unreadable")) {
			report.ok = false;
			report.warnings.push_back(QCoreApplication::translate("VibeStudioLocalization", "Invalid translation catalog: %1").arg(fileName));
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
		report.warnings.push_back(QCoreApplication::translate("VibeStudioLocalization", "Localization target set is smaller than the documented 20-language target."));
	}
	if (!report.rightToLeftLocales.contains(QStringLiteral("ar")) || !report.rightToLeftLocales.contains(QStringLiteral("ur"))) {
		report.ok = false;
		report.warnings.push_back(QCoreApplication::translate("VibeStudioLocalization", "Right-to-left smoke set must include Arabic and Urdu."));
	}
	if (!report.expansionSmokeOk) {
		report.ok = false;
		report.warnings.push_back(QCoreApplication::translate("VibeStudioLocalization", "Translation expansion smoke sample did not grow enough to stress layouts."));
	}
	if (!report.pluralizationSmokeOk) {
		report.ok = false;
		report.warnings.push_back(QCoreApplication::translate("VibeStudioLocalization", "Pluralization smoke samples did not substitute the count into the plural call site."));
	}
	if (!report.expansionLayoutSmokeOk) {
		report.ok = false;
		report.warnings.push_back(QCoreApplication::translate("VibeStudioLocalization", "Translation expansion layout smoke checks exceeded a recommended text budget."));
	}
	return report;
}

QString localizationSmokeReportText(const LocalizationSmokeReport& report)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioLocalization", "Localization smoke report");
	lines << QCoreApplication::translate("VibeStudioLocalization", "Locale: %1").arg(report.localeName);
	lines << QCoreApplication::translate("VibeStudioLocalization", "Targets: %1").arg(report.targets.size());
	lines << QCoreApplication::translate("VibeStudioLocalization", "Right-to-left: %1").arg(report.rightToLeftLocales.join(QStringLiteral(", ")));
	lines << QCoreApplication::translate("VibeStudioLocalization", "Pseudo: %1").arg(report.pseudoSample);
	lines << QCoreApplication::translate("VibeStudioLocalization", "Expansion: %1").arg(report.expansionSample);
	lines << QCoreApplication::translate("VibeStudioLocalization", "Expansion ratio: %1").arg(QLocale::c().toString(report.expansionRatio, 'f', 2));
	lines << QCoreApplication::translate("VibeStudioLocalization", "Expansion layout checks: %1").arg(report.expansionLayoutSmokeOk ? QCoreApplication::translate("VibeStudioLocalization", "passed") : QCoreApplication::translate("VibeStudioLocalization", "failed"));
	lines << QCoreApplication::translate("VibeStudioLocalization", "Catalog root: %1 (%2)").arg(report.catalogRoot.rootPath, report.catalogRoot.exists ? report.catalogRoot.source : QCoreApplication::translate("VibeStudioLocalization", "not found"));
	if (!report.catalogRoot.exists && !report.catalogRoot.candidatesTried.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLocalization", "Catalog root candidates: %1").arg(report.catalogRoot.candidatesTried.join(QStringLiteral(", ")));
	}
	lines << QCoreApplication::translate("VibeStudioLocalization", "Plural call site: %1").arg(report.pluralizationSmokeOk ? QCoreApplication::translate("VibeStudioLocalization", "verified") : QCoreApplication::translate("VibeStudioLocalization", "not verified"));
	lines << QCoreApplication::translate("VibeStudioLocalization", "Plural forms: %1").arg(report.pluralizationNote);
	lines << QCoreApplication::translate("VibeStudioLocalization", "Number: %1").arg(report.formatting.decimalNumber);
	lines << QCoreApplication::translate("VibeStudioLocalization", "Date: %1").arg(report.formatting.date);
	lines << QCoreApplication::translate("VibeStudioLocalization", "Duration: %1").arg(report.formatting.duration);
	for (const PluralizationSmokeSample& sample : report.pluralization) {
		lines << QCoreApplication::translate("VibeStudioLocalization", "- plural %1: %2").arg(sample.count).arg(sample.text);
	}
	for (const TranslationExpansionLayoutCheck& check : report.layoutChecks) {
		lines << QCoreApplication::translate("VibeStudioLocalization", "- layout %1: %2/%3 chars (%4)")
			.arg(check.surfaceId)
			.arg(check.expandedLength)
			.arg(check.maxRecommendedCharacters)
			.arg(check.passed ? QCoreApplication::translate("VibeStudioLocalization", "ok") : QCoreApplication::translate("VibeStudioLocalization", "over budget"));
	}
	lines << QCoreApplication::translate("VibeStudioLocalization", "Catalogs: %1 total, %2 needing translation, %3 unfinished messages, %4 obsolete/vanished messages")
		.arg(report.catalogCount)
		.arg(report.staleCatalogCount)
		.arg(report.untranslatedMessageCount)
		.arg(report.obsoleteMessageCount);
	lines << QCoreApplication::translate("VibeStudioLocalization", "Compiled catalogs (.qm): %1 of %2 targets")
		.arg(report.compiledCatalogCount)
		.arg(report.catalogAvailability.size());
	for (const TranslationCatalogStatus& catalog : report.catalogs) {
		QString detail = QCoreApplication::translate("VibeStudioLocalization", "- %1: %2 (%3 messages, %4 translated, %5 unfinished)")
			.arg(catalog.fileName)
			.arg(catalog.status)
			.arg(catalog.messageCount)
			.arg(catalog.translatedCount)
			.arg(catalog.unfinishedCount);
		if (!catalog.issues.isEmpty()) {
			detail += QCoreApplication::translate("VibeStudioLocalization", " - %1").arg(catalog.issues.join(QStringLiteral("; ")));
		}
		lines << detail;
	}
	if (!report.warnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLocalization", "Warnings:");
		for (const QString& warning : report.warnings) {
			lines << QCoreApplication::translate("VibeStudioLocalization", "- %1").arg(warning);
		}
	}
	return lines.join('\n');
}

} // namespace vibestudio
