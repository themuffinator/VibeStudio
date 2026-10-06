#include "app/syntax_highlight.h"
#include "core/code_files.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>

namespace vibestudio {

namespace {

// Shared regular expression fragments.
//
// Numbers deliberately allow a leading sign and an exponent so that .map plane
// coordinates and shader parameters read as numeric.
const char* const kNumberPattern = R"RX((?<![A-Za-z0-9_$.])[-+]?(?:0[xX][0-9A-Fa-f]+|\d+(?:\.\d+)?(?:[eE][-+]?\d+)?))RX";

// A double quoted run, tolerating an unterminated literal at end of line so a
// half-typed string does not bleed into the rest of the document.
const char* const kStringPattern = R"RX("(?:[^"\\\n]|\\.)*"?)RX";

QStringList escapedAlternatives(const QStringList& words)
{
	QStringList escaped;
	escaped.reserve(words.size());
	for (const QString& word : words) {
		if (word.isEmpty()) {
			continue;
		}
		escaped.append(QRegularExpression::escape(word));
	}
	// Longest first so that "set" cannot shadow "seta" in engines whose parser
	// is happy with either.
	std::stable_sort(escaped.begin(), escaped.end(), [](const QString& lhs, const QString& rhs) {
		return lhs.size() > rhs.size();
	});
	return escaped;
}

QString wordAlternationPattern(const QStringList& words)
{
	const QStringList escaped = escapedAlternatives(words);
	if (escaped.isEmpty()) {
		return QString();
	}
	return QStringLiteral("\\b(?:%1)\\b").arg(escaped.join(QLatin1Char('|')));
}

QString prefixAlternationPattern(const QStringList& prefixes)
{
	const QStringList escaped = escapedAlternatives(prefixes);
	if (escaped.isEmpty()) {
		return QString();
	}
	return QStringLiteral("\\b(?:%1)[A-Za-z0-9_]+\\b").arg(escaped.join(QLatin1Char('|')));
}

QString lineCommentPattern(const QStringList& tokens)
{
	const QStringList escaped = escapedAlternatives(tokens);
	if (escaped.isEmpty()) {
		return QString();
	}
	return QStringLiteral("(?:%1)[^\\n]*").arg(escaped.join(QLatin1Char('|')));
}

// Index of the first line-comment token in the block, or -1. Used so that a
// "/*" that appears inside a "//" comment does not open a block comment.
int firstLineCommentIndex(const QString& text, const QStringList& tokens)
{
	int best = -1;
	for (const QString& token : tokens) {
		if (token.isEmpty()) {
			continue;
		}
		const int index = static_cast<int>(text.indexOf(token));
		if (index >= 0 && (best < 0 || index < best)) {
			best = index;
		}
	}
	return best;
}

QTextCharFormat makeFormat(const QColor& color, bool bold = false, bool italic = false)
{
	QTextCharFormat format;
	if (color.isValid()) {
		format.setForeground(color);
	}
	if (bold) {
		format.setFontWeight(QFont::Bold);
	}
	if (italic) {
		format.setFontItalic(true);
	}
	return format;
}

bool themesEqual(const StudioSyntaxTheme& lhs, const StudioSyntaxTheme& rhs)
{
	return lhs.text == rhs.text
		&& lhs.comment == rhs.comment
		&& lhs.keyword == rhs.keyword
		&& lhs.secondaryKeyword == rhs.secondaryKeyword
		&& lhs.stringLiteral == rhs.stringLiteral
		&& lhs.number == rhs.number
		&& lhs.directive == rhs.directive
		&& lhs.identifier == rhs.identifier
		&& lhs.errorUnderline == rhs.errorUnderline
		&& lhs.warningUnderline == rhs.warningUnderline
		&& lhs.currentLine == rhs.currentLine
		&& lhs.highContrast == rhs.highContrast;
}

bool markersEqual(const StudioDiagnosticMarker& lhs, const StudioDiagnosticMarker& rhs)
{
	return lhs.line == rhs.line
		&& lhs.column == rhs.column
		&& lhs.length == rhs.length
		&& lhs.severity == rhs.severity
		&& lhs.message == rhs.message;
}

bool diagnosticsEqual(const QVector<StudioDiagnosticMarker>& lhs, const QVector<StudioDiagnosticMarker>& rhs)
{
	if (lhs.size() != rhs.size()) {
		return false;
	}
	for (int index = 0; index < lhs.size(); ++index) {
		if (!markersEqual(lhs.at(index), rhs.at(index))) {
			return false;
		}
	}
	return true;
}

StudioLanguageDescriptor makeConfigDescriptor()
{
	// Quake/Quake II/Quake III console scripts. Command set follows the public
	// id Tech console documentation shared by the idTech2/idTech3 engines.
	StudioLanguageDescriptor descriptor;
	descriptor.language = StudioLanguage::Config;
	descriptor.id = QStringLiteral("config");
	descriptor.displayName = QCoreApplication::translate("VibeStudioSyntaxHighlight", "Console Config");
	descriptor.extensions = {
		QStringLiteral(".cfg"),
		QStringLiteral(".rc"),
		QStringLiteral(".scr"),
		QStringLiteral("autoexec.cfg"),
		QStringLiteral("config.cfg"),
		QStringLiteral("q3config.cfg"),
		QStringLiteral("default.cfg"),
	};
	descriptor.lineCommentTokens = { QStringLiteral("//"), QStringLiteral(";") };
	descriptor.keywords = {
		QStringLiteral("bind"), QStringLiteral("unbind"), QStringLiteral("set"), QStringLiteral("seta"),
		QStringLiteral("sets"), QStringLiteral("setu"), QStringLiteral("alias"), QStringLiteral("exec"),
		QStringLiteral("echo"), QStringLiteral("wait"), QStringLiteral("vstr"), QStringLiteral("toggle"),
		QStringLiteral("cvar_restart"), QStringLiteral("bindlist"), QStringLiteral("unbindall"),
		QStringLiteral("map"), QStringLiteral("devmap"), QStringLiteral("connect"), QStringLiteral("quit"),
	};
	// Secondary keywords are cvar namespace prefixes, matched as whole cvars.
	descriptor.secondaryKeywords = {
		QStringLiteral("cg_"), QStringLiteral("r_"), QStringLiteral("cl_"), QStringLiteral("sv_"),
		QStringLiteral("com_"), QStringLiteral("g_"), QStringLiteral("ui_"), QStringLiteral("snd_"),
		QStringLiteral("s_"), QStringLiteral("in_"), QStringLiteral("net_"),
	};
	descriptor.caseSensitiveKeywords = false;
	return descriptor;
}

StudioLanguageDescriptor makeShaderDescriptor()
{
	// idTech3 material scripts. Directive names follow the Quake III Arena
	// Shader Manual (id Software, 1999) and the q3map2 shader key reference:
	// https://q3map2.robotrenegade.com/docs/shader_manual/
	StudioLanguageDescriptor descriptor;
	descriptor.language = StudioLanguage::ShaderScript;
	descriptor.id = QStringLiteral("shader");
	descriptor.displayName = QCoreApplication::translate("VibeStudioSyntaxHighlight", "idTech3 Shader");
	descriptor.extensions = { QStringLiteral(".shader") };
	descriptor.lineCommentTokens = { QStringLiteral("//") };
	descriptor.keywords = {
		QStringLiteral("surfaceparm"), QStringLiteral("cull"), QStringLiteral("sort"),
		QStringLiteral("nopicmip"), QStringLiteral("nomipmaps"), QStringLiteral("polygonOffset"),
		QStringLiteral("portal"), QStringLiteral("fogparms"), QStringLiteral("skyparms"),
		QStringLiteral("deformVertexes"), QStringLiteral("light"), QStringLiteral("entityMergable"),
		QStringLiteral("qer_editorimage"), QStringLiteral("qer_trans"), QStringLiteral("qer_nocarve"),
		QStringLiteral("qer_keyword"), QStringLiteral("cloudparms"), QStringLiteral("noshadows"),
		QStringLiteral("q3map_surfacelight"), QStringLiteral("q3map_lightimage"), QStringLiteral("q3map_sun"),
		QStringLiteral("q3map_sunExt"), QStringLiteral("q3map_lightsubdivide"), QStringLiteral("q3map_backsplash"),
		QStringLiteral("q3map_globaltexture"), QStringLiteral("q3map_tessSize"), QStringLiteral("q3map_nolightmap"),
		QStringLiteral("q3map_novertexshadows"), QStringLiteral("q3map_forcesunlight"),
		QStringLiteral("q3map_bounceScale"), QStringLiteral("q3map_lightRGB"), QStringLiteral("q3map_flare"),
		QStringLiteral("q3map_material"), QStringLiteral("q3map_shadeAngle"), QStringLiteral("q3map_alphaMod"),
		QStringLiteral("q3map_clipModel"), QStringLiteral("q3map_onlyVertexLighting"),
		QStringLiteral("q3map_lightmapSampleSize"), QStringLiteral("q3map_lightmapSampleOffset"),
		QStringLiteral("q3map_nonplanar"), QStringLiteral("q3map_splotchfix"), QStringLiteral("q3map_skyLight"),
		QStringLiteral("q3map_offset"), QStringLiteral("q3map_texturesize"),
	};
	descriptor.secondaryKeywords = {
		QStringLiteral("map"), QStringLiteral("clampmap"), QStringLiteral("animMap"),
		QStringLiteral("videoMap"), QStringLiteral("blendFunc"), QStringLiteral("alphaFunc"),
		QStringLiteral("depthFunc"), QStringLiteral("depthWrite"), QStringLiteral("detail"),
		QStringLiteral("rgbGen"), QStringLiteral("alphaGen"), QStringLiteral("tcGen"),
		QStringLiteral("tcMod"),
		QStringLiteral("GL_ONE"), QStringLiteral("GL_ZERO"), QStringLiteral("GL_DST_COLOR"),
		QStringLiteral("GL_SRC_COLOR"), QStringLiteral("GL_ONE_MINUS_DST_COLOR"),
		QStringLiteral("GL_ONE_MINUS_SRC_COLOR"), QStringLiteral("GL_SRC_ALPHA"),
		QStringLiteral("GL_ONE_MINUS_SRC_ALPHA"), QStringLiteral("GL_DST_ALPHA"),
		QStringLiteral("GL_ONE_MINUS_DST_ALPHA"), QStringLiteral("GL_SRC_ALPHA_SATURATE"),
	};
	descriptor.caseSensitiveKeywords = false;
	return descriptor;
}

StudioLanguageDescriptor makeQuakeCDescriptor()
{
	// QuakeC as described by the Quake-C Manual (id Software / community) and
	// the qcc/fteqcc language references.
	StudioLanguageDescriptor descriptor;
	descriptor.language = StudioLanguage::QuakeC;
	descriptor.id = QStringLiteral("quakec");
	descriptor.displayName = QCoreApplication::translate("VibeStudioSyntaxHighlight", "QuakeC");
	descriptor.extensions = { QStringLiteral(".qc"), QStringLiteral("progs.src") };
	descriptor.lineCommentTokens = { QStringLiteral("//") };
	descriptor.blockCommentStart = QStringLiteral("/*");
	descriptor.blockCommentEnd = QStringLiteral("*/");
	descriptor.keywords = {
		QStringLiteral("void"), QStringLiteral("float"), QStringLiteral("vector"), QStringLiteral("string"),
		QStringLiteral("entity"), QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("while"),
		QStringLiteral("do"), QStringLiteral("for"), QStringLiteral("local"), QStringLiteral("return"),
		QStringLiteral("break"), QStringLiteral("continue"), QStringLiteral("switch"), QStringLiteral("case"),
		QStringLiteral("default"), QStringLiteral("nosave"), QStringLiteral("var"),
	};
	descriptor.secondaryKeywords = {
		QStringLiteral("self"), QStringLiteral("other"), QStringLiteral("world"), QStringLiteral("time"),
		QStringLiteral("frametime"), QStringLiteral("setmodel"), QStringLiteral("setorigin"),
		QStringLiteral("setsize"), QStringLiteral("sound"), QStringLiteral("spawn"), QStringLiteral("remove"),
		QStringLiteral("makevectors"), QStringLiteral("traceline"), QStringLiteral("precache_model"),
		QStringLiteral("precache_sound"), QStringLiteral("bprint"), QStringLiteral("sprint"),
		QStringLiteral("centerprint"), QStringLiteral("cvar"), QStringLiteral("dprint"),
		QStringLiteral("vlen"), QStringLiteral("normalize"), QStringLiteral("random"),
		QStringLiteral("droptofloor"), QStringLiteral("walkmove"), QStringLiteral("findradius"),
		QStringLiteral("objerror"), QStringLiteral("error"),
	};
	descriptor.caseSensitiveKeywords = true;
	return descriptor;
}

StudioLanguageDescriptor makeMapSourceDescriptor()
{
	// Quake/Quake II/Quake III .map text format: entity blocks of quoted
	// key/value pairs plus brushes built from "( x y z )" plane points followed
	// by a texture name. See the Quake Map Format specification (Quake Standards
	// Group) and the Radiant brushDef/patchDef extensions.
	StudioLanguageDescriptor descriptor;
	descriptor.language = StudioLanguage::MapSource;
	descriptor.id = QStringLiteral("map-source");
	descriptor.displayName = QCoreApplication::translate("VibeStudioSyntaxHighlight", "Map Source");
	descriptor.extensions = { QStringLiteral(".map") };
	descriptor.lineCommentTokens = { QStringLiteral("//") };
	descriptor.keywords = {
		QStringLiteral("brushDef"), QStringLiteral("brushDef3"), QStringLiteral("patchDef2"),
		QStringLiteral("patchDef3"), QStringLiteral("terrainDef"),
	};
	descriptor.secondaryKeywords = {
		QStringLiteral("classname"), QStringLiteral("origin"), QStringLiteral("target"),
		QStringLiteral("targetname"), QStringLiteral("angle"), QStringLiteral("angles"),
		QStringLiteral("spawnflags"), QStringLiteral("model"), QStringLiteral("message"),
	};
	descriptor.caseSensitiveKeywords = false;
	return descriptor;
}

StudioLanguageDescriptor makeEntityDefDescriptor()
{
	// Entity definitions: QuakeED "/*QUAKED" blocks (.def/.ent) and Valve/Half-Life
	// style forge game data (.fgd) class declarations.
	StudioLanguageDescriptor descriptor;
	descriptor.language = StudioLanguage::EntityDef;
	descriptor.id = QStringLiteral("entity-def");
	descriptor.displayName = QCoreApplication::translate("VibeStudioSyntaxHighlight", "Entity Definitions");
	descriptor.extensions = { QStringLiteral(".def"), QStringLiteral(".fgd"), QStringLiteral(".ent") };
	descriptor.lineCommentTokens = { QStringLiteral("//") };
	descriptor.keywords = {
		QStringLiteral("QUAKED"), QStringLiteral("@baseclass"), QStringLiteral("@BaseClass"),
		QStringLiteral("@PointClass"), QStringLiteral("@SolidClass"), QStringLiteral("@NPCClass"),
		QStringLiteral("@MoveClass"), QStringLiteral("@FilterClass"), QStringLiteral("@KeyFrameClass"),
		QStringLiteral("@OverrideClass"), QStringLiteral("@include"), QStringLiteral("@mapsize"),
		QStringLiteral("@MaterialExclusion"), QStringLiteral("entityDef"), QStringLiteral("inherit"),
	};
	descriptor.secondaryKeywords = {
		QStringLiteral("base"), QStringLiteral("color"), QStringLiteral("size"), QStringLiteral("iconsprite"),
		QStringLiteral("studio"), QStringLiteral("sprite"), QStringLiteral("model"), QStringLiteral("flags"),
		QStringLiteral("choices"), QStringLiteral("string"), QStringLiteral("integer"), QStringLiteral("float"),
		QStringLiteral("target_source"), QStringLiteral("target_destination"), QStringLiteral("color255"),
		QStringLiteral("spawnflags"), QStringLiteral("angle"), QStringLiteral("readonly"),
	};
	descriptor.caseSensitiveKeywords = false;
	return descriptor;
}

StudioLanguageDescriptor makeIniDescriptor()
{
	StudioLanguageDescriptor descriptor;
	descriptor.language = StudioLanguage::Ini;
	descriptor.id = QStringLiteral("ini");
	descriptor.displayName = QCoreApplication::translate("VibeStudioSyntaxHighlight", "Key/Value Script");
	descriptor.extensions = {
		QStringLiteral(".ini"), QStringLiteral(".arena"), QStringLiteral(".menu"),
		QStringLiteral(".bot"), QStringLiteral(".conf"),
	};
	descriptor.lineCommentTokens = { QStringLiteral(";"), QStringLiteral("#"), QStringLiteral("//") };
	descriptor.keywords = {
		QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("yes"), QStringLiteral("no"),
		QStringLiteral("on"), QStringLiteral("off"), QStringLiteral("enabled"), QStringLiteral("disabled"),
	};
	descriptor.caseSensitiveKeywords = false;
	return descriptor;
}

StudioLanguageDescriptor makeJsonDescriptor()
{
	StudioLanguageDescriptor descriptor;
	descriptor.language = StudioLanguage::Json;
	descriptor.id = QStringLiteral("json");
	descriptor.displayName = QCoreApplication::translate("VibeStudioSyntaxHighlight", "JSON");
	descriptor.extensions = { QStringLiteral(".json"), QStringLiteral(".vsproj"), QStringLiteral(".vsmanifest") };
	descriptor.keywords = { QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("null") };
	descriptor.caseSensitiveKeywords = true;
	return descriptor;
}

StudioLanguageDescriptor makePlainTextDescriptor()
{
	StudioLanguageDescriptor descriptor;
	descriptor.language = StudioLanguage::PlainText;
	descriptor.id = QStringLiteral("plain-text");
	descriptor.displayName = QCoreApplication::translate("VibeStudioSyntaxHighlight", "Plain Text");
	descriptor.extensions = { QStringLiteral(".txt"), QStringLiteral(".log"), QStringLiteral(".md") };
	return descriptor;
}

QVector<StudioLanguageDescriptor> buildDescriptors()
{
	QVector<StudioLanguageDescriptor> descriptors;
	descriptors.append(makePlainTextDescriptor());
	descriptors.append(makeConfigDescriptor());
	descriptors.append(makeShaderDescriptor());
	descriptors.append(makeQuakeCDescriptor());
	descriptors.append(makeMapSourceDescriptor());
	descriptors.append(makeEntityDefDescriptor());
	descriptors.append(makeIniDescriptor());
	descriptors.append(makeJsonDescriptor());
	return descriptors;
}

} // namespace

QVector<StudioLanguageDescriptor> studioLanguageDescriptors()
{
	// Rebuilt per call so display names follow a language change at runtime.
	return buildDescriptors();
}

bool studioLanguageDescriptorFor(StudioLanguage language, StudioLanguageDescriptor* out)
{
	const QVector<StudioLanguageDescriptor> descriptors = buildDescriptors();
	for (const StudioLanguageDescriptor& descriptor : descriptors) {
		if (descriptor.language == language) {
			if (out != nullptr) {
				*out = descriptor;
			}
			return true;
		}
	}
	if (out != nullptr) {
		*out = makePlainTextDescriptor();
	}
	return false;
}

StudioLanguage studioLanguageForPath(const QString& path)
{
	return studioLanguageFromId(codeFileLanguageId(path));
}

StudioLanguage studioLanguageFromId(const QString& id)
{
	const QString normalized = id.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
	if (normalized.isEmpty()) {
		return StudioLanguage::PlainText;
	}
	const QVector<StudioLanguageDescriptor> descriptors = buildDescriptors();
	for (const StudioLanguageDescriptor& descriptor : descriptors) {
		if (descriptor.id == normalized) {
			return descriptor.language;
		}
	}
	// Tolerated aliases so stored settings and CLI arguments stay forgiving.
	if (normalized == QStringLiteral("text") || normalized == QStringLiteral("plaintext")
		|| normalized == QStringLiteral("plain")) {
		return StudioLanguage::PlainText;
	}
	if (normalized == QStringLiteral("cfg") || normalized == QStringLiteral("console")) {
		return StudioLanguage::Config;
	}
	if (normalized == QStringLiteral("shader-script") || normalized == QStringLiteral("material")) {
		return StudioLanguage::ShaderScript;
	}
	if (normalized == QStringLiteral("quake-c") || normalized == QStringLiteral("qc")) {
		return StudioLanguage::QuakeC;
	}
	if (normalized == QStringLiteral("map") || normalized == QStringLiteral("mapsource")) {
		return StudioLanguage::MapSource;
	}
	if (normalized == QStringLiteral("def") || normalized == QStringLiteral("fgd")
		|| normalized == QStringLiteral("entitydef")) {
		return StudioLanguage::EntityDef;
	}
	return StudioLanguage::PlainText;
}

QString studioLanguageId(StudioLanguage language)
{
	StudioLanguageDescriptor descriptor;
	studioLanguageDescriptorFor(language, &descriptor);
	return descriptor.id;
}

QString studioLanguageDisplayName(StudioLanguage language)
{
	StudioLanguageDescriptor descriptor;
	studioLanguageDescriptorFor(language, &descriptor);
	return descriptor.displayName;
}

StudioSyntaxTheme studioSyntaxTheme(bool lightTheme, bool highContrast)
{
	StudioSyntaxTheme theme;
	theme.highContrast = highContrast;

	if (highContrast && lightTheme) {
		// Pure white background: saturated dark hues, reinforced with weight.
		theme.text = QColor(QStringLiteral("#000000"));
		theme.comment = QColor(QStringLiteral("#005a00"));
		theme.keyword = QColor(QStringLiteral("#0000cc"));
		theme.secondaryKeyword = QColor(QStringLiteral("#6a007a"));
		theme.stringLiteral = QColor(QStringLiteral("#a00000"));
		theme.number = QColor(QStringLiteral("#00494f"));
		theme.directive = QColor(QStringLiteral("#7a3d00"));
		theme.identifier = QColor(QStringLiteral("#000000"));
		theme.errorUnderline = QColor(QStringLiteral("#a00000"));
		theme.warningUnderline = QColor(QStringLiteral("#7a3d00"));
		theme.currentLine = QColor(QStringLiteral("#dfe6ff"));
		return theme;
	}

	if (highContrast) {
		// Pure black background: bright hues, reinforced with weight.
		theme.text = QColor(QStringLiteral("#ffffff"));
		theme.comment = QColor(QStringLiteral("#00ff7f"));
		theme.keyword = QColor(QStringLiteral("#ffd800"));
		theme.secondaryKeyword = QColor(QStringLiteral("#66d9ff"));
		theme.stringLiteral = QColor(QStringLiteral("#ff9de2"));
		theme.number = QColor(QStringLiteral("#7fffd4"));
		theme.directive = QColor(QStringLiteral("#ffa040"));
		theme.identifier = QColor(QStringLiteral("#ffffff"));
		theme.errorUnderline = QColor(QStringLiteral("#ff6b6b"));
		theme.warningUnderline = QColor(QStringLiteral("#ffd800"));
		theme.currentLine = QColor(QStringLiteral("#303030"));
		return theme;
	}

	if (lightTheme) {
		theme.text = QColor(QStringLiteral("#16202c"));
		theme.comment = QColor(QStringLiteral("#2e7d32"));
		theme.keyword = QColor(QStringLiteral("#0b5fa5"));
		theme.secondaryKeyword = QColor(QStringLiteral("#6a2fa0"));
		theme.stringLiteral = QColor(QStringLiteral("#a33a0f"));
		theme.number = QColor(QStringLiteral("#1a6b52"));
		theme.directive = QColor(QStringLiteral("#8a5a00"));
		theme.identifier = QColor(QStringLiteral("#43505f"));
		theme.errorUnderline = QColor(QStringLiteral("#a00000"));
		theme.warningUnderline = QColor(QStringLiteral("#b35c00"));
		theme.currentLine = QColor(QStringLiteral("#eef3fa"));
		return theme;
	}

	theme.text = QColor(QStringLiteral("#d6dde6"));
	theme.comment = QColor(QStringLiteral("#7f9f76"));
	theme.keyword = QColor(QStringLiteral("#7ab8ff"));
	theme.secondaryKeyword = QColor(QStringLiteral("#c792ea"));
	theme.stringLiteral = QColor(QStringLiteral("#e5a76a"));
	theme.number = QColor(QStringLiteral("#b5cea8"));
	theme.directive = QColor(QStringLiteral("#ffd479"));
	theme.identifier = QColor(QStringLiteral("#9daab8"));
	theme.errorUnderline = QColor(QStringLiteral("#ff7a7a"));
	theme.warningUnderline = QColor(QStringLiteral("#ffcf70"));
	theme.currentLine = QColor(QStringLiteral("#1f2732"));
	return theme;
}

StudioSyntaxHighlighter::StudioSyntaxHighlighter(QTextDocument* document)
	: QSyntaxHighlighter(document)
{
	m_theme = studioSyntaxTheme(false, false);
	studioLanguageDescriptorFor(m_language, &m_descriptor);
	rebuildRules();
}

void StudioSyntaxHighlighter::setLanguage(StudioLanguage language)
{
	if (m_language == language) {
		return;
	}
	m_language = language;
	studioLanguageDescriptorFor(m_language, &m_descriptor);
	rebuildRules();
	rehighlight();
}

StudioLanguage StudioSyntaxHighlighter::language() const
{
	return m_language;
}

void StudioSyntaxHighlighter::setTheme(const StudioSyntaxTheme& theme)
{
	if (themesEqual(m_theme, theme)) {
		return;
	}
	m_theme = theme;
	rebuildRules();
	rehighlight();
}

void StudioSyntaxHighlighter::setDiagnostics(const QVector<StudioDiagnosticMarker>& diagnostics)
{
	if (diagnosticsEqual(m_diagnostics, diagnostics)) {
		return;
	}
	m_diagnostics = diagnostics;
	rehighlightMarkedLines();
}

void StudioSyntaxHighlighter::clearDiagnostics()
{
	if (m_diagnostics.isEmpty()) {
		return;
	}
	m_diagnostics.clear();
	rehighlightMarkedLines();
}

void StudioSyntaxHighlighter::rehighlightMarkedLines()
{
	// Only the lines that gain or lose a mark: highlighting the whole document
	// again took seconds in a large file each time a problem moved a line. A
	// line marked before is found by its cursor, not its number, since an edit
	// above it moves it without highlighting it again.
	QTextDocument* text = document();
	if (!text) {
		m_markedLines.clear();
		return;
	}
	QVector<QTextBlock> blocks;
	for (const QTextCursor& cursor : std::as_const(m_markedLines)) {
		if (!cursor.isNull() && cursor.document() == text) {
			blocks << cursor.block();
		}
	}
	m_markedLines.clear();
	for (const StudioDiagnosticMarker& marker : std::as_const(m_diagnostics)) {
		const QTextBlock block = text->findBlockByNumber(marker.line - 1);
		if (block.isValid()) {
			blocks << block;
			m_markedLines << QTextCursor(block);
		}
	}
	QSet<int> done;
	for (const QTextBlock& block : std::as_const(blocks)) {
		if (block.isValid() && !done.contains(block.blockNumber())) {
			done.insert(block.blockNumber());
			rehighlightBlock(block);
		}
	}
}

QVector<StudioDiagnosticMarker> StudioSyntaxHighlighter::diagnostics() const
{
	return m_diagnostics;
}

void StudioSyntaxHighlighter::rebuildRules()
{
	// Called once per language/theme change. Rules are applied in order and a
	// later rule wins for overlapping ranges, so structural rules come first and
	// strings/comments come last.
	m_rules.clear();
	m_blockCommentStart = QRegularExpression();
	m_blockCommentEnd = QRegularExpression();
	m_hasBlockComments = false;

	const bool hc = m_theme.highContrast;
	const QTextCharFormat keywordFormat = makeFormat(m_theme.keyword, true, false);
	const QTextCharFormat secondaryFormat = makeFormat(m_theme.secondaryKeyword, hc, hc);
	const QTextCharFormat stringFormat = makeFormat(m_theme.stringLiteral, false, hc);
	const QTextCharFormat numberFormat = makeFormat(m_theme.number, false, false);
	const QTextCharFormat directiveFormat = makeFormat(m_theme.directive, true, false);
	const QTextCharFormat identifierFormat = makeFormat(m_theme.identifier, false, false);
	m_commentFormat = makeFormat(m_theme.comment, false, true);

	const QRegularExpression::PatternOptions keywordOptions = m_descriptor.caseSensitiveKeywords
		? QRegularExpression::NoPatternOption
		: QRegularExpression::CaseInsensitiveOption;

	const auto addRule = [this](const QString& pattern,
		const QTextCharFormat& format,
		int captureGroup = 0,
		QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption) {
		if (pattern.isEmpty()) {
			return;
		}
		QRegularExpression expression(pattern, options);
		if (!expression.isValid()) {
			return;
		}
		expression.optimize();
		Rule rule;
		rule.pattern = expression;
		rule.format = format;
		rule.captureGroup = captureGroup;
		m_rules.append(rule);
	};

	// --- Language specific structure -------------------------------------
	switch (m_language) {
	case StudioLanguage::Config:
		addRule(prefixAlternationPattern(m_descriptor.secondaryKeywords), secondaryFormat, 0, keywordOptions);
		addRule(wordAlternationPattern(m_descriptor.keywords), keywordFormat, 0, keywordOptions);
		// "+attack" / "-moveup" style key actions.
		addRule(QStringLiteral(R"RX((?<![\w+-])[+-][A-Za-z_][A-Za-z0-9_]*)RX"), directiveFormat);
		break;
	case StudioLanguage::ShaderScript:
		// Shader/texture paths such as "textures/base_wall/foo".
		addRule(QStringLiteral(R"RX([A-Za-z0-9_\-]+(?:/[A-Za-z0-9_\-.]+)+)RX"), identifierFormat);
		addRule(wordAlternationPattern(m_descriptor.secondaryKeywords), secondaryFormat, 0, keywordOptions);
		addRule(QStringLiteral(R"RX(\bGL_[A-Z0-9_]+\b)RX"), secondaryFormat);
		addRule(wordAlternationPattern(m_descriptor.keywords), keywordFormat, 0, keywordOptions);
		// The q3map_* family is open ended; match anything in the namespace.
		addRule(QStringLiteral(R"RX(\bq3map_[A-Za-z0-9_]+\b)RX"), keywordFormat, 0,
			QRegularExpression::CaseInsensitiveOption);
		addRule(QStringLiteral(R"RX([{}])RX"), directiveFormat);
		// Engine supplied image/vertex tokens: $lightmap, $whiteimage, $identity.
		addRule(QStringLiteral(R"RX(\$[A-Za-z_][A-Za-z0-9_]*)RX"), directiveFormat);
		break;
	case StudioLanguage::QuakeC:
		addRule(wordAlternationPattern(m_descriptor.secondaryKeywords), secondaryFormat, 0, keywordOptions);
		addRule(wordAlternationPattern(m_descriptor.keywords), keywordFormat, 0, keywordOptions);
		// Frame macros and model directives: $frame, $modelname, ...
		addRule(QStringLiteral(R"RX(\$[A-Za-z_][A-Za-z0-9_]*)RX"), directiveFormat);
		// Vector literals are single quoted in QuakeC: '0 0 0'.
		addRule(QStringLiteral(R"RX('[^'\n]*')RX"), numberFormat);
		break;
	case StudioLanguage::MapSource:
		// Plane points and patch control points: ( x y z ).
		addRule(QStringLiteral(R"RX([()\[\]{}])RX"), identifierFormat);
		// Texture / shader name directly after a plane's closing bracket.
		addRule(QStringLiteral(R"RX(\)\s*([A-Za-z0-9_\-/\\.+*]+))RX"), identifierFormat, 1);
		addRule(wordAlternationPattern(m_descriptor.keywords), keywordFormat, 0, keywordOptions);
		addRule(wordAlternationPattern(m_descriptor.secondaryKeywords), secondaryFormat, 0, keywordOptions);
		break;
	case StudioLanguage::EntityDef:
		// QuakeED .def entity blocks open with "/*QUAKED" and close with "*/";
		// the body is content, not a comment, so it is not treated as one.
		addRule(QStringLiteral(R"RX(/\*\s*QUAKED)RX"), keywordFormat, 0, QRegularExpression::CaseInsensitiveOption);
		addRule(QStringLiteral(R"RX(\*/)RX"), keywordFormat);
		addRule(QStringLiteral(R"RX(@[A-Za-z_][A-Za-z0-9_]*)RX"), keywordFormat);
		addRule(wordAlternationPattern(m_descriptor.secondaryKeywords), secondaryFormat, 0, keywordOptions);
		addRule(wordAlternationPattern(m_descriptor.keywords), keywordFormat, 0, keywordOptions);
		// Key names in "key(type) : "Label"" declarations.
		addRule(QStringLiteral(R"RX(\b([A-Za-z_][A-Za-z0-9_]*)\s*\()RX"), directiveFormat, 1);
		break;
	case StudioLanguage::Ini:
		addRule(QStringLiteral(R"RX(^\s*([A-Za-z_][A-Za-z0-9_.\-]*)\s*(?=[=:]))RX"), directiveFormat, 1);
		addRule(QStringLiteral(R"RX(^\s*\[[^\]\n]*\])RX"), keywordFormat);
		addRule(wordAlternationPattern(m_descriptor.keywords), secondaryFormat, 0, keywordOptions);
		break;
	case StudioLanguage::Json:
		addRule(QStringLiteral(R"RX([{}\[\],:])RX"), identifierFormat);
		addRule(wordAlternationPattern(m_descriptor.keywords), keywordFormat, 0, keywordOptions);
		break;
	case StudioLanguage::PlainText:
		break;
	}

	// --- Shared rules -----------------------------------------------------
	if (m_language != StudioLanguage::PlainText) {
		addRule(QString::fromLatin1(kNumberPattern), numberFormat);
	}

	if (m_language == StudioLanguage::Json) {
		// Object keys read as directives, values as strings.
		addRule(QString::fromLatin1(kStringPattern), stringFormat);
		addRule(QStringLiteral(R"RX(("(?:[^"\\\n]|\\.)*")\s*:)RX"), directiveFormat, 1);
	} else if (m_language != StudioLanguage::PlainText && m_language != StudioLanguage::MapSource) {
		addRule(QString::fromLatin1(kStringPattern), stringFormat);
	} else if (m_language == StudioLanguage::MapSource) {
		// Lone quoted runs (a key/value pair split across lines) still read as strings.
		addRule(QStringLiteral(R"RX("[^"\n]*")RX"), stringFormat);
		// Quoted key/value pairs: the key reads as a directive, the value as a string.
		addRule(QStringLiteral(R"RX("([^"\n]*)"\s+"([^"\n]*)")RX"), stringFormat, 2);
		addRule(QStringLiteral(R"RX("([^"\n]*)"\s+"([^"\n]*)")RX"), directiveFormat, 1);
	}

	// Comments win over everything above.
	addRule(lineCommentPattern(m_descriptor.lineCommentTokens), m_commentFormat);

	if (!m_descriptor.blockCommentStart.isEmpty() && !m_descriptor.blockCommentEnd.isEmpty()) {
		m_blockCommentStart = QRegularExpression(QRegularExpression::escape(m_descriptor.blockCommentStart));
		m_blockCommentEnd = QRegularExpression(QRegularExpression::escape(m_descriptor.blockCommentEnd));
		m_blockCommentStart.optimize();
		m_blockCommentEnd.optimize();
		m_hasBlockComments = m_blockCommentStart.isValid() && m_blockCommentEnd.isValid();
	}
}

void StudioSyntaxHighlighter::highlightBlock(const QString& text)
{
	const int length = text.length();
	if (length > 0 && m_theme.text.isValid()) {
		setFormat(0, length, makeFormat(m_theme.text));
	}

	for (const Rule& rule : m_rules) {
		QRegularExpressionMatchIterator iterator = rule.pattern.globalMatch(text);
		while (iterator.hasNext()) {
			const QRegularExpressionMatch match = iterator.next();
			const int group = rule.captureGroup;
			if (group > match.lastCapturedIndex()) {
				continue;
			}
			const int start = static_cast<int>(match.capturedStart(group));
			const int matched = static_cast<int>(match.capturedLength(group));
			if (start < 0 || matched <= 0 || start >= length) {
				continue;
			}
			setFormat(start, std::min(matched, length - start), rule.format);
		}
	}

	setCurrentBlockState(0);
	if (m_hasBlockComments) {
		// A "/*" that sits inside a line comment must not open a block comment.
		const int lineCommentStart = firstLineCommentIndex(text, m_descriptor.lineCommentTokens);

		int startIndex = 0;
		if (previousBlockState() != 1) {
			startIndex = static_cast<int>(text.indexOf(m_blockCommentStart));
			if (startIndex >= 0 && lineCommentStart >= 0 && startIndex > lineCommentStart) {
				startIndex = -1;
			}
		}

		while (startIndex >= 0 && startIndex <= length) {
			const QRegularExpressionMatch endMatch = m_blockCommentEnd.match(text, startIndex);
			int commentLength = 0;
			if (!endMatch.hasMatch()) {
				setCurrentBlockState(1);
				commentLength = length - startIndex;
			} else {
				commentLength = static_cast<int>(endMatch.capturedEnd()) - startIndex;
			}
			if (commentLength <= 0) {
				break;
			}
			commentLength = std::min(commentLength, length - startIndex);
			setFormat(startIndex, commentLength, m_commentFormat);

			const int searchFrom = startIndex + commentLength;
			if (searchFrom >= length) {
				break;
			}
			startIndex = static_cast<int>(text.indexOf(m_blockCommentStart, searchFrom));
			if (startIndex >= 0 && lineCommentStart >= searchFrom && startIndex > lineCommentStart) {
				startIndex = -1;
			}
		}
	}

	applyDiagnostics(text);
}

void StudioSyntaxHighlighter::applyDiagnostics(const QString& text)
{
	if (m_diagnostics.isEmpty()) {
		return;
	}
	const int length = text.length();
	if (length <= 0) {
		return;
	}
	const int lineNumber = currentBlock().blockNumber() + 1;

	for (const StudioDiagnosticMarker& marker : m_diagnostics) {
		if (marker.line != lineNumber) {
			continue;
		}

		// Column 0 (or out of range) means the whole line; length 0 means "to
		// end of line". Everything is clamped to the block we actually have.
		int start = marker.column > 0 ? marker.column - 1 : 0;
		if (start < 0) {
			start = 0;
		}
		if (start >= length) {
			start = length - 1;
		}
		int span = marker.length > 0 ? marker.length : length - start;
		if (span <= 0) {
			continue;
		}
		span = std::min(span, length - start);
		if (span <= 0) {
			continue;
		}

		const bool isError = marker.severity.compare(QStringLiteral("error"), Qt::CaseInsensitive) == 0;
		QColor underline = isError ? m_theme.errorUnderline : m_theme.warningUnderline;
		if (!underline.isValid()) {
			underline = m_theme.text;
		}

		QString tip = marker.message.trimmed();
		if (tip.isEmpty()) {
			tip = isError ? tr("Error") : tr("Warning");
		} else if (isError) {
			tip = tr("Error: %1").arg(tip);
		} else if (marker.severity.compare(QStringLiteral("warning"), Qt::CaseInsensitive) == 0) {
			tip = tr("Warning: %1").arg(tip);
		}

		// Merge with the syntax colour already applied instead of replacing it.
		for (int index = start; index < start + span; ++index) {
			QTextCharFormat merged = format(index);
			merged.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
			if (underline.isValid()) {
				merged.setUnderlineColor(underline);
			}
			merged.setToolTip(tip);
			setFormat(index, 1, merged);
		}
	}
}

} // namespace vibestudio
