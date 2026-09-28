#include "core/entity_definitions.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QPair>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

QString definitionText(const char* source)
{
	return QCoreApplication::translate("VibeStudioEntityDefinitions", source);
}

// ---------------------------------------------------------------------------
// Limits
//
// Definition files come from mod packages and from the internet, so every loop
// below is bounded. A malformed or hostile file must warn, never hang and never
// allocate without a ceiling.
// ---------------------------------------------------------------------------

constexpr int kMaxDefinitionFiles = 512;
constexpr qint64 kMaxDefinitionFileBytes = 16LL * 1024LL * 1024LL;
constexpr int kMaxClasses = 20000;
constexpr int kMaxKeysPerClass = 512;
constexpr int kMaxChoicesPerKey = 512;
constexpr int kMaxIncludeDepth = 8;
constexpr int kMaxFgdTokens = 2000000;
constexpr int kMaxRadiantBlocks = 20000;
constexpr int kMaxEntBlocks = 65536;
constexpr int kMaxHelperTokens = 4096;
constexpr int kMaxValidationIssues = 4000;
constexpr int kMaxDirectoryEntries = 20000;
constexpr int kSpawnflagBitCount = 32;

// Bits 8..11 of `spawnflags` are reserved by the Quake and Quake II game code
// itself (NOT_EASY 0x100, NOT_MEDIUM 0x200, NOT_HARD 0x400, NOT_DEATHMATCH
// 0x800). Every entity honours them, so no per-class definition declares them
// and they must never be reported as unknown bits.
// Reference: the Quake Specifications, "Entities" appendix,
// https://www.gamers.org/dEngine/quake/spec/quake-spec34/
constexpr int kQuakeSkillFlagFirstBit = 8;
constexpr int kQuakeSkillFlagLastBit = 11;

// ---------------------------------------------------------------------------
// Small text helpers
// ---------------------------------------------------------------------------

QString decodeDefinitionText(const QByteArray& bytes)
{
	QStringDecoder decoder(QStringDecoder::Utf8);
	const QString decoded = decoder.decode(bytes);
	if (!decoder.hasError()) {
		return decoded;
	}
	// Definition files predate UTF-8 by a decade; the ones that are not valid
	// UTF-8 are Latin-1, which never fails to decode.
	return QString::fromLatin1(bytes);
}

QString stripQuotes(const QString& value)
{
	QString trimmed = value.trimmed();
	if (trimmed.size() >= 2 && trimmed.startsWith(u'"') && trimmed.endsWith(u'"')) {
		trimmed = trimmed.mid(1, trimmed.size() - 2);
	}
	return trimmed.trimmed();
}

void skipSpaces(const QString& text, qsizetype& index)
{
	while (index < text.size() && text.at(index).isSpace()) {
		++index;
	}
}

QString nextWhitespaceToken(const QString& text, qsizetype& index)
{
	skipSpaces(text, index);
	const qsizetype begin = index;
	while (index < text.size() && !text.at(index).isSpace()) {
		++index;
	}
	return text.mid(begin, index - begin);
}

// Reads a `( ... )` group and returns its inner text. Returns false when the
// next non-space character is not an opening parenthesis.
bool nextParenGroup(const QString& text, qsizetype& index, QString* out)
{
	skipSpaces(text, index);
	if (index >= text.size() || text.at(index) != u'(') {
		return false;
	}
	++index;
	const qsizetype begin = index;
	while (index < text.size() && text.at(index) != u')') {
		++index;
	}
	if (out) {
		*out = text.mid(begin, index - begin);
	}
	if (index < text.size()) {
		++index;
	}
	return true;
}

// Splitting by hand keeps the parser free of QRegularExpression and gives the
// same answer for the tabs, commas and multiple spaces these formats mix.
QStringList splitWhitespace(const QString& text)
{
	QStringList parts;
	qsizetype index = 0;
	while (index < text.size()) {
		skipSpaces(text, index);
		if (index >= text.size()) {
			break;
		}
		const qsizetype begin = index;
		while (index < text.size() && !text.at(index).isSpace()) {
			++index;
		}
		parts.append(text.mid(begin, index - begin));
	}
	return parts;
}

QVector<double> numbersIn(const QString& text, bool* allNumeric = nullptr)
{
	QVector<double> numbers;
	bool clean = true;
	const QStringList parts = splitWhitespace(text);
	for (const QString& part : parts) {
		QString token = part;
		token.remove(u',');
		if (token.isEmpty()) {
			continue;
		}
		bool ok = false;
		const double value = token.toDouble(&ok);
		if (!ok || !std::isfinite(value)) {
			clean = false;
			continue;
		}
		numbers.append(value);
	}
	if (allNumeric) {
		*allNumeric = clean;
	}
	return numbers;
}

bool isAllUpperToken(const QString& token)
{
	if (token.isEmpty()) {
		return false;
	}
	bool sawLetter = false;
	for (const QChar ch : token) {
		if (ch.isLetter()) {
			if (ch.isLower()) {
				return false;
			}
			sawLetter = true;
			continue;
		}
		if (ch.isDigit() || ch == u'_') {
			continue;
		}
		return false;
	}
	return sawLetter;
}

// VibeStudio convention, documented for users: a key whose description opens
// with `(required)`, `[required]` or `required:` is treated as mandatory. No
// definition format has a required-key marker of its own, and authors already
// write this by hand.
void applyRequiredMarker(EntityKeyDefinition& key)
{
	QString description = key.description.trimmed();
	static const char* const markers[] = {"(required)", "[required]", "required:"};
	for (const char* marker : markers) {
		const QString candidate = QString::fromLatin1(marker);
		if (description.startsWith(candidate, Qt::CaseInsensitive)) {
			key.required = true;
			description = description.mid(candidate.size()).trimmed();
			break;
		}
	}
	key.description = description;
}

EntityKeyType inferKeyTypeFromName(const QString& key)
{
	const QString lower = key.toLower();
	if (lower == QStringLiteral("targetname") || lower == QStringLiteral("target_name")) {
		return EntityKeyType::TargetSource;
	}
	if (lower == QStringLiteral("target") || lower == QStringLiteral("killtarget") || lower == QStringLiteral("pathtarget")
		|| (lower.size() == 7 && lower.startsWith(QStringLiteral("target")) && lower.at(6).isDigit())) {
		return EntityKeyType::TargetDestination;
	}
	if (lower == QStringLiteral("angle") || lower == QStringLiteral("angles")) {
		return EntityKeyType::Angle;
	}
	if (lower == QStringLiteral("origin") || lower == QStringLiteral("mins") || lower == QStringLiteral("maxs")) {
		return EntityKeyType::Vector;
	}
	if (lower == QStringLiteral("_color") || lower == QStringLiteral("color")) {
		return EntityKeyType::Color;
	}
	if (lower == QStringLiteral("spawnflags")) {
		return EntityKeyType::Flags;
	}
	if (lower == QStringLiteral("model")) {
		return EntityKeyType::Model;
	}
	if (lower == QStringLiteral("noise") || lower == QStringLiteral("sound")) {
		return EntityKeyType::Sound;
	}
	return EntityKeyType::String;
}

// Keys every engine understands on every class. They are never reported as
// undeclared, and neither is anything beginning with `_`, which is the
// q3map2 / ericw-tools convention for compiler-only keys.
bool isUniversalEntityKey(const QString& key)
{
	const QString lower = key.toLower();
	return lower == QStringLiteral("classname") || lower == QStringLiteral("origin") || lower == QStringLiteral("spawnflags")
		|| lower == QStringLiteral("angle") || lower == QStringLiteral("angles") || lower == QStringLiteral("targetname")
		|| lower == QStringLiteral("target") || lower.startsWith(u'_');
}

bool classLessThan(const EntityClassDefinition& left, const EntityClassDefinition& right)
{
	const int compared = QString::compare(left.className, right.className, Qt::CaseInsensitive);
	if (compared != 0) {
		return compared < 0;
	}
	return left.className < right.className;
}

void setBounds(EntityClassDefinition& definition, const QVector<double>& numbers)
{
	if (numbers.size() >= 6) {
		for (int axis = 0; axis < 3; ++axis) {
			definition.mins[axis] = numbers.at(axis);
			definition.maxs[axis] = numbers.at(axis + 3);
		}
		definition.hasSize = true;
		return;
	}
	if (numbers.size() == 3) {
		// A three-number `size()` is a box centred on the origin.
		for (int axis = 0; axis < 3; ++axis) {
			definition.mins[axis] = -std::abs(numbers.at(axis)) * 0.5;
			definition.maxs[axis] = std::abs(numbers.at(axis)) * 0.5;
		}
		definition.hasSize = true;
	}
}

void setColor(EntityClassDefinition& definition, const QVector<double>& numbers, bool normalized)
{
	if (numbers.size() < 3) {
		return;
	}
	bool unitRange = normalized;
	if (normalized) {
		for (int axis = 0; axis < 3; ++axis) {
			if (numbers.at(axis) > 1.0) {
				unitRange = false;
				break;
			}
		}
	}
	for (int axis = 0; axis < 3; ++axis) {
		double value = numbers.at(axis);
		if (unitRange) {
			value *= 255.0;
		}
		definition.color[axis] = static_cast<int>(std::lround(std::clamp(value, 0.0, 255.0)));
	}
	definition.hasColor = true;
}

// ---------------------------------------------------------------------------
// Class insertion and merging
// ---------------------------------------------------------------------------

void mergeKeysInto(EntityClassDefinition& target, const EntityClassDefinition& source)
{
	QHash<QString, int> keyIndex;
	for (int index = 0; index < target.keys.size(); ++index) {
		keyIndex.insert(target.keys.at(index).key.toLower(), index);
	}
	for (const EntityKeyDefinition& key : source.keys) {
		const QString lower = key.key.toLower();
		const int existing = keyIndex.value(lower, -1);
		if (existing >= 0) {
			continue;
		}
		if (target.keys.size() >= kMaxKeysPerClass) {
			break;
		}
		keyIndex.insert(lower, target.keys.size());
		target.keys.append(key);
	}
	QSet<int> bits;
	for (const EntitySpawnflagDefinition& flag : target.spawnflags) {
		bits.insert(flag.bit);
	}
	for (const EntitySpawnflagDefinition& flag : source.spawnflags) {
		if (bits.contains(flag.bit)) {
			continue;
		}
		bits.insert(flag.bit);
		target.spawnflags.append(flag);
	}
	std::sort(target.spawnflags.begin(), target.spawnflags.end(), [](const EntitySpawnflagDefinition& a, const EntitySpawnflagDefinition& b) {
		return a.bit < b.bit;
	});
	if (target.description.isEmpty()) {
		target.description = source.description;
	}
	if (!target.hasSize && source.hasSize) {
		target.hasSize = true;
		for (int axis = 0; axis < 3; ++axis) {
			target.mins[axis] = source.mins[axis];
			target.maxs[axis] = source.maxs[axis];
		}
	}
	if (!target.hasColor && source.hasColor) {
		target.hasColor = true;
		for (int axis = 0; axis < 3; ++axis) {
			target.color[axis] = source.color[axis];
		}
	}
	if (target.modelHint.isEmpty()) {
		target.modelHint = source.modelHint;
	}
	if (target.kind == EntityClassKind::Unknown) {
		target.kind = source.kind;
	}
}

// Adds one parsed class to a catalogue. `unionDuplicates` is used by the
// Quake III `.ent` reader, where the same classname legitimately appears once
// per placed entity and the union of the keys is what we want.
void insertParsedClass(EntityDefinitionCatalogue& catalogue, QHash<QString, int>& index, const EntityClassDefinition& definition, bool unionDuplicates)
{
	if (definition.className.isEmpty()) {
		return;
	}
	const QString lower = definition.className.toLower();
	const int existing = index.value(lower, -1);
	if (existing >= 0) {
		EntityClassDefinition& previous = catalogue.classes[existing];
		if (unionDuplicates) {
			mergeKeysInto(previous, definition);
			return;
		}
		catalogue.warnings << definitionText("Duplicate entity class \"%1\": the definition in %2 replaces the one from %3.")
								  .arg(definition.className,
									  definition.sourcePath.isEmpty() ? definitionText("(unnamed source)") : definition.sourcePath,
									  previous.sourcePath.isEmpty() ? definitionText("(unnamed source)") : previous.sourcePath);
		catalogue.classes[existing] = definition;
		return;
	}
	if (catalogue.classes.size() >= kMaxClasses) {
		return;
	}
	index.insert(lower, catalogue.classes.size());
	catalogue.classes.append(definition);
}

// ---------------------------------------------------------------------------
// Radiant `/*QUAKED ... */` blocks
//
// Format: `/*QUAKED <classname> (r g b) (?|mins) [maxs] FLAG1 .. FLAG8`
// followed by free-text documentation and a closing `*/`. The colour triple is
// 0..1, the size is either the literal `?` marker meaning "brush entity" or a
// `(mins) (maxs)` pair, and up to eight header flag names map to spawnflag bits
// 0..7 in order, with `-` marking an unused bit.
// Reference: GtkRadiant's eclass parser, https://github.com/TTimo/GtkRadiant
// (radiant/eclass_def.cpp) and the q3map2 entity documentation shipped with
// NetRadiant. QuakeC `.qc` sources carry the same comment block.
// ---------------------------------------------------------------------------

enum class RadiantSection {
	None,
	Keys,
	Spawnflags,
	Notes,
};

int nextFreeSpawnflagBit(const EntityClassDefinition& definition, int floorBit)
{
	QSet<int> used;
	for (const EntitySpawnflagDefinition& flag : definition.spawnflags) {
		used.insert(flag.bit);
	}
	for (int bit = std::max(0, floorBit); bit < kSpawnflagBitCount; ++bit) {
		if (!used.contains(bit)) {
			return bit;
		}
	}
	return -1;
}

EntitySpawnflagDefinition* findSpawnflag(EntityClassDefinition& definition, const QString& name)
{
	for (EntitySpawnflagDefinition& flag : definition.spawnflags) {
		if (flag.name.compare(name, Qt::CaseInsensitive) == 0) {
			return &flag;
		}
	}
	return nullptr;
}

void addRadiantKey(EntityClassDefinition& definition, const QString& key, const QString& description)
{
	if (key.isEmpty()) {
		return;
	}
	for (EntityKeyDefinition& existing : definition.keys) {
		if (existing.key.compare(key, Qt::CaseInsensitive) == 0) {
			if (existing.description.isEmpty()) {
				existing.description = description.trimmed();
				applyRequiredMarker(existing);
			}
			return;
		}
	}
	if (definition.keys.size() >= kMaxKeysPerClass) {
		return;
	}
	EntityKeyDefinition entry;
	entry.key = key;
	entry.type = inferKeyTypeFromName(key);
	entry.typeId = entityKeyTypeId(entry.type);
	entry.displayName = key;
	entry.description = description.trimmed();
	applyRequiredMarker(entry);
	definition.keys.append(entry);
}

void addRadiantBodyFlag(EntityClassDefinition& definition, const QString& name, const QString& description, int headerSlots)
{
	if (name.isEmpty()) {
		return;
	}
	if (EntitySpawnflagDefinition* existing = findSpawnflag(definition, name)) {
		if (existing->description.isEmpty()) {
			existing->description = description.trimmed();
		}
		return;
	}
	const int bit = nextFreeSpawnflagBit(definition, headerSlots);
	if (bit < 0) {
		return;
	}
	EntitySpawnflagDefinition flag;
	flag.bit = bit;
	flag.name = name;
	flag.description = description.trimmed();
	definition.spawnflags.append(flag);
}

// Splits `"key" description` and `key : "description"`. Returns false when the
// line is neither shape.
bool splitDocumentedName(const QString& line, QString* name, QString* description)
{
	const QString trimmed = line.trimmed();
	if (trimmed.isEmpty()) {
		return false;
	}
	if (trimmed.startsWith(u'"')) {
		const qsizetype close = trimmed.indexOf(u'"', 1);
		if (close <= 1) {
			return false;
		}
		*name = trimmed.mid(1, close - 1).trimmed();
		QString rest = trimmed.mid(close + 1).trimmed();
		if (rest.startsWith(u':')) {
			rest = rest.mid(1).trimmed();
		}
		*description = stripQuotes(rest);
		return !name->isEmpty();
	}
	const qsizetype colon = trimmed.indexOf(u':');
	if (colon <= 0) {
		return false;
	}
	const QString candidate = trimmed.left(colon).trimmed();
	if (candidate.isEmpty() || candidate.contains(u' ') || candidate.contains(u'\t')) {
		return false;
	}
	for (const QChar ch : candidate) {
		if (!ch.isLetterOrNumber() && ch != u'_' && ch != u'-' && ch != u'.') {
			return false;
		}
	}
	*name = candidate;
	*description = stripQuotes(trimmed.mid(colon + 1));
	return true;
}

void parseRadiantBlock(const QString& header, const QString& body, const QString& path, int line, EntityDefinitionCatalogue& catalogue, QHash<QString, int>& index)
{
	qsizetype cursor = 0;
	EntityClassDefinition definition;
	definition.className = nextWhitespaceToken(header, cursor);
	if (definition.className.isEmpty()) {
		catalogue.warnings << definitionText("%1:%2: a /*QUAKED block has no classname.").arg(path).arg(line);
		return;
	}
	definition.sourcePath = path;
	definition.sourceLine = line;

	QString group;
	bool sizeResolved = false;
	if (nextParenGroup(header, cursor, &group)) {
		if (group.trimmed() == QStringLiteral("?")) {
			definition.kind = EntityClassKind::Brush;
			sizeResolved = true;
		} else {
			setColor(definition, numbersIn(group), true);
		}
	}
	if (!sizeResolved) {
		skipSpaces(header, cursor);
		if (cursor < header.size() && header.at(cursor) == u'?') {
			++cursor;
			definition.kind = EntityClassKind::Brush;
			sizeResolved = true;
		} else if (nextParenGroup(header, cursor, &group)) {
			if (group.trimmed() == QStringLiteral("?")) {
				definition.kind = EntityClassKind::Brush;
			} else {
				const QVector<double> mins = numbersIn(group);
				QString maxsGroup;
				QVector<double> maxs;
				if (nextParenGroup(header, cursor, &maxsGroup)) {
					maxs = numbersIn(maxsGroup);
				}
				if (mins.size() >= 3 && maxs.size() >= 3) {
					QVector<double> bounds = mins.mid(0, 3);
					bounds.append(maxs.mid(0, 3));
					setBounds(definition, bounds);
				}
				definition.kind = EntityClassKind::Point;
			}
			sizeResolved = true;
		}
	}
	if (definition.kind == EntityClassKind::Unknown) {
		definition.kind = EntityClassKind::Point;
	}

	// Up to eight header flag names, in bit order. `-` marks an unused bit.
	int headerSlots = 0;
	while (cursor < header.size() && headerSlots < 8) {
		const QString token = nextWhitespaceToken(header, cursor);
		if (token.isEmpty()) {
			break;
		}
		if (token != QStringLiteral("-") && token != QStringLiteral("?") && token != QStringLiteral("x")) {
			EntitySpawnflagDefinition flag;
			flag.bit = headerSlots;
			flag.name = token;
			definition.spawnflags.append(flag);
		}
		++headerSlots;
	}

	RadiantSection section = RadiantSection::None;
	QStringList descriptionLines;
	const QStringList bodyLines = body.split(u'\n');
	for (const QString& rawLine : bodyLines) {
		QString trimmed = rawLine.trimmed();
		if (trimmed.endsWith(u'\r')) {
			trimmed.chop(1);
			trimmed = trimmed.trimmed();
		}
		if (trimmed.isEmpty()) {
			continue;
		}
		if (trimmed.startsWith(QStringLiteral("---"))) {
			const QString banner = trimmed.toUpper();
			if (banner.contains(QStringLiteral("SPAWNFLAG")) || banner.contains(QStringLiteral("FLAG"))) {
				section = RadiantSection::Spawnflags;
			} else if (banner.contains(QStringLiteral("KEY"))) {
				section = RadiantSection::Keys;
			} else {
				section = RadiantSection::Notes;
			}
			continue;
		}
		if (isAllUpperToken(trimmed)) {
			addRadiantBodyFlag(definition, trimmed, QString(), headerSlots);
			continue;
		}
		QString name;
		QString description;
		if (splitDocumentedName(trimmed, &name, &description)) {
			if (section == RadiantSection::Spawnflags || (isAllUpperToken(name) && section != RadiantSection::Keys)) {
				addRadiantBodyFlag(definition, name, description, headerSlots);
			} else {
				addRadiantKey(definition, name, description);
			}
			continue;
		}
		if (descriptionLines.size() < 64) {
			descriptionLines.append(trimmed);
		}
	}
	definition.description = descriptionLines.join(QStringLiteral("\n")).trimmed();

	std::sort(definition.spawnflags.begin(), definition.spawnflags.end(), [](const EntitySpawnflagDefinition& a, const EntitySpawnflagDefinition& b) {
		return a.bit < b.bit;
	});
	insertParsedClass(catalogue, index, definition, false);
}

void parseRadiantDefinitions(const QString& text, const QString& path, EntityDefinitionCatalogue& catalogue, QHash<QString, int>& index)
{
	const QString marker = QStringLiteral("/*QUAKED");
	qsizetype cursor = 0;
	qsizetype lineScanPos = 0;
	int lineNumber = 1;
	int blockCount = 0;
	while (true) {
		const qsizetype start = text.indexOf(marker, cursor);
		if (start < 0) {
			break;
		}
		while (lineScanPos < start) {
			if (text.at(lineScanPos) == u'\n') {
				++lineNumber;
			}
			++lineScanPos;
		}
		const qsizetype contentStart = start + marker.size();
		const qsizetype end = text.indexOf(QStringLiteral("*/"), contentStart);
		if (end < 0) {
			catalogue.warnings << definitionText("%1:%2: a /*QUAKED block is never closed.").arg(path).arg(lineNumber);
			break;
		}
		const QString block = text.mid(contentStart, end - contentStart);
		const qsizetype newline = block.indexOf(u'\n');
		const QString header = newline < 0 ? block : block.left(newline);
		const QString body = newline < 0 ? QString() : block.mid(newline + 1);
		parseRadiantBlock(header, body, path, lineNumber, catalogue, index);
		++blockCount;
		if (blockCount >= kMaxRadiantBlocks) {
			catalogue.warnings << definitionText("%1: stopped after %2 entity definitions in one file.").arg(path).arg(kMaxRadiantBlocks);
			break;
		}
		cursor = end + 2;
	}
	if (blockCount == 0) {
		catalogue.warnings << definitionText("%1: no /*QUAKED blocks were found.").arg(path);
	}
}

// ---------------------------------------------------------------------------
// Valve `.fgd`
//
// Reference: https://developer.valvesoftware.com/wiki/FGD
//
// `@PointClass`, `@SolidClass`, `@BaseClass` and friends carry helper calls
// (`base()`, `size()`, `color()`, `model()`, `studio()`, `iconsprite()`), an
// `= classname : "description"` tail and a `[ ... ]` body of
// `key(type) : "display" : "default" : "description"` entries. `choices` and
// `flags` keys add a `= [ ... ]` row block. `@include "file.fgd"` pulls in
// another file relative to the including one.
// ---------------------------------------------------------------------------

enum class FgdTokenKind {
	End,
	Symbol,
	Identifier,
	String,
	Number,
};

struct FgdToken {
	FgdTokenKind kind = FgdTokenKind::End;
	QString text;
	int line = 1;
};

class FgdLexer final {
public:
	explicit FgdLexer(const QString& text)
		: m_text(text)
	{
	}

	const FgdToken& peek(int offset = 0)
	{
		while (m_pending.size() <= offset) {
			m_pending.append(scan());
		}
		return m_pending.at(offset);
	}

	FgdToken take()
	{
		if (m_pending.isEmpty()) {
			m_pending.append(scan());
		}
		return m_pending.takeFirst();
	}

	[[nodiscard]] bool overflowed() const { return m_overflow; }
	[[nodiscard]] bool unterminatedString() const { return m_unterminatedString; }

private:
	FgdToken scan()
	{
		if (m_overflow) {
			return FgdToken{FgdTokenKind::End, QString(), m_line};
		}
		while (m_pos < m_text.size()) {
			const QChar ch = m_text.at(m_pos);
			if (ch == u'\n') {
				++m_line;
				++m_pos;
				continue;
			}
			if (ch.isSpace()) {
				++m_pos;
				continue;
			}
			if (ch == u'/' && (m_pos + 1) < m_text.size() && m_text.at(m_pos + 1) == u'/') {
				while (m_pos < m_text.size() && m_text.at(m_pos) != u'\n') {
					++m_pos;
				}
				continue;
			}
			break;
		}
		if (m_pos >= m_text.size()) {
			return FgdToken{FgdTokenKind::End, QString(), m_line};
		}
		if (++m_tokenCount > kMaxFgdTokens) {
			m_overflow = true;
			return FgdToken{FgdTokenKind::End, QString(), m_line};
		}

		const int line = m_line;
		const QChar ch = m_text.at(m_pos);
		if (ch == u'"') {
			++m_pos;
			const qsizetype begin = m_pos;
			while (m_pos < m_text.size() && m_text.at(m_pos) != u'"') {
				if (m_text.at(m_pos) == u'\n') {
					++m_line;
				}
				++m_pos;
			}
			const QString value = m_text.mid(begin, m_pos - begin);
			if (m_pos >= m_text.size()) {
				m_unterminatedString = true;
			} else {
				++m_pos;
			}
			return FgdToken{FgdTokenKind::String, value, line};
		}
		if (ch == u'@' || ch.isLetter() || ch == u'_') {
			const qsizetype begin = m_pos;
			++m_pos;
			while (m_pos < m_text.size()) {
				const QChar next = m_text.at(m_pos);
				if (next.isLetterOrNumber() || next == u'_' || next == u'.') {
					++m_pos;
					continue;
				}
				break;
			}
			return FgdToken{FgdTokenKind::Identifier, m_text.mid(begin, m_pos - begin), line};
		}
		const bool signedNumber = (ch == u'-' || ch == u'+') && (m_pos + 1) < m_text.size()
			&& (m_text.at(m_pos + 1).isDigit() || m_text.at(m_pos + 1) == u'.');
		if (ch.isDigit() || signedNumber) {
			const qsizetype begin = m_pos;
			++m_pos;
			while (m_pos < m_text.size()) {
				const QChar next = m_text.at(m_pos);
				if (next.isDigit() || next == u'.' || next == u'e' || next == u'E'
					|| ((next == u'-' || next == u'+') && m_pos > begin && (m_text.at(m_pos - 1) == u'e' || m_text.at(m_pos - 1) == u'E'))) {
					++m_pos;
					continue;
				}
				break;
			}
			return FgdToken{FgdTokenKind::Number, m_text.mid(begin, m_pos - begin), line};
		}
		++m_pos;
		return FgdToken{FgdTokenKind::Symbol, QString(ch), line};
	}

	QString m_text;
	qsizetype m_pos = 0;
	int m_line = 1;
	QVector<FgdToken> m_pending;
	int m_tokenCount = 0;
	bool m_overflow = false;
	bool m_unterminatedString = false;
};

bool peekSymbol(FgdLexer& lexer, QChar symbol, int offset = 0)
{
	const FgdToken& token = lexer.peek(offset);
	return token.kind == FgdTokenKind::Symbol && token.text.size() == 1 && token.text.at(0) == symbol;
}

bool takeSymbol(FgdLexer& lexer, QChar symbol)
{
	if (!peekSymbol(lexer, symbol)) {
		return false;
	}
	lexer.take();
	return true;
}

// Reads one value, joining `"a" + "b"` string concatenations.
QString takeValue(FgdLexer& lexer)
{
	const FgdToken& first = lexer.peek();
	if (first.kind == FgdTokenKind::End || first.kind == FgdTokenKind::Symbol) {
		return QString();
	}
	QString value = lexer.take().text;
	while (peekSymbol(lexer, u'+') && lexer.peek(1).kind == FgdTokenKind::String) {
		lexer.take();
		value += lexer.take().text;
	}
	return value;
}

// True when the lexer is parked on `identifier (`, which always begins the next
// key entry and therefore terminates an omitted `: :` field.
bool atKeyStart(FgdLexer& lexer)
{
	return lexer.peek().kind == FgdTokenKind::Identifier && peekSymbol(lexer, u'(', 1);
}

void skipBalanced(FgdLexer& lexer, QChar open, QChar close)
{
	if (!takeSymbol(lexer, open)) {
		return;
	}
	int depth = 1;
	while (depth > 0) {
		const FgdToken token = lexer.take();
		if (token.kind == FgdTokenKind::End) {
			return;
		}
		if (token.kind != FgdTokenKind::Symbol || token.text.size() != 1) {
			continue;
		}
		if (token.text.at(0) == open) {
			++depth;
		} else if (token.text.at(0) == close) {
			--depth;
		}
	}
}

void skipStatement(FgdLexer& lexer)
{
	int depth = 0;
	while (true) {
		const FgdToken& probe = lexer.peek();
		if (probe.kind == FgdTokenKind::End) {
			return;
		}
		if (depth == 0 && probe.kind == FgdTokenKind::Identifier && probe.text.startsWith(u'@')) {
			return;
		}
		const FgdToken token = lexer.take();
		if (token.kind == FgdTokenKind::Symbol && token.text.size() == 1) {
			const QChar ch = token.text.at(0);
			if (ch == u'(' || ch == u'[') {
				++depth;
			} else if (ch == u')' || ch == u']') {
				depth = std::max(0, depth - 1);
			}
		}
	}
}

int spawnflagBitForFgdValue(double value, bool* ok)
{
	*ok = false;
	if (!std::isfinite(value) || value < 0.0) {
		return -1;
	}
	const qint64 integral = static_cast<qint64>(std::llround(value));
	if (integral <= 0) {
		return -1;
	}
	// Valve FGD flag rows are written as the summed value (1, 2, 4, 8, ...).
	if ((integral & (integral - 1)) == 0) {
		int bit = 0;
		qint64 probe = integral;
		while (probe > 1) {
			probe >>= 1;
			++bit;
		}
		if (bit < kSpawnflagBitCount) {
			*ok = true;
			return bit;
		}
		return -1;
	}
	// Some hand-written catalogues list plain bit indexes instead; accept them
	// rather than dropping the row.
	if (integral < kSpawnflagBitCount) {
		*ok = true;
		return static_cast<int>(integral);
	}
	return -1;
}

void parseFgdRowBlock(FgdLexer& lexer, EntityKeyDefinition& key, EntityClassDefinition& definition, bool flagsBlock, EntityDefinitionCatalogue& catalogue, const QString& path)
{
	if (!takeSymbol(lexer, u'[')) {
		return;
	}
	int rows = 0;
	while (!peekSymbol(lexer, u']')) {
		const FgdToken& probe = lexer.peek();
		if (probe.kind == FgdTokenKind::End) {
			return;
		}
		if (probe.kind == FgdTokenKind::Symbol) {
			lexer.take();
			continue;
		}
		const FgdToken valueToken = lexer.take();
		QString label;
		QString third;
		QString fourth;
		int field = 0;
		while (peekSymbol(lexer, u':')) {
			lexer.take();
			QString captured;
			if (!peekSymbol(lexer, u':') && !peekSymbol(lexer, u']')) {
				captured = takeValue(lexer);
			}
			if (field == 0) {
				label = captured;
			} else if (field == 1) {
				third = captured;
			} else if (field == 2) {
				fourth = captured;
			}
			++field;
		}
		++rows;
		if (rows > kMaxChoicesPerKey) {
			catalogue.warnings << definitionText("%1: stopped after %2 rows in one choices/flags block.").arg(path).arg(kMaxChoicesPerKey);
			break;
		}
		if (flagsBlock) {
			bool ok = false;
			const int bit = spawnflagBitForFgdValue(valueToken.text.toDouble(), &ok);
			if (!ok) {
				catalogue.warnings << definitionText("%1:%2: spawnflag value \"%3\" is not a usable bit.").arg(path).arg(valueToken.line).arg(valueToken.text);
				continue;
			}
			EntitySpawnflagDefinition flag;
			flag.bit = bit;
			flag.name = label.isEmpty() ? valueToken.text : label;
			flag.defaultOn = third.trimmed() == QStringLiteral("1");
			flag.description = fourth;
			if (EntitySpawnflagDefinition* existing = findSpawnflag(definition, flag.name)) {
				*existing = flag;
			} else {
				definition.spawnflags.append(flag);
			}
			continue;
		}
		EntityKeyChoice choice;
		choice.value = valueToken.text;
		choice.label = label;
		key.choices.append(choice);
	}
	takeSymbol(lexer, u']');
}

void parseFgdKey(FgdLexer& lexer, EntityClassDefinition& definition, EntityDefinitionCatalogue& catalogue, const QString& path)
{
	const FgdToken nameToken = lexer.take();
	EntityKeyDefinition key;
	key.key = nameToken.text;

	QStringList typeParts;
	if (takeSymbol(lexer, u'(')) {
		int guard = 0;
		while (!peekSymbol(lexer, u')') && lexer.peek().kind != FgdTokenKind::End && guard < 64) {
			typeParts.append(lexer.take().text);
			++guard;
		}
		takeSymbol(lexer, u')');
	}
	key.typeId = typeParts.join(QString()).toLower();
	key.type = entityKeyTypeFromId(key.typeId);

	// `readonly`, `report` and similar modifiers sit between the type and the
	// first colon.
	while (lexer.peek().kind == FgdTokenKind::Identifier && !peekSymbol(lexer, u'(', 1)) {
		lexer.take();
	}

	int field = 0;
	while (peekSymbol(lexer, u':')) {
		lexer.take();
		QString captured;
		if (!peekSymbol(lexer, u':') && !peekSymbol(lexer, u'=') && !peekSymbol(lexer, u']') && !atKeyStart(lexer)) {
			captured = takeValue(lexer);
		}
		if (field == 0) {
			key.displayName = captured;
		} else if (field == 1) {
			key.defaultValue = captured;
		} else if (field == 2) {
			key.description = captured;
		}
		++field;
	}
	if (key.displayName.isEmpty()) {
		key.displayName = key.key;
	}
	applyRequiredMarker(key);

	const bool flagsBlock = key.type == EntityKeyType::Flags && key.key.compare(QStringLiteral("spawnflags"), Qt::CaseInsensitive) == 0;
	if (peekSymbol(lexer, u'=')) {
		lexer.take();
		parseFgdRowBlock(lexer, key, definition, flagsBlock, catalogue, path);
	}

	for (EntityKeyDefinition& existing : definition.keys) {
		if (existing.key.compare(key.key, Qt::CaseInsensitive) == 0) {
			existing = key;
			return;
		}
	}
	if (definition.keys.size() >= kMaxKeysPerClass) {
		return;
	}
	definition.keys.append(key);
}

void parseFgdBody(FgdLexer& lexer, EntityClassDefinition& definition, EntityDefinitionCatalogue& catalogue, const QString& path)
{
	if (!takeSymbol(lexer, u'[')) {
		return;
	}
	while (!peekSymbol(lexer, u']')) {
		const FgdToken& probe = lexer.peek();
		if (probe.kind == FgdTokenKind::End) {
			return;
		}
		if (probe.kind != FgdTokenKind::Identifier) {
			lexer.take();
			continue;
		}
		const QString lowered = probe.text.toLower();
		if ((lowered == QStringLiteral("input") || lowered == QStringLiteral("output")) && lexer.peek(1).kind == FgdTokenKind::Identifier) {
			lexer.take();
			lexer.take();
			if (peekSymbol(lexer, u'(')) {
				skipBalanced(lexer, u'(', u')');
			}
			while (peekSymbol(lexer, u':')) {
				lexer.take();
				if (!peekSymbol(lexer, u':') && !peekSymbol(lexer, u']') && !atKeyStart(lexer)) {
					takeValue(lexer);
				}
			}
			continue;
		}
		if (!peekSymbol(lexer, u'(', 1)) {
			lexer.take();
			continue;
		}
		parseFgdKey(lexer, definition, catalogue, path);
	}
	takeSymbol(lexer, u']');
}

void applyFgdHelper(EntityClassDefinition& definition, const QString& name, const QVector<FgdToken>& arguments)
{
	const QString lowered = name.toLower();
	if (lowered == QStringLiteral("base")) {
		for (const FgdToken& token : arguments) {
			if (token.kind == FgdTokenKind::Identifier || token.kind == FgdTokenKind::String) {
				if (!definition.baseClasses.contains(token.text, Qt::CaseInsensitive)) {
					definition.baseClasses.append(token.text);
				}
			}
		}
		return;
	}
	if (lowered == QStringLiteral("size")) {
		QVector<double> numbers;
		for (const FgdToken& token : arguments) {
			if (token.kind == FgdTokenKind::Number) {
				numbers.append(token.text.toDouble());
			}
		}
		setBounds(definition, numbers);
		return;
	}
	if (lowered == QStringLiteral("color")) {
		QVector<double> numbers;
		for (const FgdToken& token : arguments) {
			if (token.kind == FgdTokenKind::Number) {
				numbers.append(token.text.toDouble());
			}
		}
		setColor(definition, numbers, false);
		return;
	}
	if (lowered == QStringLiteral("model") || lowered == QStringLiteral("studio") || lowered == QStringLiteral("studioprop")
		|| lowered == QStringLiteral("iconsprite") || lowered == QStringLiteral("sprite")) {
		for (const FgdToken& token : arguments) {
			if (token.kind == FgdTokenKind::String && !token.text.isEmpty()) {
				if (definition.modelHint.isEmpty()) {
					definition.modelHint = token.text;
				}
				return;
			}
		}
	}
}

EntityClassKind fgdClassKind(const QString& token)
{
	const QString lowered = token.toLower();
	if (lowered == QStringLiteral("@baseclass")) {
		return EntityClassKind::Base;
	}
	if (lowered == QStringLiteral("@solidclass")) {
		return EntityClassKind::Brush;
	}
	if (lowered == QStringLiteral("@pointclass") || lowered == QStringLiteral("@npcclass") || lowered == QStringLiteral("@keyframeclass")
		|| lowered == QStringLiteral("@moveclass") || lowered == QStringLiteral("@filterclass") || lowered == QStringLiteral("@pathclass")
		|| lowered == QStringLiteral("@overrideclass")) {
		return EntityClassKind::Point;
	}
	return EntityClassKind::Unknown;
}

struct DefinitionLoadContext {
	QSet<QString> visitedFiles;
	int depth = 0;
	int fileBudget = kMaxDefinitionFiles;
	bool budgetWarned = false;
};

void parseFileInto(EntityDefinitionCatalogue& catalogue, QHash<QString, int>& index, const QString& path, const QByteArray& bytes, EntityDefinitionFormat format, DefinitionLoadContext& context);

void handleFgdInclude(FgdLexer& lexer, EntityDefinitionCatalogue& catalogue, QHash<QString, int>& index, const QString& path, DefinitionLoadContext& context)
{
	const QString relative = takeValue(lexer);
	if (relative.isEmpty()) {
		catalogue.warnings << definitionText("%1: an @include has no file name.").arg(path);
		return;
	}
	if (context.depth >= kMaxIncludeDepth) {
		catalogue.warnings << definitionText("%1: @include \"%2\" was skipped; the include depth limit of %3 was reached.")
								  .arg(path, relative)
								  .arg(kMaxIncludeDepth);
		return;
	}
	const QFileInfo parent(path);
	const QString resolved = QDir(parent.absolutePath()).absoluteFilePath(relative);
	const QFileInfo info(resolved);
	if (!info.exists() || !info.isFile()) {
		catalogue.warnings << definitionText("%1: @include \"%2\" was not found.").arg(path, relative);
		return;
	}
	const QString canonical = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
	if (context.visitedFiles.contains(canonical)) {
		catalogue.warnings << definitionText("%1: @include \"%2\" was already loaded; the cycle was broken.").arg(path, relative);
		return;
	}
	if (context.fileBudget <= 0) {
		if (!context.budgetWarned) {
			context.budgetWarned = true;
			catalogue.warnings << definitionText("Stopped after %1 definition files.").arg(kMaxDefinitionFiles);
		}
		return;
	}
	QFile file(info.absoluteFilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		catalogue.warnings << definitionText("%1: @include \"%2\" could not be opened.").arg(path, relative);
		return;
	}
	if (file.size() > kMaxDefinitionFileBytes) {
		catalogue.warnings << definitionText("%1: @include \"%2\" is larger than the %3 byte limit.").arg(path, relative).arg(kMaxDefinitionFileBytes);
		return;
	}
	const QByteArray includedBytes = file.read(kMaxDefinitionFileBytes);
	file.close();
	context.visitedFiles.insert(canonical);
	--context.fileBudget;
	++context.depth;
	parseFileInto(catalogue, index, info.absoluteFilePath(), includedBytes, EntityDefinitionFormat::Unknown, context);
	--context.depth;
}

void parseFgdDefinitions(const QString& text, const QString& path, EntityDefinitionCatalogue& catalogue, QHash<QString, int>& index, DefinitionLoadContext& context)
{
	FgdLexer lexer(text);
	int classCount = 0;
	while (true) {
		const FgdToken& probe = lexer.peek();
		if (probe.kind == FgdTokenKind::End) {
			break;
		}
		if (probe.kind != FgdTokenKind::Identifier || !probe.text.startsWith(u'@')) {
			lexer.take();
			continue;
		}
		const FgdToken classToken = lexer.take();
		const QString lowered = classToken.text.toLower();
		if (lowered == QStringLiteral("@include")) {
			handleFgdInclude(lexer, catalogue, index, path, context);
			continue;
		}
		const EntityClassKind kind = fgdClassKind(classToken.text);
		if (kind == EntityClassKind::Unknown) {
			skipStatement(lexer);
			continue;
		}

		EntityClassDefinition definition;
		definition.kind = kind;
		definition.sourcePath = path;
		definition.sourceLine = classToken.line;

		while (lexer.peek().kind == FgdTokenKind::Identifier && !lexer.peek().text.startsWith(u'@')) {
			const FgdToken helper = lexer.take();
			QVector<FgdToken> arguments;
			if (takeSymbol(lexer, u'(')) {
				int guard = 0;
				while (!peekSymbol(lexer, u')') && lexer.peek().kind != FgdTokenKind::End && guard < kMaxHelperTokens) {
					const FgdToken argument = lexer.take();
					if (argument.kind != FgdTokenKind::Symbol) {
						arguments.append(argument);
					}
					++guard;
				}
				takeSymbol(lexer, u')');
			}
			applyFgdHelper(definition, helper.text, arguments);
		}

		if (!takeSymbol(lexer, u'=')) {
			catalogue.warnings << definitionText("%1:%2: an entity class has no \"= classname\" tail.").arg(path).arg(classToken.line);
			skipStatement(lexer);
			continue;
		}
		const FgdToken& nameProbe = lexer.peek();
		if (nameProbe.kind != FgdTokenKind::Identifier && nameProbe.kind != FgdTokenKind::String) {
			catalogue.warnings << definitionText("%1:%2: an entity class name is missing.").arg(path).arg(classToken.line);
			skipStatement(lexer);
			continue;
		}
		definition.className = lexer.take().text;

		int descriptionField = 0;
		while (peekSymbol(lexer, u':')) {
			lexer.take();
			QString captured;
			if (!peekSymbol(lexer, u':') && !peekSymbol(lexer, u'[')) {
				captured = takeValue(lexer);
			}
			if (!captured.isEmpty()) {
				if (definition.description.isEmpty()) {
					definition.description = captured;
				} else if (descriptionField < 3) {
					definition.description += QStringLiteral("\n") + captured;
				}
			}
			++descriptionField;
		}

		if (peekSymbol(lexer, u'[')) {
			parseFgdBody(lexer, definition, catalogue, path);
		}

		std::sort(definition.spawnflags.begin(), definition.spawnflags.end(), [](const EntitySpawnflagDefinition& a, const EntitySpawnflagDefinition& b) {
			return a.bit < b.bit;
		});
		insertParsedClass(catalogue, index, definition, false);
		++classCount;
		if (classCount >= kMaxClasses) {
			catalogue.warnings << definitionText("%1: stopped after %2 entity classes in one file.").arg(path).arg(kMaxClasses);
			break;
		}
	}
	if (lexer.overflowed()) {
		catalogue.warnings << definitionText("%1: stopped after %2 tokens.").arg(path).arg(kMaxFgdTokens);
	}
	if (lexer.unterminatedString()) {
		catalogue.warnings << definitionText("%1: a quoted string is never closed.").arg(path);
	}
	if (classCount == 0) {
		catalogue.warnings << definitionText("%1: no entity classes were found.").arg(path);
	}
}

// ---------------------------------------------------------------------------
// Quake III `.ent`
//
// q3map2 writes the BSP entity lump back out as plain `{ "key" "value" }`
// blocks. It is an entity list rather than a definition file: there are no key
// types, descriptions or spawnflag names in it, so this reader recovers the
// classnames and the set of keys each class was seen with and warns about what
// the format cannot carry.
// Reference: the Quake III Arena BSP entity lump, described in the released
// q3map2 sources and in the Quake III Arena shader/entity documentation at
// https://www.qeradiant.com/manual/Q3AShader_Manual/
// ---------------------------------------------------------------------------

void parseEntDefinitions(const QString& text, const QString& path, EntityDefinitionCatalogue& catalogue, QHash<QString, int>& index)
{
	qsizetype cursor = 0;
	int line = 1;
	int blocks = 0;
	int recovered = 0;
	bool unterminatedString = false;

	auto readToken = [&](QString* out, bool* quoted, int* tokenLine) -> bool {
		while (cursor < text.size()) {
			const QChar ch = text.at(cursor);
			if (ch == u'\n') {
				++line;
				++cursor;
				continue;
			}
			if (ch.isSpace()) {
				++cursor;
				continue;
			}
			if (ch == u'/' && (cursor + 1) < text.size() && text.at(cursor + 1) == u'/') {
				while (cursor < text.size() && text.at(cursor) != u'\n') {
					++cursor;
				}
				continue;
			}
			break;
		}
		if (cursor >= text.size()) {
			return false;
		}
		*tokenLine = line;
		const QChar ch = text.at(cursor);
		if (ch == u'"') {
			++cursor;
			const qsizetype begin = cursor;
			while (cursor < text.size() && text.at(cursor) != u'"') {
				if (text.at(cursor) == u'\n') {
					++line;
				}
				++cursor;
			}
			*out = text.mid(begin, cursor - begin);
			if (cursor >= text.size()) {
				unterminatedString = true;
			} else {
				++cursor;
			}
			*quoted = true;
			return true;
		}
		const qsizetype begin = cursor;
		while (cursor < text.size() && !text.at(cursor).isSpace()) {
			++cursor;
		}
		*out = text.mid(begin, cursor - begin);
		*quoted = false;
		return true;
	};

	QString token;
	bool quoted = false;
	int tokenLine = 1;
	while (readToken(&token, &quoted, &tokenLine)) {
		if (quoted || token != QStringLiteral("{")) {
			continue;
		}
		EntityClassDefinition definition;
		definition.sourcePath = path;
		definition.sourceLine = tokenLine;
		QString modelValue;
		int pairs = 0;
		while (readToken(&token, &quoted, &tokenLine)) {
			if (!quoted && token == QStringLiteral("}")) {
				break;
			}
			if (!quoted) {
				continue;
			}
			const QString key = token;
			QString value;
			if (!readToken(&value, &quoted, &tokenLine) || !quoted) {
				break;
			}
			if (++pairs > kMaxKeysPerClass) {
				break;
			}
			if (key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
				definition.className = value;
				continue;
			}
			if (key.compare(QStringLiteral("model"), Qt::CaseInsensitive) == 0) {
				modelValue = value;
			}
			EntityKeyDefinition entry;
			entry.key = key;
			entry.type = inferKeyTypeFromName(key);
			entry.typeId = entityKeyTypeId(entry.type);
			entry.displayName = key;
			bool duplicate = false;
			for (const EntityKeyDefinition& existing : definition.keys) {
				if (existing.key.compare(key, Qt::CaseInsensitive) == 0) {
					duplicate = true;
					break;
				}
			}
			if (!duplicate && definition.keys.size() < kMaxKeysPerClass) {
				definition.keys.append(entry);
			}
		}
		++blocks;
		if (definition.className.isEmpty()) {
			continue;
		}
		// An inline `*N` model reference is what a brush entity looks like in a
		// compiled entity lump.
		definition.kind = modelValue.startsWith(u'*') ? EntityClassKind::Brush : EntityClassKind::Point;
		insertParsedClass(catalogue, index, definition, true);
		++recovered;
		if (blocks >= kMaxEntBlocks) {
			catalogue.warnings << definitionText("%1: stopped after %2 entity blocks.").arg(path).arg(kMaxEntBlocks);
			break;
		}
	}
	if (unterminatedString) {
		catalogue.warnings << definitionText("%1: a quoted string is never closed.").arg(path);
	}
	if (recovered == 0) {
		catalogue.warnings << definitionText("%1: no classnames were found.").arg(path);
	} else {
		catalogue.warnings << definitionText("%1: .ent files list placed entities, so key types, defaults, descriptions and spawnflag names are unavailable.").arg(path);
	}
}

// ---------------------------------------------------------------------------
// Shared file entry point and inheritance resolution
// ---------------------------------------------------------------------------

void parseFileInto(EntityDefinitionCatalogue& catalogue, QHash<QString, int>& index, const QString& path, const QByteArray& bytes, EntityDefinitionFormat format, DefinitionLoadContext& context)
{
	if (bytes.size() > kMaxDefinitionFileBytes) {
		catalogue.warnings << definitionText("%1 is larger than the %2 byte limit and was skipped.").arg(path).arg(kMaxDefinitionFileBytes);
		return;
	}
	EntityDefinitionFormat resolved = format;
	if (resolved == EntityDefinitionFormat::Unknown) {
		resolved = detectEntityDefinitionFormat(path, bytes);
	}
	if (resolved == EntityDefinitionFormat::Unknown) {
		catalogue.warnings << definitionText("%1 is not a recognized entity definition file.").arg(path);
		return;
	}
	catalogue.sourcePaths.append(path);
	catalogue.sourceFormats.append(resolved);

	const QString text = decodeDefinitionText(bytes);
	switch (resolved) {
	case EntityDefinitionFormat::RadiantDef:
		parseRadiantDefinitions(text, path, catalogue, index);
		break;
	case EntityDefinitionFormat::ValveFgd:
		parseFgdDefinitions(text, path, catalogue, index, context);
		break;
	case EntityDefinitionFormat::Quake3Ent:
		parseEntDefinitions(text, path, catalogue, index);
		break;
	case EntityDefinitionFormat::Unknown:
		break;
	}
}

void finalizeCatalogue(EntityDefinitionCatalogue& catalogue)
{
	std::sort(catalogue.classes.begin(), catalogue.classes.end(), classLessThan);
	catalogue.pointClassCount = 0;
	catalogue.brushClassCount = 0;
	catalogue.baseClassCount = 0;
	for (const EntityClassDefinition& definition : catalogue.classes) {
		switch (definition.kind) {
		case EntityClassKind::Point:
			++catalogue.pointClassCount;
			break;
		case EntityClassKind::Brush:
			++catalogue.brushClassCount;
			break;
		case EntityClassKind::Base:
			++catalogue.baseClassCount;
			break;
		case EntityClassKind::Unknown:
			break;
		}
	}
}

enum class ResolveState {
	Pending,
	Active,
	Done,
	Cyclic,
};

void resolveClassInheritance(EntityDefinitionCatalogue& catalogue, QHash<QString, int>& index, QVector<ResolveState>& states, QStringList& chain, int classIndex)
{
	if (classIndex < 0 || classIndex >= catalogue.classes.size()) {
		return;
	}
	if (states.at(classIndex) == ResolveState::Done || states.at(classIndex) == ResolveState::Cyclic) {
		return;
	}
	if (states.at(classIndex) == ResolveState::Active) {
		QStringList cycle = chain;
		cycle.append(catalogue.classes.at(classIndex).className);
		catalogue.warnings << definitionText("Entity class inheritance cycle: %1.").arg(cycle.join(QStringLiteral(" -> ")));
		states[classIndex] = ResolveState::Cyclic;
		return;
	}
	if (chain.size() >= kMaxIncludeDepth * 4) {
		catalogue.warnings << definitionText("Entity class \"%1\" inherits too deeply; the chain was cut.").arg(catalogue.classes.at(classIndex).className);
		states[classIndex] = ResolveState::Done;
		return;
	}

	states[classIndex] = ResolveState::Active;
	chain.append(catalogue.classes.at(classIndex).className);

	const QStringList bases = catalogue.classes.at(classIndex).baseClasses;
	QVector<EntityKeyDefinition> inheritedKeys;
	QVector<EntitySpawnflagDefinition> inheritedFlags;
	QString inheritedDescription;
	QString inheritedModel;
	bool inheritedHasSize = false;
	double inheritedMins[3] = {0.0, 0.0, 0.0};
	double inheritedMaxs[3] = {0.0, 0.0, 0.0};
	bool inheritedHasColor = false;
	int inheritedColor[3] = {0, 0, 0};

	for (const QString& base : bases) {
		const int baseIndex = index.value(base.toLower(), -1);
		if (baseIndex < 0) {
			catalogue.warnings << definitionText("Entity class \"%1\" inherits unknown base class \"%2\".").arg(catalogue.classes.at(classIndex).className, base);
			continue;
		}
		if (baseIndex == classIndex) {
			catalogue.warnings << definitionText("Entity class inheritance cycle: %1.").arg(catalogue.classes.at(classIndex).className);
			continue;
		}
		resolveClassInheritance(catalogue, index, states, chain, baseIndex);
		if (states.at(baseIndex) == ResolveState::Cyclic) {
			states[classIndex] = ResolveState::Cyclic;
			continue;
		}
		const EntityClassDefinition& resolvedBase = catalogue.classes.at(baseIndex);
		for (const EntityKeyDefinition& key : resolvedBase.keys) {
			bool present = false;
			for (const EntityKeyDefinition& existing : inheritedKeys) {
				if (existing.key.compare(key.key, Qt::CaseInsensitive) == 0) {
					present = true;
					break;
				}
			}
			if (!present && inheritedKeys.size() < kMaxKeysPerClass) {
				inheritedKeys.append(key);
			}
		}
		for (const EntitySpawnflagDefinition& flag : resolvedBase.spawnflags) {
			bool present = false;
			for (const EntitySpawnflagDefinition& existing : inheritedFlags) {
				if (existing.bit == flag.bit) {
					present = true;
					break;
				}
			}
			if (!present) {
				inheritedFlags.append(flag);
			}
		}
		if (inheritedDescription.isEmpty()) {
			inheritedDescription = resolvedBase.description;
		}
		if (inheritedModel.isEmpty()) {
			inheritedModel = resolvedBase.modelHint;
		}
		if (!inheritedHasSize && resolvedBase.hasSize) {
			inheritedHasSize = true;
			for (int axis = 0; axis < 3; ++axis) {
				inheritedMins[axis] = resolvedBase.mins[axis];
				inheritedMaxs[axis] = resolvedBase.maxs[axis];
			}
		}
		if (!inheritedHasColor && resolvedBase.hasColor) {
			inheritedHasColor = true;
			for (int axis = 0; axis < 3; ++axis) {
				inheritedColor[axis] = resolvedBase.color[axis];
			}
		}
	}

	EntityClassDefinition& definition = catalogue.classes[classIndex];
	// The derived class wins for anything it declares itself; inherited entries
	// keep the order their bases declared them in.
	QVector<EntityKeyDefinition> mergedKeys = inheritedKeys;
	for (const EntityKeyDefinition& key : definition.keys) {
		bool replaced = false;
		for (EntityKeyDefinition& existing : mergedKeys) {
			if (existing.key.compare(key.key, Qt::CaseInsensitive) == 0) {
				existing = key;
				replaced = true;
				break;
			}
		}
		if (!replaced && mergedKeys.size() < kMaxKeysPerClass) {
			mergedKeys.append(key);
		}
	}
	definition.keys = mergedKeys;

	QVector<EntitySpawnflagDefinition> mergedFlags = inheritedFlags;
	for (const EntitySpawnflagDefinition& flag : definition.spawnflags) {
		bool replaced = false;
		for (EntitySpawnflagDefinition& existing : mergedFlags) {
			if (existing.bit == flag.bit) {
				existing = flag;
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			mergedFlags.append(flag);
		}
	}
	// A name the derived class reused on a different bit must not appear twice.
	QVector<EntitySpawnflagDefinition> deduped;
	for (const EntitySpawnflagDefinition& flag : mergedFlags) {
		bool shadowed = false;
		for (const EntitySpawnflagDefinition& own : definition.spawnflags) {
			if (own.bit != flag.bit && own.name.compare(flag.name, Qt::CaseInsensitive) == 0) {
				shadowed = true;
				break;
			}
		}
		if (!shadowed) {
			deduped.append(flag);
		}
	}
	std::sort(deduped.begin(), deduped.end(), [](const EntitySpawnflagDefinition& a, const EntitySpawnflagDefinition& b) {
		return a.bit < b.bit;
	});
	definition.spawnflags = deduped;

	if (definition.description.isEmpty()) {
		definition.description = inheritedDescription;
	}
	if (definition.modelHint.isEmpty()) {
		definition.modelHint = inheritedModel;
	}
	if (!definition.hasSize && inheritedHasSize) {
		definition.hasSize = true;
		for (int axis = 0; axis < 3; ++axis) {
			definition.mins[axis] = inheritedMins[axis];
			definition.maxs[axis] = inheritedMaxs[axis];
		}
	}
	if (!definition.hasColor && inheritedHasColor) {
		definition.hasColor = true;
		for (int axis = 0; axis < 3; ++axis) {
			definition.color[axis] = inheritedColor[axis];
		}
	}

	chain.removeLast();
	if (states.at(classIndex) != ResolveState::Cyclic) {
		states[classIndex] = ResolveState::Done;
	}
}

void resolveInheritance(EntityDefinitionCatalogue& catalogue, QHash<QString, int>& index)
{
	QVector<ResolveState> states(catalogue.classes.size(), ResolveState::Pending);
	QStringList chain;
	for (int classIndex = 0; classIndex < catalogue.classes.size(); ++classIndex) {
		chain.clear();
		resolveClassInheritance(catalogue, index, states, chain, classIndex);
	}
}

// ---------------------------------------------------------------------------
// Validation helpers
// ---------------------------------------------------------------------------

bool isDoomThingClassName(const QString& className)
{
	if (!className.startsWith(QStringLiteral("thing:"))) {
		return false;
	}
	const QString suffix = className.mid(6);
	if (suffix.isEmpty()) {
		return false;
	}
	for (const QChar ch : suffix) {
		if (!ch.isDigit()) {
			return false;
		}
	}
	return true;
}

bool parseIntegerValue(const QString& value, qint64* out)
{
	bool ok = false;
	const qint64 parsed = value.trimmed().toLongLong(&ok);
	if (ok && out) {
		*out = parsed;
	}
	return ok;
}

bool parseRealValue(const QString& value)
{
	bool ok = false;
	const double parsed = value.trimmed().toDouble(&ok);
	return ok && std::isfinite(parsed);
}

bool parseBooleanValue(const QString& value)
{
	const QString lower = value.trimmed().toLower();
	return lower == QStringLiteral("0") || lower == QStringLiteral("1") || lower == QStringLiteral("true")
		|| lower == QStringLiteral("false") || lower == QStringLiteral("yes") || lower == QStringLiteral("no");
}

// Returns an empty string when the value parses, or a reason when it does not.
QString describeValueProblem(const EntityKeyDefinition& key, const QString& value)
{
	const QString trimmed = value.trimmed();
	if (trimmed.isEmpty()) {
		return QString();
	}
	switch (key.type) {
	case EntityKeyType::Integer:
	case EntityKeyType::Flags: {
		qint64 parsed = 0;
		if (!parseIntegerValue(trimmed, &parsed)) {
			return definitionText("expected a whole number");
		}
		return QString();
	}
	case EntityKeyType::Real:
		return parseRealValue(trimmed) ? QString() : definitionText("expected a number");
	case EntityKeyType::Boolean:
		return parseBooleanValue(trimmed) ? QString() : definitionText("expected 0 or 1");
	case EntityKeyType::Vector: {
		bool clean = false;
		const QVector<double> numbers = numbersIn(trimmed, &clean);
		if (!clean || numbers.size() != 3) {
			return definitionText("expected three numbers");
		}
		return QString();
	}
	case EntityKeyType::Color: {
		bool clean = false;
		const QVector<double> numbers = numbersIn(trimmed, &clean);
		if (!clean || (numbers.size() != 3 && numbers.size() != 4)) {
			return definitionText("expected three or four numbers");
		}
		return QString();
	}
	case EntityKeyType::Angle: {
		bool clean = false;
		const QVector<double> numbers = numbersIn(trimmed, &clean);
		if (!clean || (numbers.size() != 1 && numbers.size() != 3)) {
			return definitionText("expected one angle or three angles");
		}
		return QString();
	}
	case EntityKeyType::Choices: {
		if (key.choices.isEmpty()) {
			return QString();
		}
		for (const EntityKeyChoice& choice : key.choices) {
			if (choice.value.compare(trimmed, Qt::CaseInsensitive) == 0) {
				return QString();
			}
		}
		return definitionText("not one of the declared choices");
	}
	case EntityKeyType::String:
	case EntityKeyType::TargetSource:
	case EntityKeyType::TargetDestination:
	case EntityKeyType::Sound:
	case EntityKeyType::Model:
	case EntityKeyType::Texture:
		break;
	}
	return QString();
}

bool isFallbackTargetKey(const QString& key)
{
	const QString lower = key.toLower();
	return lower == QStringLiteral("target") || lower == QStringLiteral("killtarget") || lower == QStringLiteral("pathtarget")
		|| lower == QStringLiteral("target2") || lower == QStringLiteral("target3") || lower == QStringLiteral("target4");
}

bool isFallbackTargetNameKey(const QString& key)
{
	return key.compare(QStringLiteral("targetname"), Qt::CaseInsensitive) == 0;
}

QJsonArray stringArray(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

QString severityId(EntityIssueSeverity severity)
{
	switch (severity) {
	case EntityIssueSeverity::Info:
		return QStringLiteral("info");
	case EntityIssueSeverity::Warning:
		return QStringLiteral("warning");
	case EntityIssueSeverity::Error:
		return QStringLiteral("error");
	}
	return QStringLiteral("warning");
}

} // namespace

// ---------------------------------------------------------------------------
// Struct helpers
// ---------------------------------------------------------------------------

bool EntityClassDefinition::keyForName(const QString& key, EntityKeyDefinition* out) const
{
	for (const EntityKeyDefinition& entry : keys) {
		if (entry.key.compare(key, Qt::CaseInsensitive) == 0) {
			if (out) {
				*out = entry;
			}
			return true;
		}
	}
	return false;
}

bool EntityDefinitionCatalogue::isEmpty() const
{
	return classes.isEmpty();
}

bool EntityDefinitionCatalogue::classForName(const QString& className, EntityClassDefinition* out) const
{
	for (const EntityClassDefinition& definition : classes) {
		if (definition.className.compare(className, Qt::CaseInsensitive) == 0) {
			if (out) {
				*out = definition;
			}
			return true;
		}
	}
	return false;
}

QStringList EntityDefinitionCatalogue::classNames() const
{
	QStringList names;
	names.reserve(classes.size());
	for (const EntityClassDefinition& definition : classes) {
		names.append(definition.className);
	}
	return names;
}

OperationState EntityValidationReport::state() const
{
	return issues.isEmpty() ? OperationState::Completed : OperationState::Warning;
}

// ---------------------------------------------------------------------------
// Ids and display names
// ---------------------------------------------------------------------------

QString entityDefinitionFormatId(EntityDefinitionFormat format)
{
	switch (format) {
	case EntityDefinitionFormat::RadiantDef:
		return QStringLiteral("radiant-def");
	case EntityDefinitionFormat::ValveFgd:
		return QStringLiteral("valve-fgd");
	case EntityDefinitionFormat::Quake3Ent:
		return QStringLiteral("quake3-ent");
	case EntityDefinitionFormat::Unknown:
		break;
	}
	return QStringLiteral("unknown");
}

QString entityDefinitionFormatDisplayName(EntityDefinitionFormat format)
{
	switch (format) {
	case EntityDefinitionFormat::RadiantDef:
		return definitionText("Radiant definitions (.def/.qc)");
	case EntityDefinitionFormat::ValveFgd:
		return definitionText("Forge game data (.fgd)");
	case EntityDefinitionFormat::Quake3Ent:
		return definitionText("Quake III entity list (.ent)");
	case EntityDefinitionFormat::Unknown:
		break;
	}
	return definitionText("Unknown");
}

QString entityClassKindId(EntityClassKind kind)
{
	switch (kind) {
	case EntityClassKind::Point:
		return QStringLiteral("point");
	case EntityClassKind::Brush:
		return QStringLiteral("brush");
	case EntityClassKind::Base:
		return QStringLiteral("base");
	case EntityClassKind::Unknown:
		break;
	}
	return QStringLiteral("unknown");
}

QString entityKeyTypeId(EntityKeyType type)
{
	switch (type) {
	case EntityKeyType::String:
		return QStringLiteral("string");
	case EntityKeyType::Integer:
		return QStringLiteral("integer");
	case EntityKeyType::Real:
		return QStringLiteral("real");
	case EntityKeyType::Choices:
		return QStringLiteral("choices");
	case EntityKeyType::Flags:
		return QStringLiteral("flags");
	case EntityKeyType::TargetSource:
		return QStringLiteral("target_source");
	case EntityKeyType::TargetDestination:
		return QStringLiteral("target_destination");
	case EntityKeyType::Color:
		return QStringLiteral("color");
	case EntityKeyType::Vector:
		return QStringLiteral("vector");
	case EntityKeyType::Angle:
		return QStringLiteral("angle");
	case EntityKeyType::Sound:
		return QStringLiteral("sound");
	case EntityKeyType::Model:
		return QStringLiteral("model");
	case EntityKeyType::Texture:
		return QStringLiteral("texture");
	case EntityKeyType::Boolean:
		return QStringLiteral("boolean");
	}
	return QStringLiteral("string");
}

EntityKeyType entityKeyTypeFromId(const QString& id)
{
	const QString lower = id.trimmed().toLower();
	if (lower == QStringLiteral("integer") || lower == QStringLiteral("int")) {
		return EntityKeyType::Integer;
	}
	if (lower == QStringLiteral("real") || lower == QStringLiteral("float") || lower == QStringLiteral("double")) {
		return EntityKeyType::Real;
	}
	if (lower == QStringLiteral("choices")) {
		return EntityKeyType::Choices;
	}
	if (lower == QStringLiteral("flags")) {
		return EntityKeyType::Flags;
	}
	if (lower == QStringLiteral("target_source") || lower == QStringLiteral("targetname") || lower == QStringLiteral("targetsource")) {
		return EntityKeyType::TargetSource;
	}
	if (lower == QStringLiteral("target_destination") || lower == QStringLiteral("target_name_or_class")
		|| lower == QStringLiteral("targetdestination") || lower == QStringLiteral("target")) {
		return EntityKeyType::TargetDestination;
	}
	if (lower == QStringLiteral("color") || lower == QStringLiteral("color255") || lower == QStringLiteral("color1")) {
		return EntityKeyType::Color;
	}
	if (lower == QStringLiteral("vector") || lower == QStringLiteral("origin") || lower == QStringLiteral("vecline")) {
		return EntityKeyType::Vector;
	}
	if (lower == QStringLiteral("angle") || lower == QStringLiteral("angles") || lower == QStringLiteral("angle_negative_pitch")) {
		return EntityKeyType::Angle;
	}
	if (lower == QStringLiteral("sound") || lower == QStringLiteral("soundscape")) {
		return EntityKeyType::Sound;
	}
	if (lower == QStringLiteral("model") || lower == QStringLiteral("studio") || lower == QStringLiteral("sprite")
		|| lower == QStringLiteral("iconsprite") || lower == QStringLiteral("decal")) {
		return EntityKeyType::Model;
	}
	if (lower == QStringLiteral("texture") || lower == QStringLiteral("material") || lower == QStringLiteral("shader")) {
		return EntityKeyType::Texture;
	}
	if (lower == QStringLiteral("boolean") || lower == QStringLiteral("bool")) {
		return EntityKeyType::Boolean;
	}
	return EntityKeyType::String;
}

// ---------------------------------------------------------------------------
// Detection, parsing and loading
// ---------------------------------------------------------------------------

EntityDefinitionFormat detectEntityDefinitionFormat(const QString& path, const QByteArray& bytes)
{
	// Content wins over the extension: `.def` files in the wild sometimes hold
	// FGD text and vice versa. Whichever marker appears first decides.
	qsizetype fgdAt = -1;
	static const char* const fgdMarkers[] = {"@PointClass", "@BaseClass", "@SolidClass", "@NPCClass", "@include"};
	for (const char* marker : fgdMarkers) {
		const qsizetype at = bytes.indexOf(marker);
		if (at >= 0 && (fgdAt < 0 || at < fgdAt)) {
			fgdAt = at;
		}
	}
	const qsizetype radiantAt = bytes.indexOf("/*QUAKED");
	if (fgdAt >= 0 && (radiantAt < 0 || fgdAt < radiantAt)) {
		return EntityDefinitionFormat::ValveFgd;
	}
	if (radiantAt >= 0) {
		return EntityDefinitionFormat::RadiantDef;
	}

	const QString suffix = QFileInfo(path).suffix().toLower();
	if (suffix == QStringLiteral("fgd")) {
		return EntityDefinitionFormat::ValveFgd;
	}
	if (suffix == QStringLiteral("def") || suffix == QStringLiteral("qc")) {
		return EntityDefinitionFormat::RadiantDef;
	}
	if (suffix == QStringLiteral("ent")) {
		return EntityDefinitionFormat::Quake3Ent;
	}
	if (bytes.contains("\"classname\"")) {
		return EntityDefinitionFormat::Quake3Ent;
	}
	return EntityDefinitionFormat::Unknown;
}

EntityDefinitionCatalogue parseEntityDefinitions(const QString& path, const QByteArray& bytes, EntityDefinitionFormat format)
{
	EntityDefinitionCatalogue catalogue;
	if (bytes.isEmpty()) {
		catalogue.error = definitionText("The entity definition file is empty.");
		return catalogue;
	}
	if (bytes.size() > kMaxDefinitionFileBytes) {
		catalogue.error = definitionText("The entity definition file is larger than the %1 byte limit.").arg(kMaxDefinitionFileBytes);
		return catalogue;
	}
	EntityDefinitionFormat resolved = format;
	if (resolved == EntityDefinitionFormat::Unknown) {
		resolved = detectEntityDefinitionFormat(path, bytes);
	}
	if (resolved == EntityDefinitionFormat::Unknown) {
		catalogue.error = definitionText("The entity definition format could not be recognized.");
		return catalogue;
	}

	QHash<QString, int> index;
	DefinitionLoadContext context;
	const QFileInfo info(path);
	if (!path.isEmpty()) {
		const QString canonical = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
		context.visitedFiles.insert(canonical);
	}
	--context.fileBudget;
	parseFileInto(catalogue, index, path, bytes, resolved, context);
	// Base classes are folded by loadEntityDefinitions, which can see every
	// file: a single file may legitimately inherit from a sibling that has not
	// been read yet, so parsing one file never reports a missing base class.
	finalizeCatalogue(catalogue);
	return catalogue;
}

EntityDefinitionCatalogue loadEntityDefinitions(const QStringList& paths, bool recursive)
{
	EntityDefinitionCatalogue catalogue;
	QHash<QString, int> index;
	DefinitionLoadContext context;

	static const QStringList supportedSuffixes = {QStringLiteral("def"), QStringLiteral("fgd"), QStringLiteral("ent"), QStringLiteral("qc")};

	QStringList files;
	bool sawAnyPath = false;
	for (const QString& rawPath : paths) {
		const QString trimmed = rawPath.trimmed();
		if (trimmed.isEmpty()) {
			continue;
		}
		sawAnyPath = true;
		const QFileInfo info(trimmed);
		if (!info.exists()) {
			catalogue.warnings << definitionText("Definition path not found: %1").arg(QDir::toNativeSeparators(trimmed));
			continue;
		}
		if (info.isFile()) {
			files.append(info.absoluteFilePath());
			continue;
		}
		if (!info.isDir()) {
			continue;
		}
		QStringList found;
		QDirIterator iterator(info.absoluteFilePath(), QDir::Files | QDir::Readable,
			recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
		int seen = 0;
		while (iterator.hasNext()) {
			const QString candidate = iterator.next();
			if (++seen > kMaxDirectoryEntries) {
				catalogue.warnings << definitionText("Stopped after %1 files while scanning %2.")
										  .arg(kMaxDirectoryEntries)
										  .arg(QDir::toNativeSeparators(info.absoluteFilePath()));
				break;
			}
			const QFileInfo candidateInfo(candidate);
			if (!supportedSuffixes.contains(candidateInfo.suffix().toLower())) {
				continue;
			}
			found.append(candidateInfo.absoluteFilePath());
		}
		// Directory iteration order is filesystem defined; sorting keeps the
		// merge, the warnings and the duplicate resolution deterministic.
		std::sort(found.begin(), found.end());
		files.append(found);
	}

	if (!sawAnyPath) {
		catalogue.error = definitionText("No definition path was supplied.");
		return catalogue;
	}

	int loaded = 0;
	for (const QString& file : files) {
		if (context.fileBudget <= 0) {
			if (!context.budgetWarned) {
				context.budgetWarned = true;
				catalogue.warnings << definitionText("Stopped after %1 definition files.").arg(kMaxDefinitionFiles);
			}
			break;
		}
		const QFileInfo info(file);
		const QString canonical = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
		if (context.visitedFiles.contains(canonical)) {
			continue;
		}
		QFile handle(file);
		if (!handle.open(QIODevice::ReadOnly)) {
			catalogue.warnings << definitionText("Unable to read %1.").arg(QDir::toNativeSeparators(file));
			continue;
		}
		if (handle.size() > kMaxDefinitionFileBytes) {
			catalogue.warnings << definitionText("%1 is larger than the %2 byte limit and was skipped.").arg(QDir::toNativeSeparators(file)).arg(kMaxDefinitionFileBytes);
			handle.close();
			continue;
		}
		const QByteArray bytes = handle.read(kMaxDefinitionFileBytes);
		handle.close();
		context.visitedFiles.insert(canonical);
		--context.fileBudget;
		++loaded;
		parseFileInto(catalogue, index, file, bytes, EntityDefinitionFormat::Unknown, context);
		if (catalogue.classes.size() >= kMaxClasses) {
			catalogue.warnings << definitionText("Stopped after %1 entity classes.").arg(kMaxClasses);
			break;
		}
	}

	if (loaded == 0 && catalogue.classes.isEmpty()) {
		catalogue.error = definitionText("No entity definition files were found.");
		return catalogue;
	}

	// Index by name before folding: base classes can live in any loaded file.
	index.clear();
	for (int classIndex = 0; classIndex < catalogue.classes.size(); ++classIndex) {
		index.insert(catalogue.classes.at(classIndex).className.toLower(), classIndex);
	}
	resolveInheritance(catalogue, index);
	finalizeCatalogue(catalogue);
	return catalogue;
}

QStringList entityDefinitionSearchPaths(const QString& projectRootPath)
{
	static const QStringList relative = {
		QStringLiteral(".vibestudio/definitions"),
		QStringLiteral("definitions"),
		QStringLiteral("defs"),
		QStringLiteral("scripts"),
		QStringLiteral("base/scripts"),
		QStringLiteral("entities"),
	};
	const QString root = projectRootPath.trimmed();
	if (root.isEmpty()) {
		return relative;
	}
	const QDir directory(root);
	QStringList paths;
	for (const QString& entry : relative) {
		const QString candidate = QDir::cleanPath(directory.absoluteFilePath(entry));
		if (!paths.contains(candidate)) {
			paths.append(candidate);
		}
	}
	return paths;
}

// ---------------------------------------------------------------------------
// Map validation
// ---------------------------------------------------------------------------

EntityValidationReport validateLevelMapEntities(const LevelMapDocument& document, const EntityDefinitionCatalogue& catalogue)
{
	EntityValidationReport report;
	report.mapName = document.mapName;
	report.entityCount = static_cast<int>(document.entities.size());

	QHash<QString, int> classIndex;
	for (int index = 0; index < catalogue.classes.size(); ++index) {
		classIndex.insert(catalogue.classes.at(index).className.toLower(), index);
	}

	QSet<int> entitiesWithGeometry;
	for (const LevelMapBrush& brush : document.brushes) {
		entitiesWithGeometry.insert(brush.entityId);
	}
	for (const LevelMapPatch& patch : document.patches) {
		entitiesWithGeometry.insert(patch.entityId);
	}

	const bool doomDocument = document.format == LevelMapFormat::DoomWad;
	const bool quakeSkillBits = document.format == LevelMapFormat::QuakeMap;

	bool truncated = false;
	auto addIssue = [&](EntityIssueSeverity severity, const QString& code, const QString& message, int entityId, const QString& className, const QString& key, int line) {
		if (report.issues.size() >= kMaxValidationIssues) {
			if (!truncated) {
				truncated = true;
				report.warnings << definitionText("Stopped after %1 entity issues.").arg(kMaxValidationIssues);
			}
			return;
		}
		EntityValidationIssue issue;
		issue.severity = severity;
		issue.code = code;
		issue.message = message;
		issue.entityId = entityId;
		issue.className = className;
		issue.key = key;
		issue.line = line;
		report.issues.append(issue);
	};

	// Pass one: resolve classes and build the target graph.
	QVector<int> resolvedClass(document.entities.size(), -1);
	QSet<QString> providedTargetNames;
	QSet<QString> referencedTargets;
	QStringList knownClassNames;
	QSet<QString> knownSeen;
	QSet<QString> unknownSeen;
	int doomThingsWithoutDefinition = 0;

	for (int entityIndex = 0; entityIndex < document.entities.size(); ++entityIndex) {
		const LevelMapEntity& entity = document.entities.at(entityIndex);
		const int index = classIndex.value(entity.className.toLower(), -1);
		resolvedClass[entityIndex] = index;
		for (const LevelMapProperty& property : entity.properties) {
			const QString value = property.value.trimmed();
			if (value.isEmpty()) {
				continue;
			}
			EntityKeyType type = EntityKeyType::String;
			bool typed = false;
			if (index >= 0) {
				EntityKeyDefinition definitionKey;
				if (catalogue.classes.at(index).keyForName(property.key, &definitionKey)) {
					type = definitionKey.type;
					typed = true;
				}
			}
			if ((typed && type == EntityKeyType::TargetSource) || isFallbackTargetNameKey(property.key)) {
				providedTargetNames.insert(value.toLower());
			}
			if ((typed && type == EntityKeyType::TargetDestination) || isFallbackTargetKey(property.key)) {
				referencedTargets.insert(value.toLower());
			}
		}
	}

	// Pass two: per-entity issues, in document order.
	QVector<QPair<int, QString>> providedInOrder;
	for (int entityIndex = 0; entityIndex < document.entities.size(); ++entityIndex) {
		const LevelMapEntity& entity = document.entities.at(entityIndex);
		const int index = resolvedClass.at(entityIndex);
		const bool doomThing = doomDocument && isDoomThingClassName(entity.className);

		QString className;
		for (const LevelMapProperty& property : entity.properties) {
			if (property.key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
				className = property.value.trimmed();
				break;
			}
		}
		if (doomThing) {
			className = entity.className;
		}
		if (className.isEmpty()) {
			addIssue(EntityIssueSeverity::Error, QStringLiteral("entity-missing-classname"),
				definitionText("Entity %1 has no classname.").arg(entity.id), entity.id, QString(), QStringLiteral("classname"), entity.startLine);
		} else if (index < 0) {
			if (doomThing) {
				// Doom things are mirrored into entities as `thing:<type>`.
				// A Quake-style catalogue never defines those, and reporting
				// every thing as an unknown class would bury the real issues.
				++doomThingsWithoutDefinition;
			} else if (!catalogue.isEmpty()) {
				if (!unknownSeen.contains(className.toLower())) {
					unknownSeen.insert(className.toLower());
					report.unknownClassNames.append(className);
				}
				addIssue(EntityIssueSeverity::Warning, QStringLiteral("entity-unknown-class"),
					definitionText("Entity %1 uses classname \"%2\", which no loaded definition declares.").arg(entity.id).arg(className),
					entity.id, className, QStringLiteral("classname"), entity.startLine);
			}
		} else if (!knownSeen.contains(className.toLower())) {
			knownSeen.insert(className.toLower());
			knownClassNames.append(className);
		}

		if (index >= 0) {
			const EntityClassDefinition& definition = catalogue.classes.at(index);

			if (!doomDocument && definition.kind == EntityClassKind::Base) {
				addIssue(EntityIssueSeverity::Warning, QStringLiteral("entity-base-class-used"),
					definitionText("Entity %1 uses \"%2\", which is a base class and is never placed in a map.").arg(entity.id).arg(className),
					entity.id, className, QStringLiteral("classname"), entity.startLine);
			} else if (!doomDocument && definition.kind == EntityClassKind::Point && entitiesWithGeometry.contains(entity.id)) {
				addIssue(EntityIssueSeverity::Warning, QStringLiteral("entity-class-kind-mismatch"),
					definitionText("Entity %1 is a point class (\"%2\") but owns brushes.").arg(entity.id).arg(className),
					entity.id, className, QString(), entity.startLine);
			} else if (!doomDocument && definition.kind == EntityClassKind::Brush && !entitiesWithGeometry.contains(entity.id)) {
				addIssue(EntityIssueSeverity::Warning, QStringLiteral("entity-class-kind-mismatch"),
					definitionText("Entity %1 is a brush class (\"%2\") but owns no brushes.").arg(entity.id).arg(className),
					entity.id, className, QString(), entity.startLine);
			}

			for (const LevelMapProperty& property : entity.properties) {
				if (property.key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
					continue;
				}
				EntityKeyDefinition definitionKey;
				const bool declared = definition.keyForName(property.key, &definitionKey);
				if (!declared) {
					// Doom entity properties are mirrors of the binary THINGS
					// record, not authored keys, so they are never reported.
					if (!doomThing && !isUniversalEntityKey(property.key)) {
						addIssue(EntityIssueSeverity::Warning, QStringLiteral("entity-undeclared-key"),
							definitionText("Entity %1 (\"%2\") sets \"%3\", which the class does not declare. Maps legitimately carry extra keys, so this is informational.")
								.arg(entity.id)
								.arg(className, property.key),
							entity.id, className, property.key, property.line);
					}
					continue;
				}
				if (property.key.compare(QStringLiteral("spawnflags"), Qt::CaseInsensitive) == 0) {
					continue;
				}
				const QString problem = describeValueProblem(definitionKey, property.value);
				if (!problem.isEmpty()) {
					addIssue(EntityIssueSeverity::Error, QStringLiteral("entity-key-value-invalid"),
						definitionText("Entity %1 (\"%2\") sets \"%3\" to \"%4\": %5 for type %6.")
							.arg(entity.id)
							.arg(className, property.key, property.value.trimmed(), problem, entityKeyTypeId(definitionKey.type)),
						entity.id, className, property.key, property.line);
				}
			}

			if (!doomThing) {
				for (const EntityKeyDefinition& definitionKey : definition.keys) {
					if (!definitionKey.required) {
						continue;
					}
					bool present = false;
					for (const LevelMapProperty& property : entity.properties) {
						if (property.key.compare(definitionKey.key, Qt::CaseInsensitive) == 0 && !property.value.trimmed().isEmpty()) {
							present = true;
							break;
						}
					}
					if (!present) {
						addIssue(EntityIssueSeverity::Error, QStringLiteral("entity-required-key-missing"),
							definitionText("Entity %1 (\"%2\") is missing required key \"%3\".").arg(entity.id).arg(className, definitionKey.key),
							entity.id, className, definitionKey.key, entity.startLine);
					}
				}
			}

			for (const LevelMapProperty& property : entity.properties) {
				if (property.key.compare(QStringLiteral("spawnflags"), Qt::CaseInsensitive) != 0) {
					continue;
				}
				const QString trimmed = property.value.trimmed();
				if (trimmed.isEmpty()) {
					break;
				}
				qint64 parsed = 0;
				if (!parseIntegerValue(trimmed, &parsed) || parsed < 0 || parsed > 0xFFFFFFFFLL) {
					addIssue(EntityIssueSeverity::Error, QStringLiteral("entity-key-value-invalid"),
						definitionText("Entity %1 (\"%2\") sets \"spawnflags\" to \"%3\", which is not a 32-bit flag value.").arg(entity.id).arg(className, trimmed),
						entity.id, className, QStringLiteral("spawnflags"), property.line);
					break;
				}
				for (int bit = 0; bit < kSpawnflagBitCount; ++bit) {
					if ((parsed & (1LL << bit)) == 0) {
						continue;
					}
					if (quakeSkillBits && bit >= kQuakeSkillFlagFirstBit && bit <= kQuakeSkillFlagLastBit) {
						continue;
					}
					bool declared = false;
					for (const EntitySpawnflagDefinition& flag : definition.spawnflags) {
						if (flag.bit == bit) {
							declared = true;
							break;
						}
					}
					if (!declared) {
						addIssue(EntityIssueSeverity::Warning, QStringLiteral("entity-unknown-spawnflag-bit"),
							definitionText("Entity %1 (\"%2\") sets spawnflag bit %3 (value %4), which the class does not define.")
								.arg(entity.id)
								.arg(className)
								.arg(bit)
								.arg(1LL << bit),
							entity.id, className, QStringLiteral("spawnflags"), property.line);
					}
				}
				break;
			}
		}

		for (const LevelMapProperty& property : entity.properties) {
			const QString value = property.value.trimmed();
			if (value.isEmpty()) {
				continue;
			}
			EntityKeyType type = EntityKeyType::String;
			bool typed = false;
			if (index >= 0) {
				EntityKeyDefinition definitionKey;
				if (catalogue.classes.at(index).keyForName(property.key, &definitionKey)) {
					type = definitionKey.type;
					typed = true;
				}
			}
			const bool isTargetRef = (typed && type == EntityKeyType::TargetDestination) || isFallbackTargetKey(property.key);
			const bool isTargetName = (typed && type == EntityKeyType::TargetSource) || isFallbackTargetNameKey(property.key);
			if (isTargetRef && !providedTargetNames.contains(value.toLower())) {
				if (!report.danglingTargets.contains(value)) {
					report.danglingTargets.append(value);
				}
				addIssue(EntityIssueSeverity::Warning, QStringLiteral("entity-dangling-target"),
					definitionText("Entity %1 targets \"%2\", but no entity in the map carries that targetname.").arg(entity.id).arg(value),
					entity.id, className, property.key, property.line);
			}
			if (isTargetName) {
				providedInOrder.append(qMakePair(entity.id, value));
			}
		}
	}

	QSet<QString> reportedUnreachable;
	for (const QPair<int, QString>& provided : providedInOrder) {
		const QString lower = provided.second.toLower();
		if (referencedTargets.contains(lower) || reportedUnreachable.contains(lower)) {
			continue;
		}
		reportedUnreachable.insert(lower);
		report.unreachableTargetNames.append(provided.second);
		addIssue(EntityIssueSeverity::Info, QStringLiteral("entity-unreachable-targetname"),
			definitionText("Entity %1 carries targetname \"%2\", which nothing in the map targets.").arg(provided.first).arg(provided.second),
			provided.first, QString(), QStringLiteral("targetname"), 0);
	}

	if (doomThingsWithoutDefinition > 0) {
		report.warnings << definitionText("%1 Doom thing(s) were not checked: the loaded definitions declare no \"thing:<type>\" classes.")
							   .arg(doomThingsWithoutDefinition);
	}
	if (catalogue.isEmpty()) {
		report.warnings << definitionText("No entity definitions are loaded, so classnames and keys were not checked.");
	}

	std::sort(report.unknownClassNames.begin(), report.unknownClassNames.end());
	std::sort(report.danglingTargets.begin(), report.danglingTargets.end());
	std::sort(report.unreachableTargetNames.begin(), report.unreachableTargetNames.end());
	report.knownClassCount = static_cast<int>(knownClassNames.size());
	report.unknownClassCount = static_cast<int>(report.unknownClassNames.size());
	report.issueCount = static_cast<int>(report.issues.size());
	for (const EntityValidationIssue& issue : report.issues) {
		if (issue.severity == EntityIssueSeverity::Warning) {
			++report.warningCount;
		} else if (issue.severity == EntityIssueSeverity::Error) {
			++report.errorCount;
		}
	}
	return report;
}

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

QString entityKeyHelpText(const EntityClassDefinition& definition, const QString& key)
{
	EntityKeyDefinition entry;
	if (!definition.keyForName(key, &entry)) {
		return definitionText("\"%1\" is not declared by %2.").arg(key, definition.className);
	}
	QStringList lines;
	lines << definitionText("%1 (%2)").arg(entry.displayName.isEmpty() ? entry.key : entry.displayName, entityKeyTypeId(entry.type));
	lines << definitionText("Key: %1").arg(entry.key);
	if (entry.required) {
		lines << definitionText("Required: yes");
	}
	if (!entry.defaultValue.isEmpty()) {
		lines << definitionText("Default: %1").arg(entry.defaultValue);
	}
	if (!entry.description.isEmpty()) {
		lines << entry.description;
	}
	if (!entry.choices.isEmpty()) {
		lines << definitionText("Choices:");
		for (const EntityKeyChoice& choice : entry.choices) {
			lines << definitionText("  %1 - %2").arg(choice.value, choice.label.isEmpty() ? choice.value : choice.label);
		}
	}
	return lines.join(QStringLiteral("\n"));
}

QStringList entityClassSummaryLines(const EntityClassDefinition& definition)
{
	QStringList lines;
	lines << definitionText("Class: %1").arg(definition.className);
	lines << definitionText("Kind: %1").arg(entityClassKindId(definition.kind));
	if (!definition.baseClasses.isEmpty()) {
		lines << definitionText("Inherits: %1").arg(definition.baseClasses.join(QStringLiteral(", ")));
	}
	if (!definition.description.isEmpty()) {
		lines << definition.description;
	}
	if (definition.hasSize) {
		lines << definitionText("Size: %1 %2 %3 to %4 %5 %6")
					 .arg(definition.mins[0])
					 .arg(definition.mins[1])
					 .arg(definition.mins[2])
					 .arg(definition.maxs[0])
					 .arg(definition.maxs[1])
					 .arg(definition.maxs[2]);
	}
	if (definition.hasColor) {
		lines << definitionText("Colour: %1 %2 %3").arg(definition.color[0]).arg(definition.color[1]).arg(definition.color[2]);
	}
	if (!definition.modelHint.isEmpty()) {
		lines << definitionText("Model: %1").arg(definition.modelHint);
	}
	if (!definition.sourcePath.isEmpty()) {
		lines << definitionText("Source: %1:%2").arg(definition.sourcePath).arg(definition.sourceLine);
	}
	if (!definition.keys.isEmpty()) {
		lines << definitionText("Keys (%1)").arg(definition.keys.size());
		for (const EntityKeyDefinition& key : definition.keys) {
			QString line = QStringLiteral("  %1 (%2)").arg(key.key, entityKeyTypeId(key.type));
			if (key.required) {
				line += definitionText(" [required]");
			}
			if (!key.defaultValue.isEmpty()) {
				line += definitionText(" default=%1").arg(key.defaultValue);
			}
			lines << line;
		}
	}
	if (!definition.spawnflags.isEmpty()) {
		lines << definitionText("Spawnflags (%1)").arg(definition.spawnflags.size());
		for (const EntitySpawnflagDefinition& flag : definition.spawnflags) {
			lines << QStringLiteral("  %1 = %2 (%3)").arg(flag.name).arg(1LL << flag.bit).arg(flag.bit);
		}
	}
	return lines;
}

QStringList entityValidationLines(const EntityValidationReport& report)
{
	QStringList lines;
	lines << definitionText("Entity validation: %1").arg(report.mapName.isEmpty() ? definitionText("(unnamed map)") : report.mapName);
	lines << definitionText("State: %1").arg(operationStateDisplayName(report.state()));
	lines << definitionText("Entities: %1  Known classes: %2  Unknown classes: %3")
				 .arg(report.entityCount)
				 .arg(report.knownClassCount)
				 .arg(report.unknownClassCount);
	lines << definitionText("Issues: %1  Warnings: %2  Errors: %3").arg(report.issueCount).arg(report.warningCount).arg(report.errorCount);

	if (!report.unknownClassNames.isEmpty()) {
		lines << QString();
		lines << definitionText("Unknown classnames");
		for (const QString& name : report.unknownClassNames) {
			lines << QStringLiteral("  ") + name;
		}
	}
	if (!report.danglingTargets.isEmpty()) {
		lines << QString();
		lines << definitionText("Targets with no matching targetname");
		for (const QString& name : report.danglingTargets) {
			lines << QStringLiteral("  ") + name;
		}
	}
	if (!report.unreachableTargetNames.isEmpty()) {
		lines << QString();
		lines << definitionText("Targetnames nothing targets");
		for (const QString& name : report.unreachableTargetNames) {
			lines << QStringLiteral("  ") + name;
		}
	}
	if (!report.issues.isEmpty()) {
		lines << QString();
		lines << definitionText("Issues");
		for (const EntityValidationIssue& issue : report.issues) {
			lines << QStringLiteral("  [%1] %2").arg(issue.code, issue.message);
		}
	}
	if (!report.warnings.isEmpty()) {
		lines << QString();
		lines << definitionText("Warnings");
		for (const QString& warning : report.warnings) {
			lines << QStringLiteral("  ") + warning;
		}
	}
	return lines;
}

QString entityValidationText(const EntityValidationReport& report)
{
	return entityValidationLines(report).join(QStringLiteral("\n"));
}

QJsonObject entityDefinitionCatalogueJson(const EntityDefinitionCatalogue& catalogue)
{
	QJsonObject object;
	object.insert(QStringLiteral("sources"), stringArray(catalogue.sourcePaths));

	QJsonArray formats;
	for (const EntityDefinitionFormat format : catalogue.sourceFormats) {
		formats.append(entityDefinitionFormatId(format));
	}
	object.insert(QStringLiteral("formats"), formats);

	QJsonObject totals;
	totals.insert(QStringLiteral("classes"), static_cast<int>(catalogue.classes.size()));
	totals.insert(QStringLiteral("point"), catalogue.pointClassCount);
	totals.insert(QStringLiteral("brush"), catalogue.brushClassCount);
	totals.insert(QStringLiteral("base"), catalogue.baseClassCount);
	object.insert(QStringLiteral("totals"), totals);

	QJsonArray classes;
	for (const EntityClassDefinition& definition : catalogue.classes) {
		QJsonObject entry;
		entry.insert(QStringLiteral("className"), definition.className);
		entry.insert(QStringLiteral("kind"), entityClassKindId(definition.kind));
		entry.insert(QStringLiteral("description"), definition.description);
		entry.insert(QStringLiteral("baseClasses"), stringArray(definition.baseClasses));
		entry.insert(QStringLiteral("source"), definition.sourcePath);
		entry.insert(QStringLiteral("line"), definition.sourceLine);
		entry.insert(QStringLiteral("hasSize"), definition.hasSize);
		if (definition.hasSize) {
			QJsonArray mins;
			QJsonArray maxs;
			for (int axis = 0; axis < 3; ++axis) {
				mins.append(definition.mins[axis]);
				maxs.append(definition.maxs[axis]);
			}
			entry.insert(QStringLiteral("mins"), mins);
			entry.insert(QStringLiteral("maxs"), maxs);
		}
		if (definition.hasColor) {
			QJsonArray color;
			for (int axis = 0; axis < 3; ++axis) {
				color.append(definition.color[axis]);
			}
			entry.insert(QStringLiteral("color"), color);
		}
		if (!definition.modelHint.isEmpty()) {
			entry.insert(QStringLiteral("model"), definition.modelHint);
		}

		QJsonArray keys;
		for (const EntityKeyDefinition& key : definition.keys) {
			QJsonObject keyObject;
			keyObject.insert(QStringLiteral("key"), key.key);
			keyObject.insert(QStringLiteral("type"), entityKeyTypeId(key.type));
			keyObject.insert(QStringLiteral("typeId"), key.typeId);
			keyObject.insert(QStringLiteral("displayName"), key.displayName);
			keyObject.insert(QStringLiteral("description"), key.description);
			keyObject.insert(QStringLiteral("default"), key.defaultValue);
			keyObject.insert(QStringLiteral("required"), key.required);
			QJsonArray choices;
			for (const EntityKeyChoice& choice : key.choices) {
				QJsonObject choiceObject;
				choiceObject.insert(QStringLiteral("value"), choice.value);
				choiceObject.insert(QStringLiteral("label"), choice.label);
				choices.append(choiceObject);
			}
			keyObject.insert(QStringLiteral("choices"), choices);
			keys.append(keyObject);
		}
		entry.insert(QStringLiteral("keys"), keys);

		QJsonArray flags;
		for (const EntitySpawnflagDefinition& flag : definition.spawnflags) {
			QJsonObject flagObject;
			flagObject.insert(QStringLiteral("bit"), flag.bit);
			flagObject.insert(QStringLiteral("value"), static_cast<double>(1LL << flag.bit));
			flagObject.insert(QStringLiteral("name"), flag.name);
			flagObject.insert(QStringLiteral("description"), flag.description);
			flagObject.insert(QStringLiteral("defaultOn"), flag.defaultOn);
			flags.append(flagObject);
		}
		entry.insert(QStringLiteral("spawnflags"), flags);
		classes.append(entry);
	}
	object.insert(QStringLiteral("classes"), classes);
	object.insert(QStringLiteral("warnings"), stringArray(catalogue.warnings));
	object.insert(QStringLiteral("error"), catalogue.error);
	return object;
}

QJsonObject entityValidationReportJson(const EntityValidationReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("map"), report.mapName);
	object.insert(QStringLiteral("state"), operationStateId(report.state()));

	QJsonObject totals;
	totals.insert(QStringLiteral("entities"), report.entityCount);
	totals.insert(QStringLiteral("knownClasses"), report.knownClassCount);
	totals.insert(QStringLiteral("unknownClasses"), report.unknownClassCount);
	totals.insert(QStringLiteral("issues"), report.issueCount);
	totals.insert(QStringLiteral("warnings"), report.warningCount);
	totals.insert(QStringLiteral("errors"), report.errorCount);
	object.insert(QStringLiteral("totals"), totals);

	object.insert(QStringLiteral("unknownClassNames"), stringArray(report.unknownClassNames));
	object.insert(QStringLiteral("danglingTargets"), stringArray(report.danglingTargets));
	object.insert(QStringLiteral("unreachableTargetNames"), stringArray(report.unreachableTargetNames));

	QJsonArray issues;
	for (const EntityValidationIssue& issue : report.issues) {
		QJsonObject entry;
		entry.insert(QStringLiteral("severity"), severityId(issue.severity));
		entry.insert(QStringLiteral("code"), issue.code);
		entry.insert(QStringLiteral("message"), issue.message);
		entry.insert(QStringLiteral("entityId"), issue.entityId);
		entry.insert(QStringLiteral("className"), issue.className);
		entry.insert(QStringLiteral("key"), issue.key);
		entry.insert(QStringLiteral("line"), issue.line);
		issues.append(entry);
	}
	object.insert(QStringLiteral("issues"), issues);
	object.insert(QStringLiteral("warnings"), stringArray(report.warnings));
	return object;
}

} // namespace vibestudio
