#include "core/level_udmf.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
// Original parser/projection, informed by UDMF v1.1 and ZDBSP's
// processor_udmf.cpp at bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659.
// ZDBSP: Christoph Oelckers (2009), GPL-2.0-or-later. See docs/CREDITS.md.
constexpr qsizetype maximumTextBytes = 64 * 1024 * 1024;
constexpr qsizetype maximumBlocks = 250000, maximumProperties = 1000000;
QString text(const char* value) { return QCoreApplication::translate("LevelUdmf", value); }
struct Problem {
	QString message;
};
void checkpoint(const std::function<bool()>& cancel) {
	if (cancel && cancel()) {
		throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "UDMF operation cancelled."))};
	}
}
bool letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; }
struct Token {
	QByteArray bytes;
	qsizetype begin = 0, end = 0;
	int line = 1;
	bool quoted = false;
};
class Parser {
  public:
	Parser(const QByteArray& bytes, const std::function<bool()>& cancel) : input(bytes), cancel(cancel) {}
	[[noreturn]] void fail(const QString& message) const {
		throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "TEXTMAP line %1: %2")).arg(line).arg(message)};
	}
	void advance() {
		if ((at & 4095) == 0) {
			checkpoint(cancel);
		}
		if (input[at] == '\0') {
			fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "NUL bytes are not valid UDMF syntax.")));
		}
		if (input[at++] == '\n') {
			++line;
		}
	}
	Token next() {
		for (;;) {
			while (at < input.size() && space(input[at])) {
				advance();
			}
			if (at + 1 >= input.size() || input[at] != '/') {
				break;
			}
			if (input[at + 1] == '/') {
				while (at < input.size() && input[at] != '\n') {
					advance();
				}
			} else if (input[at + 1] == '*') {
				advance();
				advance();
				while (at + 1 < input.size() && !(input[at] == '*' && input[at + 1] == '/')) {
					advance();
				}
				if (at + 1 >= input.size()) {
					fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "Unterminated block comment.")));
				}
				advance();
				advance();
			} else {
				break;
			}
		}
		Token token;
		token.begin = at;
		token.line = line;
		if (at == input.size()) {
			token.end = at;
			return token;
		}
		if (input[at] == '"') {
			token.quoted = true;
			advance();
			bool closed = false;
			while (at < input.size()) {
				const char c = input[at];
				advance();
				if (c == '"') {
					closed = true;
					break;
				}
				if (c == '\\' && at < input.size()) {
					advance();
				}
				if (at - token.begin > 1024 * 1024) {
					fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "A UDMF scalar exceeds 1 MiB.")));
				}
			}
			if (!closed) {
				fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "Unterminated quoted string.")));
			}
		} else if (QByteArrayView("{}=;").contains(input[at])) {
			advance();
		} else {
			while (at < input.size() && !space(input[at]) && !QByteArrayView("{}=;\"").contains(input[at])) {
				if (input[at] == '\0') {
					fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "NUL bytes are not valid UDMF syntax.")));
				}
				if (at + 1 < input.size() && input[at] == '/' && (input[at + 1] == '/' || input[at + 1] == '*')) {
					break;
				}
				advance();
				if (at - token.begin > 1024 * 1024) {
					fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "A UDMF scalar exceeds 1 MiB.")));
				}
			}
			if (at == token.begin) {
				fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "Unexpected character.")));
			}
		}
		token.end = at;
		token.bytes = input.mid(token.begin, at - token.begin);
		return token;
	}
	QString identifier(const Token& token) {
		if (token.quoted || token.bytes.isEmpty() || token.bytes.size() > 128 || !letter(token.bytes[0]) ||
			!std::all_of(token.bytes.cbegin() + 1, token.bytes.cend(), [](char c) { return letter(c) || digit(c); })) {
			fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "Expected an identifier of at most 128 ASCII characters.")));
		}
		return QString::fromLatin1(token.bytes).toLower();
	}
	void require(const char* punctuation) {
		if (next().bytes != punctuation) {
			fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "Expected '%1'.")).arg(QString::fromLatin1(punctuation)));
		}
	}
	LevelUdmfProperty property(const Token& key) {
		LevelUdmfProperty result;
		result.name = identifier(key);
		result.begin = key.begin;
		result.line = key.line;
		const auto value = next();
		if (value.bytes.isEmpty() ||
			(!value.quoted && (value.bytes == "{" || value.bytes == "}" || value.bytes == ";" || value.bytes == "="))) {
			fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "Expected one scalar property value.")));
		}
		result.literal = QString::fromUtf8(value.bytes);
		result.valueBegin = value.begin;
		result.valueEnd = value.end;
		if (value.quoted) {
			result.kind = LevelUdmfValueKind::String;
			QByteArray decoded;
			for (qsizetype i = 1; i + 1 < value.bytes.size(); ++i) {
				char c = value.bytes[i];
				if (c == '\\' && i + 2 < value.bytes.size()) {
					c = value.bytes[++i];
					if (c == 'n') {
						c = '\n';
					} else if (c == 'r') {
						c = '\r';
					} else if (c == 't') {
						c = '\t';
					}
				}
				decoded += c;
			}
			result.value = QString::fromUtf8(decoded);
		} else if (value.bytes.compare("true", Qt::CaseInsensitive) == 0 || value.bytes.compare("false", Qt::CaseInsensitive) == 0) {
			result.kind = LevelUdmfValueKind::Boolean;
			result.value = value.bytes.compare("true", Qt::CaseInsensitive) == 0;
		} else {
			static const QRegularExpression number(
				QStringLiteral("^[+-]?(?:0[xX][0-9a-fA-F]+|[0-9]+(?:\\.[0-9]*)?(?:[eE][+-]?[0-9]+)?|\\.[0-9]+(?:[eE][+-]?[0-9]+)?)$"));
			if (number.match(result.literal).hasMatch()) {
				result.kind = LevelUdmfValueKind::Number;
				bool valid = false;
				double n = 0;
				if (result.literal.contains('x', Qt::CaseInsensitive)) {
					n = double(result.literal.toLongLong(&valid, 0));
				} else {
					n = result.literal.toDouble(&valid);
				}
				if (valid && std::isfinite(n)) {
					result.value = n;
				}
			} else {
				identifier(value);
				result.kind = LevelUdmfValueKind::Keyword;
				result.value = result.literal;
			}
		}
		require(";");
		result.end = at;
		if (++properties > maximumProperties) {
			fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "TEXTMAP exceeds one million properties.")));
		}
		return result;
	}
	LevelUdmfDocument parse() {
		if (input.size() > maximumTextBytes) {
			fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "TEXTMAP exceeds the 64 MiB editor limit.")));
		}
		if (input.startsWith("\xef\xbb\xbf")) {
			at = 3;
		}
		LevelUdmfDocument result;
		result.source = input;
		QHash<QString, int> counts;
		for (;;) {
			checkpoint(cancel);
			const auto name = next();
			if (name.bytes.isEmpty()) {
				break;
			}
			const auto type = identifier(name);
			const auto opener = next();
			if (opener.bytes == "=") {
				result.globals << property(name);
				continue;
			}
			if (opener.bytes != "{") {
				fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "Expected '=' or '{' after an identifier.")));
			}
			if (result.blocks.size() >= maximumBlocks) {
				fail(text(QT_TRANSLATE_NOOP("LevelUdmf", "TEXTMAP exceeds 250,000 blocks.")));
			}
			LevelUdmfBlock block;
			block.type = type;
			block.index = counts[type]++;
			block.begin = name.begin;
			for (;;) {
				const auto key = next();
				if (key.bytes == "}") {
					block.close = key.begin;
					block.end = key.end;
					break;
				}
				identifier(key);
				require("=");
				block.properties << property(key);
			}
			result.blocks << std::move(block);
		}
		for (const auto& p : result.globals) {
			if (p.name == "namespace") {
				result.nameSpace = p.kind == LevelUdmfValueKind::String ? p.value.toString() : QString();
			}
		}
		return result;
	}
	const QByteArray& input;
	const std::function<bool()>& cancel;
	qsizetype at = 0, properties = 0;
	int line = 1;
};
using Fields = QHash<QString, const LevelUdmfProperty*>;
Fields fields(const QVector<LevelUdmfProperty>& props) {
	Fields result;
	for (const auto& p : props) {
		result.insert(p.name, &p);
	}
	return result;
}
double numeric(const Fields& props, const QString& key, double fallback = 0, bool required = false, bool integral = false) {
	const auto* p = props.value(key);
	if (!p && !required) {
		return fallback;
	}
	if (!p || p->kind != LevelUdmfValueKind::Number || !p->value.isDouble() || !std::isfinite(p->value.toDouble()) ||
		p->value.toDouble() < (integral ? double(std::numeric_limits<int>::min()) : -1e7) ||
		p->value.toDouble() > (integral ? double(std::numeric_limits<int>::max()) : 1e7) ||
		(integral && p->value.toDouble() != std::trunc(p->value.toDouble()))) {
		throw Problem{
			text(QT_TRANSLATE_NOOP("LevelUdmf", "Property '%1' requires a finite %2 within the editor range."))
				.arg(key, integral ? text(QT_TRANSLATE_NOOP("LevelUdmf", "integer")) : text(QT_TRANSLATE_NOOP("LevelUdmf", "number")))};
	}
	return p->value.toDouble();
}
int integer(const Fields& props, const QString& key, int fallback = 0, bool required = false) {
	return int(numeric(props, key, fallback, required, true));
}
bool boolean(const Fields& props, const QString& key, bool fallback = false) {
	const auto* p = props.value(key);
	if (!p) {
		return fallback;
	}
	if (p->kind != LevelUdmfValueKind::Boolean) {
		throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "Property '%1' requires true or false.")).arg(key)};
	}
	return p->value.toBool();
}
QString string(const Fields& props, const QString& key, const QString& fallback = {}) {
	const auto* p = props.value(key);
	if (!p) {
		return fallback;
	}
	if (p->kind != LevelUdmfValueKind::String) {
		throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "Property '%1' requires a quoted string.")).arg(key)};
	}
	return p->value.toString();
}
void issue(LevelMapDocument* document, const QString& code, const QString& message, const QString& object = {}, bool error = false) {
	LevelMapIssue item;
	item.severity = error ? LevelMapIssueSeverity::Error : LevelMapIssueSeverity::Warning;
	item.code = code;
	item.message = message;
	item.objectId = object;
	document->issues << item;
}
void project(const LevelUdmfDocument& source, LevelMapDocument* document, const std::function<bool()>& cancel) {
	document->doomVertices.clear();
	document->doomLinedefs.clear();
	document->doomThings.clear();
	document->doomSidedefs.clear();
	document->doomSectors.clear();
	document->textureReferences.clear();
	document->issues.removeIf([](const auto& issue) { return issue.code.startsWith("udmf-"); });
	if (source.nameSpace.isEmpty()) {
		throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "TEXTMAP requires a quoted namespace assignment."))};
	}
	const QStringList known{"doom", "heretic", "hexen", "strife", "zdoom", "zdoomtranslated", "vavoom", "eternity"};
	if (!known.contains(source.nameSpace.toLower())) {
		issue(document, "udmf-namespace",
			  text(QT_TRANSLATE_NOOP("LevelUdmf", "Namespace '%1' is preserved; preview uses common Doom fields.")).arg(source.nameSpace));
	}
	int extensionBlocks = 0;
	for (const auto& block : source.blocks) {
		checkpoint(cancel);
		const auto p = fields(block.properties);
		if (p.size() != block.properties.size()) {
			issue(document, "udmf-duplicate-key",
				  text(QT_TRANSLATE_NOOP("LevelUdmf",
										 "Duplicate property names use the last value; edit ambiguous keys in the source text.")),
				  block.selector());
		}
		if (block.type == "vertex") {
			LevelMapDoomVertex v;
			v.id = block.index;
			v.x = numeric(p, "x", 0, true);
			v.y = numeric(p, "y", 0, true);
			document->doomVertices << v;
		} else if (block.type == "linedef") {
			LevelMapDoomLinedef line;
			line.id = block.index;
			line.startVertex = integer(p, "v1", -1, true);
			line.endVertex = integer(p, "v2", -1, true);
			line.frontSidedef = integer(p, "sidefront", -1, true);
			line.backSidedef = integer(p, "sideback", -1);
			line.special = integer(p, "special");
			line.tag = integer(p, "id", -1);
			const QStringList flags{"blocking", "blockmonsters", "twosided", "dontpegtop", "dontpegbottom",
									"secret",	"blocksound",	 "dontdraw", "mapped"};
			for (int i = 0; i < flags.size(); ++i) {
				if (boolean(p, flags[i])) {
					line.flags |= 1 << i;
				}
			}
			for (int i = 0; i < 5; ++i) {
				line.args[i] = integer(p, QStringLiteral("arg%1").arg(i));
			}
			document->doomLinedefs << line;
		} else if (block.type == "sidedef") {
			LevelMapDoomSidedef side;
			side.id = block.index;
			side.sector = integer(p, "sector", -1, true);
			side.offsetX = numeric(p, "offsetx");
			side.offsetY = numeric(p, "offsety");
			side.upperTexture = string(p, "texturetop", "-");
			side.middleTexture = string(p, "texturemiddle", "-");
			side.lowerTexture = string(p, "texturebottom", "-");
			for (const auto& name : {side.upperTexture, side.middleTexture, side.lowerTexture}) {
				if (name != "-" && !name.isEmpty()) {
					document->textureReferences << name;
				}
			}
			document->doomSidedefs << side;
		} else if (block.type == "sector") {
			LevelMapDoomSector sector;
			sector.id = block.index;
			sector.floorHeight = numeric(p, "heightfloor");
			sector.ceilingHeight = numeric(p, "heightceiling");
			sector.floorTexture = string(p, "texturefloor");
			sector.ceilingTexture = string(p, "textureceiling");
			sector.lightLevel = integer(p, "lightlevel", 160);
			sector.special = integer(p, "special");
			sector.tag = integer(p, "id");
			for (const auto& name : {sector.floorTexture, sector.ceilingTexture}) {
				if (name != "-" && !name.isEmpty()) {
					document->textureReferences << name;
				}
			}
			document->doomSectors << sector;
		} else if (block.type == "thing") {
			LevelMapDoomThing thing;
			thing.id = block.index;
			thing.x = numeric(p, "x", 0, true);
			thing.y = numeric(p, "y", 0, true);
			thing.z = numeric(p, "height");
			thing.type = integer(p, "type", 0, true);
			thing.angle = integer(p, "angle");
			thing.tid = integer(p, "id");
			thing.special = integer(p, "special");
			for (int i = 0; i < 5; ++i) {
				thing.args[i] = integer(p, QStringLiteral("arg%1").arg(i));
			}
			const bool skill1 = boolean(p, "skill1"), skill2 = boolean(p, "skill2"), skill4 = boolean(p, "skill4"),
					   skill5 = boolean(p, "skill5");
			if (skill1 || skill2) {
				thing.flags |= 1;
			}
			if (boolean(p, "skill3")) {
				thing.flags |= 2;
			}
			if (skill4 || skill5) {
				thing.flags |= 4;
			}
			if (boolean(p, "ambush")) {
				thing.flags |= 8;
			}
			document->doomThings << thing;
		} else {
			++extensionBlocks;
		}
	}
	if (extensionBlocks) {
		issue(document, "udmf-extension-blocks",
			  text(QT_TRANSLATE_NOOP("LevelUdmf", "Extension blocks are preserved by the editor. Verify that your node builder supports "
												  "them; some builders discard unknown block types.")));
	}
	for (const auto& line : document->doomLinedefs) {
		checkpoint(cancel);
		if (line.startVertex < 0 || line.endVertex < 0 || line.startVertex >= document->doomVertices.size() ||
			line.endVertex >= document->doomVertices.size() || line.frontSidedef < 0 ||
			line.frontSidedef >= document->doomSidedefs.size() || line.backSidedef < -1 ||
			line.backSidedef >= document->doomSidedefs.size()) {
			issue(document, "udmf-line-reference", text(QT_TRANSLATE_NOOP("LevelUdmf", "Linedef references a missing vertex or sidedef.")),
				  QStringLiteral("linedef:%1").arg(line.id), true);
		}
	}
	for (const auto& side : document->doomSidedefs) {
		checkpoint(cancel);
		if (side.sector < 0 || side.sector >= document->doomSectors.size()) {
			issue(document, "udmf-side-reference", text(QT_TRANSLATE_NOOP("LevelUdmf", "Sidedef references a missing sector.")),
				  QStringLiteral("sidedef:%1").arg(side.id), true);
		}
	}
}
} // namespace

QString LevelUdmfBlock::selector() const { return type + ':' + QString::number(index); }
bool readLevelUdmfText(const QByteArray& bytes, LevelMapDocument* document, QString* error, const std::function<bool()>& cancel) {
	if (error) {
		error->clear();
	}
	if (!document) {
		return false;
	}
	try {
		checkpoint(cancel);
		auto source = std::make_shared<LevelUdmfDocument>(Parser(bytes, cancel).parse());
		auto candidate = *document;
		project(*source, &candidate, cancel);
		checkpoint(cancel);
		candidate.doomLumps["TEXTMAP"] = bytes;
		candidate.doomUdmf = std::move(source);
		*document = std::move(candidate);
		return true;
	} catch (const Problem& problem) {
		if (error) {
			*error = problem.message;
		}
		return false;
	}
}
static QByteArray prepareUdmfEdits(const LevelUdmfDocument& source, const QVector<LevelUdmfPropertyEdit>& edits, QString* error,
								   const std::function<bool()>& cancel, qsizetype editLimit) {
	if (error) {
		error->clear();
	}
	try {
		struct Replacement {
			qsizetype begin, end;
			QByteArray value;
		};
		if (edits.size() > editLimit) {
			throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "This UDMF transaction exceeds its %1-property edit limit.")).arg(editLimit)};
		}
		QVector<Replacement> replacements;
		QMap<qsizetype, QByteArray> additions;
		QSet<QString> seen;
		QHash<QString, const LevelUdmfBlock*> blocks;
		for (const auto& block : source.blocks) {
			checkpoint(cancel);
			blocks.insert(block.selector(), &block);
		}
		QHash<QString, Fields> propertyIndexes;
		const QByteArray newline = source.source.contains("\r\n") ? "\r\n" : "\n";
		for (const auto& edit : edits) {
			checkpoint(cancel);
			const auto selector = edit.object.trimmed().toLower(), key = edit.key.trimmed().toLower();
			QByteArray keyBytes = key.toUtf8();
			Parser keyParser(keyBytes, cancel);
			const auto keyToken = keyParser.next();
			keyParser.identifier(keyToken);
			if (keyToken.bytes != keyBytes || !keyParser.next().bytes.isEmpty()) {
				throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "Enter exactly one UDMF property name."))};
			}
			const auto unique = selector + '/' + key;
			if (seen.contains(unique)) {
				throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "A property appears more than once in this edit."))};
			}
			seen.insert(unique);
			const QVector<LevelUdmfProperty>* props = selector == "global" ? &source.globals : nullptr;
			qsizetype insert = source.blocks.isEmpty() ? source.source.size() : source.blocks.first().begin;
			if (!props) {
				if (const auto* block = blocks.value(selector)) {
					props = &block->properties;
					insert = block->close;
				}
			}
			if (!props) {
				throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "UDMF object '%1' was not found.")).arg(selector)};
			}
			if (!propertyIndexes.contains(selector)) {
				Fields index;
				for (const auto& property : *props) {
					checkpoint(cancel);
					index.insert(property.name, index.contains(property.name) ? nullptr : &property);
				}
				propertyIndexes.insert(selector, std::move(index));
			}
			const auto& indexed = propertyIndexes[selector];
			const auto* existing = indexed.value(key);
			if (indexed.contains(key) && !existing) {
				throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "Property '%1' is duplicated; this edit would be ambiguous.")).arg(key)};
			}
			if (edit.remove) {
				if (existing) {
					replacements << Replacement{existing->begin, existing->end, {}};
				}
				continue;
			}
			const QByteArray literal = edit.literal.trimmed().toUtf8();
			const auto test = QByteArray("field=") + literal + ';';
			Parser parser(test, cancel);
			const auto parsed = parser.parse();
			if (parsed.globals.size() != 1 || !parsed.blocks.isEmpty() || parsed.globals.first().literal.toUtf8() != literal) {
				throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "Enter exactly one UDMF scalar value."))};
			}
			if (existing) {
				replacements << Replacement{existing->valueBegin, existing->valueEnd, literal};
			} else {
				additions[insert] +=
					newline + (selector == "global" ? QByteArray() : QByteArray("\t")) + keyBytes + " = " + literal + ';' + newline;
			}
		}
		for (auto it = additions.cbegin(); it != additions.cend(); ++it) {
			replacements << Replacement{it.key(), it.key(), it.value()};
		}
		std::sort(replacements.begin(), replacements.end(), [](const auto& a, const auto& b) { return a.begin < b.begin; });
		QByteArray result;
		qsizetype at = 0;
		for (const auto& replacement : replacements) {
			checkpoint(cancel);
			if (replacement.begin < at || replacement.end > source.source.size()) {
				throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "UDMF source spans overlap."))};
			}
			result += QByteArrayView(source.source).sliced(at, replacement.begin - at);
			result += replacement.value;
			at = replacement.end;
			if (result.size() > maximumTextBytes) {
				throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "TEXTMAP exceeds the 64 MiB editor limit."))};
			}
		}
		result += QByteArrayView(source.source).sliced(at);
		if (result.size() > maximumTextBytes) {
			throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "TEXTMAP exceeds the 64 MiB editor limit."))};
		}
		checkpoint(cancel);
		return result;
	} catch (const Problem& problem) {
		if (error) {
			*error = problem.message;
		}
		return {};
	}
}
QByteArray prepareLevelUdmfProperties(const LevelUdmfDocument& source, const QVector<LevelUdmfPropertyEdit>& edits, QString* error,
									  const std::function<bool()>& cancel) {
	return prepareUdmfEdits(source, edits, error, cancel, 4096);
}
QByteArray prepareLevelUdmfTransform(const LevelUdmfDocument& source, const LevelMapUndoCommand& transform, QString* error,
									 const std::function<bool()>& cancel) {
	if (error) {
		error->clear();
	}
	try {
		QVector<LevelUdmfPropertyEdit> edits;
		const auto scalar = [&](const QString& object, const QString& key, double before, double after) {
			if (before != after) {
				edits << LevelUdmfPropertyEdit{object, key, QString::number(after, 'g', 17)};
			}
		};
		const auto pairs = [&](const auto& before, const auto& after, const auto& collect) {
			if (before.size() != after.size()) {
				throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "The UDMF transform has mismatched object snapshots."))};
			}
			for (qsizetype i = 0; i < before.size(); ++i) {
				checkpoint(cancel);
				if (before[i].id != after[i].id) {
					throw Problem{text(QT_TRANSLATE_NOOP("LevelUdmf", "The UDMF transform changed object identities."))};
				}
				collect(before[i], after[i]);
			}
		};
		pairs(transform.vertexSnapshots, transform.vertexResults, [&](const auto& a, const auto& b) {
			const auto object = QStringLiteral("vertex:%1").arg(a.id);
			scalar(object, "x", a.x, b.x);
			scalar(object, "y", a.y, b.y);
		});
		pairs(transform.thingSnapshots, transform.thingResults, [&](const auto& a, const auto& b) {
			const auto object = QStringLiteral("thing:%1").arg(a.id);
			scalar(object, "x", a.x, b.x);
			scalar(object, "y", a.y, b.y);
			scalar(object, "height", a.z, b.z);
			scalar(object, "angle", a.angle, b.angle);
		});
		pairs(transform.linedefSnapshots, transform.linedefResults, [&](const auto& a, const auto& b) {
			const auto object = QStringLiteral("linedef:%1").arg(a.id);
			scalar(object, "v1", a.startVertex, b.startVertex);
			scalar(object, "v2", a.endVertex, b.endVertex);
		});
		return prepareUdmfEdits(source, edits, error, cancel, maximumProperties);
	} catch (const Problem& problem) {
		if (error) {
			*error = problem.message;
		}
		return {};
	}
}
QJsonObject levelUdmfDocumentJson(const LevelUdmfDocument& document) {
	const auto props = [](const auto& properties) {
		QJsonArray result;
		for (const auto& p : properties) {
			result << QJsonObject{{"key", p.name}, {"literal", p.literal}, {"line", p.line}};
		}
		return result;
	};
	QJsonArray blocks;
	for (const auto& b : document.blocks) {
		blocks << QJsonObject{{"object", b.selector()}, {"properties", props(b.properties)}};
	}
	return {{"namespace", document.nameSpace}, {"globals", props(document.globals)}, {"blocks", blocks}};
}
} // namespace vibestudio
