#include "core/material_script.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>
#include <optional>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

enum class TokenType {
	Word,
	Number,
	String,
	Punctuation,
};

struct Token {
	QString text;
	int start = 0;
	int end = 0;
	int line = 1;
	int column = 1;
	// A line break separates this token from the one before it.
	bool newlineBefore = true;
	TokenType type = TokenType::Word;

	[[nodiscard]] bool is(const char* value) const
	{
		return type != TokenType::String && text.compare(QLatin1String(value), Qt::CaseInsensitive) == 0;
	}
	[[nodiscard]] bool isBrace(QChar brace) const
	{
		return type != TokenType::String && text.size() == 1 && text.at(0) == brace;
	}
};

struct LineIndex {
	QVector<int> starts;

	explicit LineIndex(const QString& text)
	{
		starts.push_back(0);
		for (int index = 0; index < text.size(); ++index) {
			if (text.at(index) == QLatin1Char('\n')) {
				starts.push_back(index + 1);
			}
		}
	}
	int lineOf(int offset) const
	{
		const auto found = std::upper_bound(starts.cbegin(), starts.cend(), offset);
		return static_cast<int>(found - starts.cbegin());
	}
	int columnOf(int offset) const
	{
		const int line = lineOf(offset);
		return offset - starts.at(line - 1) + 1;
	}
};

// Quake III's COM_ParseExt: whitespace-separated words, quoted strings that
// may span lines, `//` comments and `/* */` comments. A block comment does
// not count as a line break, as in the engine.
QVector<Token> lexQuake3(const QString& text, const LineIndex& lines)
{
	QVector<Token> tokens;
	const int size = text.size();
	int position = 0;
	bool sawNewline = true;
	while (position < size) {
		const QChar c = text.at(position);
		if (c == QLatin1Char('\n')) {
			sawNewline = true;
			++position;
			continue;
		}
		if (c.unicode() <= 32) {
			++position;
			continue;
		}
		if (c == QLatin1Char('/') && position + 1 < size && text.at(position + 1) == QLatin1Char('/')) {
			while (position < size && text.at(position) != QLatin1Char('\n')) {
				++position;
			}
			continue;
		}
		if (c == QLatin1Char('/') && position + 1 < size && text.at(position + 1) == QLatin1Char('*')) {
			position += 2;
			while (position + 1 < size && !(text.at(position) == QLatin1Char('*') && text.at(position + 1) == QLatin1Char('/'))) {
				++position;
			}
			position = std::min(size, position + 2);
			continue;
		}
		Token token;
		token.start = position;
		token.newlineBefore = sawNewline;
		sawNewline = false;
		if (c == QLatin1Char('"')) {
			++position;
			const int contentStart = position;
			while (position < size && text.at(position) != QLatin1Char('"')) {
				++position;
			}
			token.text = text.mid(contentStart, position - contentStart);
			token.type = TokenType::String;
			if (position < size) {
				++position;
			}
		} else {
			while (position < size && text.at(position).unicode() > 32) {
				++position;
			}
			token.text = text.mid(token.start, position - token.start);
			token.type = TokenType::Word;
		}
		token.end = position;
		token.line = lines.lineOf(token.start);
		token.column = lines.columnOf(token.start);
		tokens.push_back(token);
	}
	return tokens;
}

bool isNameStart(QChar c)
{
	// With LEXFL_ALLOWPATHNAMES a name may also start with a path character,
	// so `time/2` is one name (a bad term) and `/2` after a number is one too.
	return c.isLetter() || c == QLatin1Char('_') || c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':')
		|| c == QLatin1Char('.');
}

bool isNameChar(QChar c)
{
	// idLexer::ReadName with LEXFL_ALLOWPATHNAMES.
	return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('/') || c == QLatin1Char('\\')
		|| c == QLatin1Char(':') || c == QLatin1Char('.');
}

// Doom 3's idLexer with the decl flags: names may hold path characters,
// punctuation is its own token, and line breaks only matter to the few
// keywords that read "on line".
QVector<Token> lexDoom3(const QString& text, const LineIndex& lines)
{
	QVector<Token> tokens;
	const int size = text.size();
	int position = 0;
	bool sawNewline = true;
	static const QStringList multiPunctuation {QStringLiteral("&&"), QStringLiteral("||"), QStringLiteral(">="),
		QStringLiteral("<="), QStringLiteral("=="), QStringLiteral("!=")};
	while (position < size) {
		const QChar c = text.at(position);
		if (c == QLatin1Char('\n')) {
			sawNewline = true;
			++position;
			continue;
		}
		if (c.isSpace() || c.unicode() < 32) {
			++position;
			continue;
		}
		if (c == QLatin1Char('/') && position + 1 < size && text.at(position + 1) == QLatin1Char('/')) {
			while (position < size && text.at(position) != QLatin1Char('\n')) {
				++position;
			}
			continue;
		}
		if (c == QLatin1Char('/') && position + 1 < size && text.at(position + 1) == QLatin1Char('*')) {
			position += 2;
			while (position + 1 < size && !(text.at(position) == QLatin1Char('*') && text.at(position + 1) == QLatin1Char('/'))) {
				if (text.at(position) == QLatin1Char('\n')) {
					sawNewline = true;
				}
				++position;
			}
			position = std::min(size, position + 2);
			continue;
		}
		Token token;
		token.start = position;
		token.newlineBefore = sawNewline;
		sawNewline = false;
		if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
			const QChar quote = c;
			++position;
			const int contentStart = position;
			while (position < size && text.at(position) != quote && text.at(position) != QLatin1Char('\n')) {
				++position;
			}
			token.text = text.mid(contentStart, position - contentStart);
			token.type = TokenType::String;
			if (position < size && text.at(position) == quote) {
				++position;
			}
		} else if (c.isDigit() || (c == QLatin1Char('.') && position + 1 < size && text.at(position + 1).isDigit())) {
			if (c == QLatin1Char('0') && position + 1 < size && (text.at(position + 1) == QLatin1Char('x') || text.at(position + 1) == QLatin1Char('X'))) {
				position += 2;
				while (position < size && (text.at(position).isDigit() || QStringLiteral("abcdefABCDEF").contains(text.at(position)))) {
					++position;
				}
			} else {
				while (position < size && text.at(position).isDigit()) {
					++position;
				}
				if (position < size && text.at(position) == QLatin1Char('.')) {
					++position;
					while (position < size && text.at(position).isDigit()) {
						++position;
					}
				}
				if (position + 1 < size && (text.at(position) == QLatin1Char('e') || text.at(position) == QLatin1Char('E'))
					&& (text.at(position + 1).isDigit()
						|| ((text.at(position + 1) == QLatin1Char('-') || text.at(position + 1) == QLatin1Char('+')) && position + 2 < size
							&& text.at(position + 2).isDigit()))) {
					position += 2;
					while (position < size && text.at(position).isDigit()) {
						++position;
					}
				}
			}
			token.text = text.mid(token.start, position - token.start);
			token.type = TokenType::Number;
		} else if (isNameStart(c)) {
			while (position < size && isNameChar(text.at(position))) {
				++position;
			}
			token.text = text.mid(token.start, position - token.start);
			token.type = TokenType::Word;
		} else {
			token.type = TokenType::Punctuation;
			const QString pair = text.mid(position, 2);
			if (multiPunctuation.contains(pair)) {
				token.text = pair;
				position += 2;
			} else {
				token.text = QString(c);
				++position;
			}
		}
		token.end = position;
		token.line = lines.lineOf(token.start);
		token.column = lines.columnOf(token.start);
		tokens.push_back(token);
	}
	return tokens;
}

double toNumber(const QString& text, bool* ok = nullptr)
{
	bool converted = false;
	double value = text.trimmed().toDouble(&converted);
	if (!converted) {
		// atof semantics: the longest numeric prefix, else zero.
		QString prefix;
		for (const QChar c : text.trimmed()) {
			if (c.isDigit() || c == QLatin1Char('.') || ((c == QLatin1Char('-') || c == QLatin1Char('+')) && prefix.isEmpty())) {
				prefix.append(c);
			} else {
				break;
			}
		}
		value = prefix.toDouble(&converted);
		if (!converted) {
			value = 0.0;
		}
		converted = false;
	}
	if (ok) {
		*ok = converted;
	}
	return std::isfinite(value) ? value : 0.0;
}

QString numberText(double value)
{
	if (std::abs(value - std::round(value)) < 1e-9 && std::abs(value) < 1e9) {
		return QString::number(static_cast<qint64>(std::llround(value)));
	}
	QString text = QString::number(value, 'f', 6);
	while (text.endsWith(QLatin1Char('0'))) {
		text.chop(1);
	}
	if (text.endsWith(QLatin1Char('.'))) {
		text.chop(1);
	}
	return text;
}

// ---------------------------------------------------------------------------
// Shared parser state
// ---------------------------------------------------------------------------

class ParserBase {
public:
	ParserBase(const QString& text, MaterialScript* script)
		: m_text(text), m_lines(text), m_script(script)
	{
	}

protected:
	[[nodiscard]] bool atEnd() const { return m_position >= m_tokens.size(); }
	const Token* peek(int ahead = 0) const
	{
		const int index = m_position + ahead;
		return index >= 0 && index < m_tokens.size() ? &m_tokens.at(index) : nullptr;
	}
	const Token* take()
	{
		return atEnd() ? nullptr : &m_tokens.at(m_position++);
	}
	// The next token when it is on the current line.
	const Token* takeOnLine()
	{
		const Token* next = peek();
		if (!next || next->newlineBefore) {
			return nullptr;
		}
		++m_position;
		return next;
	}
	[[nodiscard]] bool nextOnLine() const
	{
		const Token* next = peek();
		return next && !next->newlineBefore;
	}
	MaterialSourceSpan spanOf(int start, int end) const
	{
		MaterialSourceSpan span;
		span.start = start;
		span.end = end;
		span.line = m_lines.lineOf(start);
		span.endLine = m_lines.lineOf(std::max(start, end - 1));
		return span;
	}
	void diagnose(MaterialDefinition* definition, MaterialDiagnosticSeverity severity, const QString& code, const QString& message,
		const Token* at, int stage = -1)
	{
		MaterialDiagnostic diagnostic;
		diagnostic.severity = severity;
		diagnostic.code = code;
		diagnostic.message = message;
		diagnostic.stage = stage;
		if (at) {
			diagnostic.line = at->line;
			diagnostic.column = at->column;
			diagnostic.length = at->end - at->start;
		}
		if (definition) {
			diagnostic.material = definition->name;
			definition->diagnostics.push_back(diagnostic);
		} else {
			m_script->diagnostics.push_back(diagnostic);
		}
	}
	void reject(MaterialDefinition* definition, const QString& reason, const Token* at, int stage = -1)
	{
		if (definition->engineRejection.isEmpty()) {
			definition->engineRejection = reason;
		}
		diagnose(definition, MaterialDiagnosticSeverity::Error, QStringLiteral("engine-rejects"), reason, at, stage);
	}
	// Builds a directive from the keyword token through the last consumed
	// token.
	MaterialDirective directiveFrom(int keywordIndex, int endIndex) const
	{
		MaterialDirective directive;
		const Token& keyword = m_tokens.at(keywordIndex);
		directive.keyword = keyword.text;
		int end = keyword.end;
		for (int index = keywordIndex + 1; index < endIndex; ++index) {
			directive.arguments << m_tokens.at(index).text;
			end = m_tokens.at(index).end;
		}
		if (endIndex > keywordIndex + 1) {
			const int argumentStart = m_tokens.at(keywordIndex + 1).start;
			directive.argumentText = m_text.mid(argumentStart, end - argumentStart);
		}
		directive.span = spanOf(keyword.start, end);
		return directive;
	}

	const QString& m_text;
	LineIndex m_lines;
	MaterialScript* m_script = nullptr;
	QVector<Token> m_tokens;
	int m_position = 0;
};

// ---------------------------------------------------------------------------
// Quake III
// ---------------------------------------------------------------------------

// tr_shader.c ParseSort names and their numbers.
const QHash<QString, double>& quake3SortValues()
{
	static const QHash<QString, double> values {
		{QStringLiteral("portal"), 1},
		{QStringLiteral("sky"), 2},
		{QStringLiteral("opaque"), 3},
		{QStringLiteral("decal"), 4},
		{QStringLiteral("seethrough"), 5},
		{QStringLiteral("banner"), 6},
		{QStringLiteral("underwater"), 8},
		{QStringLiteral("additive"), 10},
		{QStringLiteral("nearest"), 16},
	};
	return values;
}

// The infoParms[] surface parameters that q3map and the engine read.
const QStringList& quake3SurfaceParms()
{
	static const QStringList parms {
		QStringLiteral("water"), QStringLiteral("slime"), QStringLiteral("lava"), QStringLiteral("playerclip"),
		QStringLiteral("monsterclip"), QStringLiteral("nodrop"), QStringLiteral("nonsolid"), QStringLiteral("origin"),
		QStringLiteral("trans"), QStringLiteral("detail"), QStringLiteral("structural"), QStringLiteral("areaportal"),
		QStringLiteral("clusterportal"), QStringLiteral("donotenter"), QStringLiteral("fog"), QStringLiteral("sky"),
		QStringLiteral("lightfilter"), QStringLiteral("alphashadow"), QStringLiteral("hint"), QStringLiteral("slick"),
		QStringLiteral("noimpact"), QStringLiteral("nomarks"), QStringLiteral("ladder"), QStringLiteral("nodamage"),
		QStringLiteral("metalsteps"), QStringLiteral("flesh"), QStringLiteral("nosteps"), QStringLiteral("nodraw"),
		QStringLiteral("pointlight"), QStringLiteral("nolightmap"), QStringLiteral("nodlight"), QStringLiteral("dust"),
		QStringLiteral("botclip"), QStringLiteral("antiportal"), QStringLiteral("skip"),
	};
	return parms;
}

// Stage keywords of Quake III derivatives (Return to Castle Wolfenstein,
// Enemy Territory, Jedi Knight II/Academy). Vanilla Quake III rejects them.
const QStringList& quake3ExtensionStageKeywords()
{
	static const QStringList keywords {
		QStringLiteral("glow"), QStringLiteral("surfacesprites"), QStringLiteral("ssfademax"), QStringLiteral("ssfadescale"),
		QStringLiteral("ssvariance"), QStringLiteral("sshangdown"), QStringLiteral("ssanyangle"), QStringLiteral("ssfaceup"),
		QStringLiteral("sswind"), QStringLiteral("sswindidle"), QStringLiteral("ssduration"), QStringLiteral("ssgrow"),
		QStringLiteral("ssweather"), QStringLiteral("mapcomp"), QStringLiteral("mapnocomp"), QStringLiteral("animmapcomp"),
		QStringLiteral("animmapnocomp"), QStringLiteral("fog"), QStringLiteral("nodepthtest"), QStringLiteral("lightmap"),
	};
	return keywords;
}

// General keywords of Quake III derivatives: Return to Castle Wolfenstein
// and Enemy Territory (fog, sun, light grid and compression controls) and
// the Jedi Knight games (surface materials). Vanilla Quake III rejects them.
const QStringList& quake3ExtensionGlobalKeywords()
{
	static const QStringList keywords {
		QStringLiteral("fogvars"), QStringLiteral("skyfogvars"), QStringLiteral("waterfogvars"), QStringLiteral("sunshader"),
		QStringLiteral("lightgridmulamb"), QStringLiteral("lightgridmuldir"), QStringLiteral("nofog"), QStringLiteral("allowcompress"),
		QStringLiteral("nocompress"), QStringLiteral("distancecull"), QStringLiteral("material"), QStringLiteral("hitlocation"),
		QStringLiteral("hitmaterial"),
	};
	return keywords;
}

const QStringList& quake3StageKeywordList()
{
	static const QStringList keywords {
		QStringLiteral("map"), QStringLiteral("clampmap"), QStringLiteral("animmap"), QStringLiteral("videomap"),
		QStringLiteral("alphafunc"), QStringLiteral("depthfunc"), QStringLiteral("detail"), QStringLiteral("blendfunc"),
		QStringLiteral("rgbgen"), QStringLiteral("alphagen"), QStringLiteral("tcgen"), QStringLiteral("texgen"),
		QStringLiteral("tcmod"), QStringLiteral("depthwrite"),
	};
	return keywords;
}

const QStringList& quake3GlobalKeywordList()
{
	static const QStringList keywords {
		QStringLiteral("qer_editorimage"), QStringLiteral("qer_trans"), QStringLiteral("qer_nocarve"), QStringLiteral("qer_alphafunc"),
		QStringLiteral("q3map_sun"), QStringLiteral("q3map_surfacelight"), QStringLiteral("q3map_lightimage"),
		QStringLiteral("q3map_lightsubdivide"), QStringLiteral("q3map_globaltexture"), QStringLiteral("q3map_backsplash"),
		QStringLiteral("q3map_nolightmap"), QStringLiteral("q3map_novertexshadows"), QStringLiteral("q3map_flare"),
		QStringLiteral("deformvertexes"), QStringLiteral("tesssize"), QStringLiteral("clamptime"), QStringLiteral("surfaceparm"),
		QStringLiteral("nomipmaps"), QStringLiteral("nopicmip"), QStringLiteral("polygonoffset"), QStringLiteral("entitymergable"),
		QStringLiteral("fogparms"), QStringLiteral("portal"), QStringLiteral("skyparms"), QStringLiteral("light"),
		QStringLiteral("cull"), QStringLiteral("sort"),
	};
	return keywords;
}

class Quake3Parser final : public ParserBase {
public:
	using ParserBase::ParserBase;

	void parse()
	{
		m_tokens = lexQuake3(m_text, m_lines);
		while (!atEnd()) {
			const int nameIndex = m_position;
			const Token* name = take();
			if (name->isBrace(QLatin1Char('{')) || name->isBrace(QLatin1Char('}'))) {
				MaterialDiagnostic diagnostic;
				diagnostic.severity = MaterialDiagnosticSeverity::Error;
				diagnostic.code = QStringLiteral("unexpected-brace");
				diagnostic.message = Text::tr("A brace appears where a shader name was expected.");
				diagnostic.line = name->line;
				diagnostic.column = name->column;
				m_script->diagnostics.push_back(diagnostic);
				if (name->isBrace(QLatin1Char('{'))) {
					skipBracedSection(1);
				}
				continue;
			}
			parseShader(nameIndex);
		}
	}

private:
	void skipBracedSection(int depth)
	{
		while (depth > 0 && !atEnd()) {
			const Token* token = take();
			if (token->isBrace(QLatin1Char('{'))) {
				++depth;
			} else if (token->isBrace(QLatin1Char('}'))) {
				--depth;
			}
		}
	}

	void parseShader(int nameIndex)
	{
		const Token& nameToken = m_tokens.at(nameIndex);
		m_implicitKind.clear();
		m_implicitImage.clear();
		MaterialDefinition definition;
		definition.name = nameToken.text;
		definition.engine = MaterialEngine::Quake3;
		definition.kind = QStringLiteral("shader");
		definition.sourcePath = m_script->path;
		definition.nameSpan = spanOf(nameToken.start, nameToken.end);
		const Token* open = peek();
		if (!open || !open->isBrace(QLatin1Char('{'))) {
			// ParseShader warns "expecting '{'" and the shader is not loaded.
			reject(&definition, Text::tr("Quake III expects '{' after the shader name and does not load this shader."), open ? open : &nameToken);
			if (open) {
				take();
			}
			definition.span = spanOf(nameToken.start, open ? open->end : nameToken.end);
			m_script->materials.push_back(definition);
			return;
		}
		take();
		const int bodyStart = open->start;
		int stageCount = 0;
		bool closed = false;
		int bodyEnd = open->end;
		while (!atEnd()) {
			const int keywordIndex = m_position;
			const Token* token = take();
			if (token->isBrace(QLatin1Char('}'))) {
				closed = true;
				bodyEnd = token->end;
				break;
			}
			if (token->isBrace(QLatin1Char('{'))) {
				if (stageCount >= 8) {
					reject(&definition, Text::tr("Quake III allows at most 8 stages (MAX_SHADER_STAGES) and drops this shader."), token, stageCount);
				}
				parseStage(&definition, keywordIndex, stageCount);
				++stageCount;
				continue;
			}
			parseGlobal(&definition, keywordIndex);
		}
		if (!closed) {
			reject(&definition, Text::tr("The shader has no closing brace; Quake III stops reading it."), &nameToken);
			bodyEnd = m_tokens.isEmpty() ? nameToken.end : m_tokens.last().end;
		}
		definition.bodySpan = spanOf(bodyStart, bodyEnd);
		definition.span = spanOf(nameToken.start, bodyEnd);
		applyImplicitStages(&definition);
		finishShader(&definition);
		m_script->materials.push_back(definition);
	}

	// Enemy Territory's SetImplicitShaderStages for a lightmapped surface:
	// the lightmap, then the image multiplied in; masked or blended, the image
	// first, then the lightmap where it drew (depthFunc equal). Scripted
	// stages are left as they are.
	void applyImplicitStages(MaterialDefinition* definition) const
	{
		if (m_implicitKind.isEmpty() || !definition->stages.isEmpty()) {
			return;
		}
		QString image = m_implicitImage.isEmpty() || m_implicitImage == QStringLiteral("-") ? definition->name : m_implicitImage;
		if (QFileInfo(image).suffix().isEmpty()) {
			image += QStringLiteral(".tga");
		}
		const MaterialDefinition defaults = implicitQuake3Material(definition->name, MaterialSurfaceContext::World, image);
		if (m_implicitKind == QStringLiteral("map") || defaults.stages.size() != 2) {
			definition->stages = defaults.stages;
		} else {
			MaterialStage texture = defaults.stages.at(1);
			MaterialStage lightmap = defaults.stages.at(0);
			texture.index = 0;
			texture.blend = MaterialBlend();
			texture.depthWrite = true;
			if (m_implicitKind == QStringLiteral("mask")) {
				texture.alphaTest = MaterialAlphaTest::GreaterEqual128;
			} else {
				texture.blend.source = MaterialBlendFactor::SourceAlpha;
				texture.blend.destination = MaterialBlendFactor::OneMinusSourceAlpha;
				texture.blend.explicitBlend = true;
			}
			lightmap.index = 1;
			lightmap.blend.source = MaterialBlendFactor::DestinationColor;
			lightmap.blend.destination = MaterialBlendFactor::Zero;
			lightmap.blend.explicitBlend = true;
			lightmap.depthFunc = MaterialDepthFunc::Equal;
			lightmap.depthWrite = false;
			definition->stages = {texture, lightmap};
			// "if ( implicitCullType && !shader.cullType )": front-sided is the
			// unset value, so the implicit two-sided cull wins over it.
			if (definition->cull == MaterialCull::Front) {
				definition->cull = MaterialCull::None;
			}
		}
		MaterialDiagnostic note;
		note.severity = MaterialDiagnosticSeverity::Info;
		note.code = QStringLiteral("implicit-stages");
		note.message = Text::tr("The stages come from the implicit keyword; write stage blocks to edit them one by one.");
		note.material = definition->name;
		note.line = definition->span.line;
		definition->diagnostics.push_back(note);
	}

	QString m_implicitKind;
	QString m_implicitImage;

	// Consumes the rest of the line, as SkipRestOfLine does, warning when a
	// brace is swallowed with it.
	int skipRestOfLine(MaterialDefinition* definition, int stage)
	{
		while (nextOnLine()) {
			const Token* token = take();
			if (token->isBrace(QLatin1Char('{')) || token->isBrace(QLatin1Char('}'))) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("brace-swallowed"),
					Text::tr("Quake III skips the rest of this line, including this brace."), token, stage);
			}
		}
		return m_position;
	}

	static bool parseWave(const QStringList& words, int first, MaterialWave* wave)
	{
		if (words.size() < first + 5) {
			return false;
		}
		MaterialWaveFunction function = MaterialWaveFunction::Sin;
		if (!materialWaveFunctionFromId(words.at(first), &function)) {
			return false;
		}
		wave->function = function;
		wave->base = toNumber(words.at(first + 1));
		wave->amplitude = toNumber(words.at(first + 2));
		wave->phase = toNumber(words.at(first + 3));
		wave->frequency = toNumber(words.at(first + 4));
		return true;
	}

	// Parse1DMatrix: "( a b c )".
	static bool parseVector(const QStringList& words, int first, int count, double* out)
	{
		if (words.size() < first + count + 2 || words.at(first) != QStringLiteral("(") || words.at(first + count + 1) != QStringLiteral(")")) {
			return false;
		}
		for (int index = 0; index < count; ++index) {
			out[index] = toNumber(words.at(first + 1 + index));
		}
		return true;
	}

	QStringList takeLineWords()
	{
		QStringList words;
		while (nextOnLine()) {
			words << take()->text;
		}
		return words;
	}

	void parseGlobal(MaterialDefinition* definition, int keywordIndex)
	{
		const Token& keyword = m_tokens.at(keywordIndex);
		const QString key = keyword.text.toLower();
		bool recognised = true;
		bool previewIgnored = false;
		if (key.startsWith(QStringLiteral("qer"))) {
			// Editor-only keys: ParseShader skips the rest of the line.
			previewIgnored = true;
			const Token* argument = peek();
			if (key == QStringLiteral("qer_editorimage") && argument && !argument->newlineBefore) {
				definition->editorImage = argument->text;
			} else if (key == QStringLiteral("qer_trans") && argument && !argument->newlineBefore) {
				definition->editorTransparency = toNumber(argument->text);
			}
			skipRestOfLine(definition, -1);
		} else if (key.startsWith(QStringLiteral("q3map"))) {
			previewIgnored = true;
			skipRestOfLine(definition, -1);
		} else if (key == QStringLiteral("deformvertexes")) {
			parseDeform(definition, keywordIndex);
		} else if (key == QStringLiteral("tesssize")) {
			const Token* size = peek();
			if (size && !size->newlineBefore) {
				definition->tessSize = toNumber(size->text);
			}
			skipRestOfLine(definition, -1);
		} else if (key == QStringLiteral("clamptime")) {
			takeOnLine();
		} else if (key == QStringLiteral("surfaceparm")) {
			const Token* parm = takeOnLine();
			if (!parm) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("surfaceparm needs a parameter name."), &keyword);
			} else {
				const QString name = parm->text.toLower();
				if (!quake3SurfaceParms().contains(name)) {
					diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("unknown-surfaceparm"),
						Text::tr("Unknown surface parameter '%1'; Quake III and q3map2 ignore it.").arg(parm->text), parm);
				} else if (!definition->surfaceParms.contains(name)) {
					definition->surfaceParms << name;
				}
			}
		} else if (key == QStringLiteral("nomipmaps")) {
			definition->noMipMaps = true;
			definition->noPicMip = true;
		} else if (key == QStringLiteral("nopicmip")) {
			definition->noPicMip = true;
		} else if (key == QStringLiteral("polygonoffset")) {
			definition->polygonOffset = true;
			definition->polygonOffsetValue = 1.0;
		} else if (key == QStringLiteral("entitymergable")) {
			definition->flags << QStringLiteral("entitymergable");
		} else if (key == QStringLiteral("fogparms")) {
			const QStringList words = takeLineWords();
			double color[3] = {0.0, 0.0, 0.0};
			if (!parseVector(words, 0, 3, color)) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
					Text::tr("fogParms needs a colour in parentheses: fogParms ( r g b ) distance."), &keyword);
			} else if (words.size() < 6) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("fogParms is missing the distance to opaque."), &keyword);
			} else {
				definition->fog.present = true;
				definition->fog.color = {color[0], color[1], color[2]};
				definition->fog.distanceToOpaque = toNumber(words.at(5));
			}
		} else if (key == QStringLiteral("portal")) {
			definition->portal = true;
			definition->sort = QStringLiteral("portal");
			definition->sortValue = 1;
		} else if (key == QStringLiteral("skyparms")) {
			const QStringList words = takeLineWords();
			if (words.size() < 3) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("skyParms needs a far box, a cloud height and a near box (use - for none)."), &keyword);
			}
			definition->sky.present = true;
			definition->sky.farBox = words.value(0);
			const double height = toNumber(words.value(1));
			definition->sky.cloudHeight = height == 0.0 ? 512.0 : height;
			definition->sky.nearBox = words.value(2);
			if (definition->sort.isEmpty()) {
				definition->sort = QStringLiteral("sky");
				definition->sortValue = 2;
			}
		} else if (key == QStringLiteral("light")) {
			takeOnLine();
			previewIgnored = true;
		} else if (key == QStringLiteral("cull")) {
			const Token* side = takeOnLine();
			if (!side) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("cull needs none, back or front."), &keyword);
			} else {
				const QString value = side->text.toLower();
				if (value == QStringLiteral("none") || value == QStringLiteral("twosided") || value == QStringLiteral("disable")) {
					definition->cull = MaterialCull::None;
				} else if (value == QStringLiteral("back") || value == QStringLiteral("backside") || value == QStringLiteral("backsided")) {
					definition->cull = MaterialCull::Back;
				} else if (value == QStringLiteral("front")) {
					diagnose(definition, MaterialDiagnosticSeverity::Info, QStringLiteral("bad-arguments"),
						Text::tr("'cull front' is not a Quake III keyword; it warns and keeps the default, which already draws front faces."), side);
				} else {
					diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
						Text::tr("Unknown cull side '%1'; Quake III keeps the default.").arg(side->text), side);
				}
			}
		} else if (key == QStringLiteral("sort")) {
			const Token* value = takeOnLine();
			if (!value) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("sort needs a name or a number."), &keyword);
			} else {
				const auto named = quake3SortValues().constFind(value->text.toLower());
				definition->sort = value->text;
				definition->sortValue = named != quake3SortValues().constEnd() ? *named : toNumber(value->text);
			}
		} else if (key.startsWith(QStringLiteral("implicit"))) {
			// Enemy Territory (tr_shader.c ParseShader): any keyword starting
			// "implicit" draws an image, or the shader's own name for "-",
			// with the engine's default stages: implicitMask alpha-tested and
			// implicitBlend blended, both two-sided.
			m_implicitKind = key == QStringLiteral("implicitmask") ? QStringLiteral("mask")
				: key == QStringLiteral("implicitblend")			 ? QStringLiteral("blend")
																	 : QStringLiteral("map");
			const Token* image = takeOnLine();
			m_implicitImage = image ? image->text : QString();
			skipRestOfLine(definition, -1);
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("extension-keyword"),
				Text::tr("'%1' is an Enemy Territory keyword; vanilla Quake III rejects the shader. The preview draws it as Enemy Territory does.")
					.arg(keyword.text),
				&keyword);
		} else if (quake3ExtensionGlobalKeywords().contains(key)) {
			previewIgnored = true;
			skipRestOfLine(definition, -1);
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("extension-keyword"),
				Text::tr("'%1' belongs to a Quake III derivative (Wolfenstein, Jedi Knight); vanilla Quake III rejects the shader. "
						 "The preview ignores it.")
					.arg(keyword.text),
				&keyword);
		} else {
			recognised = false;
			reject(definition, Text::tr("Unknown general shader parameter '%1'; Quake III drops this shader and draws its default.").arg(keyword.text),
				&keyword);
			skipRestOfLine(definition, -1);
		}
		MaterialDirective directive = directiveFrom(keywordIndex, m_position);
		directive.recognised = recognised;
		directive.previewIgnored = previewIgnored;
		definition->directives.push_back(directive);
	}

	void parseDeform(MaterialDefinition* definition, int keywordIndex)
	{
		const Token& keyword = m_tokens.at(keywordIndex);
		const QStringList words = takeLineWords();
		MaterialDeform deform;
		deform.directive = definition->directives.size();
		const QString type = words.value(0).toLower();
		bool ok = true;
		if (type == QStringLiteral("wave")) {
			deform.kind = MaterialDeformKind::Wave;
			const double div = toNumber(words.value(1));
			// ParseDeform: a zero spread falls back to 100 with a warning.
			if (div == 0.0) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
					Text::tr("deformVertexes wave needs a non-zero spread; Quake III uses 100."), &keyword);
				deform.spread = 1.0 / 100.0;
			} else {
				deform.spread = 1.0 / div;
			}
			ok = parseWave(words, 2, &deform.wave);
		} else if (type == QStringLiteral("normal")) {
			deform.kind = MaterialDeformKind::Normal;
			deform.wave.amplitude = toNumber(words.value(1));
			deform.wave.frequency = toNumber(words.value(2));
			ok = words.size() >= 3;
		} else if (type == QStringLiteral("bulge")) {
			deform.kind = MaterialDeformKind::Bulge;
			deform.values = {toNumber(words.value(1)), toNumber(words.value(2)), toNumber(words.value(3))};
			ok = words.size() >= 4;
		} else if (type == QStringLiteral("move")) {
			deform.kind = MaterialDeformKind::Move;
			deform.values = {toNumber(words.value(1)), toNumber(words.value(2)), toNumber(words.value(3))};
			ok = parseWave(words, 4, &deform.wave);
		} else if (type == QStringLiteral("autosprite")) {
			deform.kind = MaterialDeformKind::Autosprite;
		} else if (type == QStringLiteral("autosprite2")) {
			deform.kind = MaterialDeformKind::Autosprite2;
		} else if (type == QStringLiteral("projectionshadow")) {
			deform.kind = MaterialDeformKind::ProjectionShadow;
		} else if (type.startsWith(QStringLiteral("text")) && type.size() == 5) {
			deform.kind = MaterialDeformKind::Text;
		} else {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("Unknown deformVertexes type '%1'; Quake III ignores it.").arg(words.value(0)), &keyword);
			return;
		}
		if (!ok) {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("deformVertexes %1 is missing parameters.").arg(type), &keyword);
			return;
		}
		if ((deform.kind == MaterialDeformKind::Wave || deform.kind == MaterialDeformKind::Move)
			&& deform.wave.function == MaterialWaveFunction::Noise) {
			reject(definition, Text::tr("Only rgbGen wave accepts noise; Quake III stops with an error here."), &keyword);
			return;
		}
		if (definition->deforms.size() >= 3) {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("too-many"),
				Text::tr("Quake III supports at most 3 deformVertexes per shader; this one is ignored."), &keyword);
			return;
		}
		definition->deforms.push_back(deform);
	}

	void parseStage(MaterialDefinition* definition, int openIndex, int stageIndex)
	{
		const Token& open = m_tokens.at(openIndex);
		MaterialStage stage;
		stage.index = stageIndex;
		bool closed = false;
		int end = open.end;
		bool blendSeen = false;
		while (!atEnd()) {
			const int keywordIndex = m_position;
			const Token* token = take();
			if (token->isBrace(QLatin1Char('}'))) {
				closed = true;
				end = token->end;
				break;
			}
			if (token->isBrace(QLatin1Char('{'))) {
				reject(definition, Text::tr("A stage cannot contain another stage; Quake III drops this shader."), token, stageIndex);
				stage.directives.push_back(directiveFrom(keywordIndex, m_position));
				continue;
			}
			parseStageKeyword(definition, &stage, keywordIndex, &blendSeen);
		}
		if (!closed) {
			reject(definition, Text::tr("A stage has no closing brace; Quake III drops this shader."), &open, stageIndex);
			end = m_tokens.isEmpty() ? open.end : m_tokens.last().end;
		}
		stage.span = spanOf(open.start, end);
		finishStage(&stage, blendSeen);
		definition->stages.push_back(stage);
	}

	void parseStageKeyword(MaterialDefinition* definition, MaterialStage* stage, int keywordIndex, bool* blendSeen)
	{
		const Token& keyword = m_tokens.at(keywordIndex);
		const QString key = keyword.text.toLower();
		const int stageIndex = stage->index;
		bool recognised = true;
		const auto missing = [&](const QString& what) {
			reject(definition, Text::tr("Missing parameter for '%1'; Quake III drops this shader.").arg(what), &keyword, stageIndex);
		};
		if (key == QStringLiteral("map") || key == QStringLiteral("clampmap")) {
			const Token* image = takeOnLine();
			if (!image) {
				missing(keyword.text);
			} else {
				const QString name = image->text;
				stage->clamp = key == QStringLiteral("clampmap");
				if (name.compare(QStringLiteral("$lightmap"), Qt::CaseInsensitive) == 0) {
					stage->imageKind = MaterialImageKind::Lightmap;
					stage->tcGen.source = MaterialTexCoordSource::Lightmap;
				} else if (name.compare(QStringLiteral("$whiteimage"), Qt::CaseInsensitive) == 0
					|| name.compare(QStringLiteral("*white"), Qt::CaseInsensitive) == 0) {
					stage->imageKind = MaterialImageKind::White;
				} else {
					stage->imageKind = MaterialImageKind::File;
					stage->imagePath = name;
				}
			}
		} else if (key == QStringLiteral("animmap")) {
			const Token* frequency = takeOnLine();
			if (!frequency) {
				missing(keyword.text);
			} else {
				stage->imageKind = MaterialImageKind::Animation;
				stage->animationFrequency = toNumber(frequency->text);
				while (nextOnLine()) {
					const Token* frame = take();
					if (stage->animationFrames.size() < 8) {
						stage->animationFrames << frame->text;
					} else {
						diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("too-many"),
							Text::tr("animMap uses at most 8 images; Quake III ignores '%1'.").arg(frame->text), frame, stageIndex);
					}
				}
				if (stage->animationFrames.isEmpty()) {
					stage->imageKind = MaterialImageKind::None;
				}
			}
		} else if (key == QStringLiteral("videomap")) {
			const Token* video = takeOnLine();
			if (!video) {
				missing(keyword.text);
			} else {
				stage->imageKind = MaterialImageKind::Video;
				stage->imagePath = video->text;
			}
		} else if (key == QStringLiteral("alphafunc")) {
			const Token* function = takeOnLine();
			if (!function) {
				missing(keyword.text);
			} else {
				const QString value = function->text.toUpper();
				if (value == QStringLiteral("GT0")) {
					stage->alphaTest = MaterialAlphaTest::Greater0;
				} else if (value == QStringLiteral("LT128")) {
					stage->alphaTest = MaterialAlphaTest::Less128;
				} else if (value == QStringLiteral("GE128")) {
					stage->alphaTest = MaterialAlphaTest::GreaterEqual128;
				} else {
					diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
						Text::tr("Invalid alphaFunc '%1'; use GT0, LT128 or GE128.").arg(function->text), function, stageIndex);
				}
			}
		} else if (key == QStringLiteral("depthfunc")) {
			const Token* function = takeOnLine();
			if (!function) {
				missing(keyword.text);
			} else if (function->text.compare(QStringLiteral("equal"), Qt::CaseInsensitive) == 0) {
				stage->depthFunc = MaterialDepthFunc::Equal;
			} else if (function->text.compare(QStringLiteral("lequal"), Qt::CaseInsensitive) == 0) {
				stage->depthFunc = MaterialDepthFunc::LessEqual;
			} else {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
					Text::tr("Unknown depthFunc '%1'; use lequal or equal.").arg(function->text), function, stageIndex);
			}
		} else if (key == QStringLiteral("detail")) {
			stage->detail = true;
		} else if (key == QStringLiteral("blendfunc")) {
			parseBlendFunc(definition, stage, keyword);
			*blendSeen = true;
		} else if (key == QStringLiteral("rgbgen")) {
			parseColorGen(definition, stage, keyword, false);
		} else if (key == QStringLiteral("alphagen")) {
			parseColorGen(definition, stage, keyword, true);
		} else if (key == QStringLiteral("tcgen") || key == QStringLiteral("texgen")) {
			const QStringList words = takeLineWords();
			const QString source = words.value(0).toLower();
			stage->tcGen.explicitlySet = true;
			if (source == QStringLiteral("environment")) {
				stage->tcGen.source = MaterialTexCoordSource::Environment;
			} else if (source == QStringLiteral("lightmap")) {
				stage->tcGen.source = MaterialTexCoordSource::Lightmap;
			} else if (source == QStringLiteral("texture") || source == QStringLiteral("base")) {
				stage->tcGen.source = MaterialTexCoordSource::Base;
			} else if (source == QStringLiteral("vector")) {
				double s[3] = {1, 0, 0};
				double t[3] = {0, 1, 0};
				if (!parseVector(words, 1, 3, s) || !parseVector(words, 6, 3, t)) {
					diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
						Text::tr("tcGen vector needs two vectors: ( sx sy sz ) ( tx ty tz )."), &keyword, stageIndex);
				}
				stage->tcGen.source = MaterialTexCoordSource::Vector;
				stage->tcGen.vectorS = {s[0], s[1], s[2]};
				stage->tcGen.vectorT = {t[0], t[1], t[2]};
			} else {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
					Text::tr("Unknown tcGen '%1'.").arg(words.value(0)), &keyword, stageIndex);
			}
		} else if (key == QStringLiteral("tcmod")) {
			parseTcMod(definition, stage, keywordIndex);
		} else if (key == QStringLiteral("depthwrite")) {
			stage->depthWrite = true;
			stage->depthWriteExplicit = true;
		} else if (quake3ExtensionStageKeywords().contains(key)) {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("extension-keyword"),
				Text::tr("'%1' belongs to a Quake III derivative (Wolfenstein, Jedi Knight); vanilla Quake III rejects the shader. "
						 "The preview ignores it.")
					.arg(keyword.text),
				&keyword, stageIndex);
			takeLineWords();
		} else {
			recognised = false;
			reject(definition, Text::tr("Unknown stage keyword '%1'; Quake III drops this shader and draws its default.").arg(keyword.text),
				&keyword, stageIndex);
			takeLineWords();
		}
		MaterialDirective directive = directiveFrom(keywordIndex, m_position);
		directive.recognised = recognised;
		if (key == QStringLiteral("tcmod") && !stage->tcMods.isEmpty() && stage->tcMods.last().directive == -2) {
			stage->tcMods.last().directive = stage->directives.size();
		}
		stage->directives.push_back(directive);
	}

	void parseBlendFunc(MaterialDefinition* definition, MaterialStage* stage, const Token& keyword)
	{
		const Token* first = takeOnLine();
		if (!first) {
			reject(definition, Text::tr("Missing parameter for 'blendFunc'; Quake III drops this shader."), &keyword, stage->index);
			return;
		}
		stage->blend.explicitBlend = true;
		const QString shorthand = first->text.toLower();
		if (shorthand == QStringLiteral("add")) {
			stage->blend.source = MaterialBlendFactor::One;
			stage->blend.destination = MaterialBlendFactor::One;
			stage->blend.written = first->text;
			return;
		}
		if (shorthand == QStringLiteral("filter")) {
			stage->blend.source = MaterialBlendFactor::DestinationColor;
			stage->blend.destination = MaterialBlendFactor::Zero;
			stage->blend.written = first->text;
			return;
		}
		if (shorthand == QStringLiteral("blend")) {
			stage->blend.source = MaterialBlendFactor::SourceAlpha;
			stage->blend.destination = MaterialBlendFactor::OneMinusSourceAlpha;
			stage->blend.written = first->text;
			return;
		}
		const Token* second = takeOnLine();
		if (!second) {
			reject(definition, Text::tr("blendFunc needs two GL factors or add, filter or blend; Quake III drops this shader."), &keyword, stage->index);
			return;
		}
		stage->blend.written = first->text + QLatin1Char(' ') + second->text;
		MaterialBlendFactor source = MaterialBlendFactor::One;
		MaterialBlendFactor destination = MaterialBlendFactor::One;
		static const QSet<QString> sourceFactors {QStringLiteral("GL_ONE"), QStringLiteral("GL_ZERO"), QStringLiteral("GL_DST_COLOR"),
			QStringLiteral("GL_ONE_MINUS_DST_COLOR"), QStringLiteral("GL_SRC_ALPHA"), QStringLiteral("GL_ONE_MINUS_SRC_ALPHA"),
			QStringLiteral("GL_DST_ALPHA"), QStringLiteral("GL_ONE_MINUS_DST_ALPHA"), QStringLiteral("GL_SRC_ALPHA_SATURATE")};
		static const QSet<QString> destinationFactors {QStringLiteral("GL_ONE"), QStringLiteral("GL_ZERO"), QStringLiteral("GL_SRC_ALPHA"),
			QStringLiteral("GL_ONE_MINUS_SRC_ALPHA"), QStringLiteral("GL_DST_ALPHA"), QStringLiteral("GL_ONE_MINUS_DST_ALPHA"),
			QStringLiteral("GL_SRC_COLOR"), QStringLiteral("GL_ONE_MINUS_SRC_COLOR")};
		// NameToSrcBlendMode / NameToDstBlendMode warn and fall back to GL_ONE.
		if (!sourceFactors.contains(first->text.toUpper()) || !materialBlendFactorFromId(first->text, &source)) {
			source = MaterialBlendFactor::One;
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("Unknown blend source '%1'; Quake III uses GL_ONE.").arg(first->text), first, stage->index);
		}
		if (!destinationFactors.contains(second->text.toUpper()) || !materialBlendFactorFromId(second->text, &destination)) {
			destination = MaterialBlendFactor::One;
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("Unknown blend destination '%1'; Quake III uses GL_ONE.").arg(second->text), second, stage->index);
		}
		stage->blend.source = source;
		stage->blend.destination = destination;
	}

	void parseColorGen(MaterialDefinition* definition, MaterialStage* stage, const Token& keyword, bool alpha)
	{
		const QStringList words = takeLineWords();
		if (words.isEmpty()) {
			reject(definition, Text::tr("Missing parameter for '%1'; Quake III drops this shader.").arg(keyword.text), &keyword, stage->index);
			return;
		}
		MaterialColorGen& gen = alpha ? stage->alphaGen : stage->rgbGen;
		gen.explicitlySet = true;
		const QString source = words.first().toLower();
		const auto bad = [&]() {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("%1 %2 has missing or invalid parameters.").arg(keyword.text, words.first()), &keyword, stage->index);
		};
		if (source == QStringLiteral("wave")) {
			gen.source = MaterialColorSource::Wave;
			if (!parseWave(words, 1, &gen.wave)) {
				bad();
			} else if (alpha && gen.wave.function == MaterialWaveFunction::Noise) {
				// TableForFunc has no noise table: only rgbGen may use noise.
				reject(definition, Text::tr("Only rgbGen wave accepts noise; Quake III stops with an error here."), &keyword, stage->index);
			}
		} else if (source == QStringLiteral("const")) {
			gen.source = MaterialColorSource::Constant;
			if (alpha) {
				if (words.size() < 2) {
					bad();
				}
				gen.constantAlpha = toNumber(words.value(1));
			} else {
				double color[3] = {1, 1, 1};
				if (!parseVector(words, 1, 3, color)) {
					bad();
				}
				gen.constant = {color[0], color[1], color[2]};
			}
		} else if (source == QStringLiteral("identity")) {
			gen.source = MaterialColorSource::Identity;
		} else if (!alpha && source == QStringLiteral("identitylighting")) {
			gen.source = MaterialColorSource::IdentityLighting;
		} else if (source == QStringLiteral("entity")) {
			gen.source = MaterialColorSource::Entity;
		} else if (source == QStringLiteral("oneminusentity")) {
			gen.source = MaterialColorSource::OneMinusEntity;
		} else if (source == QStringLiteral("vertex")) {
			gen.source = MaterialColorSource::Vertex;
			// rgbGen vertex also sets alphaGen vertex when none was given.
			if (!alpha && !stage->alphaGen.explicitlySet) {
				stage->alphaGen.source = MaterialColorSource::Vertex;
			}
		} else if (!alpha && source == QStringLiteral("exactvertex")) {
			gen.source = MaterialColorSource::ExactVertex;
		} else if (!alpha && source == QStringLiteral("lightingdiffuse")) {
			gen.source = MaterialColorSource::LightingDiffuse;
		} else if (source == QStringLiteral("oneminusvertex")) {
			gen.source = MaterialColorSource::OneMinusVertex;
		} else if (alpha && source == QStringLiteral("lightingspecular")) {
			gen.source = MaterialColorSource::LightingSpecular;
		} else if (alpha && source == QStringLiteral("portal")) {
			gen.source = MaterialColorSource::Portal;
			if (words.size() < 2) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("alphaGen portal has no range; Quake III uses 256."), &keyword, stage->index);
				gen.portalRange = 256.0;
			} else {
				gen.portalRange = toNumber(words.at(1));
			}
			definition->portal = true;
		} else {
			gen.explicitlySet = false;
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("Unknown %1 '%2'; Quake III ignores it.").arg(keyword.text, words.first()), &keyword, stage->index);
		}
	}

	void parseTcMod(MaterialDefinition* definition, MaterialStage* stage, int keywordIndex)
	{
		const Token& keyword = m_tokens.at(keywordIndex);
		const QStringList words = takeLineWords();
		if (words.isEmpty()) {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
				Text::tr("tcMod needs a type."), &keyword, stage->index);
			return;
		}
		if (stage->tcMods.size() >= 4) {
			// TR_MAX_TEXMODS: the engine stops with ERR_DROP.
			reject(definition, Text::tr("More than 4 tcMods in one stage; Quake III stops with an error."), &keyword, stage->index);
			return;
		}
		MaterialTexMod mod;
		mod.directive = -2;
		const QString type = words.first().toLower();
		bool ok = true;
		if (type == QStringLiteral("turb")) {
			mod.kind = MaterialTexModKind::Turbulent;
			ok = words.size() >= 5;
			mod.wave.function = MaterialWaveFunction::Sin;
			mod.wave.base = toNumber(words.value(1));
			mod.wave.amplitude = toNumber(words.value(2));
			mod.wave.phase = toNumber(words.value(3));
			mod.wave.frequency = toNumber(words.value(4));
		} else if (type == QStringLiteral("scale")) {
			mod.kind = MaterialTexModKind::Scale;
			ok = words.size() >= 3;
			mod.values[0] = toNumber(words.value(1));
			mod.values[1] = toNumber(words.value(2));
		} else if (type == QStringLiteral("scroll")) {
			mod.kind = MaterialTexModKind::Scroll;
			ok = words.size() >= 3;
			mod.values[0] = toNumber(words.value(1));
			mod.values[1] = toNumber(words.value(2));
		} else if (type == QStringLiteral("stretch")) {
			mod.kind = MaterialTexModKind::Stretch;
			ok = parseWave(words, 1, &mod.wave);
			if (ok && mod.wave.function == MaterialWaveFunction::Noise) {
				reject(definition, Text::tr("Only rgbGen wave accepts noise; Quake III stops with an error here."), &keyword, stage->index);
			}
		} else if (type == QStringLiteral("transform")) {
			mod.kind = MaterialTexModKind::Transform;
			ok = words.size() >= 7;
			for (int index = 0; index < 6; ++index) {
				mod.values[static_cast<size_t>(index)] = toNumber(words.value(index + 1));
			}
		} else if (type == QStringLiteral("rotate")) {
			mod.kind = MaterialTexModKind::Rotate;
			ok = words.size() >= 2;
			mod.values[0] = toNumber(words.value(1));
		} else if (type == QStringLiteral("entitytranslate")) {
			mod.kind = MaterialTexModKind::EntityTranslate;
		} else {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("Unknown tcMod '%1'; Quake III ignores it.").arg(words.first()), &keyword, stage->index);
			return;
		}
		if (!ok) {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
				Text::tr("tcMod %1 is missing parameters.").arg(words.first()), &keyword, stage->index);
		}
		stage->tcMods.push_back(mod);
	}

	static void finishStage(MaterialStage* stage, bool blendSeen)
	{
		// ParseStage: GL_ONE GL_ZERO means no blending at all.
		const bool replace = !blendSeen || stage->blend.isOpaqueReplace();
		if (replace) {
			stage->blend.source = MaterialBlendFactor::One;
			stage->blend.destination = MaterialBlendFactor::Zero;
		}
		// Blended stages leave the depth buffer alone unless depthWrite says
		// otherwise.
		if (!stage->depthWriteExplicit) {
			stage->depthWrite = replace;
		}
		// The default colour: identityLighting unless the source factor reads
		// the destination.
		if (!stage->rgbGen.explicitlySet) {
			const MaterialBlendFactor source = stage->blend.source;
			stage->rgbGen.source = (replace || source == MaterialBlendFactor::One || source == MaterialBlendFactor::SourceAlpha)
				? MaterialColorSource::IdentityLighting
				: MaterialColorSource::Identity;
		}
		if (!stage->alphaGen.explicitlySet && stage->alphaGen.source != MaterialColorSource::Vertex) {
			stage->alphaGen.source = MaterialColorSource::Identity;
		}
		if (stage->imageKind == MaterialImageKind::Lightmap && !stage->tcGen.explicitlySet) {
			stage->tcGen.source = MaterialTexCoordSource::Lightmap;
		}
	}

	static void finishShader(MaterialDefinition* definition)
	{
		if (definition->sort.isEmpty()) {
			// FinishShader: decal for polygonOffset, fog without stages;
			// a blended first stage sorts as seeThrough when a blended
			// stage writes depth, else blend0; otherwise opaque.
			if (definition->polygonOffset) {
				definition->sort = QStringLiteral("decal");
				definition->sortValue = 4;
			} else if (definition->stages.isEmpty()) {
				definition->sort = QStringLiteral("fog");
				definition->sortValue = 7;
			} else if (!definition->stages.first().blend.isOpaqueReplace()) {
				bool blendedDepth = false;
				for (const MaterialStage& stage : definition->stages) {
					blendedDepth |= !stage.blend.isOpaqueReplace() && stage.depthWrite;
				}
				definition->sort = blendedDepth ? QStringLiteral("seeThrough") : QStringLiteral("blend0");
				definition->sortValue = blendedDepth ? 5 : 9;
			} else {
				definition->sort = QStringLiteral("opaque");
				definition->sortValue = 3;
			}
		}
		for (const MaterialStage& stage : definition->stages) {
			if (stage.imageKind == MaterialImageKind::None) {
				// FinishShader turns such a stage off and keeps the rest.
				MaterialDiagnostic diagnostic;
				diagnostic.severity = MaterialDiagnosticSeverity::Warning;
				diagnostic.code = QStringLiteral("stage-without-image");
				diagnostic.message = Text::tr("Stage %1 has no image; Quake III skips it.").arg(stage.index + 1);
				diagnostic.material = definition->name;
				diagnostic.stage = stage.index;
				diagnostic.line = stage.span.line;
				definition->diagnostics.push_back(diagnostic);
			}
		}
	}
};

// ---------------------------------------------------------------------------
// Doom 3
// ---------------------------------------------------------------------------

const QStringList& doom3SurfaceParms()
{
	// infoParms[] in Material.cpp: content and surface flags, surface types.
	static const QStringList parms {
		QStringLiteral("solid"), QStringLiteral("water"), QStringLiteral("playerclip"), QStringLiteral("monsterclip"),
		QStringLiteral("moveableclip"), QStringLiteral("ikclip"), QStringLiteral("blood"), QStringLiteral("trigger"),
		QStringLiteral("aassolid"), QStringLiteral("aasobstacle"), QStringLiteral("flashlight_trigger"), QStringLiteral("nonsolid"),
		QStringLiteral("nullnormal"), QStringLiteral("areaportal"), QStringLiteral("qer_nocarve"), QStringLiteral("discrete"),
		QStringLiteral("nofragment"), QStringLiteral("slick"), QStringLiteral("collision"), QStringLiteral("noimpact"),
		QStringLiteral("nodamage"), QStringLiteral("ladder"), QStringLiteral("nosteps"), QStringLiteral("metal"),
		QStringLiteral("stone"), QStringLiteral("flesh"), QStringLiteral("wood"), QStringLiteral("cardboard"),
		QStringLiteral("liquid"), QStringLiteral("glass"), QStringLiteral("plastic"), QStringLiteral("ricochet"),
		QStringLiteral("surftype10"), QStringLiteral("surftype11"), QStringLiteral("surftype12"), QStringLiteral("surftype13"),
		QStringLiteral("surftype14"), QStringLiteral("surftype15"),
	};
	return parms;
}

const QStringList& doom3SurfaceTypes()
{
	static const QStringList types {QStringLiteral("metal"), QStringLiteral("stone"), QStringLiteral("flesh"), QStringLiteral("wood"),
		QStringLiteral("cardboard"), QStringLiteral("liquid"), QStringLiteral("glass"), QStringLiteral("plastic"),
		QStringLiteral("ricochet"), QStringLiteral("surftype10"), QStringLiteral("surftype11"), QStringLiteral("surftype12"),
		QStringLiteral("surftype13"), QStringLiteral("surftype14"), QStringLiteral("surftype15")};
	return types;
}

const QStringList& doom3GlobalFlags()
{
	static const QStringList flags {QStringLiteral("noshadows"), QStringLiteral("noselfshadow"), QStringLiteral("forceshadows"),
		QStringLiteral("noportalfog"), QStringLiteral("nooverlays"), QStringLiteral("forceoverlays"), QStringLiteral("translucent"),
		QStringLiteral("zeroclamp"), QStringLiteral("clamp"), QStringLiteral("alphazeroclamp"), QStringLiteral("forceopaque"),
		QStringLiteral("twosided"), QStringLiteral("backsided"), QStringLiteral("foglight"), QStringLiteral("blendlight"),
		QStringLiteral("ambientlight"), QStringLiteral("mirror"), QStringLiteral("nofog"), QStringLiteral("unsmoothedtangents"),
		QStringLiteral("suppressinsubview"), QStringLiteral("portalsky"), QStringLiteral("islightgemsurf")};
	return flags;
}

const QStringList& doom3GlobalKeywordList()
{
	static const QStringList keywords = [] {
		QStringList list {QStringLiteral("qer_editorimage"), QStringLiteral("description"), QStringLiteral("polygonoffset"),
			QStringLiteral("lightfalloffimage"), QStringLiteral("guisurf"), QStringLiteral("sort"), QStringLiteral("spectrum"),
			QStringLiteral("deform"), QStringLiteral("decalinfo"), QStringLiteral("renderbump"), QStringLiteral("diffusemap"),
			QStringLiteral("specularmap"), QStringLiteral("bumpmap"), QStringLiteral("decal_macro")};
		list += doom3GlobalFlags();
		list += doom3SurfaceParms();
		return list;
	}();
	return keywords;
}

const QStringList& doom3StageKeywordList()
{
	static const QStringList keywords {QStringLiteral("blend"), QStringLiteral("map"), QStringLiteral("remoterendermap"),
		QStringLiteral("mirrorrendermap"), QStringLiteral("xrayrendermap"), QStringLiteral("screen"), QStringLiteral("screen2"),
		QStringLiteral("glasswarp"), QStringLiteral("videomap"), QStringLiteral("soundmap"), QStringLiteral("cubemap"),
		QStringLiteral("cameracubemap"), QStringLiteral("ignorealphatest"), QStringLiteral("nearest"), QStringLiteral("linear"),
		QStringLiteral("clamp"), QStringLiteral("noclamp"), QStringLiteral("zeroclamp"), QStringLiteral("alphazeroclamp"),
		QStringLiteral("uncompressed"), QStringLiteral("highquality"), QStringLiteral("forcehighquality"), QStringLiteral("nopicmip"),
		QStringLiteral("vertexcolor"), QStringLiteral("inversevertexcolor"), QStringLiteral("privatepolygonoffset"),
		QStringLiteral("texgen"), QStringLiteral("scroll"), QStringLiteral("translate"), QStringLiteral("scale"),
		QStringLiteral("centerscale"), QStringLiteral("shear"), QStringLiteral("rotate"), QStringLiteral("maskred"),
		QStringLiteral("maskgreen"), QStringLiteral("maskblue"), QStringLiteral("maskalpha"), QStringLiteral("maskcolor"),
		QStringLiteral("maskdepth"), QStringLiteral("alphatest"), QStringLiteral("colored"), QStringLiteral("color"),
		QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("alpha"), QStringLiteral("rgb"),
		QStringLiteral("rgba"), QStringLiteral("if"), QStringLiteral("program"), QStringLiteral("fragmentprogram"),
		QStringLiteral("vertexprogram"), QStringLiteral("vertexparm"), QStringLiteral("fragmentmap"), QStringLiteral("megatexture"),
		QStringLiteral("name")};
	return keywords;
}

// Doom 3 decl types other than material and table that may appear in a decl
// file; their bodies are skipped.
const QStringList& doom3OtherDeclTypes()
{
	static const QStringList types {QStringLiteral("skin"), QStringLiteral("sound"), QStringLiteral("entitydef"),
		QStringLiteral("mapdef"), QStringLiteral("fx"), QStringLiteral("particle"), QStringLiteral("articulatedfigure"),
		QStringLiteral("pda"), QStringLiteral("video"), QStringLiteral("audio"), QStringLiteral("email"), QStringLiteral("model"),
		QStringLiteral("export")};
	return types;
}

const QStringList& doom3ImageFunctions()
{
	static const QStringList functions {QStringLiteral("heightmap"), QStringLiteral("addnormals"), QStringLiteral("smoothnormals"),
		QStringLiteral("add"), QStringLiteral("scale"), QStringLiteral("invertalpha"), QStringLiteral("invertcolor"),
		QStringLiteral("makeintensity"), QStringLiteral("makealpha")};
	return functions;
}

class Doom3Parser final : public ParserBase {
public:
	using ParserBase::ParserBase;

	void parse()
	{
		m_tokens = lexDoom3(m_text, m_lines);
		while (!atEnd()) {
			const int startIndex = m_position;
			const Token* token = take();
			if (token->isBrace(QLatin1Char('{')) || token->isBrace(QLatin1Char('}'))) {
				MaterialDiagnostic diagnostic;
				diagnostic.severity = MaterialDiagnosticSeverity::Error;
				diagnostic.code = QStringLiteral("unexpected-brace");
				diagnostic.message = Text::tr("A brace appears where a decl name was expected.");
				diagnostic.line = token->line;
				diagnostic.column = token->column;
				m_script->diagnostics.push_back(diagnostic);
				if (token->isBrace(QLatin1Char('{'))) {
					skipBracedSection(1);
				}
				continue;
			}
			const QString key = token->text.toLower();
			if (key == QStringLiteral("table")) {
				parseTable(startIndex);
			} else if (key == QStringLiteral("material")) {
				const int nameIndex = m_position;
				if (!take()) {
					break;
				}
				parseMaterial(startIndex, nameIndex);
			} else if (key == QStringLiteral("guide")) {
				parseGuide(startIndex);
			} else if (doom3OtherDeclTypes().contains(key) && peek() && !peek()->isBrace(QLatin1Char('{'))) {
				take();
				MaterialDiagnostic diagnostic;
				diagnostic.severity = MaterialDiagnosticSeverity::Info;
				diagnostic.code = QStringLiteral("other-decl");
				diagnostic.message = Text::tr("A %1 decl in a material file is skipped.").arg(token->text);
				diagnostic.line = token->line;
				diagnostic.column = token->column;
				m_script->diagnostics.push_back(diagnostic);
				if (peek() && peek()->isBrace(QLatin1Char('{'))) {
					take();
					skipBracedSection(1);
				}
			} else {
				parseMaterial(startIndex, startIndex);
			}
		}
	}

private:
	void skipBracedSection(int depth)
	{
		while (depth > 0 && !atEnd()) {
			const Token* token = take();
			if (token->isBrace(QLatin1Char('{'))) {
				++depth;
			} else if (token->isBrace(QLatin1Char('}'))) {
				--depth;
			}
		}
	}

	bool expect(MaterialDefinition* definition, const char* punctuation, const Token* near)
	{
		const Token* next = peek();
		if (next && next->type != TokenType::String && next->text == QLatin1String(punctuation)) {
			take();
			return true;
		}
		defaulted(definition, Text::tr("Expected '%1'.").arg(QLatin1String(punctuation)), next ? next : near);
		return false;
	}

	void defaulted(MaterialDefinition* definition, const QString& reason, const Token* at, int stage = -1)
	{
		if (m_lastReason.isEmpty()) {
			m_lastReason = reason;
		}
		reject(definition, Text::tr("%1 Doom 3 marks the material as defaulted.").arg(reason), at, stage);
	}

public:
	// One expression on its own, for the graph editor and the CLI.
	int parseStandaloneExpression(QVector<MaterialExpressionNode>* nodes, QString* error)
	{
		m_tokens = lexDoom3(m_text, m_lines);
		MaterialDefinition scratch;
		scratch.name = QStringLiteral("expression");
		scratch.engine = MaterialEngine::Doom3;
		if (m_tokens.isEmpty()) {
			if (error) {
				*error = Text::tr("The expression is empty.");
			}
			return -1;
		}
		const int root = parseExpression(&scratch, nullptr, -1);
		if (root < 0 || !scratch.engineRejection.isEmpty()) {
			if (error) {
				*error = m_lastReason.isEmpty() ? Text::tr("The expression could not be read.") : m_lastReason;
			}
			return -1;
		}
		if (!atEnd()) {
			if (error) {
				*error = Text::tr("Unexpected '%1' after the expression.").arg(peek()->text);
			}
			return -1;
		}
		const int offset = nodes->size();
		for (MaterialExpressionNode node : std::as_const(scratch.expressions)) {
			if (node.a >= 0) {
				node.a += offset;
			}
			if (node.b >= 0) {
				node.b += offset;
			}
			nodes->push_back(node);
		}
		return root + offset;
	}

private:
	QString m_lastReason;

	// After the first error the engine stops reading the material: skip the
	// rest of its body, braces balanced from `depth`.
	int skipRestOfBody(int depth)
	{
		int end = m_tokens.isEmpty() ? 0 : m_tokens.at(std::max(0, m_position - 1)).end;
		while (depth > 0 && !atEnd()) {
			const Token* token = take();
			end = token->end;
			if (token->isBrace(QLatin1Char('{'))) {
				++depth;
			} else if (token->isBrace(QLatin1Char('}'))) {
				--depth;
			}
		}
		return depth == 0 ? end : -1;
	}

	// A float that may be written with a leading minus (idLexer::ParseFloat).
	std::optional<double> parseFloat()
	{
		const Token* token = peek();
		if (!token) {
			return std::nullopt;
		}
		if (token->type == TokenType::Punctuation && token->text == QStringLiteral("-")) {
			const Token* number = peek(1);
			if (number && number->type == TokenType::Number) {
				m_position += 2;
				return -toNumber(number->text);
			}
			return std::nullopt;
		}
		if (token->type == TokenType::Number) {
			take();
			return toNumber(token->text);
		}
		return std::nullopt;
	}

	void parseTable(int startIndex)
	{
		const Token& keyword = m_tokens.at(startIndex);
		const Token* name = take();
		MaterialTable table;
		table.sourcePath = m_script->path;
		if (!name) {
			return;
		}
		table.name = name->text;
		const auto fail = [&](const QString& message, const Token* at) {
			MaterialDiagnostic diagnostic;
			diagnostic.severity = MaterialDiagnosticSeverity::Error;
			diagnostic.code = QStringLiteral("bad-table");
			diagnostic.message = message;
			diagnostic.line = at ? at->line : keyword.line;
			diagnostic.column = at ? at->column : keyword.column;
			m_script->diagnostics.push_back(diagnostic);
		};
		// SkipUntilString("{").
		while (!atEnd() && !peek()->isBrace(QLatin1Char('{'))) {
			take();
		}
		if (atEnd()) {
			fail(Text::tr("Table '%1' has no body.").arg(table.name), name);
			return;
		}
		take();
		bool ok = true;
		int end = name->end;
		while (!atEnd()) {
			const Token* token = take();
			end = token->end;
			if (token->isBrace(QLatin1Char('}'))) {
				break;
			}
			if (token->is("snap")) {
				table.snap = true;
			} else if (token->is("clamp")) {
				table.clamp = true;
			} else if (token->isBrace(QLatin1Char('{'))) {
				while (true) {
					const std::optional<double> value = parseFloat();
					if (!value) {
						fail(Text::tr("Table '%1' holds a value that is not a number.").arg(table.name), peek());
						ok = false;
						break;
					}
					table.values.push_back(*value);
					const Token* separator = take();
					if (!separator) {
						ok = false;
						break;
					}
					end = separator->end;
					if (separator->isBrace(QLatin1Char('}'))) {
						break;
					}
					if (separator->text == QStringLiteral(",")) {
						continue;
					}
					fail(Text::tr("Table '%1' expects a comma or a closing brace.").arg(table.name), separator);
					ok = false;
					break;
				}
				if (!ok) {
					skipBracedSection(1);
					break;
				}
			} else {
				fail(Text::tr("Unknown token '%1' in table '%2'.").arg(token->text, table.name), token);
				ok = false;
				skipBracedSection(1);
				break;
			}
		}
		table.span = spanOf(keyword.start, end);
		if (ok && table.values.isEmpty()) {
			fail(Text::tr("Table '%1' has no values.").arg(table.name), name);
		}
		if (ok) {
			m_script->tables.push_back(table);
		}
	}

	// Quake 4: `guide <name> <template>( args )`.
	void parseGuide(int startIndex)
	{
		const Token& keyword = m_tokens.at(startIndex);
		const Token* name = take();
		if (!name) {
			return;
		}
		MaterialDefinition definition;
		definition.name = name->text;
		definition.engine = MaterialEngine::Doom3;
		definition.kind = QStringLiteral("guide");
		definition.sourcePath = m_script->path;
		definition.nameSpan = spanOf(name->start, name->end);
		const Token* templateName = take();
		int end = name->end;
		if (templateName) {
			definition.guideTemplate = templateName->text;
			end = templateName->end;
			if (peek() && peek()->text == QStringLiteral("(")) {
				take();
				QString argument;
				int depth = 1;
				while (!atEnd()) {
					const Token* token = take();
					end = token->end;
					if (token->text == QStringLiteral("(")) {
						++depth;
					} else if (token->text == QStringLiteral(")")) {
						if (--depth == 0) {
							break;
						}
					} else if (token->text == QStringLiteral(",") && depth == 1) {
						definition.guideArguments << argument.trimmed();
						argument.clear();
						continue;
					}
					argument += (argument.isEmpty() ? QString() : QStringLiteral(" ")) + token->text;
				}
				if (!argument.trimmed().isEmpty()) {
					definition.guideArguments << argument.trimmed();
				}
			}
		}
		definition.span = spanOf(keyword.start, end);
		MaterialDiagnostic diagnostic;
		diagnostic.severity = MaterialDiagnosticSeverity::Info;
		diagnostic.code = QStringLiteral("guide");
		diagnostic.message = Text::tr("A Quake 4 guide instance of '%1'; the preview expands it when the guide template is loaded.")
								 .arg(definition.guideTemplate);
		diagnostic.material = definition.name;
		diagnostic.line = keyword.line;
		diagnostic.column = keyword.column;
		definition.diagnostics.push_back(diagnostic);
		m_script->materials.push_back(definition);
	}

	void parseMaterial(int startIndex, int nameIndex)
	{
		const Token& start = m_tokens.at(startIndex);
		const Token& name = m_tokens.at(nameIndex);
		MaterialDefinition definition;
		definition.name = name.text;
		definition.engine = MaterialEngine::Doom3;
		definition.kind = QStringLiteral("material");
		definition.sourcePath = m_script->path;
		definition.nameSpan = spanOf(name.start, name.end);
		const Token* open = peek();
		if (!open || !open->isBrace(QLatin1Char('{'))) {
			defaulted(&definition, Text::tr("Expected '{' after the material name."), open ? open : &name);
			definition.span = spanOf(start.start, name.end);
			m_script->materials.push_back(definition);
			return;
		}
		take();
		int end = open->end;
		bool closed = false;
		while (!atEnd()) {
			if (!definition.engineRejection.isEmpty()) {
				const int bodyEnd = skipRestOfBody(1);
				if (bodyEnd >= 0) {
					closed = true;
					end = bodyEnd;
				}
				break;
			}
			const int keywordIndex = m_position;
			const Token* token = take();
			if (token->isBrace(QLatin1Char('}'))) {
				closed = true;
				end = token->end;
				break;
			}
			if (token->isBrace(QLatin1Char('{'))) {
				parseStage(&definition, keywordIndex);
				continue;
			}
			parseGlobal(&definition, keywordIndex);
		}
		if (!closed) {
			defaulted(&definition, Text::tr("The material has no closing brace."), &name);
			end = m_tokens.isEmpty() ? name.end : m_tokens.last().end;
		}
		definition.bodySpan = spanOf(open->start, end);
		definition.span = spanOf(start.start, end);
		finishMaterial(&definition);
		m_script->materials.push_back(definition);
	}

	// R_ParsePastImageProgram: an image name or a function call.
	int parseImageProgram(MaterialDefinition* definition, const Token* near, int stage)
	{
		const Token* token = take();
		if (!token) {
			defaulted(definition, Text::tr("An image program is missing."), near, stage);
			return -1;
		}
		MaterialImageProgramNode node;
		const QString function = token->text.toLower();
		if (token->type != TokenType::String && doom3ImageFunctions().contains(function) && peek() && peek()->text == QStringLiteral("(")) {
			take();
			node.function = function;
			const int childLimit = (function == QStringLiteral("addnormals") || function == QStringLiteral("add")) ? 2 : 1;
			int numbers = 0;
			if (function == QStringLiteral("heightmap")) {
				numbers = 1;
			} else if (function == QStringLiteral("scale")) {
				numbers = 4;
			}
			for (int child = 0; child < childLimit; ++child) {
				if (child > 0 && !expect(definition, ",", token)) {
					return -1;
				}
				const int childIndex = parseImageProgram(definition, token, stage);
				if (childIndex < 0) {
					return -1;
				}
				node.children << childIndex;
			}
			for (int number = 0; number < numbers; ++number) {
				if (!expect(definition, ",", token)) {
					return -1;
				}
				const std::optional<double> value = parseFloat();
				if (!value) {
					defaulted(definition, Text::tr("%1() expects a number.").arg(token->text), peek() ? peek() : token, stage);
					return -1;
				}
				node.numbers << *value;
			}
			const Token* close = peek();
			if (!expect(definition, ")", token)) {
				return -1;
			}
			node.span = spanOf(token->start, close->end);
		} else {
			node.path = token->text;
			node.span = spanOf(token->start, token->end);
		}
		definition->imagePrograms.push_back(node);
		return definition->imagePrograms.size() - 1;
	}

	// ParseExpressionPriority / ParseTerm.
	int parseExpression(MaterialDefinition* definition, const Token* near, int stage)
	{
		return parsePriority(definition, 4, near, stage);
	}

	int parsePriority(MaterialDefinition* definition, int priority, const Token* near, int stage)
	{
		if (!definition->engineRejection.isEmpty()) {
			return -1;
		}
		if (priority == 0) {
			return parseTerm(definition, near, stage);
		}
		const int a = parsePriority(definition, priority - 1, near, stage);
		if (a < 0) {
			return -1;
		}
		// idMaterial compares the token text whatever its type: a lone `/`
		// lexes as a path name and still divides.
		const Token* token = peek();
		if (!token || token->type == TokenType::String) {
			return a;
		}
		static const QHash<QString, std::pair<int, MaterialExpressionOp>> operators {
			{QStringLiteral("*"), {1, MaterialExpressionOp::Multiply}},
			{QStringLiteral("/"), {1, MaterialExpressionOp::Divide}},
			{QStringLiteral("%"), {1, MaterialExpressionOp::Modulo}},
			{QStringLiteral("+"), {2, MaterialExpressionOp::Add}},
			{QStringLiteral("-"), {2, MaterialExpressionOp::Subtract}},
			{QStringLiteral(">"), {3, MaterialExpressionOp::Greater}},
			{QStringLiteral(">="), {3, MaterialExpressionOp::GreaterEqual}},
			{QStringLiteral("<"), {3, MaterialExpressionOp::Less}},
			{QStringLiteral("<="), {3, MaterialExpressionOp::LessEqual}},
			{QStringLiteral("=="), {3, MaterialExpressionOp::Equal}},
			{QStringLiteral("!="), {3, MaterialExpressionOp::NotEqual}},
			{QStringLiteral("&&"), {4, MaterialExpressionOp::And}},
			{QStringLiteral("||"), {4, MaterialExpressionOp::Or}},
		};
		const auto found = operators.constFind(token->text);
		if (found == operators.constEnd() || found->first != priority) {
			return a;
		}
		take();
		// The right operand is parsed at the same priority, so equal
		// priorities group to the right: a - b - c is a - (b - c).
		const int b = parsePriority(definition, priority, token, stage);
		if (b < 0) {
			return -1;
		}
		MaterialExpressionNode node;
		node.op = found->second;
		node.a = a;
		node.b = b;
		const MaterialSourceSpan left = definition->expressions.at(a).span;
		const MaterialSourceSpan right = definition->expressions.at(b).span;
		node.span = spanOf(left.start, right.end);
		definition->expressions.push_back(node);
		return definition->expressions.size() - 1;
	}

	int parseTerm(MaterialDefinition* definition, const Token* near, int stage)
	{
		const Token* token = take();
		if (!token) {
			defaulted(definition, Text::tr("An expression ends early."), near, stage);
			return -1;
		}
		MaterialExpressionNode node;
		node.span = spanOf(token->start, token->end);
		if (token->type == TokenType::Punctuation && token->text == QStringLiteral("(")) {
			const int inner = parseExpression(definition, token, stage);
			if (inner < 0) {
				return -1;
			}
			const Token* close = peek();
			if (!close || close->text != QStringLiteral(")")) {
				defaulted(definition, Text::tr("Expected ')' to close the expression."), close ? close : token, stage);
				return -1;
			}
			take();
			definition->expressions[inner].parenthesised = true;
			definition->expressions[inner].span = spanOf(token->start, close->end);
			return inner;
		}
		const QString key = token->text.toLower();
		if (token->type == TokenType::Word) {
			if (key == QStringLiteral("time")) {
				node.op = MaterialExpressionOp::Time;
			} else if (key.startsWith(QStringLiteral("parm")) && key.size() <= 6) {
				bool ok = false;
				const int index = key.mid(4).toInt(&ok);
				if (!ok || index < 0 || index > 11) {
					return tableTerm(definition, token, stage);
				}
				node.op = MaterialExpressionOp::Parm;
				node.index = index;
			} else if (key.startsWith(QStringLiteral("global")) && key.size() == 7) {
				bool ok = false;
				const int index = key.mid(6).toInt(&ok);
				if (!ok || index < 0 || index > 7) {
					return tableTerm(definition, token, stage);
				}
				node.op = MaterialExpressionOp::Global;
				node.index = index;
			} else if (key == QStringLiteral("fragmentprograms")) {
				node.op = MaterialExpressionOp::FragmentPrograms;
			} else if (key == QStringLiteral("sound")) {
				node.op = MaterialExpressionOp::Sound;
			} else {
				return tableTerm(definition, token, stage);
			}
			definition->expressions.push_back(node);
			return definition->expressions.size() - 1;
		}
		if (token->type == TokenType::Punctuation && token->text == QStringLiteral("-")) {
			// Only a number may follow a unary minus.
			const Token* number = take();
			if (!number || number->type != TokenType::Number) {
				defaulted(definition, Text::tr("Doom 3 only accepts a number after a minus sign."), number ? number : token, stage);
				return -1;
			}
			node.op = MaterialExpressionOp::Constant;
			node.value = -toNumber(number->text);
			node.span = spanOf(token->start, number->end);
			definition->expressions.push_back(node);
			return definition->expressions.size() - 1;
		}
		if (token->type == TokenType::Number) {
			node.op = MaterialExpressionOp::Constant;
			node.value = toNumber(token->text);
			definition->expressions.push_back(node);
			return definition->expressions.size() - 1;
		}
		defaulted(definition, Text::tr("Bad expression term '%1'.").arg(token->text), token, stage);
		return -1;
	}

	int tableTerm(MaterialDefinition* definition, const Token* token, int stage)
	{
		const Token* open = peek();
		if (!open || open->text != QStringLiteral("[")) {
			defaulted(definition, Text::tr("Bad expression term '%1': not a register or a table lookup.").arg(token->text), token, stage);
			return -1;
		}
		take();
		const int index = parseExpression(definition, open, stage);
		if (index < 0) {
			return -1;
		}
		const Token* close = peek();
		if (!close || close->text != QStringLiteral("]")) {
			defaulted(definition, Text::tr("Expected ']' after the table index."), close ? close : open, stage);
			return -1;
		}
		take();
		MaterialExpressionNode node;
		node.op = MaterialExpressionOp::Table;
		node.name = token->text;
		node.a = index;
		node.span = spanOf(token->start, close->end);
		definition->expressions.push_back(node);
		return definition->expressions.size() - 1;
	}

	void parseGlobal(MaterialDefinition* definition, int keywordIndex)
	{
		const Token& keyword = m_tokens.at(keywordIndex);
		const QString key = keyword.text.toLower();
		bool recognised = true;
		bool previewIgnored = false;
		if (key == QStringLiteral("qer_editorimage")) {
			previewIgnored = true;
			if (const Token* image = takeOnLine()) {
				definition->editorImage = image->text;
			}
			while (nextOnLine()) {
				take();
			}
		} else if (key == QStringLiteral("description")) {
			previewIgnored = true;
			if (const Token* text = takeOnLine()) {
				definition->description = text->text;
			}
		} else if (doom3SurfaceParms().contains(key)) {
			if (doom3SurfaceTypes().contains(key)) {
				definition->surfaceType = key;
			}
			if (!definition->surfaceParms.contains(key)) {
				definition->surfaceParms << key;
			}
			previewIgnored = true;
		} else if (key == QStringLiteral("polygonoffset")) {
			definition->polygonOffset = true;
			definition->polygonOffsetValue = 1.0;
			if (nextOnLine()) {
				if (const std::optional<double> value = parseFloat()) {
					definition->polygonOffsetValue = *value;
				} else {
					take();
				}
			}
		} else if (key == QStringLiteral("lightfalloffimage")) {
			const int program = parseImageProgram(definition, &keyword, -1);
			if (program >= 0) {
				definition->lightFalloffImage = materialImageProgramText(definition->imagePrograms, program);
			}
		} else if (key == QStringLiteral("guisurf")) {
			if (const Token* gui = takeOnLine()) {
				definition->guiSurface = gui->text;
			}
		} else if (key == QStringLiteral("sort")) {
			if (const Token* value = takeOnLine()) {
				static const QHash<QString, double> sorts {
					{QStringLiteral("subview"), -3},
					{QStringLiteral("opaque"), 0},
					{QStringLiteral("decal"), 2},
					{QStringLiteral("far"), 3},
					{QStringLiteral("medium"), 4},
					{QStringLiteral("close"), 5},
					{QStringLiteral("almostnearest"), 6},
					{QStringLiteral("nearest"), 7},
					{QStringLiteral("postprocess"), 100},
					{QStringLiteral("portalsky"), 1},
				};
				definition->sort = value->text;
				const auto named = sorts.constFind(value->text.toLower());
				definition->sortValue = named != sorts.constEnd() ? *named : toNumber(value->text);
			}
		} else if (key == QStringLiteral("spectrum")) {
			takeOnLine();
			previewIgnored = true;
		} else if (key == QStringLiteral("deform")) {
			parseDeform(definition, keyword);
		} else if (key == QStringLiteral("decalinfo")) {
			// decalInfo <stay> <fade> ( r g b a ) ( r g b a )
			QStringList words;
			for (int count = 0; count < 14 && !atEnd() && !peek()->isBrace(QLatin1Char('{')) && !peek()->isBrace(QLatin1Char('}')); ++count) {
				words << take()->text;
				if (words.size() >= 2 && words.last() == QStringLiteral(")") && words.count(QStringLiteral(")")) == 2) {
					break;
				}
			}
			definition->decalInfo = words.join(QLatin1Char(' '));
			previewIgnored = true;
		} else if (key == QStringLiteral("renderbump")) {
			while (nextOnLine()) {
				take();
			}
			previewIgnored = true;
		} else if (key == QStringLiteral("diffusemap") || key == QStringLiteral("specularmap") || key == QStringLiteral("bumpmap")) {
			MaterialStage stage;
			stage.index = definition->stages.size();
			stage.role = key == QStringLiteral("diffusemap") ? MaterialStageRole::Diffuse
				: key == QStringLiteral("specularmap")       ? MaterialStageRole::Specular
															 : MaterialStageRole::Bump;
			stage.shorthandDirective = definition->directives.size();
			const int program = parseImageProgram(definition, &keyword, stage.index);
			setStageImage(definition, &stage, program);
			const int endIndex = m_position;
			stage.span = spanOf(keyword.start, endIndex > keywordIndex + 1 ? m_tokens.at(endIndex - 1).end : keyword.end);
			definition->stages.push_back(stage);
		} else if (key == QStringLiteral("decal_macro")) {
			definition->polygonOffset = true;
			definition->polygonOffsetValue = 1.0;
			definition->sort = QStringLiteral("decal");
			definition->sortValue = 2;
			for (const QString& flag : {QStringLiteral("discrete"), QStringLiteral("noshadows")}) {
				if (!definition->flags.contains(flag)) {
					definition->flags << flag;
				}
			}
		} else if (doom3GlobalFlags().contains(key)) {
			if (!definition->flags.contains(key)) {
				definition->flags << key;
			}
			if (key == QStringLiteral("twosided")) {
				definition->cull = MaterialCull::None;
			} else if (key == QStringLiteral("backsided")) {
				definition->cull = MaterialCull::Back;
			} else if (key == QStringLiteral("translucent")) {
				definition->translucent = true;
			} else if (key == QStringLiteral("foglight")) {
				definition->fogLight = true;
			} else if (key == QStringLiteral("blendlight")) {
				definition->blendLight = true;
			} else if (key == QStringLiteral("ambientlight")) {
				definition->ambientLight = true;
			} else if (key == QStringLiteral("mirror")) {
				definition->sort = QStringLiteral("subview");
				definition->sortValue = -3;
			}
		} else {
			recognised = false;
			defaulted(definition, Text::tr("Unknown general material parameter '%1'.").arg(keyword.text), &keyword);
		}
		MaterialDirective directive = directiveFrom(keywordIndex, m_position);
		directive.recognised = recognised;
		directive.previewIgnored = previewIgnored;
		definition->directives.push_back(directive);
	}

	void parseDeform(MaterialDefinition* definition, const Token& keyword)
	{
		const Token* type = take();
		if (!type) {
			defaulted(definition, Text::tr("deform needs a type."), &keyword);
			return;
		}
		MaterialDeform deform;
		deform.directive = definition->directives.size();
		const QString key = type->text.toLower();
		if (key == QStringLiteral("sprite") || key == QStringLiteral("tube")) {
			deform.kind = key == QStringLiteral("sprite") ? MaterialDeformKind::Sprite : MaterialDeformKind::Tube;
			definition->cull = MaterialCull::None;
			definition->flags << QStringLiteral("noshadows");
		} else if (key == QStringLiteral("flare")) {
			deform.kind = MaterialDeformKind::Flare;
			definition->cull = MaterialCull::None;
			deform.expressions[0] = parseExpression(definition, type, -1);
			definition->flags << QStringLiteral("noshadows");
		} else if (key == QStringLiteral("expand")) {
			deform.kind = MaterialDeformKind::Expand;
			deform.expressions[0] = parseExpression(definition, type, -1);
		} else if (key == QStringLiteral("move")) {
			deform.kind = MaterialDeformKind::MoveExpression;
			deform.expressions[0] = parseExpression(definition, type, -1);
		} else if (key == QStringLiteral("turbulent")) {
			deform.kind = MaterialDeformKind::Turbulent;
			const Token* table = take();
			if (!table) {
				defaulted(definition, Text::tr("deform turbulent needs a table name."), type);
				return;
			}
			deform.name = table->text;
			for (int index = 0; index < 3; ++index) {
				deform.expressions[static_cast<size_t>(index)] = parseExpression(definition, table, -1);
			}
		} else if (key == QStringLiteral("eyeball")) {
			deform.kind = MaterialDeformKind::EyeBall;
		} else if (key == QStringLiteral("particle") || key == QStringLiteral("particle2")) {
			deform.kind = key == QStringLiteral("particle") ? MaterialDeformKind::Particle : MaterialDeformKind::Particle2;
			const Token* decl = take();
			if (!decl) {
				defaulted(definition, Text::tr("deform %1 needs a particle decl.").arg(type->text), type);
				return;
			}
			deform.name = decl->text;
		} else {
			defaulted(definition, Text::tr("Bad deform type '%1'.").arg(type->text), type);
			return;
		}
		definition->deforms.push_back(deform);
	}

	void setStageImage(MaterialDefinition* definition, MaterialStage* stage, int program)
	{
		if (program < 0) {
			return;
		}
		const MaterialImageProgramNode& node = definition->imagePrograms.at(program);
		stage->imageProgram = program;
		if (!node.function.isEmpty()) {
			stage->imageKind = MaterialImageKind::Program;
			return;
		}
		const QString key = node.path.toLower();
		if (key == QStringLiteral("_white")) {
			stage->imageKind = MaterialImageKind::White;
		} else if (key == QStringLiteral("_currentrender") || key == QStringLiteral("_scratch")) {
			stage->imageKind = MaterialImageKind::RenderTarget;
			stage->imagePath = node.path;
		} else if (key.startsWith(QLatin1Char('_'))) {
			stage->imageKind = MaterialImageKind::Builtin;
			stage->imagePath = node.path;
		} else {
			stage->imageKind = MaterialImageKind::File;
			stage->imagePath = node.path;
		}
	}

	void parseStage(MaterialDefinition* definition, int openIndex)
	{
		const Token& open = m_tokens.at(openIndex);
		MaterialStage stage;
		stage.index = definition->stages.size();
		int end = open.end;
		bool closed = false;
		while (!atEnd()) {
			if (!definition->engineRejection.isEmpty()) {
				// A defaulted material stops parsing; skip to this stage's end.
				int depth = 1;
				while (depth > 0 && !atEnd()) {
					const Token* token = take();
					end = token->end;
					if (token->isBrace(QLatin1Char('{'))) {
						++depth;
					} else if (token->isBrace(QLatin1Char('}'))) {
						--depth;
					}
				}
				closed = depth == 0;
				break;
			}
			const int keywordIndex = m_position;
			const Token* token = take();
			if (token->isBrace(QLatin1Char('}'))) {
				closed = true;
				end = token->end;
				break;
			}
			parseStageKeyword(definition, &stage, keywordIndex);
		}
		if (!closed) {
			defaulted(definition, Text::tr("A stage has no closing brace."), &open, stage.index);
			end = m_tokens.isEmpty() ? open.end : m_tokens.last().end;
		}
		stage.span = spanOf(open.start, end);
		definition->stages.push_back(stage);
	}

	void parseTwoExpressions(MaterialDefinition* definition, MaterialStage* stage, const Token& keyword, MaterialTexModKind kind)
	{
		MaterialTexMod mod;
		mod.kind = kind;
		mod.expressions[0] = parseExpression(definition, &keyword, stage->index);
		if (mod.expressions[0] < 0 || !expect(definition, ",", &keyword)) {
			return;
		}
		mod.expressions[1] = parseExpression(definition, &keyword, stage->index);
		if (mod.expressions[1] < 0) {
			return;
		}
		mod.directive = stage->directives.size();
		stage->tcMods.push_back(mod);
	}

	void parseStageKeyword(MaterialDefinition* definition, MaterialStage* stage, int keywordIndex)
	{
		const Token& keyword = m_tokens.at(keywordIndex);
		const QString key = keyword.text.toLower();
		const int stageIndex = stage->index;
		bool recognised = true;
		bool previewIgnored = false;
		if (key == QStringLiteral("name")) {
			while (nextOnLine()) {
				take();
			}
			previewIgnored = true;
		} else if (key == QStringLiteral("blend")) {
			parseBlend(definition, stage, keyword);
		} else if (key == QStringLiteral("map")) {
			setStageImage(definition, stage, parseImageProgram(definition, &keyword, stageIndex));
		} else if (key == QStringLiteral("cubemap") || key == QStringLiteral("cameracubemap")) {
			const int program = parseImageProgram(definition, &keyword, stageIndex);
			if (program >= 0) {
				stage->imageProgram = program;
				stage->imageKind = MaterialImageKind::CubeMap;
				stage->imagePath = materialImageProgramText(definition->imagePrograms, program);
				stage->cameraCubeMap = key == QStringLiteral("cameracubemap");
			}
		} else if (key == QStringLiteral("remoterendermap") || key == QStringLiteral("mirrorrendermap") || key == QStringLiteral("xrayrendermap")) {
			parseFloat();
			parseFloat();
			stage->imageKind = MaterialImageKind::RenderTarget;
			stage->imagePath = key;
			if (key != QStringLiteral("remoterendermap")) {
				stage->tcGen.source = MaterialTexCoordSource::Screen;
			}
		} else if (key == QStringLiteral("screen")) {
			stage->tcGen.source = MaterialTexCoordSource::Screen;
			stage->tcGen.explicitlySet = true;
		} else if (key == QStringLiteral("screen2")) {
			stage->tcGen.source = MaterialTexCoordSource::Screen2;
			stage->tcGen.explicitlySet = true;
		} else if (key == QStringLiteral("glasswarp")) {
			stage->tcGen.source = MaterialTexCoordSource::GlassWarp;
			stage->tcGen.explicitlySet = true;
		} else if (key == QStringLiteral("videomap")) {
			const Token* file = take();
			if (file && file->is("loop")) {
				stage->videoLoop = true;
				file = take();
			}
			if (!file) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("videoMap needs a file name."), &keyword, stageIndex);
			} else {
				stage->imageKind = MaterialImageKind::Video;
				stage->imagePath = file->text;
				stage->clamp = true;
			}
		} else if (key == QStringLiteral("soundmap")) {
			const Token* mode = take();
			stage->imageKind = MaterialImageKind::SoundMap;
			stage->imagePath = mode ? mode->text : QString();
		} else if (key == QStringLiteral("ignorealphatest")) {
			stage->ignoreAlphaTest = true;
		} else if (key == QStringLiteral("nearest")) {
			stage->nearest = true;
		} else if (key == QStringLiteral("linear")) {
			stage->nearest = false;
		} else if (key == QStringLiteral("clamp")) {
			stage->clamp = true;
		} else if (key == QStringLiteral("noclamp")) {
			stage->clamp = false;
			stage->zeroClamp = false;
			stage->alphaZeroClamp = false;
		} else if (key == QStringLiteral("zeroclamp")) {
			stage->zeroClamp = true;
		} else if (key == QStringLiteral("alphazeroclamp")) {
			stage->alphaZeroClamp = true;
		} else if (key == QStringLiteral("uncompressed") || key == QStringLiteral("highquality") || key == QStringLiteral("forcehighquality")
			|| key == QStringLiteral("nopicmip")) {
			previewIgnored = true;
		} else if (key == QStringLiteral("vertexcolor")) {
			stage->vertexColor = MaterialVertexColor::Vertex;
		} else if (key == QStringLiteral("inversevertexcolor")) {
			stage->vertexColor = MaterialVertexColor::InverseVertex;
		} else if (key == QStringLiteral("privatepolygonoffset")) {
			stage->privatePolygonOffset = 1.0;
			if (nextOnLine()) {
				if (const std::optional<double> value = parseFloat()) {
					stage->privatePolygonOffset = *value;
				}
			}
		} else if (key == QStringLiteral("texgen")) {
			const Token* source = take();
			const QString value = source ? source->text.toLower() : QString();
			stage->tcGen.explicitlySet = true;
			if (value == QStringLiteral("normal")) {
				stage->tcGen.source = MaterialTexCoordSource::Normal;
			} else if (value == QStringLiteral("reflect")) {
				stage->tcGen.source = MaterialTexCoordSource::Reflect;
			} else if (value == QStringLiteral("skybox")) {
				stage->tcGen.source = MaterialTexCoordSource::Skybox;
			} else if (value == QStringLiteral("wobblesky")) {
				stage->tcGen.source = MaterialTexCoordSource::WobbleSky;
				for (int index = 0; index < 3; ++index) {
					stage->tcGen.expressions[static_cast<size_t>(index)] = parseExpression(definition, source, stageIndex);
				}
			} else {
				// screen, screen2 and glassWarp are stage keywords of their own.
				defaulted(definition, Text::tr("Bad texGen '%1'; use normal, reflect, skybox or wobbleSky.").arg(source ? source->text : QString()),
					source ? source : &keyword, stageIndex);
			}
		} else if (key == QStringLiteral("scroll") || key == QStringLiteral("translate")) {
			parseTwoExpressions(definition, stage, keyword, MaterialTexModKind::Translate);
		} else if (key == QStringLiteral("scale")) {
			parseTwoExpressions(definition, stage, keyword, MaterialTexModKind::ScaleExpression);
		} else if (key == QStringLiteral("centerscale")) {
			parseTwoExpressions(definition, stage, keyword, MaterialTexModKind::CenterScale);
		} else if (key == QStringLiteral("shear")) {
			parseTwoExpressions(definition, stage, keyword, MaterialTexModKind::Shear);
		} else if (key == QStringLiteral("rotate")) {
			MaterialTexMod mod;
			mod.kind = MaterialTexModKind::RotateExpression;
			mod.expressions[0] = parseExpression(definition, &keyword, stageIndex);
			mod.directive = stage->directives.size();
			if (mod.expressions[0] >= 0) {
				stage->tcMods.push_back(mod);
			}
		} else if (key == QStringLiteral("maskred")) {
			stage->maskRed = true;
		} else if (key == QStringLiteral("maskgreen")) {
			stage->maskGreen = true;
		} else if (key == QStringLiteral("maskblue")) {
			stage->maskBlue = true;
		} else if (key == QStringLiteral("maskalpha")) {
			stage->maskAlpha = true;
		} else if (key == QStringLiteral("maskcolor")) {
			stage->maskRed = stage->maskGreen = stage->maskBlue = true;
		} else if (key == QStringLiteral("maskdepth")) {
			stage->maskDepth = true;
		} else if (key == QStringLiteral("alphatest")) {
			stage->alphaTest = MaterialAlphaTest::Expression;
			stage->alphaTestExpression = parseExpression(definition, &keyword, stageIndex);
		} else if (key == QStringLiteral("colored")) {
			for (int channel = 0; channel < 4; ++channel) {
				MaterialExpressionNode node;
				node.op = MaterialExpressionOp::Parm;
				node.index = channel;
				node.span = spanOf(keyword.start, keyword.end);
				definition->expressions.push_back(node);
				stage->colorExpressions[static_cast<size_t>(channel)] = definition->expressions.size() - 1;
			}
		} else if (key == QStringLiteral("color")) {
			for (int channel = 0; channel < 4; ++channel) {
				if (channel > 0 && !expect(definition, ",", &keyword)) {
					break;
				}
				stage->colorExpressions[static_cast<size_t>(channel)] = parseExpression(definition, &keyword, stageIndex);
			}
		} else if (key == QStringLiteral("red") || key == QStringLiteral("green") || key == QStringLiteral("blue") || key == QStringLiteral("alpha")) {
			const int channel = key == QStringLiteral("red") ? 0 : key == QStringLiteral("green") ? 1 : key == QStringLiteral("blue") ? 2 : 3;
			stage->colorExpressions[static_cast<size_t>(channel)] = parseExpression(definition, &keyword, stageIndex);
		} else if (key == QStringLiteral("rgb")) {
			const int expression = parseExpression(definition, &keyword, stageIndex);
			stage->colorExpressions[0] = stage->colorExpressions[1] = stage->colorExpressions[2] = expression;
		} else if (key == QStringLiteral("rgba")) {
			const int expression = parseExpression(definition, &keyword, stageIndex);
			stage->colorExpressions = {expression, expression, expression, expression};
		} else if (key == QStringLiteral("if")) {
			stage->conditionExpression = parseExpression(definition, &keyword, stageIndex);
		} else if (key == QStringLiteral("program") || key == QStringLiteral("fragmentprogram") || key == QStringLiteral("vertexprogram")
			|| key == QStringLiteral("megatexture")) {
			if (const Token* name = takeOnLine()) {
				if (key != QStringLiteral("fragmentprogram")) {
					stage->vertexProgram = name->text;
				}
				if (key != QStringLiteral("vertexprogram")) {
					stage->fragmentProgram = name->text;
				}
			}
			previewIgnored = true;
			diagnose(definition, MaterialDiagnosticSeverity::Info, QStringLiteral("not-simulated"),
				Text::tr("ARB programs run on the graphics card in game; the preview draws the stage's image without them."), &keyword, stageIndex);
		} else if (key == QStringLiteral("vertexparm")) {
			parseFloat();
			parseExpression(definition, &keyword, stageIndex);
			for (int extra = 0; extra < 3 && peek() && peek()->text == QStringLiteral(","); ++extra) {
				take();
				parseExpression(definition, &keyword, stageIndex);
			}
			previewIgnored = true;
		} else if (key == QStringLiteral("fragmentmap")) {
			parseFloat();
			while (peek() && peek()->type == TokenType::Word
				&& QStringList {QStringLiteral("cubemap"), QStringLiteral("cameracubemap"), QStringLiteral("nearest"), QStringLiteral("linear"),
					   QStringLiteral("clamp"), QStringLiteral("noclamp"), QStringLiteral("zeroclamp"), QStringLiteral("alphazeroclamp"),
					   QStringLiteral("forcehighquality"), QStringLiteral("uncompressed"), QStringLiteral("highquality"),
					   QStringLiteral("nopicmip")}
					   .contains(peek()->text.toLower())) {
				take();
			}
			parseImageProgram(definition, &keyword, stageIndex);
			previewIgnored = true;
		} else {
			recognised = false;
			defaulted(definition, Text::tr("Unknown stage keyword '%1'.").arg(keyword.text), &keyword, stageIndex);
		}
		MaterialDirective directive = directiveFrom(keywordIndex, m_position);
		directive.recognised = recognised;
		directive.previewIgnored = previewIgnored;
		stage->directives.push_back(directive);
	}

	void parseBlend(MaterialDefinition* definition, MaterialStage* stage, const Token& keyword)
	{
		const Token* first = take();
		if (!first) {
			defaulted(definition, Text::tr("blend needs a mode."), &keyword, stage->index);
			return;
		}
		stage->blend.explicitBlend = true;
		stage->blend.written = first->text;
		const QString key = first->text.toLower();
		if (key == QStringLiteral("blend")) {
			stage->blend.source = MaterialBlendFactor::SourceAlpha;
			stage->blend.destination = MaterialBlendFactor::OneMinusSourceAlpha;
		} else if (key == QStringLiteral("add")) {
			stage->blend.source = MaterialBlendFactor::One;
			stage->blend.destination = MaterialBlendFactor::One;
		} else if (key == QStringLiteral("filter") || key == QStringLiteral("modulate")) {
			stage->blend.source = MaterialBlendFactor::DestinationColor;
			stage->blend.destination = MaterialBlendFactor::Zero;
		} else if (key == QStringLiteral("none")) {
			stage->blend.source = MaterialBlendFactor::Zero;
			stage->blend.destination = MaterialBlendFactor::One;
		} else if (key == QStringLiteral("bumpmap")) {
			stage->role = MaterialStageRole::Bump;
		} else if (key == QStringLiteral("diffusemap")) {
			stage->role = MaterialStageRole::Diffuse;
		} else if (key == QStringLiteral("specularmap")) {
			stage->role = MaterialStageRole::Specular;
		} else {
			MaterialBlendFactor source = MaterialBlendFactor::One;
			if (!materialBlendFactorFromId(first->text, &source)) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
					Text::tr("Unknown blend factor '%1'; Doom 3 uses GL_ONE.").arg(first->text), first, stage->index);
			}
			if (!expect(definition, ",", first)) {
				return;
			}
			const Token* second = take();
			MaterialBlendFactor destination = MaterialBlendFactor::Zero;
			if (!second || !materialBlendFactorFromId(second->text, &destination)) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
					Text::tr("Unknown blend factor '%1'; Doom 3 uses GL_ONE.").arg(second ? second->text : QString()), second ? second : first,
					stage->index);
				destination = MaterialBlendFactor::One;
			}
			stage->blend.source = source;
			stage->blend.destination = destination;
			stage->blend.written = first->text + QStringLiteral(", ") + (second ? second->text : QString());
		}
	}

	static void note(MaterialDefinition* definition, const QString& code, const QString& message)
	{
		MaterialDiagnostic diagnostic;
		diagnostic.severity = MaterialDiagnosticSeverity::Info;
		diagnostic.code = code;
		diagnostic.message = message;
		diagnostic.material = definition->name;
		diagnostic.line = definition->span.line;
		definition->diagnostics.push_back(diagnostic);
	}

	// What idMaterial::ParseMaterial settles after the body: implicit
	// interaction images, coverage and the default sort.
	void finishMaterial(MaterialDefinition* definition)
	{
		bool bump = false;
		bool diffuse = false;
		bool specular = false;
		bool reflect = false;
		bool currentRender = false;
		for (const MaterialStage& stage : definition->stages) {
			bump |= stage.role == MaterialStageRole::Bump;
			diffuse |= stage.role == MaterialStageRole::Diffuse;
			specular |= stage.role == MaterialStageRole::Specular;
			reflect |= stage.tcGen.source == MaterialTexCoordSource::Reflect;
			currentRender |= stage.imagePath.compare(QStringLiteral("_currentRender"), Qt::CaseInsensitive) == 0;
			if (stage.imageKind == MaterialImageKind::None && definition->engineRejection.isEmpty()) {
				MaterialDiagnostic diagnostic;
				diagnostic.severity = MaterialDiagnosticSeverity::Warning;
				diagnostic.code = QStringLiteral("stage-without-image");
				diagnostic.message = Text::tr("Stage %1 has no image; Doom 3 draws _default there.").arg(stage.index + 1);
				diagnostic.material = definition->name;
				diagnostic.stage = stage.index;
				diagnostic.line = stage.span.line;
				definition->diagnostics.push_back(diagnostic);
			}
		}
		if (!definition->engineRejection.isEmpty()) {
			return;
		}
		if ((diffuse || specular) && !bump) {
			note(definition, QStringLiteral("implicit-bump"), Text::tr("No bump map: Doom 3 lights this material with _flat."));
		}
		if (bump && !diffuse && !specular && !reflect) {
			note(definition, QStringLiteral("implicit-diffuse"), Text::tr("No diffuse map: Doom 3 lights this material with _white."));
		}
		// Coverage: the last of translucent, forceOpaque, mirror and
		// alphaTest wins; otherwise it follows the stages.
		QString coverage;
		for (const MaterialDirective& directive : definition->directives) {
			const QString key = directive.keyword.toLower();
			if (key == QStringLiteral("translucent")) {
				coverage = QStringLiteral("translucent");
			} else if (key == QStringLiteral("forceopaque") || key == QStringLiteral("mirror")) {
				coverage = QStringLiteral("opaque");
			}
		}
		for (const MaterialStage& stage : definition->stages) {
			if (stage.alphaTest == MaterialAlphaTest::Expression) {
				coverage = QStringLiteral("perforated");
			}
		}
		if (coverage.isEmpty()) {
			if (definition->stages.isEmpty()) {
				coverage = QStringLiteral("translucent");
			} else if (bump || diffuse || specular) {
				coverage = QStringLiteral("opaque");
			} else {
				const MaterialBlend& first = definition->stages.first().blend;
				const bool readsDestination = first.source == MaterialBlendFactor::DestinationColor
					|| first.source == MaterialBlendFactor::OneMinusDestinationColor || first.source == MaterialBlendFactor::DestinationAlpha
					|| first.source == MaterialBlendFactor::OneMinusDestinationAlpha;
				coverage = (first.destination != MaterialBlendFactor::Zero || readsDestination) ? QStringLiteral("translucent")
																								 : QStringLiteral("opaque");
			}
		}
		if (currentRender && !definition->hasFlag(QStringLiteral("portalsky"))) {
			coverage = QStringLiteral("translucent");
			if (definition->sort.isEmpty()) {
				definition->sort = QStringLiteral("postProcess");
				definition->sortValue = 100;
			}
		}
		definition->coverage = coverage;
		definition->translucent = coverage == QStringLiteral("translucent");
		if (definition->translucent && !definition->flags.contains(QStringLiteral("noshadows"))) {
			definition->flags << QStringLiteral("noshadows");
		}
		if (definition->sort.isEmpty()) {
			if (definition->polygonOffset) {
				definition->sort = QStringLiteral("decal");
				definition->sortValue = 2;
			} else if (definition->translucent) {
				definition->sort = QStringLiteral("medium");
				definition->sortValue = 4;
			} else {
				definition->sort = QStringLiteral("opaque");
				definition->sortValue = 0;
			}
		}
	}
};

// ---------------------------------------------------------------------------
// Expression printing
// ---------------------------------------------------------------------------

QString expressionText(const QVector<MaterialExpressionNode>& nodes, int root, bool forceParentheses)
{
	if (root < 0 || root >= nodes.size()) {
		return QStringLiteral("0");
	}
	const MaterialExpressionNode& node = nodes.at(root);
	QString text;
	switch (node.op) {
	case MaterialExpressionOp::Constant:
		text = numberText(node.value);
		break;
	case MaterialExpressionOp::Time:
		text = QStringLiteral("time");
		break;
	case MaterialExpressionOp::Parm:
		text = QStringLiteral("parm%1").arg(node.index);
		break;
	case MaterialExpressionOp::Global:
		text = QStringLiteral("global%1").arg(node.index);
		break;
	case MaterialExpressionOp::Sound:
		text = QStringLiteral("sound");
		break;
	case MaterialExpressionOp::FragmentPrograms:
		text = QStringLiteral("fragmentPrograms");
		break;
	case MaterialExpressionOp::Table:
		text = QStringLiteral("%1[ %2 ]").arg(node.name, expressionText(nodes, node.a, false));
		break;
	case MaterialExpressionOp::Negate: {
		const MaterialExpressionNode* operand = node.a >= 0 && node.a < nodes.size() ? &nodes.at(node.a) : nullptr;
		if (operand && operand->op == MaterialExpressionOp::Constant) {
			text = numberText(-operand->value);
		} else {
			text = QStringLiteral("( 0 - %1 )").arg(expressionText(nodes, node.a, true));
		}
		break;
	}
	default: {
		const int priority = materialExpressionOpPriority(node.op);
		const auto childNeedsParentheses = [&](int child, bool left) {
			if (child < 0 || child >= nodes.size()) {
				return false;
			}
			const MaterialExpressionNode& operand = nodes.at(child);
			if (operand.parenthesised) {
				return true;
			}
			const int childPriority = materialExpressionOpPriority(operand.op);
			if (childPriority == 0) {
				return false;
			}
			// Equal priorities group to the right, so only a left operand of
			// equal priority needs parentheses.
			return left ? childPriority >= priority : childPriority > priority;
		};
		const QString left = expressionText(nodes, node.a, childNeedsParentheses(node.a, true));
		const QString right = expressionText(nodes, node.b, childNeedsParentheses(node.b, false));
		text = QStringLiteral("%1 %2 %3").arg(left, materialExpressionOpToken(node.op), right);
		break;
	}
	}
	if (forceParentheses && materialExpressionOpPriority(node.op) > 0) {
		return QStringLiteral("( %1 )").arg(text);
	}
	return text;
}

// ---------------------------------------------------------------------------
// Editing helpers
// ---------------------------------------------------------------------------

int lineStartOf(const QString& text, int offset)
{
	const int newline = offset > 0 ? text.lastIndexOf(QLatin1Char('\n'), offset - 1) : -1;
	return newline + 1;
}

int lineEndOf(const QString& text, int offset)
{
	const int newline = text.indexOf(QLatin1Char('\n'), offset);
	return newline < 0 ? text.size() : newline;
}

QString indentationOf(const QString& text, int offset)
{
	const int start = lineStartOf(text, offset);
	int end = start;
	while (end < text.size() && (text.at(end) == QLatin1Char(' ') || text.at(end) == QLatin1Char('\t'))) {
		++end;
	}
	return text.mid(start, end - start);
}

bool onlyWhitespace(const QString& text, int from, int to)
{
	for (int index = from; index < to; ++index) {
		if (!text.at(index).isSpace()) {
			return false;
		}
	}
	return true;
}

bool whitespaceOrComment(const QString& text, int from, int to)
{
	int index = from;
	while (index < to && text.at(index).isSpace()) {
		++index;
	}
	return index >= to || text.mid(index, 2) == QStringLiteral("//");
}

QString newlineFor(const QString& text)
{
	return text.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
}

// The text range that removing [start, end) should take with it: whole
// lines when nothing else shares them.
std::pair<int, int> removalRange(const QString& text, int start, int end)
{
	const int lineStart = lineStartOf(text, start);
	int lineEnd = lineEndOf(text, end);
	if (onlyWhitespace(text, lineStart, start) && whitespaceOrComment(text, end, lineEnd)) {
		if (lineEnd < text.size()) {
			++lineEnd;
		}
		return {lineStart, lineEnd};
	}
	int trailing = end;
	while (trailing < text.size() && (text.at(trailing) == QLatin1Char(' ') || text.at(trailing) == QLatin1Char('\t'))) {
		++trailing;
	}
	return {start, trailing};
}

QString unitIndent(const QString& text)
{
	return text.contains(QStringLiteral("\n\t")) || !text.contains(QStringLiteral("\n    ")) ? QStringLiteral("\t") : QStringLiteral("    ");
}

struct Block {
	const QVector<MaterialDirective>* directives = nullptr;
	MaterialSourceSpan span; // braces inclusive
	bool shorthand = false;
};

bool blockFor(const MaterialDefinition& definition, int stage, Block* block, QString* error)
{
	if (stage < 0) {
		block->directives = &definition.directives;
		block->span = definition.bodySpan;
		return block->span.isValid();
	}
	if (stage >= definition.stages.size()) {
		*error = Text::tr("Stage %1 does not exist.").arg(stage + 1);
		return false;
	}
	const MaterialStage& target = definition.stages.at(stage);
	if (target.shorthandDirective >= 0) {
		*error = Text::tr("Stage %1 is a %2 shorthand; edit its global directive or expand it into a stage block.")
					 .arg(stage + 1)
					 .arg(definition.directives.value(target.shorthandDirective).keyword);
		return false;
	}
	block->directives = &target.directives;
	block->span = target.span;
	return block->span.isValid();
}

int findDirective(const QVector<MaterialDirective>& directives, const MaterialEdit& edit)
{
	if (edit.directive >= 0) {
		return edit.directive < directives.size() ? edit.directive : -1;
	}
	for (int index = 0; index < directives.size(); ++index) {
		if (directives.at(index).keyword.compare(edit.keyword, Qt::CaseInsensitive) == 0) {
			return index;
		}
	}
	return -1;
}

QString directiveText(const QString& keyword, const QString& arguments)
{
	const QString trimmed = arguments.trimmed();
	return trimmed.isEmpty() ? keyword : keyword + QLatin1Char(' ') + trimmed;
}

struct Patch {
	int start = 0;
	int end = 0;
	QString replacement;
};

QString applyPatch(const QString& text, const Patch& patch)
{
	QString result = text;
	result.replace(patch.start, patch.end - patch.start, patch.replacement);
	return result;
}

// Inserts a directive line into a block before directive `position` or, for
// -1, after the last one.
Patch insertionPatch(const QString& text, const Block& block, int position, const QString& line)
{
	const QString newline = newlineFor(text);
	const QVector<MaterialDirective>& directives = *block.directives;
	QString indent;
	if (!directives.isEmpty()) {
		indent = indentationOf(text, directives.first().span.start);
		if (lineStartOf(text, directives.first().span.start) == lineStartOf(text, block.span.start)) {
			indent = indentationOf(text, block.span.start) + unitIndent(text);
		}
	} else {
		indent = indentationOf(text, block.span.start) + unitIndent(text);
	}
	if (position >= 0 && position < directives.size()) {
		const int anchor = directives.at(position).span.start;
		const int lineStart = lineStartOf(text, anchor);
		if (onlyWhitespace(text, lineStart, anchor)) {
			return {lineStart, lineStart, indent + line + newline};
		}
		return {anchor, anchor, line + newline + indent};
	}
	// After the block's last content, before the closing brace.
	const int close = block.span.end - 1;
	const int closeLineStart = lineStartOf(text, close);
	if (onlyWhitespace(text, closeLineStart, close)) {
		return {closeLineStart, closeLineStart, indent + line + newline};
	}
	return {close, close, newline + indent + line + newline + indentationOf(text, block.span.start)};
}

MaterialEditResult finish(const MaterialScript& script, const QString& text, int changeStart, int changeEnd)
{
	MaterialEditResult result;
	result.text = text;
	result.script = parseMaterialScript(text, script.engine, script.path);
	result.ok = true;
	result.changeStart = changeStart;
	result.changeEnd = changeEnd;
	return result;
}

MaterialEditResult failure(const QString& message)
{
	MaterialEditResult result;
	result.ok = false;
	result.error = message;
	return result;
}

QString stageBlockText(const QString& text, const QString& indent, const QString& body)
{
	const QString newline = newlineFor(text);
	QString block = indent + QStringLiteral("{") + newline;
	const QString inner = indent + unitIndent(text);
	for (const QString& line : body.split(QLatin1Char('\n'))) {
		const QString trimmed = line.trimmed();
		if (!trimmed.isEmpty()) {
			block += inner + trimmed + newline;
		}
	}
	block += indent + QStringLiteral("}") + newline;
	return block;
}

QString stageIndent(const QString& text, const MaterialDefinition& definition)
{
	for (const MaterialStage& stage : definition.stages) {
		if (stage.shorthandDirective < 0 && stage.span.isValid()) {
			return indentationOf(text, stage.span.start);
		}
	}
	return indentationOf(text, definition.bodySpan.start) + unitIndent(text);
}

// A blocks's full-line text range [start, end) when it owns its lines.
std::pair<int, int> stageLines(const QString& text, const MaterialStage& stage)
{
	return removalRange(text, stage.span.start, stage.span.end);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

MaterialEngine materialEngineForScriptPath(const QString& path)
{
	const QString suffix = QFileInfo(path).suffix().toLower();
	if (suffix == QStringLiteral("shader")) {
		return MaterialEngine::Quake3;
	}
	if (suffix == QStringLiteral("mtr")) {
		return MaterialEngine::Doom3;
	}
	return MaterialEngine::Unknown;
}

bool isMaterialScriptPath(const QString& path)
{
	return materialEngineForScriptPath(path) != MaterialEngine::Unknown;
}

MaterialScript parseMaterialScript(const QString& text, MaterialEngine engine, const QString& path)
{
	MaterialScript script;
	script.path = path;
	script.text = text;
	script.engine = engine == MaterialEngine::Unknown ? materialEngineForScriptPath(path) : engine;
	if (script.engine == MaterialEngine::Unknown) {
		script.engine = MaterialEngine::Quake3;
	}
	if (script.engine == MaterialEngine::Doom3) {
		Doom3Parser parser(script.text, &script);
		parser.parse();
	} else {
		Quake3Parser parser(script.text, &script);
		parser.parse();
	}
	// Same-name definitions inside one script: the first one is reported as
	// the one tools find; the library resolves overrides across scripts.
	QHash<QString, int> seen;
	for (int index = 0; index < script.materials.size(); ++index) {
		MaterialDefinition& definition = script.materials[index];
		const QString key = materialLookupKey(definition.name);
		const auto first = seen.constFind(key);
		if (first != seen.constEnd()) {
			MaterialDiagnostic diagnostic;
			diagnostic.severity = MaterialDiagnosticSeverity::Warning;
			diagnostic.code = QStringLiteral("duplicate-definition");
			diagnostic.message = Text::tr("'%1' is defined again; line %2 has the earlier definition.")
									 .arg(definition.name)
									 .arg(script.materials.at(*first).span.line);
			diagnostic.material = definition.name;
			diagnostic.line = definition.span.line;
			definition.diagnostics.push_back(diagnostic);
		} else {
			seen.insert(key, index);
		}
	}
	return script;
}

bool loadMaterialScript(const QString& path, MaterialScript* script, QString* error, MaterialEngine engine)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = file.errorString();
		}
		return false;
	}
	constexpr qint64 limit = 32 * 1024 * 1024;
	if (file.size() > limit) {
		if (error) {
			*error = Text::tr("The script is larger than 32 MiB.");
		}
		return false;
	}
	const QByteArray bytes = file.readAll();
	QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
	QString text = decoder(bytes);
	if (decoder.hasError()) {
		text = QString::fromLatin1(bytes);
	}
	*script = parseMaterialScript(text, engine, path);
	return true;
}

int parseMaterialExpression(const QString& text, QVector<MaterialExpressionNode>* nodes, QString* error)
{
	MaterialScript scratch;
	scratch.engine = MaterialEngine::Doom3;
	scratch.text = text;
	Doom3Parser parser(scratch.text, &scratch);
	return parser.parseStandaloneExpression(nodes, error);
}

QString materialExpressionText(const QVector<MaterialExpressionNode>& nodes, int root)
{
	if (root >= 0 && root < nodes.size() && nodes.at(root).parenthesised && materialExpressionOpPriority(nodes.at(root).op) > 0) {
		return expressionText(nodes, root, true);
	}
	return expressionText(nodes, root, false);
}

QString materialImageProgramText(const QVector<MaterialImageProgramNode>& nodes, int root)
{
	if (root < 0 || root >= nodes.size()) {
		return QString();
	}
	const MaterialImageProgramNode& node = nodes.at(root);
	if (node.function.isEmpty()) {
		return node.path;
	}
	QStringList arguments;
	for (int child : node.children) {
		arguments << materialImageProgramText(nodes, child);
	}
	for (double number : node.numbers) {
		arguments << numberText(number);
	}
	return QStringLiteral("%1( %2 )").arg(node.function, arguments.join(QStringLiteral(", ")));
}

QStringList materialImageProgramImages(const QVector<MaterialImageProgramNode>& nodes, int root)
{
	QStringList images;
	if (root < 0 || root >= nodes.size()) {
		return images;
	}
	const MaterialImageProgramNode& node = nodes.at(root);
	if (node.function.isEmpty()) {
		images << node.path;
		return images;
	}
	for (int child : node.children) {
		images += materialImageProgramImages(nodes, child);
	}
	return images;
}

QStringList materialGlobalKeywords(MaterialEngine engine)
{
	if (engine == MaterialEngine::Doom3) {
		return doom3GlobalKeywordList();
	}
	if (engine == MaterialEngine::Quake3) {
		return quake3GlobalKeywordList();
	}
	return {};
}

QStringList materialStageKeywords(MaterialEngine engine)
{
	if (engine == MaterialEngine::Doom3) {
		return doom3StageKeywordList();
	}
	if (engine == MaterialEngine::Quake3) {
		return quake3StageKeywordList();
	}
	return {};
}

namespace {

struct KeywordHelp {
	MaterialEngine engine;
	bool stage;
	const char* keyword;
	const char* help;
};

// Shown in completion and the graph inspector. Translated where shown.
const KeywordHelp kKeywordHelp[] = {
	{MaterialEngine::Quake3, true, "map", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The stage's image: a path, $lightmap or $whiteimage.")},
	{MaterialEngine::Quake3, true, "clampmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Like map, but the image does not repeat.")},
	{MaterialEngine::Quake3, true, "animmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Cycles up to 8 images at the given frames per second.")},
	{MaterialEngine::Quake3, true, "videomap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Plays a RoQ video as the stage's image.")},
	{MaterialEngine::Quake3, true, "blendfunc", QT_TRANSLATE_NOOP("VibeStudioMaterials", "How the stage mixes with what is drawn: add, filter, blend, or two GL factors.")},
	{MaterialEngine::Quake3, true, "rgbgen", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Where the stage's colour comes from: identity, wave, const, vertex, entity, lightingDiffuse.")},
	{MaterialEngine::Quake3, true, "alphagen", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Where the stage's alpha comes from: wave, const, vertex, entity, lightingSpecular, portal.")},
	{MaterialEngine::Quake3, true, "tcgen", QT_TRANSLATE_NOOP("VibeStudioMaterials", "How texture coordinates are made: base, lightmap, environment, vector.")},
	{MaterialEngine::Quake3, true, "tcmod", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Moves the texture coordinates: scroll, scale, rotate, stretch, turb, transform. Applied in order.")},
	{MaterialEngine::Quake3, true, "alphafunc", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Discards pixels by alpha: GT0, LT128 or GE128.")},
	{MaterialEngine::Quake3, true, "depthfunc", QT_TRANSLATE_NOOP("VibeStudioMaterials", "lequal (default) or equal, to draw only where an earlier stage drew.")},
	{MaterialEngine::Quake3, true, "depthwrite", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Writes depth even when the stage blends.")},
	{MaterialEngine::Quake3, true, "detail", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Marks a detail texture that r_detailtextures can turn off.")},
	{MaterialEngine::Quake3, false, "surfaceparm", QT_TRANSLATE_NOOP("VibeStudioMaterials", "A compiler and game flag such as nonsolid, trans, water or sky.")},
	{MaterialEngine::Quake3, false, "cull", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Which faces draw: front (default), back, or none for both.")},
	{MaterialEngine::Quake3, false, "deformvertexes", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Moves vertices every frame: wave, normal, bulge, move, autosprite.")},
	{MaterialEngine::Quake3, false, "skyparms", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Sky box images, cloud height and inner box.")},
	{MaterialEngine::Quake3, false, "fogparms", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Fog volume colour and the distance at which it is opaque.")},
	{MaterialEngine::Quake3, false, "sort", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Draw order: portal, sky, opaque, decal, banner, underwater, additive, nearest or a number.")},
	{MaterialEngine::Quake3, false, "polygonoffset", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Pulls the surface towards the viewer so decals do not flicker.")},
	{MaterialEngine::Quake3, false, "nopicmip", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Keeps full resolution whatever r_picmip says.")},
	{MaterialEngine::Quake3, false, "nomipmaps", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Uses no mip levels at all.")},
	{MaterialEngine::Quake3, false, "portal", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Marks a mirror or portal surface.")},
	{MaterialEngine::Quake3, false, "qer_editorimage", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The image level editors show for this shader.")},
	{MaterialEngine::Quake3, false, "tesssize", QT_TRANSLATE_NOOP("VibeStudioMaterials", "How finely q3map2 subdivides surfaces so vertex deforms look smooth.")},
	{MaterialEngine::Doom3, true, "blend", QT_TRANSLATE_NOOP("VibeStudioMaterials", "blend, add, filter, modulate, none, two GL factors, or diffusemap, bumpmap, specularmap for lighting.")},
	{MaterialEngine::Doom3, true, "map", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The stage's image or image program, such as heightmap( image, 4 ).")},
	{MaterialEngine::Doom3, true, "cubemap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "A cube map from six images with _px, _nx... suffixes.")},
	{MaterialEngine::Doom3, true, "cameracubemap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "A cube map from six camera-style images (_forward, _back...).")},
	{MaterialEngine::Doom3, true, "texgen", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Generated coordinates: normal, reflect, skybox, wobbleSky, screen.")},
	{MaterialEngine::Doom3, true, "scroll", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Offsets the coordinates by two expressions, such as scroll time * 0.1, 0.")},
	{MaterialEngine::Doom3, true, "translate", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The same as scroll.")},
	{MaterialEngine::Doom3, true, "scale", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Scales the coordinates about the origin.")},
	{MaterialEngine::Doom3, true, "centerscale", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Scales the coordinates about the image centre.")},
	{MaterialEngine::Doom3, true, "shear", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Shears the coordinates about the image centre.")},
	{MaterialEngine::Doom3, true, "rotate", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Rotates about the image centre; the expression counts whole turns.")},
	{MaterialEngine::Doom3, true, "rgb", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Sets red, green and blue to one expression.")},
	{MaterialEngine::Doom3, true, "rgba", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Sets every channel to one expression.")},
	{MaterialEngine::Doom3, true, "color", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Four expressions: red, green, blue, alpha.")},
	{MaterialEngine::Doom3, true, "colored", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Colour from the entity's parm0 to parm3.")},
	{MaterialEngine::Doom3, true, "alphatest", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Discards pixels whose alpha is below the expression.")},
	{MaterialEngine::Doom3, true, "if", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Draws the stage only while the expression is not zero.")},
	{MaterialEngine::Doom3, true, "vertexcolor", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Multiplies by the vertex colour.")},
	{MaterialEngine::Doom3, true, "clamp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The image does not repeat.")},
	{MaterialEngine::Doom3, true, "zeroclamp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Outside the image is black.")},
	{MaterialEngine::Doom3, false, "diffusemap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Shorthand for a diffuse lighting stage.")},
	{MaterialEngine::Doom3, false, "bumpmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Shorthand for a normal map stage.")},
	{MaterialEngine::Doom3, false, "specularmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Shorthand for a specular lighting stage.")},
	{MaterialEngine::Doom3, false, "translucent", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Blended, not lit, and drawn after opaque surfaces.")},
	{MaterialEngine::Doom3, false, "twosided", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Draws both faces and casts no shadows.")},
	{MaterialEngine::Doom3, false, "noshadows", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Casts no shadows.")},
	{MaterialEngine::Doom3, false, "deform", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Changes geometry: sprite, tube, flare, expand, move, turbulent, eyeBall, particle.")},
	{MaterialEngine::Doom3, false, "sort", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Draw order: subview, opaque, decal, far, medium, close, almostNearest, nearest, postProcess.")},
	{MaterialEngine::Doom3, false, "lightfalloffimage", QT_TRANSLATE_NOOP("VibeStudioMaterials", "For a light: the image that fades it with distance.")},
	{MaterialEngine::Doom3, false, "polygonoffset", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Pulls the surface towards the viewer.")},
	{MaterialEngine::Doom3, false, "qer_editorimage", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The image the editor shows.")},
	{MaterialEngine::Doom3, false, "decal_macro", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Polygon offset, discrete, decal sort and no shadows in one word.")},
};

} // namespace

QString materialKeywordHelp(MaterialEngine engine, const QString& keyword, bool stage)
{
	const QString key = keyword.trimmed().toLower();
	for (const KeywordHelp& entry : kKeywordHelp) {
		if (entry.engine == engine && entry.stage == stage && key == QLatin1String(entry.keyword)) {
			return QCoreApplication::translate("VibeStudioMaterials", entry.help);
		}
	}
	return QString();
}

QString materialEditKindId(MaterialEditKind kind)
{
	switch (kind) {
	case MaterialEditKind::SetDirective:
		return QStringLiteral("set");
	case MaterialEditKind::AddDirective:
		return QStringLiteral("add");
	case MaterialEditKind::RemoveDirective:
		return QStringLiteral("remove");
	case MaterialEditKind::AddStage:
		return QStringLiteral("add-stage");
	case MaterialEditKind::RemoveStage:
		return QStringLiteral("remove-stage");
	case MaterialEditKind::MoveStage:
		return QStringLiteral("move-stage");
	case MaterialEditKind::ReplaceDefinition:
		return QStringLiteral("replace-definition");
	case MaterialEditKind::AddDefinition:
		return QStringLiteral("add-definition");
	case MaterialEditKind::RemoveDefinition:
		return QStringLiteral("remove-definition");
	case MaterialEditKind::RenameDefinition:
		return QStringLiteral("rename");
	case MaterialEditKind::MoveDirective:
		return QStringLiteral("move");
	}
	return QStringLiteral("set");
}

bool materialEditKindFromId(const QString& id, MaterialEditKind* kind)
{
	static const QHash<QString, MaterialEditKind> kinds {
		{QStringLiteral("set"), MaterialEditKind::SetDirective},
		{QStringLiteral("add"), MaterialEditKind::AddDirective},
		{QStringLiteral("remove"), MaterialEditKind::RemoveDirective},
		{QStringLiteral("add-stage"), MaterialEditKind::AddStage},
		{QStringLiteral("remove-stage"), MaterialEditKind::RemoveStage},
		{QStringLiteral("move-stage"), MaterialEditKind::MoveStage},
		{QStringLiteral("replace-definition"), MaterialEditKind::ReplaceDefinition},
		{QStringLiteral("add-definition"), MaterialEditKind::AddDefinition},
		{QStringLiteral("remove-definition"), MaterialEditKind::RemoveDefinition},
		{QStringLiteral("rename"), MaterialEditKind::RenameDefinition},
		{QStringLiteral("move"), MaterialEditKind::MoveDirective},
	};
	const auto found = kinds.constFind(id.trimmed().toLower());
	if (found == kinds.constEnd()) {
		return false;
	}
	*kind = *found;
	return true;
}

MaterialEditResult applyMaterialEdit(const MaterialScript& script, const MaterialEdit& edit)
{
	const QString& text = script.text;
	const QString newline = newlineFor(text);
	if (edit.kind == MaterialEditKind::AddDefinition) {
		const QString body = edit.text.trimmed();
		if (body.isEmpty()) {
			return failure(Text::tr("The new definition is empty."));
		}
		QString prefix;
		if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) {
			prefix = newline;
		}
		if (!text.trimmed().isEmpty()) {
			prefix += newline;
		}
		QString addition = body;
		addition.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		if (newline != QStringLiteral("\n")) {
			addition.replace(QStringLiteral("\n"), newline);
		}
		const int start = text.size();
		const QString updated = text + prefix + addition + newline;
		return finish(script, updated, start, updated.size());
	}
	const int materialIndex = script.indexOf(edit.material);
	if (materialIndex < 0) {
		return failure(Text::tr("No material named '%1' in this script.").arg(edit.material));
	}
	const MaterialDefinition& definition = script.materials.at(materialIndex);
	if (!definition.span.isValid()) {
		return failure(Text::tr("'%1' has no editable text.").arg(definition.name));
	}
	QString error;
	switch (edit.kind) {
	case MaterialEditKind::RenameDefinition: {
		const QString name = edit.text.trimmed();
		if (name.isEmpty() || name.contains(QRegularExpression(QStringLiteral("\\s")))) {
			return failure(Text::tr("A material name cannot be empty or contain spaces."));
		}
		const Patch patch {definition.nameSpan.start, definition.nameSpan.end, name};
		return finish(script, applyPatch(text, patch), patch.start, patch.start + name.size());
	}
	case MaterialEditKind::ReplaceDefinition: {
		QString replacement = edit.text.trimmed();
		replacement.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		if (newline != QStringLiteral("\n")) {
			replacement.replace(QStringLiteral("\n"), newline);
		}
		const Patch patch {definition.span.start, definition.span.end, replacement};
		return finish(script, applyPatch(text, patch), patch.start, patch.start + replacement.size());
	}
	case MaterialEditKind::RemoveDefinition: {
		const auto [start, end] = removalRange(text, definition.span.start, definition.span.end);
		const Patch patch {start, end, QString()};
		return finish(script, applyPatch(text, patch), start, start);
	}
	case MaterialEditKind::SetDirective:
	case MaterialEditKind::AddDirective:
	case MaterialEditKind::RemoveDirective:
	case MaterialEditKind::MoveDirective: {
		Block block;
		if (!blockFor(definition, edit.stage, &block, &error)) {
			return failure(error.isEmpty() ? Text::tr("That block has no editable text.") : error);
		}
		const QVector<MaterialDirective>& directives = *block.directives;
		if (edit.kind == MaterialEditKind::AddDirective) {
			if (edit.keyword.trimmed().isEmpty()) {
				return failure(Text::tr("A directive needs a keyword."));
			}
			const Patch patch = insertionPatch(text, block, edit.position, directiveText(edit.keyword.trimmed(), edit.arguments));
			return finish(script, applyPatch(text, patch), patch.start, patch.start + patch.replacement.size());
		}
		const int index = findDirective(directives, edit);
		if (edit.kind == MaterialEditKind::SetDirective) {
			if (index < 0) {
				if (edit.keyword.trimmed().isEmpty()) {
					return failure(Text::tr("Directive %1 does not exist.").arg(edit.directive + 1));
				}
				const Patch patch = insertionPatch(text, block, -1, directiveText(edit.keyword.trimmed(), edit.arguments));
				return finish(script, applyPatch(text, patch), patch.start, patch.start + patch.replacement.size());
			}
			const MaterialDirective& directive = directives.at(index);
			const QString keyword = edit.keyword.trimmed().isEmpty() ? directive.keyword : edit.keyword.trimmed();
			const QString replacement = directiveText(keyword, edit.arguments);
			const Patch patch {directive.span.start, directive.span.end, replacement};
			return finish(script, applyPatch(text, patch), patch.start, patch.start + replacement.size());
		}
		if (index < 0) {
			return failure(Text::tr("No directive '%1' to change.").arg(edit.keyword.isEmpty() ? QString::number(edit.directive + 1) : edit.keyword));
		}
		const MaterialDirective& directive = directives.at(index);
		const auto [start, end] = removalRange(text, directive.span.start, directive.span.end);
		if (edit.kind == MaterialEditKind::RemoveDirective) {
			const Patch patch {start, end, QString()};
			return finish(script, applyPatch(text, patch), start, start);
		}
		// MoveDirective: remove, then insert before the target in the
		// re-parsed text.
		const QString line = text.mid(directive.span.start, directive.span.end - directive.span.start);
		int target = std::clamp(edit.position, 0, static_cast<int>(directives.size()) - 1);
		if (target == index) {
			return finish(script, text, directive.span.start, directive.span.end);
		}
		const QString removed = applyPatch(text, {start, end, QString()});
		MaterialScript intermediate = parseMaterialScript(removed, script.engine, script.path);
		const int intermediateIndex = intermediate.indexOf(definition.name);
		if (intermediateIndex < 0) {
			return failure(Text::tr("The directive could not be moved."));
		}
		Block moved;
		if (!blockFor(intermediate.materials.at(intermediateIndex), edit.stage, &moved, &error)) {
			return failure(error);
		}
		const Patch patch = insertionPatch(removed, moved, target < moved.directives->size() ? target : -1, line);
		return finish(script, applyPatch(removed, patch), patch.start, patch.start + patch.replacement.size());
	}
	case MaterialEditKind::AddStage: {
		if (!definition.bodySpan.isValid()) {
			return failure(Text::tr("'%1' has no body to add a stage to.").arg(definition.name));
		}
		const QString indent = stageIndent(text, definition);
		const QString block = stageBlockText(text, indent, edit.text);
		int insertAt = -1;
		QVector<const MaterialStage*> blocks;
		for (const MaterialStage& stage : definition.stages) {
			blocks << &stage;
		}
		if (edit.position >= 0 && edit.position < blocks.size()) {
			const MaterialStage& before = *blocks.at(edit.position);
			insertAt = lineStartOf(text, before.span.start);
			if (!onlyWhitespace(text, insertAt, before.span.start)) {
				insertAt = before.span.start;
				const Patch patch {insertAt, insertAt, block.trimmed() + newline + indentationOf(text, before.span.start)};
				return finish(script, applyPatch(text, patch), patch.start, patch.start + patch.replacement.size());
			}
		} else {
			const int close = definition.bodySpan.end - 1;
			insertAt = lineStartOf(text, close);
			if (!onlyWhitespace(text, insertAt, close)) {
				const Patch patch {close, close, newline + block + indentationOf(text, definition.bodySpan.start)};
				return finish(script, applyPatch(text, patch), patch.start, patch.start + patch.replacement.size());
			}
		}
		const Patch patch {insertAt, insertAt, block};
		return finish(script, applyPatch(text, patch), patch.start, patch.start + block.size());
	}
	case MaterialEditKind::RemoveStage: {
		if (edit.stage < 0 || edit.stage >= definition.stages.size()) {
			return failure(Text::tr("Stage %1 does not exist.").arg(edit.stage + 1));
		}
		const MaterialStage& stage = definition.stages.at(edit.stage);
		if (stage.shorthandDirective >= 0) {
			const MaterialDirective& directive = definition.directives.at(stage.shorthandDirective);
			const auto [start, end] = removalRange(text, directive.span.start, directive.span.end);
			return finish(script, applyPatch(text, {start, end, QString()}), start, start);
		}
		const auto [start, end] = stageLines(text, stage);
		return finish(script, applyPatch(text, {start, end, QString()}), start, start);
	}
	case MaterialEditKind::MoveStage: {
		if (edit.stage < 0 || edit.stage >= definition.stages.size()) {
			return failure(Text::tr("Stage %1 does not exist.").arg(edit.stage + 1));
		}
		const MaterialStage& stage = definition.stages.at(edit.stage);
		if (stage.shorthandDirective >= 0) {
			return failure(Text::tr("Stage %1 is a shorthand directive; it has no block to move.").arg(edit.stage + 1));
		}
		const int target = std::clamp(edit.position, 0, static_cast<int>(definition.stages.size()) - 1);
		if (target == edit.stage) {
			return finish(script, text, stage.span.start, stage.span.end);
		}
		// Carry the block's own lines, then re-insert as a fresh block.
		QString body;
		for (const MaterialDirective& directive : stage.directives) {
			body += text.mid(directive.span.start, directive.span.end - directive.span.start) + QLatin1Char('\n');
		}
		const auto [start, end] = stageLines(text, stage);
		const QString removed = applyPatch(text, {start, end, QString()});
		MaterialScript intermediate = parseMaterialScript(removed, script.engine, script.path);
		MaterialEdit insert;
		insert.kind = MaterialEditKind::AddStage;
		insert.material = definition.name;
		insert.text = body;
		insert.position = target;
		return applyMaterialEdit(intermediate, insert);
	}
	case MaterialEditKind::AddDefinition:
		break;
	}
	return failure(Text::tr("Unsupported edit."));
}

MaterialEditResult applyMaterialEdits(const MaterialScript& script, const QVector<MaterialEdit>& edits)
{
	MaterialEditResult result;
	result.ok = true;
	result.text = script.text;
	result.script = script;
	for (int index = 0; index < edits.size(); ++index) {
		MaterialEditResult step = applyMaterialEdit(result.script, edits.at(index));
		if (!step.ok) {
			step.error = Text::tr("Edit %1: %2").arg(index + 1).arg(step.error);
			return step;
		}
		result = step;
	}
	return result;
}

bool parseMaterialEditsJson(const QByteArray& json, const QString& defaultMaterial, QVector<MaterialEdit>* edits, QString* error)
{
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
		*error = Text::tr("Edits must be a JSON array: %1").arg(parseError.errorString());
		return false;
	}
	const QJsonArray array = document.array();
	if (array.size() > 256) {
		*error = Text::tr("At most 256 edits are accepted at once.");
		return false;
	}
	for (int index = 0; index < array.size(); ++index) {
		if (!array.at(index).isObject()) {
			*error = Text::tr("Edit %1 is not an object.").arg(index + 1);
			return false;
		}
		const QJsonObject object = array.at(index).toObject();
		static const QSet<QString> known {QStringLiteral("op"), QStringLiteral("material"), QStringLiteral("stage"), QStringLiteral("directive"),
			QStringLiteral("keyword"), QStringLiteral("arguments"), QStringLiteral("position"), QStringLiteral("text")};
		for (const QString& key : object.keys()) {
			if (!known.contains(key)) {
				*error = Text::tr("Edit %1 has an unknown field '%2'.").arg(index + 1).arg(key);
				return false;
			}
		}
		MaterialEdit edit;
		if (!materialEditKindFromId(object.value(QStringLiteral("op")).toString(), &edit.kind)) {
			*error = Text::tr("Edit %1 has an unknown op '%2'.").arg(index + 1).arg(object.value(QStringLiteral("op")).toString());
			return false;
		}
		edit.material = object.value(QStringLiteral("material")).toString(defaultMaterial);
		const auto integer = [&](const QString& key, int fallback, int* out) {
			const QJsonValue value = object.value(key);
			if (value.isUndefined()) {
				*out = fallback;
				return true;
			}
			if (!value.isDouble() || value.toDouble() != std::floor(value.toDouble())) {
				*error = Text::tr("Edit %1: '%2' must be an integer.").arg(index + 1).arg(key);
				return false;
			}
			*out = value.toInt();
			return true;
		};
		// Stage and directive numbers are 1-based in JSON, like the UI.
		int stage = 0;
		int directive = 0;
		int position = 0;
		if (!integer(QStringLiteral("stage"), 0, &stage) || !integer(QStringLiteral("directive"), 0, &directive)
			|| !integer(QStringLiteral("position"), 0, &position)) {
			return false;
		}
		edit.stage = stage - 1;
		edit.directive = directive - 1;
		edit.position = position - 1;
		edit.keyword = object.value(QStringLiteral("keyword")).toString();
		edit.arguments = object.value(QStringLiteral("arguments")).toString();
		edit.text = object.value(QStringLiteral("text")).toString();
		edits->push_back(edit);
	}
	return true;
}

QString materialSurfaceContextId(MaterialSurfaceContext context)
{
	switch (context) {
	case MaterialSurfaceContext::World:
		return QStringLiteral("world");
	case MaterialSurfaceContext::Model:
		return QStringLiteral("model");
	case MaterialSurfaceContext::TwoD:
		return QStringLiteral("2d");
	}
	return QStringLiteral("world");
}

MaterialDefinition implicitQuake3Material(const QString& name, MaterialSurfaceContext context, const QString& image)
{
	MaterialDefinition definition;
	definition.name = name;
	definition.engine = MaterialEngine::Quake3;
	definition.kind = QStringLiteral("implicit");
	QString path = image.trimmed();
	if (path.isEmpty()) {
		path = name;
		if (QFileInfo(path).suffix().isEmpty()) {
			path += QStringLiteral(".tga");
		}
	}
	MaterialStage texture;
	texture.imageKind = MaterialImageKind::File;
	texture.imagePath = path;
	texture.rgbGen.explicitlySet = true;
	texture.alphaGen.source = MaterialColorSource::Identity;
	if (context == MaterialSurfaceContext::World) {
		// Two passes: the lightmap (scaled for identity light when loaded),
		// then the image multiplied in.
		MaterialStage lightmap;
		lightmap.index = 0;
		lightmap.imageKind = MaterialImageKind::Lightmap;
		lightmap.tcGen.source = MaterialTexCoordSource::Lightmap;
		lightmap.rgbGen.source = MaterialColorSource::Identity;
		lightmap.rgbGen.explicitlySet = true;
		definition.stages.push_back(lightmap);
		texture.index = 1;
		texture.blend.source = MaterialBlendFactor::DestinationColor;
		texture.blend.destination = MaterialBlendFactor::Zero;
		texture.blend.explicitBlend = true;
		texture.depthWrite = false;
		texture.rgbGen.source = MaterialColorSource::Identity;
	} else if (context == MaterialSurfaceContext::Model) {
		texture.rgbGen.source = MaterialColorSource::LightingDiffuse;
	} else {
		texture.rgbGen.source = MaterialColorSource::Identity;
		texture.blend.source = MaterialBlendFactor::SourceAlpha;
		texture.blend.destination = MaterialBlendFactor::OneMinusSourceAlpha;
		texture.blend.explicitBlend = true;
	}
	definition.stages.push_back(texture);
	definition.sort = QStringLiteral("opaque");
	definition.sortValue = 3;
	return definition;
}

QString implicitDoom3MaterialText(const QString& name)
{
	// MakeNameCanonical: lower case, forward slashes, no extension.
	QString canonical = name.trimmed().toLower();
	canonical.replace(QLatin1Char('\\'), QLatin1Char('/'));
	const int dot = canonical.lastIndexOf(QLatin1Char('.'));
	if (dot > canonical.lastIndexOf(QLatin1Char('/'))) {
		canonical.truncate(dot);
	}
	return QStringLiteral("material %1 // IMPLICITLY GENERATED\n{\n\t{\n\t\tblend blend\n\t\tcolored\n\t\tmap \"%1\"\n\t\tclamp\n\t}\n}\n").arg(canonical);
}

MaterialDefinition implicitDoom3Material(const QString& name)
{
	MaterialScript script = parseMaterialScript(implicitDoom3MaterialText(name), MaterialEngine::Doom3);
	MaterialDefinition definition = script.materials.isEmpty() ? MaterialDefinition() : script.materials.first();
	definition.name = name;
	definition.engine = MaterialEngine::Doom3;
	definition.kind = QStringLiteral("implicit");
	definition.span = {};
	definition.bodySpan = {};
	definition.nameSpan = {};
	definition.sourcePath.clear();
	return definition;
}

QVector<MaterialTemplate> materialTemplates(MaterialEngine engine)
{
	static const QVector<MaterialTemplate> all = [] {
		QVector<MaterialTemplate> list;
		const auto add = [&](const char* id, MaterialEngine engine, const QString& name, const QString& description, const char* body) {
			list.push_back({QString::fromLatin1(id), engine, name, description, QString::fromLatin1(body)});
		};
		add("q3-lightmapped", MaterialEngine::Quake3, Text::tr("Lightmapped wall"),
			Text::tr("The standard two-pass world surface: lightmap, then the image multiplied in."),
			"%1\n{\n\tqer_editorimage %2\n\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap %2\n\t\tblendFunc filter\n\t\trgbGen identity\n\t}\n}\n");
		add("q3-glow", MaterialEngine::Quake3, Text::tr("Pulsing glow"),
			Text::tr("A lit wall with an added light pass whose brightness follows a sine wave."),
			"%1\n{\n\tqer_editorimage %2\n\tq3map_surfacelight 500\n\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap %2\n\t\tblendFunc filter\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap %2\n\t\tblendFunc add\n\t\trgbGen wave sin 0.5 0.5 0 0.5\n\t}\n}\n");
		add("q3-liquid", MaterialEngine::Quake3, Text::tr("Rippling liquid"),
			Text::tr("Translucent water with turbulent, scrolling coordinates and gently moving vertices."),
			"%1\n{\n\tqer_editorimage %2\n\tqer_trans 0.5\n\tsurfaceparm trans\n\tsurfaceparm nonsolid\n\tsurfaceparm water\n\tcull disable\n\tdeformVertexes wave 64 sin 0 2 0 0.5\n\ttessSize 64\n\t{\n\t\tmap %2\n\t\tblendFunc blend\n\t\talphaGen const 0.6\n\t\ttcMod turb 0 0.1 0 0.2\n\t\ttcMod scroll 0.02 0.01\n\t}\n\t{\n\t\tmap $lightmap\n\t\tblendFunc filter\n\t\trgbGen identity\n\t\tdepthFunc equal\n\t}\n}\n");
		add("q3-sky", MaterialEngine::Quake3, Text::tr("Sky with clouds"),
			Text::tr("A sky box with two scrolling cloud layers."),
			"%1\n{\n\tqer_editorimage %2\n\tsurfaceparm sky\n\tsurfaceparm noimpact\n\tsurfaceparm nolightmap\n\tskyParms - 512 -\n\t{\n\t\tmap %2\n\t\ttcMod scale 3 2\n\t\ttcMod scroll 0.015 0.016\n\t\tdepthWrite\n\t}\n\t{\n\t\tmap %2\n\t\tblendFunc add\n\t\ttcMod scale 3 3\n\t\ttcMod scroll 0.005 0.003\n\t}\n}\n");
		add("q3-grate", MaterialEngine::Quake3, Text::tr("Alpha-tested grate"),
			Text::tr("A see-through grate: alpha test writes depth, then the lightmap draws only where it passed."),
			"%1\n{\n\tqer_editorimage %2\n\tsurfaceparm alphashadow\n\tsurfaceparm trans\n\tcull none\n\tnopicmip\n\t{\n\t\tmap %2\n\t\talphaFunc GE128\n\t\tdepthWrite\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t\tblendFunc filter\n\t\tdepthFunc equal\n\t}\n}\n");
		add("q3-envmap", MaterialEngine::Quake3, Text::tr("Shiny metal"),
			Text::tr("A lit surface with an environment-mapped reflection added."),
			"%1\n{\n\tqer_editorimage %2\n\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap %2\n\t\tblendFunc filter\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap %2\n\t\ttcGen environment\n\t\tblendFunc add\n\t\trgbGen const ( 0.25 0.25 0.25 )\n\t}\n}\n");
		add("q3-animmap", MaterialEngine::Quake3, Text::tr("Animated frames"),
			Text::tr("Cycles images at a fixed rate, such as fire or a screen."),
			"%1\n{\n\tqer_editorimage %2\n\tsurfaceparm nolightmap\n\t{\n\t\tanimMap 8 %2 %2 %2 %2\n\t\trgbGen identity\n\t}\n}\n");
		add("q3-flicker", MaterialEngine::Quake3, Text::tr("Flickering light"),
			Text::tr("A light panel that flickers with a noise wave."),
			"%1\n{\n\tqer_editorimage %2\n\tq3map_surfacelight 1000\n\tsurfaceparm nolightmap\n\t{\n\t\tmap %2\n\t\trgbGen wave noise 0.75 0.25 0 6\n\t}\n}\n");
		add("q3-fog", MaterialEngine::Quake3, Text::tr("Fog volume"),
			Text::tr("A fog brush that reaches full opacity at the given distance."),
			"%1\n{\n\tqer_editorimage %2\n\tqer_nocarve\n\tsurfaceparm trans\n\tsurfaceparm nonsolid\n\tsurfaceparm fog\n\tsurfaceparm nolightmap\n\tfogparms ( 0.4 0.45 0.5 ) 512\n}\n");
		add("q3-decal", MaterialEngine::Quake3, Text::tr("Decal"),
			Text::tr("A stain or marking drawn on top of walls without flicker."),
			"%1\n{\n\tqer_editorimage %2\n\tsurfaceparm nonsolid\n\tsurfaceparm nomarks\n\tsurfaceparm trans\n\tpolygonOffset\n\t{\n\t\tmap %2\n\t\tblendFunc filter\n\t\trgbGen identity\n\t}\n}\n");
		add("d3-standard", MaterialEngine::Doom3, Text::tr("Lit surface"),
			Text::tr("Diffuse, normal and specular maps lit by the scene's lights."),
			"%1\n{\n\tqer_editorimage %2\n\tdiffusemap %2\n\tbumpmap addnormals( %2_local, heightmap( %2_h, 4 ) )\n\tspecularmap %2_s\n}\n");
		add("d3-glow", MaterialEngine::Doom3, Text::tr("Glowing panel"),
			Text::tr("A lit surface with an added glow that pulses using sinTable."),
			"%1\n{\n\tqer_editorimage %2\n\tdiffusemap %2\n\tbumpmap %2_local\n\t{\n\t\tblend add\n\t\tmap %2_add\n\t\trgb 0.75 + 0.25 * sinTable[ time * 0.5 ]\n\t}\n}\n");
		add("d3-glass", MaterialEngine::Doom3, Text::tr("Reflective glass"),
			Text::tr("Translucent glass reflecting an environment cube map."),
			"%1\n{\n\tqer_editorimage %2\n\ttranslucent\n\tnoShadows\n\tglass\n\t{\n\t\tblend filter\n\t\tmap %2\n\t}\n\t{\n\t\tblend add\n\t\tcubeMap env/gen1\n\t\ttexgen reflect\n\t\trgb 0.2\n\t}\n}\n");
		add("d3-scroll", MaterialEngine::Doom3, Text::tr("Scrolling screen"),
			Text::tr("An unlit screen whose image scrolls over time."),
			"%1\n{\n\tqer_editorimage %2\n\tnoShadows\n\t{\n\t\tblend add\n\t\tmap %2\n\t\tscroll time * 0.1, 0\n\t}\n}\n");
		add("d3-light", MaterialEngine::Doom3, Text::tr("Light"),
			Text::tr("A light shader: a projected image faded by a falloff image, tinted by the light's colour."),
			"%1\n{\n\tlightFalloffImage makeintensity( lights/squarelight1a.tga )\n\t{\n\t\tmap %2\n\t\tcolored\n\t\tzeroClamp\n\t}\n}\n");
		add("d3-flicker-light", MaterialEngine::Doom3, Text::tr("Flickering light"),
			Text::tr("A light whose brightness follows a table that you can edit."),
			"%1\n{\n\tlightFalloffImage makeintensity( lights/squarelight1a.tga )\n\t{\n\t\tmap %2\n\t\tzeroClamp\n\t\tred parm0 * flickertable[ time * 3 ]\n\t\tgreen parm1 * flickertable[ time * 3 ]\n\t\tblue parm2 * flickertable[ time * 3 ]\n\t}\n}\n");
		add("d3-sky", MaterialEngine::Doom3, Text::tr("Sky box"),
			Text::tr("An unlit sky drawn from a camera cube map behind everything."),
			"%1\n{\n\tqer_editorimage %2\n\tnoShadows\n\tnoFragment\n\tnonsolid\n\tnoimpact\n\tforceOpaque\n\t{\n\t\tblend add\n\t\tcameraCubeMap %2\n\t\ttexgen skybox\n\t\ttexgen wobblesky 0 0 0\n\t}\n}\n");
		add("d3-decal", MaterialEngine::Doom3, Text::tr("Decal"),
			Text::tr("A marking drawn over walls, fading after a while."),
			"%1\n{\n\tqer_editorimage %2\n\tDECAL_MACRO\n\tdecalInfo 10 2 ( 1 1 1 1 ) ( 0 0 0 0 )\n\t{\n\t\tblend filter\n\t\tmap %2\n\t\tvertexColor\n\t}\n}\n");
		return list;
	}();
	if (engine == MaterialEngine::Unknown) {
		return all;
	}
	QVector<MaterialTemplate> filtered;
	for (const MaterialTemplate& entry : all) {
		if (entry.engine == engine) {
			filtered.push_back(entry);
		}
	}
	return filtered;
}

bool materialTemplateById(const QString& id, MaterialTemplate* out)
{
	for (const MaterialTemplate& entry : materialTemplates()) {
		if (entry.id.compare(id.trimmed(), Qt::CaseInsensitive) == 0) {
			if (out) {
				*out = entry;
			}
			return true;
		}
	}
	return false;
}

QString instantiateMaterialTemplate(const MaterialTemplate& materialTemplate, const QString& name, const QString& image)
{
	QString body = materialTemplate.body;
	QString imagePath = image.trimmed();
	if (imagePath.isEmpty()) {
		imagePath = name;
	}
	if (materialTemplate.engine == MaterialEngine::Doom3) {
		// Doom 3 images drop the extension.
		const QString suffix = QFileInfo(imagePath).suffix().toLower();
		if (suffix == QStringLiteral("tga") || suffix == QStringLiteral("jpg") || suffix == QStringLiteral("png")) {
			imagePath.chop(suffix.size() + 1);
		}
	} else if (materialTemplate.engine == MaterialEngine::Quake3 && QFileInfo(imagePath).suffix().isEmpty()) {
		imagePath += QStringLiteral(".tga");
	}
	body.replace(QStringLiteral("%1"), name.trimmed());
	body.replace(QStringLiteral("%2"), imagePath);
	if (materialTemplate.id == QStringLiteral("d3-flicker-light")) {
		body = QStringLiteral("table flickertable { { 1, 0.9, 1, 0.4, 1, 0.95, 1, 0.8, 1, 0.2, 1 } }\n\n") + body;
	}
	return body;
}

} // namespace vibestudio
