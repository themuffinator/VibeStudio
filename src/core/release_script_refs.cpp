#include "core/release_script_refs.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <algorithm>
#include <numeric>
#include <utility>

namespace vibestudio {

namespace {

// Quake's MAX_QPATH is 64; ports allow more. Anything longer is not a path.
constexpr int kMaximumPathLength = 128;
constexpr int kMaximumNameLength = 64;
constexpr int kMaximumUsesPerUnit = 4096;
constexpr int kMaximumReachedUnits = 1024;
constexpr int kMaximumCallDepth = 8;
constexpr int kMaximumAliasDepth = 8;
constexpr int kMaximumReplacementChain = 16;
constexpr int kMaximumCommandDepth = 3;
constexpr qsizetype kMaximumArgumentScan = 4096;

QString folded(const QString& value)
{
	return value.trimmed().toCaseFolded();
}

QString unitKey(const QString& kind, const QString& name)
{
	return kind + QLatin1Char('|') + folded(name);
}

void appendUnique(QStringList* list, const QString& value)
{
	if (!value.isEmpty() && !list->contains(value, Qt::CaseInsensitive)) {
		list->append(value);
	}
}

QString fileNameOf(const QString& path)
{
	const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
	return slash < 0 ? path : path.mid(slash + 1);
}

// The suffix after the file name's last dot, lower-cased, or empty.
QString suffixOf(const QString& path)
{
	const QString name = fileNameOf(path);
	const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
	return dot <= 0 ? QString() : name.mid(dot + 1).toLower();
}

QString withoutSuffix(const QString& path)
{
	const QString suffix = suffixOf(path);
	return suffix.isEmpty() ? path : path.left(path.size() - suffix.size() - 1);
}

// A literal that can be a file in a game's virtual file system: printable
// ASCII, relative, no traversal, no wildcards, format codes or variables.
bool looksLikeGamePath(const QString& value, bool requireExtension, QString* normalized)
{
	if (value.isEmpty() || value.size() > kMaximumPathLength || value.front().isSpace() || value.back().isSpace()) {
		return false;
	}
	for (const QChar ch : value) {
		const char16_t c = ch.unicode();
		if (c < 0x20 || c > 0x7e) {
			return false;
		}
		switch (c) {
		case u'"': case u'*': case u'?': case u'<': case u'>': case u'|': case u'%': case u'$': case u':':
			return false;
		default:
			break;
		}
	}
	if (value.startsWith(QLatin1Char('/')) || value.startsWith(QLatin1Char('\\')) || value.endsWith(QLatin1Char('/')) || value.endsWith(QLatin1Char('\\'))) {
		return false;
	}
	const PackageVirtualPath path = normalizePackageVirtualPath(value, false);
	if (!path.isSafe() || path.normalizedPath.isEmpty()) {
		return false;
	}
	if (requireExtension) {
		const QString suffix = suffixOf(path.normalizedPath);
		if (suffix.isEmpty() || suffix.size() > 5) {
			return false;
		}
		for (const QChar ch : suffix) {
			if (!ch.isLetterOrNumber()) {
				return false;
			}
		}
	}
	if (normalized) {
		*normalized = path.normalizedPath;
	}
	return true;
}

bool isLumpCharacter(QChar ch)
{
	const char16_t c = ch.unicode();
	return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'_' || c == u'-' || c == u'[' || c == u']'
		|| c == u'\\' || c == u'^';
}

// Doom lump names are one to eight characters.
bool looksLikeLumpName(const QString& value)
{
	if (value.isEmpty() || value.size() > 8) {
		return false;
	}
	return std::all_of(value.cbegin(), value.cend(), isLumpCharacter);
}

bool looksLikeSpriteName(const QString& value)
{
	return value.size() == 4 && std::all_of(value.cbegin(), value.cend(), isLumpCharacter);
}

// ZDoom logical sound names: "imp/sight", "*death", "misc/i_pkup".
bool looksLikeSoundName(const QString& value)
{
	if (value.isEmpty() || value.size() > kMaximumNameLength) {
		return false;
	}
	return std::all_of(value.cbegin(), value.cend(), [](QChar ch) {
		const char16_t c = ch.unicode();
		return c > 0x20 && c < 0x7f && c != u'"' && c != u'\'';
	});
}

bool looksLikeIdentifier(const QString& value)
{
	if (value.isEmpty() || value.size() > kMaximumNameLength || !(value.front().isLetter() || value.front() == QLatin1Char('_'))) {
		return false;
	}
	return std::all_of(value.cbegin(), value.cend(), [](QChar ch) {
		return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('.');
	});
}

// Sounds: the name as written, then the other format a port may load in its
// place (ioquake3's code/client/snd_codec.c tries its other codecs when the
// named file is missing).
QStringList soundCandidates(const QString& path)
{
	QStringList candidates;
	const QString suffix = suffixOf(path);
	if (suffix.isEmpty()) {
		candidates << path + QStringLiteral(".wav") << path + QStringLiteral(".ogg");
		return candidates;
	}
	candidates << path;
	if (suffix == QStringLiteral("wav")) {
		candidates << withoutSuffix(path) + QStringLiteral(".ogg");
	} else if (suffix == QStringLiteral("ogg")) {
		candidates << withoutSuffix(path) + QStringLiteral(".wav");
	}
	return candidates;
}

// Images: Quake III loads a .jpg in place of a missing .tga (tr_image.c) and
// ioquake3 a .png too; a name without an extension tries each.
QStringList imageCandidates(const QString& path, bool quakeLump)
{
	QStringList candidates;
	const QString suffix = suffixOf(path);
	const QString base = suffix.isEmpty() ? path : withoutSuffix(path);
	if (!suffix.isEmpty()) {
		candidates << path;
	}
	for (const QString& extension : {QStringLiteral(".tga"), QStringLiteral(".jpg"), QStringLiteral(".png")}) {
		appendUnique(&candidates, base + extension);
	}
	if (quakeLump) {
		appendUnique(&candidates, base + QStringLiteral(".lmp"));
		appendUnique(&candidates, base + QStringLiteral(".pcx"));
	}
	return candidates;
}

// --- Tokens ------------------------------------------------------------------

struct Token {
	enum Kind : quint8 { Word, String, Name, Number, Punct, Include };
	Kind kind = Word;
	QString text;
	int line = 0;

	[[nodiscard]] bool is(char punct) const { return kind == Punct && text.size() == 1 && text.front() == QLatin1Char(punct); }
	[[nodiscard]] bool word(const char* keyword) const { return kind == Word && text.compare(QLatin1String(keyword), Qt::CaseInsensitive) == 0; }
	[[nodiscard]] bool literal() const { return kind == String || kind == Name; }
};

struct CTokenOptions {
	// Backslash escapes in strings (QuakeC, DECORATE, ZScript). Quake III's
	// parser takes backslashes literally.
	bool escapes = true;
	// QuakeC model-generation lines ($cd, $frame...) are skipped.
	bool dollarLines = false;
};

bool isIdentifierStart(QChar ch)
{
	return ch.isLetter() || ch == QLatin1Char('_');
}

bool isIdentifierPart(QChar ch)
{
	return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

// C-like tokens: words, numbers, "strings", 'names' and single punctuation,
// without comments. A '#' first on a line is a directive: #include "x"
// becomes an Include token and the rest are skipped. Strings end at the line.
QVector<Token> tokenizeC(const QString& text, const CTokenOptions& options)
{
	QVector<Token> tokens;
	const qsizetype n = text.size();
	qsizetype i = 0;
	int line = 1;
	bool lineStart = true;
	const auto skipToLineEnd = [&]() {
		while (i < n && text[i] != QLatin1Char('\n')) {
			++i;
		}
	};
	const auto readQuoted = [&](QChar quote) {
		QString value;
		++i;
		while (i < n && text[i] != quote && text[i] != QLatin1Char('\n')) {
			if (options.escapes && text[i] == QLatin1Char('\\') && i + 1 < n && text[i + 1] != QLatin1Char('\n')) {
				const QChar next = text[i + 1];
				value += next == QLatin1Char('n') ? QLatin1Char('\n') : next == QLatin1Char('t') ? QLatin1Char('\t') : next;
				i += 2;
				continue;
			}
			value += text[i++];
		}
		if (i < n && text[i] == quote) {
			++i;
		}
		return value;
	};
	while (i < n) {
		const QChar ch = text[i];
		if (ch == QLatin1Char('\n')) {
			++line;
			lineStart = true;
			++i;
			continue;
		}
		if (ch.isSpace()) {
			++i;
			continue;
		}
		if (ch == QLatin1Char('/') && i + 1 < n && text[i + 1] == QLatin1Char('/')) {
			skipToLineEnd();
			continue;
		}
		if (ch == QLatin1Char('/') && i + 1 < n && text[i + 1] == QLatin1Char('*')) {
			i += 2;
			while (i < n && !(text[i] == QLatin1Char('*') && i + 1 < n && text[i + 1] == QLatin1Char('/'))) {
				line += text[i] == QLatin1Char('\n') ? 1 : 0;
				++i;
			}
			i = std::min(n, i + 2);
			continue;
		}
		const bool first = lineStart;
		lineStart = false;
		if (first && ch == QLatin1Char('#')) {
			qsizetype j = i + 1;
			while (j < n && (text[j] == QLatin1Char(' ') || text[j] == QLatin1Char('\t'))) {
				++j;
			}
			qsizetype end = j;
			while (end < n && isIdentifierPart(text[end])) {
				++end;
			}
			if (text.mid(j, end - j).compare(QLatin1String("include"), Qt::CaseInsensitive) == 0) {
				i = end;
				while (i < n && (text[i] == QLatin1Char(' ') || text[i] == QLatin1Char('\t'))) {
					++i;
				}
				if (i < n && (text[i] == QLatin1Char('"') || text[i] == QLatin1Char('<'))) {
					const QChar close = text[i] == QLatin1Char('<') ? QLatin1Char('>') : QLatin1Char('"');
					QString value;
					++i;
					while (i < n && text[i] != close && text[i] != QLatin1Char('\n')) {
						value += text[i++];
					}
					tokens.push_back({Token::Include, value.trimmed(), line});
				}
			}
			skipToLineEnd();
			continue;
		}
		if (first && ch == QLatin1Char('$') && options.dollarLines) {
			skipToLineEnd();
			continue;
		}
		if (ch == QLatin1Char('"')) {
			const int at = line;
			tokens.push_back({Token::String, readQuoted(ch), at});
			continue;
		}
		if (ch == QLatin1Char('\'')) {
			const int at = line;
			tokens.push_back({Token::Name, readQuoted(ch), at});
			continue;
		}
		if (isIdentifierStart(ch)) {
			const qsizetype start = i;
			while (i < n && isIdentifierPart(text[i])) {
				++i;
			}
			tokens.push_back({Token::Word, text.mid(start, i - start), line});
			continue;
		}
		if (ch.isDigit() || (ch == QLatin1Char('.') && i + 1 < n && text[i + 1].isDigit())) {
			const qsizetype start = i;
			while (i < n && (isIdentifierPart(text[i]) || text[i] == QLatin1Char('.'))) {
				++i;
			}
			tokens.push_back({Token::Number, text.mid(start, i - start), line});
			continue;
		}
		tokens.push_back({Token::Punct, QString(ch), line});
		++i;
	}
	return tokens;
}

// Whitespace-separated words and "strings", with `separators` as tokens of
// their own. Comments: // and /* */, and ';' to the line's end when asked
// (SNDINFO and old MAPINFO).
QVector<Token> tokenizeLoose(const QString& text, const QString& separators, bool semicolonComments)
{
	QVector<Token> tokens;
	const qsizetype n = text.size();
	qsizetype i = 0;
	int line = 1;
	while (i < n) {
		const QChar ch = text[i];
		if (ch == QLatin1Char('\n')) {
			++line;
			++i;
			continue;
		}
		if (ch.isSpace()) {
			++i;
			continue;
		}
		if ((ch == QLatin1Char('/') && i + 1 < n && text[i + 1] == QLatin1Char('/')) || (semicolonComments && ch == QLatin1Char(';'))) {
			while (i < n && text[i] != QLatin1Char('\n')) {
				++i;
			}
			continue;
		}
		if (ch == QLatin1Char('/') && i + 1 < n && text[i + 1] == QLatin1Char('*')) {
			i += 2;
			while (i < n && !(text[i] == QLatin1Char('*') && i + 1 < n && text[i + 1] == QLatin1Char('/'))) {
				line += text[i] == QLatin1Char('\n') ? 1 : 0;
				++i;
			}
			i = std::min(n, i + 2);
			continue;
		}
		if (ch == QLatin1Char('"')) {
			QString value;
			const int at = line;
			++i;
			while (i < n && text[i] != QLatin1Char('"') && text[i] != QLatin1Char('\n')) {
				value += text[i++];
			}
			if (i < n && text[i] == QLatin1Char('"')) {
				++i;
			}
			tokens.push_back({Token::String, value, at});
			continue;
		}
		if (separators.contains(ch)) {
			tokens.push_back({Token::Punct, QString(ch), line});
			++i;
			continue;
		}
		const qsizetype start = i;
		while (i < n && !text[i].isSpace() && text[i] != QLatin1Char('"') && !separators.contains(text[i])
			&& !(text[i] == QLatin1Char('/') && i + 1 < n && (text[i + 1] == QLatin1Char('/') || text[i + 1] == QLatin1Char('*')))
			&& !(semicolonComments && text[i] == QLatin1Char(';'))) {
			++i;
		}
		tokens.push_back({Token::Word, text.mid(start, i - start), line});
	}
	return tokens;
}

// The index of the token closing the bracket at `open`, or tokens.size().
qsizetype matchingClose(const QVector<Token>& tokens, qsizetype open, qsizetype end = -1)
{
	const qsizetype limit = end < 0 ? tokens.size() : end;
	int depth = 0;
	for (qsizetype i = open; i < limit; ++i) {
		const Token& token = tokens[i];
		if (token.kind != Token::Punct) {
			continue;
		}
		if (token.is('{') || token.is('(') || token.is('[')) {
			++depth;
		} else if (token.is('}') || token.is(')') || token.is(']')) {
			if (--depth == 0) {
				return i;
			}
		}
	}
	return limit;
}

struct ArgumentRange {
	qsizetype begin = 0;
	qsizetype end = 0;
};

// The arguments of the call whose '(' is at `open`, split at top-level commas.
bool callArguments(const QVector<Token>& tokens, qsizetype open, QVector<ArgumentRange>* arguments)
{
	arguments->clear();
	int depth = 0;
	qsizetype start = open + 1;
	const qsizetype limit = std::min<qsizetype>(tokens.size(), open + kMaximumArgumentScan);
	for (qsizetype i = open; i < limit; ++i) {
		const Token& token = tokens[i];
		if (token.kind != Token::Punct) {
			continue;
		}
		if (token.is('(') || token.is('[') || token.is('{')) {
			++depth;
		} else if (token.is(')') || token.is(']') || token.is('}')) {
			if (--depth == 0) {
				if (i > start || !arguments->isEmpty()) {
					arguments->push_back({start, i});
				}
				return token.is(')');
			}
		} else if (depth == 1 && token.is(',')) {
			arguments->push_back({start, i});
			start = i + 1;
		} else if (depth == 1 && token.is(';')) {
			return false;
		}
	}
	return false;
}

// An argument that is nothing but string literals, joined as the compilers
// join adjacent literals.
bool stringArgument(const QVector<Token>& tokens, const ArgumentRange& range, QString* value)
{
	if (range.begin >= range.end) {
		return false;
	}
	QString joined;
	for (qsizetype i = range.begin; i < range.end; ++i) {
		if (tokens[i].kind != Token::String) {
			return false;
		}
		joined += tokens[i].text;
	}
	*value = joined;
	return true;
}

// A dotted name such as Inventory.PickupSound starting at `at`; `next`
// receives the token after it.
QString dottedName(const QVector<Token>& tokens, qsizetype at, qsizetype end, qsizetype* next)
{
	QString name = tokens[at].text;
	qsizetype i = at + 1;
	while (i + 1 < end && tokens[i].is('.') && tokens[i + 1].kind == Token::Word) {
		name += QLatin1Char('.') + tokens[i + 1].text;
		i += 2;
	}
	*next = i;
	return name;
}

// --- Collecting ----------------------------------------------------------------

class Collector {
public:
	Collector(ScriptReferenceScan* scan, const ScriptReferenceScanOptions& options, QString label, QString language, QStringList* includes)
		: m_scan(scan), m_options(options), m_label(std::move(label)), m_language(std::move(language)), m_includes(includes)
	{
	}

	[[nodiscard]] const QString& label() const { return m_label; }
	[[nodiscard]] const ScriptReferenceScanOptions& options() const { return m_options; }

	// A function, class or map block; existing ones are extended when `merge`.
	int beginUnit(const QString& kind, const QString& name, int line, bool merge)
	{
		if (name.trimmed().isEmpty()) {
			return -1;
		}
		if (merge) {
			if (const ScriptCodeUnit* existing = m_scan->unit(kind, name)) {
				const int index = int(existing - m_scan->units.constData());
				appendUnique(&m_scan->units[index].scripts, m_label);
				return index;
			}
		}
		ScriptCodeUnit unit;
		unit.kind = kind;
		unit.name = name.trimmed();
		unit.language = m_language;
		unit.script = m_label;
		unit.scripts << m_label;
		unit.line = line;
		m_scan->units.push_back(unit);
		const int index = int(m_scan->units.size() - 1);
		m_scan->indexUnit(index);
		return index;
	}

	ScriptCodeUnit* unit(int index) { return index >= 0 && index < m_scan->units.size() ? &m_scan->units[index] : nullptr; }

	void addUse(int index, const QString& name)
	{
		ScriptCodeUnit* target = unit(index);
		if (!target || !looksLikeIdentifier(name) || target->uses.size() >= kMaximumUsesPerUnit) {
			return;
		}
		QSet<QString>& seen = m_uses[index];
		const QString key = folded(name);
		if (!seen.contains(key) && folded(target->name) != key) {
			seen.insert(key);
			target->uses << name;
		}
	}

	// An included script: a file from the game's root, or a lump in a WAD.
	void include(const QString& path, int line, const QString& via)
	{
		if (m_includes && !path.trimmed().isEmpty()) {
			m_includes->append(path.trimmed());
		}
		if (isDoomLanguage()) {
			addDoomName(QStringLiteral("script"), path, line, -1, via);
		} else {
			addPath(QStringLiteral("script"), path, line, -1, via, false);
		}
	}

	[[nodiscard]] bool isDoomLanguage() const
	{
		return m_language == QStringLiteral("decorate") || m_language == QStringLiteral("zscript") || m_language == QStringLiteral("sndinfo")
			|| m_language == QStringLiteral("mapinfo");
	}

	// A reference with game paths to try.
	int add(const QString& kind, const QString& reference, const QStringList& candidates, int line, int owner, const QString& via,
		const QString& shaderName = {})
	{
		if (candidates.isEmpty()) {
			return -1;
		}
		ScriptAssetReference row = base(kind, reference, line, owner, via);
		row.candidates = candidates;
		row.shaderName = shaderName;
		return push(row, owner);
	}

	// A path as written, checked and normalized; `prefix` is the folder the
	// engine loads that kind from when the literal leaves it out.
	int addPath(const QString& kind, const QString& literal, int line, int owner, const QString& via, bool requireExtension, const QString& prefix = {})
	{
		QString path;
		if (!looksLikeGamePath(literal, requireExtension, &path)) {
			return -1;
		}
		QStringList candidates;
		if (kind == QStringLiteral("sound")) {
			if (!prefix.isEmpty()) {
				candidates = soundCandidates(prefix + path);
				// Some code names the folder too; the engine would not find it.
				if (path.startsWith(prefix, Qt::CaseInsensitive)) {
					for (const QString& candidate : soundCandidates(path)) {
						appendUnique(&candidates, candidate);
					}
				}
			} else {
				candidates = soundCandidates(path);
			}
		} else if (kind == QStringLiteral("image")) {
			candidates = imageCandidates(prefix + path, m_language == QStringLiteral("quakec"));
		} else {
			candidates << prefix + path;
		}
		const QString shader = kind == QStringLiteral("image") && m_language == QStringLiteral("menu") ? withoutSuffix(path) : QString();
		return add(kind, literal, candidates, line, owner, via, shader);
	}

	// A Doom lump name, or a path when the literal is one (PK3 layouts).
	int addDoomName(const QString& kind, const QString& literal, int line, int owner, const QString& via)
	{
		const QString value = literal.trimmed();
		if (value.contains(QLatin1Char('/')) || value.contains(QLatin1Char('.'))) {
			return addPath(kind, value, line, owner, via, false);
		}
		if (!looksLikeLumpName(value)) {
			return -1;
		}
		ScriptAssetReference row = base(kind, literal, line, owner, via);
		row.lumpName = value.toUpper();
		return push(row, owner);
	}

	int addSprite(const QString& sprite, int line, int owner)
	{
		const QString name = sprite.toUpper();
		if (!looksLikeSpriteName(name) || name == QStringLiteral("TNT1") || name == QStringLiteral("####") || name == QStringLiteral("----")) {
			return -1;
		}
		// One reference per sprite and class: states repeat the name often.
		const QString key = QString::number(owner) + QLatin1Char('|') + name;
		if (m_sprites.contains(key)) {
			return -1;
		}
		m_sprites.insert(key);
		ScriptAssetReference row = base(QStringLiteral("sprite"), name, line, owner, QStringLiteral("States"));
		row.lumpName = name;
		return push(row, owner);
	}

	int addLogicalSound(const QString& name, int line, int owner, const QString& via)
	{
		const QString value = name.trimmed();
		if (!looksLikeSoundName(value)) {
			return -1;
		}
		ScriptAssetReference row = base(QStringLiteral("sound"), value, line, owner, via);
		row.soundName = value;
		return push(row, owner);
	}

	void defineSound(const QString& name, const QString& target, const QStringList& aliases, int line)
	{
		if (!looksLikeSoundName(name)) {
			return;
		}
		ScriptSoundDefinition definition;
		definition.name = name;
		definition.aliases = aliases;
		definition.script = m_label;
		definition.line = line;
		if (!target.isEmpty()) {
			definition.target = target;
			definition.reference = addDoomName(QStringLiteral("sound"), target, line, -1, QStringLiteral("SNDINFO"));
			if (definition.reference < 0) {
				return;
			}
			m_scan->references[definition.reference].owner = name;
		}
		m_scan->sounds.push_back(definition);
	}

	void defineEditorNumber(int number, const QString& className, int line)
	{
		if (number <= 0 || !looksLikeIdentifier(className)) {
			return;
		}
		m_scan->editorNumbers.push_back({number, className, m_label, line});
	}

	[[nodiscard]] bool full() const { return m_scan->references.size() >= m_options.maximumReferences; }

private:
	ScriptAssetReference base(const QString& kind, const QString& reference, int line, int owner, const QString& via) const
	{
		ScriptAssetReference row;
		row.kind = kind;
		row.reference = reference;
		row.language = m_language;
		row.script = m_label;
		row.line = line;
		row.via = via;
		if (owner >= 0 && owner < m_scan->units.size()) {
			row.owner = m_scan->units[owner].name;
		}
		return row;
	}

	int push(const ScriptAssetReference& row, int owner)
	{
		if (full()) {
			return -1;
		}
		m_scan->references.push_back(row);
		const int index = int(m_scan->references.size() - 1);
		if (ScriptCodeUnit* target = unit(owner)) {
			target->references << index;
		}
		// The scanners stop at the limit, so say so as it is reached.
		if (full() && !m_scan->warnings.contains(fullWarning())) {
			m_scan->complete = false;
			m_scan->warnings << fullWarning();
		}
		return index;
	}

	QString fullWarning() const
	{
		return QCoreApplication::translate("VibeStudioReleaseScripts", "Game code and scripts name more than %1 files; the rest were not checked.")
			.arg(m_options.maximumReferences);
	}

	ScriptReferenceScan* m_scan;
	const ScriptReferenceScanOptions& m_options;
	QString m_label;
	QString m_language;
	QStringList* m_includes;
	QHash<int, QSet<QString>> m_uses;
	QSet<QString> m_sprites;
};

// --- QuakeC ------------------------------------------------------------------

struct QuakeCBuiltin {
	const char* name;
	int argument;
	const char* kind;
	// The folder the engine prefixes: Quake loads sounds from sound/.
	const char* prefix;
	bool requireExtension;
};

// WinQuake/pr_cmds.c: the builtins that load a file by name, the "2"
// variants registered Quake added, and the ports' pointsound and precache_pic.
const QuakeCBuiltin* quakeCBuiltin(const QString& name)
{
	static const QuakeCBuiltin builtins[] = {
		{"precache_sound", 0, "sound", "sound/", true},
		{"precache_sound2", 0, "sound", "sound/", true},
		{"precache_model", 0, "model", "", true},
		{"precache_model2", 0, "model", "", true},
		{"precache_file", 0, "file", "", true},
		{"precache_file2", 0, "file", "", true},
		{"precache_pic", 0, "image", "", false},
		{"setmodel", 1, "model", "", true},
		{"sound", 2, "sound", "sound/", true},
		{"ambientsound", 1, "sound", "sound/", true},
		{"pointsound", 1, "sound", "sound/", true},
	};
	for (const QuakeCBuiltin& builtin : builtins) {
		if (name.compare(QLatin1String(builtin.name), Qt::CaseInsensitive) == 0) {
			return &builtin;
		}
	}
	return nullptr;
}

bool isQuakeCKeyword(const QString& word)
{
	static const QSet<QString> keywords {
		QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("while"), QStringLiteral("do"), QStringLiteral("for"), QStringLiteral("switch"),
		QStringLiteral("case"), QStringLiteral("default"), QStringLiteral("return"), QStringLiteral("break"), QStringLiteral("continue"),
		QStringLiteral("local"), QStringLiteral("sizeof"), QStringLiteral("void"), QStringLiteral("float"), QStringLiteral("vector"),
		QStringLiteral("string"), QStringLiteral("entity"), QStringLiteral("int"), QStringLiteral("integer"), QStringLiteral("var"),
		QStringLiteral("const"), QStringLiteral("static"), QStringLiteral("nonstatic"), QStringLiteral("noref"), QStringLiteral("shared"),
		QStringLiteral("typedef"), QStringLiteral("struct"), QStringLiteral("union"), QStringLiteral("enum"), QStringLiteral("enumflags"),
		QStringLiteral("class"), QStringLiteral("goto"), QStringLiteral("state"), QStringLiteral("not"),
	};
	return keywords.contains(word.toLower());
}

// The function a '{' at file scope opens: `name = {`, `name = [frame, next] {`
// (old style) or `name(...) {` (new style).
QString quakeCFunctionName(const QVector<Token>& tokens, qsizetype brace)
{
	qsizetype i = brace - 1;
	if (i < 1) {
		return {};
	}
	if (tokens[i].is(']')) {
		int depth = 0;
		for (; i >= 0; --i) {
			if (tokens[i].is(']')) {
				++depth;
			} else if (tokens[i].is('[') && --depth == 0) {
				break;
			}
		}
		--i;
	}
	if (i >= 1 && tokens[i].is('=') && tokens[i - 1].kind == Token::Word && !isQuakeCKeyword(tokens[i - 1].text)) {
		return tokens[i - 1].text;
	}
	if (i >= 1 && tokens[i].is(')')) {
		int depth = 0;
		for (; i >= 0; --i) {
			if (tokens[i].is(')')) {
				++depth;
			} else if (tokens[i].is('(') && --depth == 0) {
				break;
			}
		}
		if (i >= 1 && tokens[i - 1].kind == Token::Word && !isQuakeCKeyword(tokens[i - 1].text)) {
			return tokens[i - 1].text;
		}
	}
	return {};
}

bool cancelledEvery(const ScriptReferenceScanOptions& options, qsizetype i)
{
	return (i & 4095) == 4095 && options.isCancelled && options.isCancelled();
}

void scanQuakeC(const QVector<Token>& tokens, Collector& collect)
{
	int depth = 0;
	int unit = -1;
	QVector<ArgumentRange> arguments;
	for (qsizetype i = 0; i < tokens.size() && !collect.full(); ++i) {
		if (cancelledEvery(collect.options(), i)) {
			return;
		}
		const Token& token = tokens[i];
		if (token.is('{')) {
			if (depth == 0) {
				const QString name = quakeCFunctionName(tokens, i);
				// Functions defined twice ([[accumulate]] and similar) add up.
				unit = name.isEmpty() ? -1 : collect.beginUnit(QStringLiteral("function"), name, token.line, true);
			}
			++depth;
			continue;
		}
		if (token.is('}')) {
			depth = std::max(0, depth - 1);
			if (depth == 0) {
				unit = -1;
			}
			continue;
		}
		if (depth == 0 || token.kind != Token::Word || i + 1 >= tokens.size() || !tokens[i + 1].is('(') || isQuakeCKeyword(token.text)) {
			continue;
		}
		collect.addUse(unit, token.text);
		const QuakeCBuiltin* builtin = quakeCBuiltin(token.text);
		QString value;
		if (builtin && callArguments(tokens, i + 1, &arguments) && builtin->argument < arguments.size()
			&& stringArgument(tokens, arguments[builtin->argument], &value)) {
			collect.addPath(QString::fromLatin1(builtin->kind), value, token.line, unit, QString::fromLatin1(builtin->name), builtin->requireExtension,
				QString::fromLatin1(builtin->prefix));
		}
	}
}

// --- Configs -----------------------------------------------------------------

struct ConfigCommand {
	QStringList arguments;
	int line = 0;
};

// Commands as Quake's command buffer splits them: at ';' and line ends
// outside quotes, with // and /* */ comments.
QVector<ConfigCommand> configCommands(const QString& text, int firstLine)
{
	QVector<ConfigCommand> commands;
	ConfigCommand current;
	current.line = firstLine;
	QString word;
	bool haveWord = false;
	int line = firstLine;
	const auto endWord = [&]() {
		if (haveWord) {
			current.arguments << word;
			word.clear();
			haveWord = false;
		}
	};
	const auto endCommand = [&]() {
		endWord();
		if (!current.arguments.isEmpty()) {
			commands << current;
		}
		current = {};
		current.line = line;
	};
	const qsizetype n = text.size();
	for (qsizetype i = 0; i < n; ++i) {
		const QChar ch = text[i];
		if (ch == QLatin1Char('"')) {
			haveWord = true;
			++i;
			while (i < n && text[i] != QLatin1Char('"') && text[i] != QLatin1Char('\n')) {
				word += text[i++];
			}
			if (i < n && text[i] == QLatin1Char('\n')) {
				--i;
			}
			continue;
		}
		if (ch == QLatin1Char('/') && i + 1 < n && text[i + 1] == QLatin1Char('/')) {
			while (i + 1 < n && text[i + 1] != QLatin1Char('\n')) {
				++i;
			}
			continue;
		}
		if (ch == QLatin1Char('/') && i + 1 < n && text[i + 1] == QLatin1Char('*')) {
			i += 2;
			while (i + 1 < n && !(text[i] == QLatin1Char('*') && text[i + 1] == QLatin1Char('/'))) {
				line += text[i] == QLatin1Char('\n') ? 1 : 0;
				++i;
			}
			++i;
			continue;
		}
		if (ch == QLatin1Char('\n') || ch == QLatin1Char(';')) {
			endCommand();
			if (ch == QLatin1Char('\n')) {
				++line;
				current.line = line;
			}
			continue;
		}
		if (ch.isSpace()) {
			endWord();
			continue;
		}
		if (!haveWord && current.arguments.isEmpty()) {
			current.line = line;
		}
		word += ch;
		haveWord = true;
	}
	endCommand();
	return commands;
}

void scanConfigText(const QString& text, int firstLine, int depth, Collector& collect)
{
	if (depth > kMaximumCommandDepth) {
		return;
	}
	const QString game = collect.options().gameKey;
	const bool quake3 = game == QStringLiteral("quake3");
	const bool quakeFamily = game == QStringLiteral("quake") || game == QStringLiteral("quake2") || quake3;
	for (const ConfigCommand& command : configCommands(text, firstLine)) {
		if (collect.full()) {
			return;
		}
		const QString name = command.arguments.first().toLower();
		const QStringList& args = command.arguments;
		if ((name == QStringLiteral("exec") || name == QStringLiteral("execq")) && args.size() >= 2) {
			QString path;
			if (looksLikeGamePath(args[1], false, &path)) {
				QStringList candidates {path};
				if (suffixOf(path).isEmpty()) {
					candidates << path + QStringLiteral(".cfg");
				}
				collect.add(QStringLiteral("script"), args[1], candidates, command.line, -1, name);
			}
		} else if (name == QStringLiteral("play") || name == QStringLiteral("playvol")) {
			// play takes files; playvol takes file and volume pairs.
			const int step = name == QStringLiteral("playvol") ? 2 : 1;
			for (qsizetype i = 1; i < args.size(); i += step) {
				QString path;
				if (!looksLikeGamePath(args[i], false, &path)) {
					continue;
				}
				// Quake and Quake II play from sound/; Quake III takes the whole path.
				QStringList candidates;
				const QStringList inFolder = path.startsWith(QStringLiteral("sound/"), Qt::CaseInsensitive) ? QStringList() : soundCandidates(QStringLiteral("sound/") + path);
				if (quake3) {
					candidates = soundCandidates(path);
				}
				for (const QString& candidate : inFolder) {
					appendUnique(&candidates, candidate);
				}
				for (const QString& candidate : soundCandidates(path)) {
					appendUnique(&candidates, candidate);
				}
				collect.add(QStringLiteral("sound"), args[i], candidates, command.line, -1, name);
			}
		} else if (name == QStringLiteral("music") && quake3) {
			for (qsizetype i = 1; i < std::min<qsizetype>(args.size(), 3); ++i) {
				collect.addPath(QStringLiteral("music"), args[i], command.line, -1, name, false);
			}
		} else if (quakeFamily && args.size() >= 2
			&& (name == QStringLiteral("map") || name == QStringLiteral("devmap") || name == QStringLiteral("spmap") || name == QStringLiteral("spdevmap")
				|| name == QStringLiteral("changelevel"))) {
			QString path;
			if (looksLikeGamePath(args[1], false, &path) && !path.contains(QLatin1Char('/'))) {
				collect.add(QStringLiteral("map"), args[1], {QStringLiteral("maps/") + withoutSuffix(path) + QStringLiteral(".bsp")}, command.line, -1, name);
			}
		} else if ((name == QStringLiteral("bind") || name == QStringLiteral("alias") || name == QStringLiteral("set") || name == QStringLiteral("seta")
					   || name == QStringLiteral("sets") || name == QStringLiteral("setu"))
			&& args.size() >= 3) {
			// The bound or stored text runs as commands later.
			scanConfigText(args.mid(2).join(QLatin1Char(' ')), command.line, depth + 1, collect);
		}
	}
}

// --- Quake III menus -----------------------------------------------------------

// code/ui/ui_shared.c keywords whose value is a file: item and menu
// backgrounds, global assets, sounds, models and cinematics; script commands
// that play sounds, set backgrounds or run console commands.
QString menuKeywordKind(const QString& keyword)
{
	static const QHash<QString, QString> kinds {
		{QStringLiteral("background"), QStringLiteral("image")}, {QStringLiteral("cursor"), QStringLiteral("image")},
		{QStringLiteral("gradientbar"), QStringLiteral("image")}, {QStringLiteral("asset_shader"), QStringLiteral("image")},
		{QStringLiteral("setbackground"), QStringLiteral("image")}, {QStringLiteral("asset_model"), QStringLiteral("model")},
		{QStringLiteral("cinematic"), QStringLiteral("video")}, {QStringLiteral("menuentersound"), QStringLiteral("sound")},
		{QStringLiteral("menuexitsound"), QStringLiteral("sound")}, {QStringLiteral("menubuzzsound"), QStringLiteral("sound")},
		{QStringLiteral("itemfocussound"), QStringLiteral("sound")}, {QStringLiteral("focussound"), QStringLiteral("sound")},
		{QStringLiteral("soundloop"), QStringLiteral("sound")}, {QStringLiteral("play"), QStringLiteral("sound")},
		{QStringLiteral("playlooped"), QStringLiteral("sound")}, {QStringLiteral("exec"), QStringLiteral("command")},
	};
	return kinds.value(keyword.toLower());
}

void scanMenu(const QVector<Token>& tokens, Collector& collect)
{
	for (qsizetype i = 0; i < tokens.size() && !collect.full(); ++i) {
		if (cancelledEvery(collect.options(), i)) {
			return;
		}
		const Token& token = tokens[i];
		if (token.kind == Token::Include) {
			collect.include(token.text, token.line, QStringLiteral("#include"));
			continue;
		}
		if (token.kind != Token::Word || i + 1 >= tokens.size()) {
			continue;
		}
		if (token.word("loadmenu")) {
			// ui_main.c: loadMenu { "ui/main.menu" ... }
			qsizetype j = i + 1;
			const bool block = tokens[j].is('{');
			j += block ? 1 : 0;
			for (; j < tokens.size() && tokens[j].kind == Token::String; ++j) {
				collect.addPath(QStringLiteral("script"), tokens[j].text, tokens[j].line, -1, QStringLiteral("loadMenu"), true);
				if (!block) {
					break;
				}
			}
			i = j;
			continue;
		}
		const QString kind = menuKeywordKind(token.text);
		const Token& value = tokens[i + 1];
		if (kind.isEmpty() || value.kind != Token::String) {
			continue;
		}
		const QString via = token.text;
		if (kind == QStringLiteral("command")) {
			scanConfigText(value.text, value.line, 1, collect);
		} else if (kind == QStringLiteral("video")) {
			QString path;
			// cl_cin.c plays a bare name from video/.
			if (looksLikeGamePath(value.text, true, &path)) {
				collect.add(kind, value.text, {path.contains(QLatin1Char('/')) ? path : QStringLiteral("video/") + path}, value.line, -1, via);
			}
		} else {
			collect.addPath(kind, value.text, value.line, -1, via, kind == QStringLiteral("model"));
		}
		++i;
	}
}

// --- DECORATE and ZScript ------------------------------------------------------

bool isSpriteToken(const Token& token)
{
	return (token.kind == Token::Word || token.kind == Token::Number || token.kind == Token::String) && looksLikeSpriteName(token.text);
}

bool isFramesToken(const Token& token)
{
	if ((token.kind != Token::Word && token.kind != Token::String) || token.text.isEmpty() || token.text.size() > 32) {
		return false;
	}
	return std::all_of(token.text.cbegin(), token.text.cend(), [](QChar ch) {
		return ch.isLetter() || ch == QLatin1Char('[') || ch == QLatin1Char(']') || ch == QLatin1Char('\\') || ch == QLatin1Char('#');
	});
}

bool isDurationStart(const QVector<Token>& tokens, qsizetype at, qsizetype end)
{
	if (at >= end) {
		return false;
	}
	const Token& token = tokens[at];
	return token.kind == Token::Number || (token.is('-') && at + 1 < end && tokens[at + 1].kind == Token::Number)
		|| (token.word("random") && at + 1 < end && tokens[at + 1].is('('));
}

bool isStateKeyword(const Token& token)
{
	return token.word("loop") || token.word("stop") || token.word("wait") || token.word("fail") || token.word("goto");
}

// A state label (Spawn:, Death.Fire:) at `at`; returns the token after the
// colon, or `at` when there is none.
qsizetype skipLabel(const QVector<Token>& tokens, qsizetype at, qsizetype end)
{
	if (at >= end || tokens[at].kind != Token::Word) {
		return at;
	}
	qsizetype i = at + 1;
	while (i + 1 < end && tokens[i].is('.') && tokens[i + 1].kind == Token::Word) {
		i += 2;
	}
	if (i < end && tokens[i].is(':') && !(i + 1 < end && tokens[i + 1].is(':'))) {
		return i + 1;
	}
	return at;
}

// DECORATE states sit one to a line: [labels:] SPRT FRAMES TICS [flags] [action].
void scanDecorateStates(const QVector<Token>& tokens, qsizetype begin, qsizetype end, int unit, Collector& collect)
{
	qsizetype i = begin;
	while (i < end) {
		const int line = tokens[i].line;
		qsizetype lineEnd = i;
		while (lineEnd < end && tokens[lineEnd].line == line) {
			++lineEnd;
		}
		qsizetype at = i;
		for (qsizetype next = skipLabel(tokens, at, lineEnd); next != at; next = skipLabel(tokens, at, lineEnd)) {
			at = next;
		}
		if (at + 1 < lineEnd && !isStateKeyword(tokens[at]) && isSpriteToken(tokens[at]) && isFramesToken(tokens[at + 1])
			&& isDurationStart(tokens, at + 2, lineEnd)) {
			collect.addSprite(tokens[at].text, tokens[at].line, unit);
		}
		i = lineEnd;
	}
}

// ZScript states end at ';' or after an anonymous function's braces.
void scanZScriptStates(const QVector<Token>& tokens, qsizetype begin, qsizetype end, int unit, Collector& collect)
{
	qsizetype i = begin;
	while (i < end) {
		const qsizetype afterLabel = skipLabel(tokens, i, end);
		if (afterLabel != i) {
			i = afterLabel;
			continue;
		}
		const Token& token = tokens[i];
		const bool sprite = !isStateKeyword(token) && i + 2 < end && isSpriteToken(token) && isFramesToken(tokens[i + 1]) && isDurationStart(tokens, i + 2, end);
		if (sprite) {
			collect.addSprite(token.text, token.line, unit);
		}
		if (sprite || isStateKeyword(token)) {
			// The rest of this state.
			int parens = 0;
			for (++i; i < end; ++i) {
				if (tokens[i].is('(')) {
					++parens;
				} else if (tokens[i].is(')')) {
					--parens;
				} else if (parens <= 0 && tokens[i].is(';')) {
					++i;
					break;
				} else if (parens <= 0 && tokens[i].is('{')) {
					i = matchingClose(tokens, i, end) + 1;
					if (i < end && tokens[i].is(';')) {
						++i;
					}
					break;
				}
			}
			continue;
		}
		++i;
	}
}

// The parts of an actor or class body both languages share: sound and icon
// properties, sound and music calls, TexMan lookups, and every name that may
// be another class. States blocks give the sprites.
void scanActorBody(const QVector<Token>& tokens, qsizetype begin, qsizetype end, int unit, bool zscript, Collector& collect)
{
	QVector<ArgumentRange> arguments;
	for (qsizetype i = begin; i < end && !collect.full(); ++i) {
		if (cancelledEvery(collect.options(), i)) {
			return;
		}
		const Token& token = tokens[i];
		if (token.word("states")) {
			qsizetype open = i + 1;
			if (open < end && tokens[open].is('(')) {
				open = matchingClose(tokens, open, end) + 1;
			}
			if (open < end && tokens[open].is('{')) {
				const qsizetype close = matchingClose(tokens, open, end);
				if (zscript) {
					scanZScriptStates(tokens, open + 1, close, unit, collect);
				} else {
					scanDecorateStates(tokens, open + 1, close, unit, collect);
				}
				// Calls and names inside the states are read below as well.
			}
		}
		if (token.literal()) {
			collect.addUse(unit, token.text);
			continue;
		}
		if (token.kind != Token::Word) {
			continue;
		}
		qsizetype next = i + 1;
		const QString name = dottedName(tokens, i, end, &next);
		const QString lower = name.toLower();
		if (next < end && tokens[next].kind == Token::String) {
			// SeeSound "imp/sight", Inventory.PickupSound "misc/i_pkup", Inventory.Icon "ARM1A0".
			if (lower.endsWith(QStringLiteral("sound"))) {
				collect.addLogicalSound(tokens[next].text, tokens[next].line, unit, name);
			} else if (lower == QStringLiteral("inventory.icon") || lower == QStringLiteral("icon")) {
				collect.addDoomName(QStringLiteral("graphic"), tokens[next].text, tokens[next].line, unit, name);
			}
		} else if (next < end && tokens[next].is('(') && callArguments(tokens, next, &arguments) && !arguments.isEmpty()) {
			QString value;
			const QString call = fileNameOf(QString(name).replace(QLatin1Char('.'), QLatin1Char('/'))).toLower();
			if (stringArgument(tokens, arguments.first(), &value)) {
				if (call.contains(QStringLiteral("sound"))) {
					collect.addLogicalSound(value, tokens[next].line, unit, name);
				} else if (call.contains(QStringLiteral("music"))) {
					collect.addDoomName(QStringLiteral("music"), value, tokens[next].line, unit, name);
				} else if (lower == QStringLiteral("texman.checkfortexture")) {
					collect.addDoomName(QStringLiteral("graphic"), value, tokens[next].line, unit, name);
				}
			}
		}
		collect.addUse(unit, token.text);
		// Past the dotted name; a call's arguments are read next.
		i = next - 1;
	}
}

// actor Name [: Parent] [replaces Other] [editor number] [native] { ... }
void scanDecorate(const QVector<Token>& tokens, Collector& collect)
{
	qsizetype i = 0;
	while (i < tokens.size() && !collect.full()) {
		const Token& token = tokens[i];
		if (token.kind == Token::Include) {
			collect.include(token.text, token.line, QStringLiteral("#include"));
			++i;
			continue;
		}
		if (token.word("actor") && i + 1 < tokens.size() && (tokens[i + 1].kind == Token::Word || tokens[i + 1].kind == Token::String)) {
			const QString name = tokens[i + 1].text;
			QString parent;
			QString replaces;
			int number = -1;
			qsizetype j = i + 2;
			for (; j < tokens.size() && j < i + 16 && !tokens[j].is('{'); ++j) {
				if (tokens[j].is(':') && j + 1 < tokens.size()) {
					parent = tokens[++j].text;
				} else if (tokens[j].word("replaces") && j + 1 < tokens.size()) {
					replaces = tokens[++j].text;
				} else if (tokens[j].kind == Token::Number) {
					bool ok = false;
					const int value = tokens[j].text.toInt(&ok);
					number = ok ? value : -1;
				}
			}
			if (j >= tokens.size() || !tokens[j].is('{')) {
				i = j;
				continue;
			}
			const qsizetype close = matchingClose(tokens, j);
			const int unit = collect.beginUnit(QStringLiteral("class"), name, token.line, false);
			if (ScriptCodeUnit* target = collect.unit(unit)) {
				target->parent = parent;
				target->replaces = replaces;
				target->editorNumber = number;
				collect.addUse(unit, parent);
			}
			scanActorBody(tokens, j + 1, close, unit, false, collect);
			i = close + 1;
			continue;
		}
		if (token.is('{')) {
			// const, enum and damagetype blocks.
			i = matchingClose(tokens, i) + 1;
			continue;
		}
		++i;
	}
}

// [extend|mixin] class Name [: Parent] [replaces Other] [modifiers] { ... }
void scanZScript(const QVector<Token>& tokens, Collector& collect)
{
	qsizetype i = 0;
	while (i < tokens.size() && !collect.full()) {
		const Token& token = tokens[i];
		if (token.kind == Token::Include) {
			collect.include(token.text, token.line, QStringLiteral("#include"));
			++i;
			continue;
		}
		const bool extend = token.word("extend") && i + 1 < tokens.size() && tokens[i + 1].word("class");
		const bool mixin = token.word("mixin") && i + 1 < tokens.size() && tokens[i + 1].word("class");
		const qsizetype classAt = extend || mixin ? i + 1 : i;
		if (tokens[classAt].word("class") && classAt + 1 < tokens.size() && tokens[classAt + 1].kind == Token::Word) {
			const QString name = tokens[classAt + 1].text;
			QString parent;
			QString replaces;
			qsizetype j = classAt + 2;
			for (; j < tokens.size() && j < classAt + 32 && !tokens[j].is('{') && !tokens[j].is(';'); ++j) {
				if (tokens[j].is(':') && j + 1 < tokens.size()) {
					parent = tokens[++j].text;
				} else if (tokens[j].word("replaces") && j + 1 < tokens.size()) {
					replaces = tokens[++j].text;
				} else if (tokens[j].is('(')) {
					j = matchingClose(tokens, j);
				}
			}
			if (j >= tokens.size() || !tokens[j].is('{')) {
				i = j + 1;
				continue;
			}
			const qsizetype close = matchingClose(tokens, j);
			const int unit = collect.beginUnit(QStringLiteral("class"), name, tokens[classAt].line, extend);
			if (ScriptCodeUnit* target = collect.unit(unit)) {
				if (!extend) {
					target->parent = parent;
					target->replaces = replaces;
				}
				collect.addUse(unit, parent);
			}
			scanActorBody(tokens, j + 1, close, unit, true, collect);
			i = close + 1;
			continue;
		}
		if (token.is('{')) {
			// struct and enum bodies.
			i = matchingClose(tokens, i) + 1;
			continue;
		}
		++i;
	}
}

// --- SNDINFO -------------------------------------------------------------------

void scanSndinfo(const QVector<Token>& tokens, Collector& collect)
{
	qsizetype i = 0;
	const auto skipLine = [&tokens](qsizetype at) {
		const int line = tokens[at].line;
		while (at < tokens.size() && tokens[at].line == line) {
			++at;
		}
		return at;
	};
	while (i < tokens.size() && !collect.full()) {
		const Token& token = tokens[i];
		if (token.kind == Token::Punct) {
			++i;
			continue;
		}
		const QString word = token.text.toLower();
		if (token.kind == Token::Word && word.startsWith(QLatin1Char('$'))) {
			if (word == QStringLiteral("$random") && i + 2 < tokens.size() && tokens[i + 2].is('{')) {
				QStringList members;
				qsizetype j = i + 3;
				for (; j < tokens.size() && !tokens[j].is('}'); ++j) {
					if (!tokens[j].is('{')) {
						members << tokens[j].text;
					}
				}
				collect.defineSound(tokens[i + 1].text, QString(), members, token.line);
				i = j + 1;
			} else if (word == QStringLiteral("$alias") && i + 2 < tokens.size()) {
				collect.defineSound(tokens[i + 1].text, QString(), {tokens[i + 2].text}, token.line);
				i += 3;
			} else if (word == QStringLiteral("$playersound") && i + 4 < tokens.size()) {
				// $playersound <class> <gender> <*name> <lump>
				collect.defineSound(tokens[i + 3].text, tokens[i + 4].text, {}, token.line);
				i += 5;
			} else {
				i = skipLine(i);
			}
			continue;
		}
		// <logical name> <lump or file>
		if (i + 1 < tokens.size() && tokens[i + 1].line == token.line && tokens[i + 1].kind != Token::Punct) {
			collect.defineSound(token.text, tokens[i + 1].text, {}, token.line);
			i = skipLine(i);
			continue;
		}
		i = skipLine(i);
	}
}

// --- MAPINFO -------------------------------------------------------------------

QString mapInfoKeyKind(const QString& key)
{
	static const QHash<QString, QString> kinds {
		{QStringLiteral("music"), QStringLiteral("music")}, {QStringLiteral("intermusic"), QStringLiteral("music")},
		{QStringLiteral("titlemusic"), QStringLiteral("music")}, {QStringLiteral("finalemusic"), QStringLiteral("music")},
		{QStringLiteral("exitmusic"), QStringLiteral("music")}, {QStringLiteral("sky1"), QStringLiteral("texture")},
		{QStringLiteral("sky2"), QStringLiteral("texture")}, {QStringLiteral("titlepatch"), QStringLiteral("graphic")},
		{QStringLiteral("enterpic"), QStringLiteral("graphic")}, {QStringLiteral("exitpic"), QStringLiteral("graphic")},
		{QStringLiteral("titlepage"), QStringLiteral("graphic")}, {QStringLiteral("finalepage"), QStringLiteral("graphic")},
	};
	return kinds.value(key);
}

bool isMapInfoBlockKeyword(const QString& key)
{
	// "cluster" is a block only with braces: old MAPINFO maps say `cluster 1`.
	static const QSet<QString> blocks {
		QStringLiteral("map"), QStringLiteral("defaultmap"), QStringLiteral("adddefaultmap"), QStringLiteral("gamedefaults"), QStringLiteral("gameinfo"),
		QStringLiteral("clusterdef"), QStringLiteral("episode"), QStringLiteral("skill"), QStringLiteral("intermission"), QStringLiteral("automap"),
		QStringLiteral("automap_overlay"), QStringLiteral("damagetype"), QStringLiteral("doomednums"), QStringLiteral("spawnnums"),
		QStringLiteral("conversationids"), QStringLiteral("clearepisodes"), QStringLiteral("clearskills"),
	};
	return blocks.contains(key);
}

// The map lump a MAPINFO map header names: Hexen numbers its maps.
QString mapInfoMapName(const QString& value)
{
	bool number = false;
	const int index = value.toInt(&number);
	return number && index > 0 && index < 100 ? QStringLiteral("MAP%1").arg(index, 2, 10, QLatin1Char('0')) : value.toUpper();
}

void scanMapInfo(const QVector<Token>& tokens, Collector& collect)
{
	int depth = 0;
	int owner = -1;
	int ownerDepth = -1;
	qsizetype i = 0;
	while (i < tokens.size() && !collect.full()) {
		const Token& token = tokens[i];
		if (token.is('{')) {
			++depth;
			++i;
			continue;
		}
		if (token.is('}')) {
			--depth;
			if (ownerDepth >= 0 && depth < ownerDepth) {
				owner = -1;
				ownerDepth = -1;
			}
			++i;
			continue;
		}
		if (token.kind != Token::Word) {
			++i;
			continue;
		}
		const QString key = token.text.toLower();
		if (key == QStringLiteral("include") && i + 1 < tokens.size() && tokens[i + 1].kind != Token::Punct) {
			collect.include(tokens[i + 1].text, tokens[i + 1].line, token.text);
			i += 2;
			continue;
		}
		if ((key == QStringLiteral("doomednums") || key == QStringLiteral("spawnnums") || key == QStringLiteral("conversationids"))
			&& i + 1 < tokens.size() && tokens[i + 1].is('{')) {
			const qsizetype close = matchingClose(tokens, i + 1);
			if (key == QStringLiteral("doomednums")) {
				// DoomEdNums { 30001 = FireImp, 30002 = "Spark", 1, 2 }
				for (qsizetype j = i + 2; j + 2 < close; ++j) {
					bool ok = false;
					const int number = tokens[j].text.toInt(&ok);
					if (ok && tokens[j].kind == Token::Word && tokens[j + 1].is('=') && tokens[j + 2].kind != Token::Punct) {
						collect.defineEditorNumber(number, tokens[j + 2].text, tokens[j].line);
						j += 2;
					}
				}
			}
			i = close + 1;
			continue;
		}
		const bool clusterBlock = key == QStringLiteral("cluster") && i + 2 < tokens.size() && tokens[i + 2].is('{');
		if (depth == 0 && (isMapInfoBlockKeyword(key) || clusterBlock)) {
			owner = -1;
			ownerDepth = -1;
			QString name;
			if (key == QStringLiteral("map") && i + 1 < tokens.size() && tokens[i + 1].kind != Token::Punct) {
				name = mapInfoMapName(tokens[i + 1].text);
			} else if (key == QStringLiteral("defaultmap") || key == QStringLiteral("adddefaultmap") || key == QStringLiteral("gamedefaults")) {
				name = QStringLiteral("defaultmap");
			} else if (key == QStringLiteral("gameinfo")) {
				name = QStringLiteral("gameinfo");
			}
			if (!name.isEmpty()) {
				owner = collect.beginUnit(QStringLiteral("map"), name, token.line, true);
				// A brace block scopes the map; old MAPINFO runs to the next block.
				for (qsizetype j = i + 1; j < tokens.size() && j < i + 8; ++j) {
					if (tokens[j].is('{')) {
						ownerDepth = depth + 1;
						break;
					}
					if (tokens[j].kind == Token::Word && (isMapInfoBlockKeyword(tokens[j].text.toLower()) || !mapInfoKeyKind(tokens[j].text.toLower()).isEmpty())) {
						break;
					}
				}
			}
			++i;
			continue;
		}
		const QString kind = mapInfoKeyKind(key);
		if (!kind.isEmpty()) {
			qsizetype j = i + 1;
			if (j < tokens.size() && tokens[j].is('=')) {
				++j;
			}
			if (j < tokens.size() && tokens[j].kind != Token::Punct && !tokens[j].text.startsWith(QLatin1Char('$'))) {
				collect.addDoomName(kind, tokens[j].text, tokens[j].line, owner, token.text);
				i = j + 1;
				continue;
			}
		}
		++i;
	}
}

// --- The driver ----------------------------------------------------------------

struct ScriptJob {
	QString label;
	QString language;
	QString includedBy;
	int depth = 0;
	// A project file, or a lump in wads[wad].
	qsizetype fileIndex = -1;
	int wad = -1;
	qsizetype lumpIndex = -1;
};

bool isDoomLanguage(const QString& language)
{
	return language == QStringLiteral("decorate") || language == QStringLiteral("zscript") || language == QStringLiteral("sndinfo")
		|| language == QStringLiteral("mapinfo");
}

} // namespace

const ScriptCodeUnit* ScriptReferenceScan::unit(const QString& kind, const QString& name) const
{
	const auto it = m_unitIndex.constFind(unitKey(kind, name));
	return it == m_unitIndex.cend() ? nullptr : &units[*it];
}

const ScriptSource* ScriptReferenceScan::source(const QString& label) const
{
	const auto it = m_sourceIndex.constFind(folded(label));
	return it == m_sourceIndex.cend() ? nullptr : &scripts[*it];
}

const ScriptSoundDefinition* ScriptReferenceScan::soundDefinition(const QString& name) const
{
	const auto it = m_soundIndex.constFind(folded(name));
	return it == m_soundIndex.cend() ? nullptr : &sounds[*it];
}

QString ScriptReferenceScan::classForEditorNumber(int number) const
{
	const QString defined = m_editorNumberIndex.value(number);
	return defined.isEmpty() ? doomStandardClassForEditorNumber(number) : defined;
}

void ScriptReferenceScan::rebuildIndex()
{
	m_unitIndex.clear();
	m_sourceIndex.clear();
	m_soundIndex.clear();
	m_editorNumberIndex.clear();
	for (qsizetype i = 0; i < units.size(); ++i) {
		m_unitIndex.insert(unitKey(units[i].kind, units[i].name), int(i));
	}
	for (qsizetype i = 0; i < scripts.size(); ++i) {
		m_sourceIndex.insert(folded(scripts[i].label), int(i));
	}
	for (qsizetype i = 0; i < sounds.size(); ++i) {
		m_soundIndex.insert(folded(sounds[i].name), int(i));
	}
	// DECORATE headers first, then MAPINFO, which overrides them.
	for (const ScriptCodeUnit& unit : units) {
		if (unit.kind == QStringLiteral("class") && unit.editorNumber > 0) {
			m_editorNumberIndex.insert(unit.editorNumber, unit.name);
		}
	}
	for (const ScriptEditorNumber& entry : editorNumbers) {
		m_editorNumberIndex.insert(entry.number, entry.className);
	}
}

QString scriptLanguageForPath(const QString& virtualPath)
{
	const QString lower = virtualPath.trimmed().toLower().replace(QLatin1Char('\\'), QLatin1Char('/'));
	const QString suffix = suffixOf(lower);
	if (suffix == QStringLiteral("qc") || suffix == QStringLiteral("qh")) {
		return QStringLiteral("quakec");
	}
	if (suffix == QStringLiteral("menu") || (lower.startsWith(QStringLiteral("ui/")) && suffix == QStringLiteral("txt") && !lower.mid(3).contains(QLatin1Char('/')))) {
		return QStringLiteral("menu");
	}
	if (suffix == QStringLiteral("cfg") || suffix == QStringLiteral("rc")) {
		return QStringLiteral("config");
	}
	if (!lower.contains(QLatin1Char('/'))) {
		// ZDoom reads root files by their name before the first dot.
		const QString stem = lower.section(QLatin1Char('.'), 0, 0);
		return scriptLanguageForLump(stem);
	}
	return {};
}

QString scriptLanguageForLump(const QString& lumpName)
{
	const QString name = lumpName.trimmed().toLower();
	if (name == QStringLiteral("decorate")) {
		return QStringLiteral("decorate");
	}
	if (name == QStringLiteral("zscript")) {
		return QStringLiteral("zscript");
	}
	if (name == QStringLiteral("sndinfo")) {
		return QStringLiteral("sndinfo");
	}
	if (name == QStringLiteral("mapinfo") || name == QStringLiteral("zmapinfo")) {
		return QStringLiteral("mapinfo");
	}
	return {};
}

void ScriptReferenceScan::indexUnit(int index)
{
	if (index >= 0 && index < units.size()) {
		m_unitIndex.insert(unitKey(units[index].kind, units[index].name), index);
	}
}

namespace {

void scanText(const QString& language, const QString& text, const QString& label, const ScriptReferenceScanOptions& options,
	ScriptReferenceScan* scan, QStringList* includes)
{
	Collector collect(scan, options, label, language, includes);
	if (language == QStringLiteral("quakec")) {
		CTokenOptions tokenOptions;
		tokenOptions.dollarLines = true;
		scanQuakeC(tokenizeC(text, tokenOptions), collect);
	} else if (language == QStringLiteral("menu")) {
		CTokenOptions tokenOptions;
		tokenOptions.escapes = false;
		scanMenu(tokenizeC(text, tokenOptions), collect);
	} else if (language == QStringLiteral("config")) {
		scanConfigText(text, 1, 0, collect);
	} else if (language == QStringLiteral("decorate")) {
		scanDecorate(tokenizeC(text, {}), collect);
	} else if (language == QStringLiteral("zscript")) {
		scanZScript(tokenizeC(text, {}), collect);
	} else if (language == QStringLiteral("sndinfo")) {
		scanSndinfo(tokenizeLoose(text, QStringLiteral("{}"), true), collect);
	} else if (language == QStringLiteral("mapinfo")) {
		scanMapInfo(tokenizeLoose(text, QStringLiteral("{}=,"), true), collect);
	}
}

} // namespace

void scanScriptText(const QString& language, const QString& text, const QString& label, const ScriptReferenceScanOptions& options,
	ScriptReferenceScan* scan, QStringList* includes)
{
	scanText(language, text, label, options, scan, includes);
	scan->rebuildIndex();
}

ScriptReferenceScan scanScriptReferences(const PackageArchiveReader& files, const QVector<ScriptWadSource>& wads, const ScriptReferenceScanOptions& options)
{
	ScriptReferenceScan scan;
	const QVector<PackageEntry> entries = files.entries();
	QHash<QString, qsizetype> fileIndex;
	QVector<ScriptJob> jobs;
	for (qsizetype i = 0; i < entries.size(); ++i) {
		const PackageEntry& entry = entries[i];
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		fileIndex.insert(folded(entry.virtualPath), i);
		const QString language = scriptLanguageForPath(entry.virtualPath);
		if (!language.isEmpty()) {
			jobs.push_back({entry.virtualPath, language, QString(), 0, i, -1, -1});
		}
	}
	// WAD lumps, in each WAD's directory order.
	QVector<QVector<PackageEntry>> wadEntries;
	for (int w = 0; w < wads.size(); ++w) {
		wadEntries << (wads[w].reader ? wads[w].reader->entries() : QVector<PackageEntry>());
		const QVector<PackageEntry>& lumps = wadEntries.last();
		QVector<qsizetype> order(lumps.size());
		std::iota(order.begin(), order.end(), qsizetype(0));
		std::stable_sort(order.begin(), order.end(), [&lumps](qsizetype left, qsizetype right) { return lumps[left].sourceOrdinal < lumps[right].sourceOrdinal; });
		QHash<QString, int> seen;
		for (const qsizetype index : std::as_const(order)) {
			const QString language = scriptLanguageForLump(lumps[index].virtualPath);
			if (language.isEmpty() || lumps[index].kind != PackageEntryKind::File) {
				continue;
			}
			const QString lump = lumps[index].virtualPath.toUpper();
			const int occurrence = ++seen[lump];
			const QString label = wads[w].label + QLatin1Char(':') + lump + (occurrence > 1 ? QStringLiteral("#%1").arg(occurrence) : QString());
			jobs.push_back({label, language, QString(), 0, -1, w, index});
		}
	}

	QSet<QString> done;
	qint64 total = 0;
	for (qsizetype j = 0; j < jobs.size(); ++j) {
		const ScriptJob job = jobs[j];
		if (done.contains(folded(job.label))) {
			continue;
		}
		if ((options.isCancelled && options.isCancelled()) || (options.progress && !options.progress(int(j), int(jobs.size())))) {
			scan.cancelled = true;
			scan.complete = false;
			break;
		}
		if (int(done.size()) >= options.maximumScripts) {
			scan.complete = false;
			scan.warnings << QCoreApplication::translate("VibeStudioReleaseScripts", "The project has more than %1 scripts; the rest were not read.").arg(options.maximumScripts);
			break;
		}
		done.insert(folded(job.label));
		const PackageEntry& entry = job.wad >= 0 ? wadEntries[job.wad][job.lumpIndex] : entries[job.fileIndex];
		if (qint64(entry.sizeBytes) > options.maximumScriptBytes) {
			scan.complete = false;
			scan.warnings << QCoreApplication::translate("VibeStudioReleaseScripts", "%1 is too large to read for file names (over %2 MiB).")
				.arg(job.label).arg(options.maximumScriptBytes / (1024 * 1024));
			continue;
		}
		if (total + qint64(entry.sizeBytes) > options.maximumTotalBytes) {
			scan.complete = false;
			scan.warnings << QCoreApplication::translate("VibeStudioReleaseScripts", "The project's scripts are over %1 MiB in all; the rest were not read.")
				.arg(options.maximumTotalBytes / (1024 * 1024));
			break;
		}
		QByteArray bytes;
		QString error;
		const bool read = job.wad >= 0 ? wads[job.wad].reader->readEntryAt(job.lumpIndex, &bytes, &error, options.maximumScriptBytes + 1)
										: files.readEntryAt(job.fileIndex, &bytes, &error, options.maximumScriptBytes + 1);
		if (!read) {
			scan.complete = false;
			scan.warnings << QCoreApplication::translate("VibeStudioReleaseScripts", "%1 could not be read: %2").arg(job.label, error);
			continue;
		}
		total += bytes.size();
		scan.bytesRead = total;
		ScriptSource source;
		source.label = job.label;
		source.language = job.language;
		source.includedBy = job.includedBy;
		source.sizeBytes = bytes.size();
		if (job.wad >= 0) {
			source.wad = wads[job.wad].label;
			source.lumpOrdinal = entry.sourceOrdinal;
		}
		scan.scripts << source;
		// Binary files that happen to share a script's name are not scripts.
		if (bytes.contains('\0')) {
			continue;
		}
		QStringList includes;
		scanText(job.language, QString::fromUtf8(bytes), job.label, options, &scan, &includes);
		if (!isDoomLanguage(job.language) || job.depth >= options.maximumIncludeDepth) {
			if (!includes.isEmpty() && isDoomLanguage(job.language)) {
				scan.complete = false;
				scan.warnings << QCoreApplication::translate("VibeStudioReleaseScripts", "%1 includes scripts more than %2 levels deep; the deeper ones were not read.")
					.arg(job.label).arg(options.maximumIncludeDepth);
			}
			continue;
		}
		// #include and include name a lump in a WAD, or a file from the root.
		for (const QString& include : std::as_const(includes)) {
			ScriptJob next;
			next.language = job.language;
			next.includedBy = job.label;
			next.depth = job.depth + 1;
			QString path;
			if (job.wad >= 0 && looksLikeLumpName(include)) {
				// The including WAD first, then the others, last loaded first.
				QVector<int> order {job.wad};
				for (int w = int(wads.size()) - 1; w >= 0; --w) {
					if (w != job.wad) {
						order << w;
					}
				}
				for (const int w : std::as_const(order)) {
					const QVector<PackageEntry>& lumps = wadEntries[w];
					for (qsizetype l = 0; l < lumps.size() && next.lumpIndex < 0; ++l) {
						if (lumps[l].kind == PackageEntryKind::File && lumps[l].virtualPath.compare(include, Qt::CaseInsensitive) == 0) {
							next.wad = w;
							next.lumpIndex = l;
							next.label = wads[w].label + QLatin1Char(':') + include.toUpper();
						}
					}
					if (next.lumpIndex >= 0) {
						break;
					}
				}
			}
			if (next.lumpIndex < 0 && looksLikeGamePath(include, false, &path)) {
				const auto found = fileIndex.constFind(folded(path));
				if (found != fileIndex.cend()) {
					next.fileIndex = *found;
					next.label = entries[*found].virtualPath;
				}
			}
			if (!next.label.isEmpty() && !done.contains(folded(next.label))) {
				jobs.push_back(next);
			}
		}
	}
	scan.rebuildIndex();
	return scan;
}

namespace {

void addScriptWithIncluders(const ScriptReferenceScan& scan, const QString& label, QStringList* scripts)
{
	QString current = label;
	for (int depth = 0; depth < 32 && !current.isEmpty(); ++depth) {
		if (scripts->contains(current, Qt::CaseInsensitive)) {
			return;
		}
		scripts->append(current);
		const ScriptSource* source = scan.source(current);
		current = source ? source->includedBy : QString();
	}
}

} // namespace

ScriptReachable scriptReferencesForEntityClasses(const ScriptReferenceScan& scan, const QStringList& classNames)
{
	ScriptReachable reachable;
	QSet<QString> visited;
	QVector<QPair<QString, int>> queue;
	for (const QString& name : classNames) {
		if (scan.unit(QStringLiteral("function"), name) && !visited.contains(folded(name))) {
			visited.insert(folded(name));
			queue.push_back({name, 0});
		}
	}
	for (qsizetype i = 0; i < queue.size() && i < kMaximumReachedUnits; ++i) {
		const ScriptCodeUnit* unit = scan.unit(QStringLiteral("function"), queue[i].first);
		if (!unit) {
			continue;
		}
		reachable.units << unit->name;
		for (const QString& script : unit->scripts) {
			addScriptWithIncluders(scan, script, &reachable.scripts);
		}
		reachable.references += unit->references;
		if (queue[i].second >= kMaximumCallDepth) {
			continue;
		}
		for (const QString& use : unit->uses) {
			if (!visited.contains(folded(use)) && scan.unit(QStringLiteral("function"), use)) {
				visited.insert(folded(use));
				queue.push_back({use, queue[i].second + 1});
			}
		}
	}
	return reachable;
}

QVector<int> scriptSoundTargets(const ScriptReferenceScan& scan, const QString& logicalName, QStringList* scripts)
{
	QVector<int> targets;
	QSet<QString> visited;
	QVector<QPair<QString, int>> queue {{logicalName, 0}};
	for (qsizetype i = 0; i < queue.size(); ++i) {
		const QString key = folded(queue[i].first);
		if (visited.contains(key)) {
			continue;
		}
		visited.insert(key);
		const ScriptSoundDefinition* definition = scan.soundDefinition(queue[i].first);
		if (!definition) {
			continue;
		}
		if (scripts) {
			addScriptWithIncluders(scan, definition->script, scripts);
		}
		if (definition->reference >= 0 && !targets.contains(definition->reference)) {
			targets << definition->reference;
		}
		if (queue[i].second < kMaximumAliasDepth) {
			for (const QString& alias : definition->aliases) {
				queue.push_back({alias, queue[i].second + 1});
			}
		}
	}
	return targets;
}

ScriptReachable scriptReferencesForDoomThings(const ScriptReferenceScan& scan, const QSet<int>& thingTypes, const QStringList& mapNames)
{
	ScriptReachable reachable;
	// What each class is replaced by: the engine spawns the replacement.
	QHash<QString, QString> replacedBy;
	for (const ScriptCodeUnit& unit : scan.units) {
		if (unit.kind == QStringLiteral("class") && !unit.replaces.isEmpty()) {
			replacedBy.insert(folded(unit.replaces), unit.name);
		}
	}
	const auto spawned = [&replacedBy](QString name) {
		for (int i = 0; i < kMaximumReplacementChain; ++i) {
			const QString next = replacedBy.value(folded(name));
			if (next.isEmpty() || folded(next) == folded(name)) {
				break;
			}
			name = next;
		}
		return name;
	};
	QSet<QString> visited;
	QStringList queue;
	const auto enqueue = [&](const QString& name) {
		if (name.isEmpty() || visited.contains(folded(name)) || !scan.unit(QStringLiteral("class"), name)) {
			return;
		}
		visited.insert(folded(name));
		queue << name;
	};
	QList<int> types(thingTypes.cbegin(), thingTypes.cend());
	std::sort(types.begin(), types.end());
	for (const int type : std::as_const(types)) {
		const QString name = scan.classForEditorNumber(type);
		if (name.isEmpty()) {
			continue;
		}
		const QString actual = spawned(name);
		if (!scan.unit(QStringLiteral("class"), actual)) {
			continue;
		}
		enqueue(actual);
		// A number given in MAPINFO needs that MAPINFO.
		for (qsizetype i = scan.editorNumbers.size() - 1; i >= 0; --i) {
			if (scan.editorNumbers[i].number == type) {
				addScriptWithIncluders(scan, scan.editorNumbers[i].script, &reachable.scripts);
				break;
			}
		}
	}
	for (qsizetype i = 0; i < queue.size() && i < kMaximumReachedUnits; ++i) {
		const ScriptCodeUnit* unit = scan.unit(QStringLiteral("class"), queue[i]);
		if (!unit) {
			continue;
		}
		reachable.units << unit->name;
		for (const QString& script : unit->scripts) {
			addScriptWithIncluders(scan, script, &reachable.scripts);
		}
		for (const int reference : unit->references) {
			reachable.references << reference;
			const ScriptAssetReference& row = scan.references[reference];
			if (!row.soundName.isEmpty()) {
				scriptSoundTargets(scan, row.soundName, &reachable.scripts);
			}
		}
		// A parent is inherited as written; classes spawned by name are
		// replaced as the engine replaces them.
		enqueue(unit->parent);
		for (const QString& use : unit->uses) {
			enqueue(spawned(use));
		}
	}
	QStringList maps = mapNames;
	maps << QStringLiteral("defaultmap");
	for (const QString& map : std::as_const(maps)) {
		if (const ScriptCodeUnit* unit = scan.unit(QStringLiteral("map"), map)) {
			reachable.units << unit->name;
			for (const QString& script : unit->scripts) {
				addScriptWithIncluders(scan, script, &reachable.scripts);
			}
			reachable.references += unit->references;
		}
	}
	return reachable;
}

QString doomStandardClassForEditorNumber(int number)
{
	// Doom and Doom II monsters, weapons, ammunition, pickups, keys and the
	// barrel: Doom Wiki thing types, with ZDoom's class names.
	static const QHash<int, QString> classes {
		{3004, QStringLiteral("ZombieMan")}, {9, QStringLiteral("ShotgunGuy")}, {65, QStringLiteral("ChaingunGuy")}, {3001, QStringLiteral("DoomImp")},
		{3002, QStringLiteral("Demon")}, {58, QStringLiteral("Spectre")}, {3006, QStringLiteral("LostSoul")}, {3005, QStringLiteral("Cacodemon")},
		{69, QStringLiteral("HellKnight")}, {3003, QStringLiteral("BaronOfHell")}, {68, QStringLiteral("Arachnotron")}, {71, QStringLiteral("PainElemental")},
		{66, QStringLiteral("Revenant")}, {67, QStringLiteral("Fatso")}, {64, QStringLiteral("Archvile")}, {16, QStringLiteral("Cyberdemon")},
		{7, QStringLiteral("SpiderMastermind")}, {84, QStringLiteral("WolfensteinSS")}, {72, QStringLiteral("CommanderKeen")}, {88, QStringLiteral("BossBrain")},
		{89, QStringLiteral("BossEye")}, {87, QStringLiteral("BossTarget")},
		{2005, QStringLiteral("Chainsaw")}, {2001, QStringLiteral("Shotgun")}, {82, QStringLiteral("SuperShotgun")}, {2002, QStringLiteral("Chaingun")},
		{2003, QStringLiteral("RocketLauncher")}, {2004, QStringLiteral("PlasmaRifle")}, {2006, QStringLiteral("BFG9000")},
		{2007, QStringLiteral("Clip")}, {2048, QStringLiteral("ClipBox")}, {2008, QStringLiteral("Shell")}, {2049, QStringLiteral("ShellBox")},
		{2010, QStringLiteral("RocketAmmo")}, {2046, QStringLiteral("RocketBox")}, {2047, QStringLiteral("Cell")}, {17, QStringLiteral("CellPack")},
		{8, QStringLiteral("Backpack")},
		{2011, QStringLiteral("Stimpack")}, {2012, QStringLiteral("Medikit")}, {2014, QStringLiteral("HealthBonus")}, {2015, QStringLiteral("ArmorBonus")},
		{2018, QStringLiteral("GreenArmor")}, {2019, QStringLiteral("BlueArmor")}, {2013, QStringLiteral("Soulsphere")}, {83, QStringLiteral("Megasphere")},
		{2022, QStringLiteral("InvulnerabilitySphere")}, {2023, QStringLiteral("Berserk")}, {2024, QStringLiteral("BlurSphere")}, {2025, QStringLiteral("RadSuit")},
		{2026, QStringLiteral("Allmap")}, {2045, QStringLiteral("Infrared")},
		{5, QStringLiteral("BlueCard")}, {40, QStringLiteral("BlueSkull")}, {13, QStringLiteral("RedCard")}, {38, QStringLiteral("RedSkull")},
		{6, QStringLiteral("YellowCard")}, {39, QStringLiteral("YellowSkull")}, {2035, QStringLiteral("ExplosiveBarrel")},
	};
	return classes.value(number);
}

} // namespace vibestudio
