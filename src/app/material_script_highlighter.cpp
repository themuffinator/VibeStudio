#include "app/material_script_highlighter.h"

#include "core/material_script.h"

#include <QTextDocument>

#include <algorithm>

namespace vibestudio {

namespace {

bool isNumber(const QString& token)
{
	bool ok = false;
	token.toDouble(&ok);
	return ok;
}

// Characters that end a word in Doom 3 expressions and Quake III values.
bool isBreak(QChar c, bool doom3)
{
	if (c.isSpace() || c == QLatin1Char('{') || c == QLatin1Char('}') || c == QLatin1Char('"')) {
		return true;
	}
	if (!doom3) {
		return false;
	}
	static const QString punctuation = QStringLiteral("()[],+*%<>=!&|");
	return punctuation.contains(c);
}

} // namespace

MaterialScriptHighlighter::MaterialScriptHighlighter(QTextDocument* document)
	: QSyntaxHighlighter(document)
{
	m_theme = studioSyntaxTheme(false, false);
	rebuildFormats();
}

void MaterialScriptHighlighter::setTextKind(MaterialTextKind kind)
{
	if (kind == m_kind) {
		return;
	}
	m_kind = kind;
	m_global.clear();
	m_stage.clear();
	m_values.clear();
	const MaterialEngine engine = kind == MaterialTextKind::Doom3Material ? MaterialEngine::Doom3 : MaterialEngine::Quake3;
	if (kind == MaterialTextKind::Quake3Shader || kind == MaterialTextKind::Doom3Material) {
		for (const QString& word : materialGlobalKeywords(engine)) {
			m_global.insert(word.toLower());
		}
		for (const QString& word : materialStageKeywords(engine)) {
			m_stage.insert(word.toLower());
		}
		static const QStringList values {QStringLiteral("add"), QStringLiteral("filter"), QStringLiteral("blend"), QStringLiteral("sin"),
			QStringLiteral("square"), QStringLiteral("triangle"), QStringLiteral("sawtooth"), QStringLiteral("inversesawtooth"),
			QStringLiteral("noise"), QStringLiteral("identity"), QStringLiteral("identitylighting"), QStringLiteral("vertex"),
			QStringLiteral("exactvertex"), QStringLiteral("lightingdiffuse"), QStringLiteral("lightingspecular"), QStringLiteral("wave"),
			QStringLiteral("const"), QStringLiteral("entity"), QStringLiteral("oneminusentity"), QStringLiteral("portal"),
			QStringLiteral("environment"), QStringLiteral("base"), QStringLiteral("lightmap"), QStringLiteral("scroll"), QStringLiteral("scale"),
			QStringLiteral("rotate"), QStringLiteral("stretch"), QStringLiteral("transform"), QStringLiteral("turb"), QStringLiteral("none"),
			QStringLiteral("twosided"), QStringLiteral("back"), QStringLiteral("front"), QStringLiteral("disable"), QStringLiteral("gt0"),
			QStringLiteral("lt128"), QStringLiteral("ge128"), QStringLiteral("equal"), QStringLiteral("lequal"), QStringLiteral("time"),
			QStringLiteral("diffusemap"), QStringLiteral("bumpmap"), QStringLiteral("specularmap"), QStringLiteral("modulate"),
			QStringLiteral("normal"), QStringLiteral("reflect"), QStringLiteral("skybox"), QStringLiteral("wobblesky"), QStringLiteral("clamp"),
			QStringLiteral("zeroclamp"), QStringLiteral("alphazeroclamp"), QStringLiteral("nearest"), QStringLiteral("linear"),
			QStringLiteral("snap"), QStringLiteral("highquality"), QStringLiteral("forcehighquality"), QStringLiteral("nopicmip")};
		for (const QString& value : values) {
			m_values.insert(value);
		}
	}
	rehighlight();
}

void MaterialScriptHighlighter::setTheme(const StudioSyntaxTheme& theme)
{
	m_theme = theme;
	rebuildFormats();
	rehighlight();
}

void MaterialScriptHighlighter::setDiagnostics(const QVector<MaterialDiagnostic>& diagnostics)
{
	QHash<int, QVector<MaterialDiagnostic>> byLine;
	for (const MaterialDiagnostic& diagnostic : diagnostics) {
		if (diagnostic.line > 0 && diagnostic.severity != MaterialDiagnosticSeverity::Info) {
			byLine[diagnostic.line].push_back(diagnostic);
		}
	}
	QSet<int> lines;
	for (auto it = m_diagnostics.constBegin(); it != m_diagnostics.constEnd(); ++it) {
		lines.insert(it.key());
	}
	for (auto it = byLine.constBegin(); it != byLine.constEnd(); ++it) {
		lines.insert(it.key());
	}
	m_diagnostics = byLine;
	QTextDocument* doc = document();
	if (!doc) {
		return;
	}
	for (int line : std::as_const(lines)) {
		const QTextBlock block = doc->findBlockByNumber(line - 1);
		if (block.isValid()) {
			rehighlightBlock(block);
		}
	}
}

void MaterialScriptHighlighter::rebuildFormats()
{
	m_keyword = QTextCharFormat();
	m_keyword.setForeground(m_theme.keyword);
	m_keyword.setFontWeight(QFont::DemiBold);
	m_stageKeyword = QTextCharFormat();
	m_stageKeyword.setForeground(m_theme.secondaryKeyword);
	m_value = QTextCharFormat();
	m_value.setForeground(m_theme.directive);
	m_name = QTextCharFormat();
	m_name.setForeground(m_theme.identifier);
	m_name.setFontWeight(QFont::Bold);
	m_path = QTextCharFormat();
	m_path.setForeground(m_theme.identifier);
	m_number = QTextCharFormat();
	m_number.setForeground(m_theme.number);
	m_string = QTextCharFormat();
	m_string.setForeground(m_theme.stringLiteral);
	m_comment = QTextCharFormat();
	m_comment.setForeground(m_theme.comment);
	m_comment.setFontItalic(!m_theme.highContrast);
	m_brace = QTextCharFormat();
	m_brace.setForeground(m_theme.directive);
	m_brace.setFontWeight(QFont::Bold);
}

void MaterialScriptHighlighter::highlightBlock(const QString& text)
{
	switch (m_kind) {
	case MaterialTextKind::Quake3Shader:
	case MaterialTextKind::Doom3Material:
		highlightScript(text);
		break;
	case MaterialTextKind::DoomSwantbls:
		highlightSwantbls(text);
		break;
	case MaterialTextKind::DoomAnimdefs:
		highlightAnimdefs(text);
		break;
	case MaterialTextKind::Quake2WalJson:
		highlightJson(text);
		break;
	case MaterialTextKind::None:
		break;
	}
	underlineDiagnostics(text);
}

void MaterialScriptHighlighter::highlightScript(const QString& text)
{
	// Block state: brace depth times two, plus one inside a /* comment.
	const int previous = previousBlockState();
	bool comment = previous > 0 && (previous & 1);
	int depth = previous > 0 ? previous >> 1 : 0;
	const bool doom3 = m_kind == MaterialTextKind::Doom3Material;
	const int length = static_cast<int>(text.size());
	bool firstWord = true;
	int i = 0;
	while (i < length) {
		if (comment) {
			const int end = static_cast<int>(text.indexOf(QStringLiteral("*/"), i));
			if (end < 0) {
				setFormat(i, length - i, m_comment);
				i = length;
				break;
			}
			setFormat(i, end + 2 - i, m_comment);
			i = end + 2;
			comment = false;
			continue;
		}
		const QChar c = text.at(i);
		const QChar next = i + 1 < length ? text.at(i + 1) : QChar();
		if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
			setFormat(i, length - i, m_comment);
			break;
		}
		if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
			comment = true;
			setFormat(i, 2, m_comment);
			i += 2;
			continue;
		}
		if (c.isSpace()) {
			++i;
			continue;
		}
		if (c == QLatin1Char('"')) {
			int end = static_cast<int>(text.indexOf(QLatin1Char('"'), i + 1));
			end = end < 0 ? length - 1 : end;
			setFormat(i, end + 1 - i, m_string);
			i = end + 1;
			firstWord = false;
			continue;
		}
		if (c == QLatin1Char('{') || c == QLatin1Char('}')) {
			depth = c == QLatin1Char('{') ? depth + 1 : std::max(0, depth - 1);
			setFormat(i, 1, m_brace);
			++i;
			firstWord = true;
			continue;
		}
		if (doom3 && isBreak(c, true)) {
			++i;
			continue;
		}
		int end = i;
		while (end < length && !isBreak(text.at(end), doom3)
			&& !(text.at(end) == QLatin1Char('/') && end + 1 < length && (text.at(end + 1) == QLatin1Char('/') || text.at(end + 1) == QLatin1Char('*')))) {
			++end;
		}
		const QString token = text.mid(i, end - i);
		const QString lower = token.toLower();
		if (isNumber(token)) {
			setFormat(i, end - i, m_number);
		} else if (depth == 0) {
			// Outside braces: material names and Doom 3 decl kinds.
			const bool decl = doom3 && (lower == QStringLiteral("table") || lower == QStringLiteral("material") || lower == QStringLiteral("guide"));
			setFormat(i, end - i, decl ? m_keyword : m_name);
		} else if (firstWord && depth == 1 && (m_global.contains(lower) || lower.startsWith(QStringLiteral("q3map_"))
						|| lower.startsWith(QStringLiteral("qer_")))) {
			setFormat(i, end - i, m_keyword);
		} else if (firstWord && depth >= 2 && m_stage.contains(lower)) {
			setFormat(i, end - i, m_stageKeyword);
		} else if (m_values.contains(lower) || lower.startsWith(QStringLiteral("gl_")) || lower.startsWith(QLatin1Char('$'))
			|| (lower.startsWith(QLatin1Char('_')) && lower.size() > 1)) {
			setFormat(i, end - i, m_value);
		} else if (token.contains(QLatin1Char('/'))) {
			setFormat(i, end - i, m_path);
		}
		firstWord = false;
		i = std::max(end, i + 1);
	}
	setCurrentBlockState((depth << 1) | (comment ? 1 : 0));
}

void MaterialScriptHighlighter::highlightSwantbls(const QString& text)
{
	const int comment = static_cast<int>(text.indexOf(QLatin1Char('#')));
	const QString body = comment >= 0 ? text.left(comment) : text;
	if (comment >= 0) {
		setFormat(comment, static_cast<int>(text.size()) - comment, m_comment);
	}
	const QString trimmed = body.trimmed();
	if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
		setFormat(static_cast<int>(body.indexOf(QLatin1Char('['))), static_cast<int>(trimmed.size()), m_keyword);
		return;
	}
	int i = 0;
	const int length = static_cast<int>(body.size());
	while (i < length) {
		if (body.at(i).isSpace()) {
			++i;
			continue;
		}
		int end = i;
		while (end < length && !body.at(end).isSpace()) {
			++end;
		}
		setFormat(i, end - i, isNumber(body.mid(i, end - i)) ? m_number : m_name);
		i = end;
	}
}

void MaterialScriptHighlighter::highlightAnimdefs(const QString& text)
{
	static const QSet<QString> keywords {QStringLiteral("flat"), QStringLiteral("texture"), QStringLiteral("pic"), QStringLiteral("tics"),
		QStringLiteral("rand"), QStringLiteral("range"), QStringLiteral("warp"), QStringLiteral("warp2"), QStringLiteral("switch"),
		QStringLiteral("on"), QStringLiteral("off"), QStringLiteral("sound"), QStringLiteral("oscillate"), QStringLiteral("allowdecals"),
		QStringLiteral("optional"), QStringLiteral("cameratexture"), QStringLiteral("animateddoor"), QStringLiteral("skyoffset"),
		QStringLiteral("canvastexture"), QStringLiteral("doom"), QStringLiteral("heretic"), QStringLiteral("hexen"), QStringLiteral("strife"),
		QStringLiteral("opensound"), QStringLiteral("closesound"), QStringLiteral("fit"), QStringLiteral("worldpanning"),
		QStringLiteral("nodecals"), QStringLiteral("random")};
	const int previous = previousBlockState();
	bool comment = previous > 0 && (previous & 1);
	const int length = static_cast<int>(text.size());
	int i = 0;
	while (i < length) {
		if (comment) {
			const int end = static_cast<int>(text.indexOf(QStringLiteral("*/"), i));
			if (end < 0) {
				setFormat(i, length - i, m_comment);
				i = length;
				break;
			}
			setFormat(i, end + 2 - i, m_comment);
			i = end + 2;
			comment = false;
			continue;
		}
		const QChar c = text.at(i);
		const QChar next = i + 1 < length ? text.at(i + 1) : QChar();
		if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
			setFormat(i, length - i, m_comment);
			break;
		}
		if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
			comment = true;
			setFormat(i, 2, m_comment);
			i += 2;
			continue;
		}
		if (c.isSpace()) {
			++i;
			continue;
		}
		if (c == QLatin1Char('"')) {
			int end = static_cast<int>(text.indexOf(QLatin1Char('"'), i + 1));
			end = end < 0 ? length - 1 : end;
			setFormat(i, end + 1 - i, m_string);
			i = end + 1;
			continue;
		}
		int end = i;
		while (end < length && !text.at(end).isSpace() && text.at(end) != QLatin1Char('"')) {
			++end;
		}
		const QString token = text.mid(i, end - i);
		if (isNumber(token)) {
			setFormat(i, end - i, m_number);
		} else if (keywords.contains(token.toLower())) {
			setFormat(i, end - i, m_keyword);
		} else {
			setFormat(i, end - i, m_name);
		}
		i = end;
	}
	setCurrentBlockState(comment ? 1 : 0);
}

void MaterialScriptHighlighter::highlightJson(const QString& text)
{
	const int length = static_cast<int>(text.size());
	int i = 0;
	while (i < length) {
		const QChar c = text.at(i);
		if (c == QLatin1Char('"')) {
			int end = i + 1;
			while (end < length && text.at(end) != QLatin1Char('"')) {
				end += text.at(end) == QLatin1Char('\\') ? 2 : 1;
			}
			end = std::min(end, length - 1);
			int after = end + 1;
			while (after < length && text.at(after).isSpace()) {
				++after;
			}
			const bool key = after < length && text.at(after) == QLatin1Char(':');
			setFormat(i, end + 1 - i, key ? m_keyword : m_string);
			i = end + 1;
			continue;
		}
		if (c.isDigit() || c == QLatin1Char('-')) {
			int end = i + 1;
			while (end < length && (text.at(end).isDigit() || text.at(end) == QLatin1Char('.') || text.at(end) == QLatin1Char('e')
									   || text.at(end) == QLatin1Char('E') || text.at(end) == QLatin1Char('+') || text.at(end) == QLatin1Char('-'))) {
				++end;
			}
			setFormat(i, end - i, m_number);
			i = end;
			continue;
		}
		if (c.isLetter()) {
			int end = i;
			while (end < length && text.at(end).isLetter()) {
				++end;
			}
			setFormat(i, end - i, m_value);
			i = end;
			continue;
		}
		++i;
	}
}

void MaterialScriptHighlighter::underlineDiagnostics(const QString& text)
{
	const auto found = m_diagnostics.constFind(currentBlock().blockNumber() + 1);
	if (found == m_diagnostics.constEnd()) {
		return;
	}
	for (const MaterialDiagnostic& diagnostic : found.value()) {
		int start = 0;
		int span = static_cast<int>(text.size());
		if (diagnostic.column > 0) {
			start = std::min(static_cast<int>(text.size()), diagnostic.column - 1);
			span = diagnostic.length > 0 ? diagnostic.length : static_cast<int>(text.size()) - start;
		} else {
			// The whole line, without its indentation.
			while (start < text.size() && text.at(start).isSpace()) {
				++start;
			}
			span = static_cast<int>(text.size()) - start;
		}
		span = std::max(1, std::min(span, static_cast<int>(text.size()) - start));
		if (start >= text.size()) {
			continue;
		}
		for (int position = start; position < start + span; ++position) {
			QTextCharFormat merged = format(position);
			merged.setUnderlineStyle(QTextCharFormat::WaveUnderline);
			merged.setUnderlineColor(diagnostic.severity == MaterialDiagnosticSeverity::Error ? m_theme.errorUnderline : m_theme.warningUnderline);
			merged.setToolTip(diagnostic.message);
			setFormat(position, 1, merged);
		}
	}
}

} // namespace vibestudio
