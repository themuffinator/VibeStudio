#include "core/advanced_studio.h"
#include "core/code_files.h"

#include "core/compiler_profiles.h"
#include "core/text_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringDecoder>
#include <QUuid>

#include <algorithm>

namespace vibestudio {

namespace {

QString normalizedId(QString value)
{
	value = value.trimmed().toLower();
	value.replace('_', '-');
	return value;
}

QString normalizedVirtualPath(QString value)
{
	value = value.trimmed();
	value.replace('\\', '/');
	while (value.startsWith(QStringLiteral("./"))) {
		value.remove(0, 2);
	}
	return value;
}

QString sanitizePathToken(QString value)
{
	value = value.trimmed().toLower();
	value.replace(QRegularExpression(QStringLiteral("[^a-z0-9_/-]+")), QStringLiteral("-"));
	value.replace(QRegularExpression(QStringLiteral("-+")), QStringLiteral("-"));
	value = value.trimmed();
	if (value.isEmpty()) {
		return QStringLiteral("generated");
	}
	return value.left(48);
}

QString safeSpritePrefix(QString prefix)
{
	prefix = prefix.trimmed().toUpper();
	prefix.replace(QRegularExpression(QStringLiteral("[^A-Z0-9]")), QString());
	if (prefix.isEmpty()) {
		prefix = QStringLiteral("SPRT");
	}
	return prefix.leftJustified(4, QLatin1Char('X'), true).left(4);
}

QString safeShaderName(const QString& prompt)
{
	QString token = sanitizePathToken(prompt);
	token.replace(' ', '-');
	if (token.contains('/')) {
		token = QFileInfo(token).fileName();
	}
	return QStringLiteral("textures/vibestudio/%1").arg(token.isEmpty() ? QStringLiteral("generated") : token.left(32));
}

QString issueLine(const AdvancedStudioIssue& issue)
{
	return QStringLiteral("%1 [%2] %3%4")
		.arg(issue.severity, issue.code.isEmpty() ? QStringLiteral("note") : issue.code, issue.message)
		.arg(issue.line > 0 ? QCoreApplication::translate("VibeStudioAdvancedStudio", " (line %1)").arg(issue.line) : QString());
}

bool decodeTextFile(const QString& path, QString* out, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!out) {
		return false;
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAdvancedStudio", "Unable to open file.");
		}
		return false;
	}
	const QByteArray bytes = file.readAll();
	QStringDecoder decoder(QStringDecoder::Utf8);
	const QString decoded = decoder.decode(bytes);
	if (decoder.hasError()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAdvancedStudio", "File is not valid UTF-8 text.");
		}
		return false;
	}
	*out = decoded;
	return true;
}

QString stripShaderComment(const QString& line)
{
	const int comment = line.indexOf(QStringLiteral("//"));
	return comment >= 0 ? line.left(comment) : line;
}

QString firstToken(const QString& line)
{
	const QString trimmed = line.trimmed();
	const int split = trimmed.indexOf(QRegularExpression(QStringLiteral("\\s+")));
	return split < 0 ? trimmed : trimmed.left(split);
}

QString directiveTail(const QString& line)
{
	const QString trimmed = line.trimmed();
	const QString token = firstToken(trimmed);
	return trimmed.mid(token.size()).trimmed();
}

bool shaderDirectiveLooksTexture(const QString& token)
{
	const QString normalized = normalizedId(token);
	return normalized == QStringLiteral("map")
		|| normalized == QStringLiteral("clampmap")
		|| normalized == QStringLiteral("animmap")
		|| normalized == QStringLiteral("videomap")
		|| normalized == QStringLiteral("qer-editorimage")
		|| normalized == QStringLiteral("q3map-lightimage")
		|| normalized == QStringLiteral("skyparms");
}

QStringList shaderDirectiveArguments(const QString& tail)
{
	QStringList arguments;
	static const QRegularExpression words(QStringLiteral("\"([^\"]*)\"|([^\\s]+)"));
	auto word = words.globalMatch(tail);
	while (word.hasNext()) {
		const auto match = word.next();
		arguments << (match.capturedStart(1) >= 0 ? match.captured(1) : match.captured(2));
	}
	return arguments;
}

QStringList textureReferencesFromDirective(const QString& line)
{
	QStringList references;
	const QString token = normalizedId(firstToken(line));
	if (!shaderDirectiveLooksTexture(token)) {
		return references;
	}
	const QString tail = directiveTail(line);
	const QStringList arguments = shaderDirectiveArguments(tail);
	// Quake III Shader Manual revision 12, sections 3.1 and 4.6:
	// https://icculus.org/gtkradiant/documentation/Q3AShader_Manual/index.htm
	// skyParms names six faces per environment box; '-' means no box.
	if (token == QStringLiteral("skyparms")) {
		const QStringList& parts = arguments;
		for (int index : {0, 2}) {
			QString base = parts.value(index);
			if (base.startsWith(QLatin1Char('"')) && base.endsWith(QLatin1Char('"'))) {
				base = base.mid(1, base.size() - 2);
			}
			if (base.isEmpty() || base == QStringLiteral("-")) { continue; }
			for (const QString& face : {QStringLiteral("rt"), QStringLiteral("lf"), QStringLiteral("ft"), QStringLiteral("bk"), QStringLiteral("up"), QStringLiteral("dn")}) {
				references << normalizedVirtualPath(base + QLatin1Char('_') + face + QStringLiteral(".tga"));
			}
		}
		return references;
	}
	if (token == QStringLiteral("animmap")) {
		QStringList parts = arguments;
		if (!parts.isEmpty()) {
			bool speedOk = false;
			parts.first().toDouble(&speedOk);
			if (speedOk) {
				parts.removeFirst();
			}
		}
		for (const QString& part : parts) {
			if (!part.startsWith('$')) {
				references << normalizedVirtualPath(part);
			}
		}
		return references;
	}
	if (!arguments.value(0).startsWith('$') && !arguments.value(0).isEmpty()) {
		references << normalizedVirtualPath(arguments.first());
	}
	return references;
}

void appendUnique(QStringList* list, const QString& value)
{
	if (!list || value.trimmed().isEmpty()) {
		return;
	}
	if (!list->contains(value)) {
		list->push_back(value);
	}
}

void setStageDirectiveValue(ShaderStage* stage, const QString& directive, const QString& value)
{
	if (!stage) {
		return;
	}
	const QString normalized = normalizedId(directive);
	if (normalized == QStringLiteral("map") || normalized == QStringLiteral("clampmap")) {
		stage->mapDirective = value;
		stage->textureReferences = textureReferencesFromDirective(QStringLiteral("map %1").arg(value));
	} else if (normalized == QStringLiteral("blendfunc")) {
		stage->blendFunc = value;
	} else if (normalized == QStringLiteral("rgbgen")) {
		stage->rgbGen = value;
	} else if (normalized == QStringLiteral("alphagen")) {
		stage->alphaGen = value;
	} else if (normalized == QStringLiteral("tcmod")) {
		stage->tcMod = value;
	}

	bool replaced = false;
	for (QString& line : stage->directives) {
		if (normalizedId(firstToken(line)) == normalized) {
			line = QStringLiteral("%1 %2").arg(directive.trimmed(), value.trimmed()).trimmed();
			replaced = true;
			break;
		}
	}
	if (!replaced) {
		stage->directives << QStringLiteral("%1 %2").arg(directive.trimmed(), value.trimmed()).trimmed();
	}
}

QStringList stageTextLines(const ShaderStage& stage)
{
	QStringList lines;
	lines << QStringLiteral("\t{");
	QStringList directives = stage.directives;
	if (directives.isEmpty()) {
		if (!stage.mapDirective.isEmpty()) {
			directives << QStringLiteral("map %1").arg(stage.mapDirective);
		}
		if (!stage.blendFunc.isEmpty()) {
			directives << QStringLiteral("blendFunc %1").arg(stage.blendFunc);
		}
		if (!stage.rgbGen.isEmpty()) {
			directives << QStringLiteral("rgbGen %1").arg(stage.rgbGen);
		}
		if (!stage.alphaGen.isEmpty()) {
			directives << QStringLiteral("alphaGen %1").arg(stage.alphaGen);
		}
		if (!stage.tcMod.isEmpty()) {
			directives << QStringLiteral("tcMod %1").arg(stage.tcMod);
		}
	}
	for (const QString& directive : directives) {
		lines << QStringLiteral("\t\t%1").arg(directive.trimmed());
	}
	lines << QStringLiteral("\t}");
	return lines;
}

QString languageForExtension(const QString& suffix)
{
	const QString ext = suffix.toLower();
	if (ext == QStringLiteral("qc") || ext == QStringLiteral("qh")) {
		return QStringLiteral("quakec");
	}
	if (ext == QStringLiteral("c") || ext == QStringLiteral("cc") || ext == QStringLiteral("cpp") || ext == QStringLiteral("h") || ext == QStringLiteral("hpp")) {
		return QStringLiteral("cpp");
	}
	if (ext == QStringLiteral("shader")) {
		return QStringLiteral("idtech3-shader");
	}
	if (ext == QStringLiteral("def") || ext == QStringLiteral("ent") || ext == QStringLiteral("fgd")) {
		return QStringLiteral("entity-definition");
	}
	if (ext == QStringLiteral("cfg") || ext == QStringLiteral("arena") || ext == QStringLiteral("skin")) {
		return QStringLiteral("idtech-config");
	}
	if (ext == QStringLiteral("json")) {
		return QStringLiteral("json");
	}
	if (ext == QStringLiteral("meson")) {
		return QStringLiteral("meson");
	}
	return QStringLiteral("text");
}

QStringList defaultCodeExtensions()
{
	return {
		QStringLiteral("qc"),
		QStringLiteral("qh"),
		QStringLiteral("c"),
		QStringLiteral("cc"),
		QStringLiteral("cpp"),
		QStringLiteral("h"),
		QStringLiteral("hpp"),
		QStringLiteral("shader"),
		QStringLiteral("def"),
		QStringLiteral("ent"),
		QStringLiteral("fgd"),
		QStringLiteral("cfg"),
		QStringLiteral("arena"),
		QStringLiteral("skin"),
		QStringLiteral("json"),
		QStringLiteral("meson"),
		QStringLiteral("txt"),
	};
}

bool shouldSkipSourceDirectory(const QString& path)
{
	return isExcludedCodeDirectory(path);
}

QString relativeToRoot(const QString& rootPath, const QString& filePath)
{
	QString relative = QDir(rootPath).relativeFilePath(filePath);
	relative.replace('\\', '/');
	return relative;
}

QVector<CodeSymbol> symbolsFromText(const QString& rootPath, const QString& filePath, const QString& text, const QString& query,
	const std::function<bool()>& isCancelled = {}, int limit = 20000, bool* limited = nullptr)
{
	QVector<CodeSymbol> symbols;
	const QString relative = relativeToRoot(rootPath, filePath);
	const QStringList lines = text.split('\n');
	const QRegularExpression functionPattern(QStringLiteral(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*(?:\{|$))"));
	const QRegularExpression shaderPattern(QStringLiteral(R"(^\s*([A-Za-z0-9_./-]+)\s*$)"));
	const QRegularExpression entityPattern(QStringLiteral(R"(\b(classname|spawnclass|model)\b\s+\"?([A-Za-z0-9_./-]+)\"?)"));
	// QuakeC declares a function as `type(args) name = ...`.
	// https://www.gamers.org/dEngine/quake/spec/quake-spec34/qc-menu.htm
	const QRegularExpression quakeCFunctionPattern(QStringLiteral(R"(^[A-Za-z_][A-Za-z0-9_]*\s*\([^)]*\)\s*([A-Za-z_][A-Za-z0-9_]*)\s*=)"));
	// Radiant .def comments open with `/*QUAKED classname`; FGD classes read
	// `@PointClass ... = classname`.
	const QRegularExpression quakedPattern(QStringLiteral(R"(^/\*QUAKED\s+([A-Za-z0-9_]+))"));
	const QRegularExpression fgdPattern(QStringLiteral(R"(^@[A-Za-z]+Class\b[^=]*=\s*([A-Za-z0-9_]+))"));
	const QString suffix = QFileInfo(filePath).suffix().toLower();
	for (int index = 0; index < lines.size(); ++index) {
		if (isCancelled && isCancelled()) { break; }
		if (symbols.size() >= limit) { if (limited) { *limited = true; } break; }
		// Bound regex work even for malformed, generated, single-line input.
		if (lines[index].size() > 8192) { if (limited) { *limited = true; } continue; }
		const QString raw = lines[index].trimmed();
		const int indentation = lines[index].indexOf(raw);
		CodeSymbol symbol;
		symbol.filePath = filePath;
		symbol.relativePath = relative;
		symbol.line = index + 1;
		// Checked before comments are stripped: a .def class lives in one.
		if (const QRegularExpressionMatch quaked = quakedPattern.match(raw); quaked.hasMatch()) {
			symbol.name = quaked.captured(1);
			symbol.kind = QStringLiteral("entity class");
		} else if (const QRegularExpressionMatch fgd = fgdPattern.match(raw); fgd.hasMatch()) {
			symbol.name = fgd.captured(1);
			symbol.kind = QStringLiteral("entity class");
		}
		if (!symbol.name.isEmpty()) {
			if (query.trimmed().isEmpty() || symbol.name.contains(query, Qt::CaseInsensitive)) {
				symbols.push_back(symbol);
			}
			continue;
		}
		const QString line = stripShaderComment(lines[index]).trimmed();
		if (line.isEmpty()) {
			continue;
		}
		if (suffix == QStringLiteral("shader")) {
			const QRegularExpressionMatch match = shaderPattern.match(line);
			if (match.hasMatch() && !line.contains('{') && !line.contains('}') && !line.contains(' ')) {
				symbol.name = match.captured(1);
				symbol.kind = QStringLiteral("shader");
			}
		}
		if (symbol.name.isEmpty()) {
			const QRegularExpressionMatch entity = entityPattern.match(line);
			if (entity.hasMatch()) {
				symbol.name = entity.captured(2);
				symbol.kind = QStringLiteral("entity");
			}
		}
		if (symbol.name.isEmpty() && (suffix == QStringLiteral("qc") || suffix == QStringLiteral("qh"))) {
			const QRegularExpressionMatch function = quakeCFunctionPattern.match(line);
			if (function.hasMatch()) {
				symbol.name = function.captured(1);
				symbol.kind = QStringLiteral("function");
				symbol.column = indentation + function.capturedStart(1) + 1;
			}
		}
		if (symbol.name.isEmpty()) {
			const QRegularExpressionMatch function = functionPattern.match(line);
			if (function.hasMatch()) {
				const QString name = function.captured(1);
				if (!QStringList {QStringLiteral("if"), QStringLiteral("while"), QStringLiteral("for"), QStringLiteral("switch")}.contains(name)) {
					symbol.name = name;
					symbol.kind = QStringLiteral("function");
					symbol.column = indentation + function.capturedStart(1) + 1;
				}
			}
		}
		if (!symbol.name.isEmpty() && (query.trimmed().isEmpty() || symbol.name.contains(query, Qt::CaseInsensitive))) {
			symbols.push_back(symbol);
		}
	}
	return symbols;
}

} // namespace

QVector<CodeSymbol> codeSymbolsInText(const QString& filePath, const QString& text)
{
	return symbolsFromText(QFileInfo(filePath).absolutePath(), filePath, text, QString());
}

namespace {

QVector<CodeDiagnostic> diagnosticsFromText(const QString& rootPath, const QString& filePath, const QString& text,
	const std::function<bool()>& isCancelled = {}, int limit = 10000, bool* limited = nullptr)
{
	QVector<CodeDiagnostic> diagnostics;
	const QString relative = relativeToRoot(rootPath, filePath);
	int braceBalance = 0;
	const QStringList lines = text.split('\n');
	for (int index = 0; index < lines.size(); ++index) {
		if (isCancelled && isCancelled()) { return diagnostics; }
		if (diagnostics.size() + 2 >= limit) { if (limited) { *limited = true; } return diagnostics; }
		const QString line = lines[index];
		braceBalance += line.count('{');
		braceBalance -= line.count('}');
		if (line.size() > 180) {
			diagnostics.push_back({QStringLiteral("warning"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Line may wrap in compact code surfaces."), filePath, relative, index + 1, 181});
		}
		if (line.contains(QStringLiteral("TODO"), Qt::CaseInsensitive) || line.contains(QStringLiteral("FIXME"), Qt::CaseInsensitive)) {
			const int markerColumn = static_cast<int>(line.indexOf(QRegularExpression(QStringLiteral("TODO|FIXME"), QRegularExpression::CaseInsensitiveOption))) + 1;
			diagnostics.push_back({QStringLiteral("info"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Task marker found."), filePath, relative, index + 1, std::max(1, markerColumn)});
		}
	}
	if (braceBalance != 0) {
		diagnostics.push_back({QStringLiteral("warning"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Brace balance is %1.").arg(braceBalance), filePath, relative, std::max(1, static_cast<int>(lines.size())), 1});
	}
	return diagnostics;
}

QStringList jsonStringArray(const QJsonValue& value)
{
	QStringList list;
	if (!value.isArray()) {
		return list;
	}
	for (const QJsonValue& item : value.toArray()) {
		if (item.isString()) {
			const QString text = item.toString().trimmed();
			if (!text.isEmpty()) {
				list << text;
			}
		}
	}
	return list;
}

ExtensionGeneratedFile generatedFileFromJson(const QJsonObject& object)
{
	ExtensionGeneratedFile file;
	file.virtualPath = normalizedVirtualPath(object.value(QStringLiteral("virtualPath")).toString());
	file.sourceDescription = object.value(QStringLiteral("source")).toString();
	file.summary = object.value(QStringLiteral("summary")).toString();
	file.staged = object.value(QStringLiteral("staged")).toBool(true);
	return file;
}

QString substitutedExtensionValue(QString value, const ExtensionManifest& manifest)
{
	value.replace(QStringLiteral("${extensionRoot}"), manifest.rootPath);
	value.replace(QStringLiteral("${manifestDir}"), manifest.rootPath);
	return value;
}

} // namespace

bool ShaderSaveReport::succeeded() const
{
	return errors.isEmpty() && (dryRun || written);
}

QStringList advancedStudioCapabilityLines()
{
	return {
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader graph: parse idTech3 shader scripts, list stages, preview directives, edit blend/map directives, round-trip to text, and validate texture references against mounted packages."),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Sprite creator: produce Doom lump naming plans, Quake sprite package plans, palette conversion previews, frame sequencing, and staged package virtual paths."),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Code IDE: index source trees, expose language-service hook descriptors, collect lightweight diagnostics, search symbols, and list build/run profile handoffs."),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "AI-assisted creation: generate reviewable shader, entity definition, package validation, batch conversion, and CLI command proposals without writing files."),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Extensions: parse manifests, describe trust/sandbox boundaries, discover extensions, plan/execute approved commands, and stage generated files."),
	};
}

ShaderDocument parseShaderScriptText(const QString& text, const QString& sourcePath)
{
	ShaderDocument document;
	document.sourcePath = sourcePath;
	document.originalText = text;
	struct Piece { QString text; int line; qsizetype start; qsizetype end; };
	QVector<Piece> pieces;
	QString current;
	int line = 1;
	int firstLine = 1;
	qsizetype firstOffset = 0;
	bool quoted = false;
	bool blockComment = false;
	bool hasText = false;
	const auto issue = [&](const QString& code, const QString& message, int at) {
		if (document.issues.size() < 256) {
			const QString severity = code == QStringLiteral("shader-unclosed") || code == QStringLiteral("shader-reference-arguments")
				|| code.startsWith(QStringLiteral("shader-unclosed-")) ? QStringLiteral("error") : QStringLiteral("warning");
			document.issues.append({severity, code, message, QString(), at});
		}
	};
	const auto flush = [&](qsizetype end) {
		if (!current.trimmed().isEmpty()) {
			pieces.append({current.trimmed(), firstLine, firstOffset, end});
		}
		current.clear();
		hasText = false;
	};
	// Braces delimit blocks even on a directive's line. Comments and quoted
	// names do not contribute braces; keep original offsets for source links.
	for (qsizetype i = 0; i < text.size(); ++i) {
		if (pieces.size() >= 262144) {
			issue(QStringLiteral("shader-token-limit"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader token limit exceeded; parsing stopped."), line);
			break;
		}
		const QChar ch = text.at(i);
		const QChar next = i + 1 < text.size() ? text.at(i + 1) : QChar();
		if (blockComment) {
			if (ch == QLatin1Char('*') && next == QLatin1Char('/')) { blockComment = false; ++i; }
			else if (ch == QLatin1Char('\n')) { flush(i); ++line; }
			continue;
		}
		if (!quoted && ch == QLatin1Char('/') && next == QLatin1Char('*')) {
			blockComment = true;
			current += QLatin1Char(' ');
			++i;
			continue;
		}
		if (!quoted && ch == QLatin1Char('/') && next == QLatin1Char('/')) {
			while (i + 1 < text.size() && text.at(i + 1) != QLatin1Char('\n')) { ++i; }
			continue;
		}
		if (ch == QLatin1Char('\n')) {
			if (quoted) {
				issue(QStringLiteral("shader-unclosed-quote"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Quoted shader value crosses a line boundary."), line);
				quoted = false;
			}
			flush(i);
			++line;
			continue;
		}
		if (!quoted && (ch == QLatin1Char('{') || ch == QLatin1Char('}'))) {
			flush(i);
			pieces.append({QString(ch), line, i, i + 1});
			continue;
		}
		if (!hasText && !ch.isSpace()) { firstLine = line; firstOffset = i; hasText = true; }
		current += ch;
		if (ch == QLatin1Char('"')) { quoted = !quoted; }
	}
	flush(text.size());
	if (blockComment || quoted) {
		issue(QStringLiteral("shader-unclosed-token"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader script ends inside a comment or quoted value."), line);
	}
	int depth = 0;
	QString pending;
	int pendingLine = 0;
	qsizetype shaderStart = 0;
	qsizetype stageStart = 0;
	ShaderDefinition shader;
	ShaderStage stage;
	const auto finishStage = [&](int lastLine, qsizetype end) {
		stage.endLine = lastLine;
		stage.rawText = text.mid(stageStart, end - stageStart);
		for (const QString& reference : stage.textureReferences) { appendUnique(&shader.textureReferences, reference); }
		shader.stages.append(stage);
		stage = {};
	};
	const auto finishShader = [&](int lastLine, qsizetype end) {
		shader.endLine = lastLine;
		shader.rawText = text.mid(shaderStart, end - shaderStart);
		document.shaders.append(shader);
		shader = {};
	};
	for (const Piece& piece : std::as_const(pieces)) {
		if (piece.text == QStringLiteral("{")) {
			if (depth == 0) {
				if (pending.isEmpty()) {
					issue(QStringLiteral("shader-orphan-brace"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader brace appeared before a shader name."), piece.line);
					continue;
				}
				shader = {};
				shader.id = document.shaders.size();
				shader.name = pending;
				if (shader.name.startsWith(QLatin1Char('"')) && shader.name.endsWith(QLatin1Char('"'))) { shader.name = shader.name.mid(1, shader.name.size() - 2); }
				shader.startLine = pendingLine;
				pending.clear();
			} else if (depth == 1) {
				stage = {};
				stage.id = shader.stages.size();
				stage.shaderName = shader.name;
				stage.startLine = piece.line;
				stageStart = piece.start;
			} else {
				issue(QStringLiteral("shader-nested-stage"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Unexpected nested block inside a shader stage."), piece.line);
			}
			++depth;
			continue;
		}
		if (piece.text == QStringLiteral("}")) {
			if (depth == 0) {
				issue(QStringLiteral("shader-orphan-brace"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader brace appeared before a shader name."), piece.line);
			} else {
				if (depth == 2) { finishStage(piece.line, piece.end); }
				if (depth == 1) { finishShader(piece.line, piece.end); }
				--depth;
			}
			continue;
		}
		if (depth == 0) {
			if (!pending.isEmpty()) {
				issue(QStringLiteral("shader-missing-brace"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader name was not followed by an opening brace."), pendingLine);
			}
			pending = piece.text;
			pendingLine = piece.line;
			shaderStart = piece.start;
			continue;
		}
		const QString directive = normalizedId(firstToken(piece.text));
		if (shaderDirectiveLooksTexture(directive)) {
			const QStringList arguments = shaderDirectiveArguments(directiveTail(piece.text));
			bool valid = arguments.size() == 1 && !arguments.first().isEmpty();
			if (directive == QStringLiteral("animmap")) {
				bool numeric = false;
				arguments.value(0).toDouble(&numeric);
				valid = numeric && arguments.size() >= 2;
			} else if (directive == QStringLiteral("skyparms")) {
				bool numeric = false;
				arguments.value(1).toDouble(&numeric);
				valid = numeric && arguments.size() == 3;
			}
			if (!valid) {
				issue(QStringLiteral("shader-reference-arguments"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Invalid arguments for shader asset directive: %1").arg(firstToken(piece.text)), piece.line);
			}
		}
		if (depth == 1) {
			shader.directives << piece.text;
			for (const QString& reference : textureReferencesFromDirective(piece.text)) { appendUnique(&shader.textureReferences, reference); }
		} else if (depth == 2) {
			stage.directives << piece.text;
			const QString token = normalizedId(firstToken(piece.text));
			const QString value = directiveTail(piece.text);
			if (token == QStringLiteral("map") || token == QStringLiteral("clampmap")) { stage.mapDirective = value; }
			else if (token == QStringLiteral("blendfunc")) { stage.blendFunc = value; }
			else if (token == QStringLiteral("rgbgen")) { stage.rgbGen = value; }
			else if (token == QStringLiteral("alphagen")) { stage.alphaGen = value; }
			else if (token == QStringLiteral("tcmod")) { stage.tcMod = value; }
			for (const QString& reference : textureReferencesFromDirective(piece.text)) { appendUnique(&stage.textureReferences, reference); }
		}
	}
	if (depth > 0) {
		issue(QStringLiteral("shader-unclosed"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader block ended before its closing brace."), shader.startLine);
		if (depth >= 2) { finishStage(line, text.size()); }
		finishShader(line, text.size());
	}
	if (!pending.isEmpty()) { issue(QStringLiteral("shader-dangling-name"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader name has no body."), pendingLine); }
	if (document.shaders.isEmpty() && !pieces.isEmpty()) { issue(QStringLiteral("shader-empty"), QCoreApplication::translate("VibeStudioAdvancedStudio", "No idTech3 shader definitions were parsed."), 0); }
	return document;
}

bool loadShaderScript(const QString& path, ShaderDocument* document, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document) {
		return false;
	}
	QString text;
	if (path.trimmed().isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader script path is empty.");
		}
		return false;
	}
	if (!decodeTextFile(path, &text, error)) {
		return false;
	}
	*document = parseShaderScriptText(text, QFileInfo(path).absoluteFilePath());
	return true;
}

bool setShaderStageDirective(ShaderDocument* document, const QString& shaderName, int stageIndex, const QString& directive, const QString& value, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAdvancedStudio", "No shader document is loaded.");
		}
		return false;
	}
	if (shaderName.trimmed().isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader name is required.");
		}
		return false;
	}
	if (stageIndex < 0) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAdvancedStudio", "Stage index must be zero or greater.");
		}
		return false;
	}
	for (ShaderDefinition& shader : document->shaders) {
		if (QString::compare(shader.name, shaderName, Qt::CaseInsensitive) != 0) {
			continue;
		}
		if (stageIndex >= shader.stages.size()) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader stage index is out of range.");
			}
			return false;
		}
		setStageDirectiveValue(&shader.stages[stageIndex], directive, value);
		shader.textureReferences.clear();
		for (const QString& line : shader.directives) {
			for (const QString& reference : textureReferencesFromDirective(line)) {
				appendUnique(&shader.textureReferences, reference);
			}
		}
		for (const ShaderStage& stage : shader.stages) {
			for (const QString& reference : stage.textureReferences) {
				appendUnique(&shader.textureReferences, reference);
			}
		}
		document->editState = QStringLiteral("modified");
		return true;
	}
	if (error) {
		*error = QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader was not found.");
	}
	return false;
}

QString shaderDocumentText(const ShaderDocument& document)
{
	QStringList lines;
	for (const ShaderDefinition& shader : document.shaders) {
		lines << shader.name;
		lines << QStringLiteral("{");
		for (const QString& directive : shader.directives) {
			lines << QStringLiteral("\t%1").arg(directive.trimmed());
		}
		for (const ShaderStage& stage : shader.stages) {
			lines << stageTextLines(stage);
		}
		lines << QStringLiteral("}");
		lines << QString();
	}
	return lines.join('\n').trimmed() + QLatin1Char('\n');
}

QStringList shaderGraphLines(const ShaderDocument& document)
{
	QStringList lines;
	if (document.shaders.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "No shader graph nodes.");
		return lines;
	}
	for (const ShaderDefinition& shader : document.shaders) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader %1: %2 stage(s), %3 texture reference(s)").arg(shader.name).arg(shader.stages.size()).arg(shader.textureReferences.size());
		for (const ShaderStage& stage : shader.stages) {
			lines << QStringLiteral("  [%1] map=%2 blend=%3 rgb=%4")
				.arg(stage.id)
				.arg(stage.mapDirective.isEmpty() ? QCoreApplication::translate("VibeStudioAdvancedStudio", "none") : stage.mapDirective)
				.arg(stage.blendFunc.isEmpty() ? QCoreApplication::translate("VibeStudioAdvancedStudio", "default") : stage.blendFunc)
				.arg(stage.rgbGen.isEmpty() ? QCoreApplication::translate("VibeStudioAdvancedStudio", "identity") : stage.rgbGen);
			for (const QString& reference : stage.textureReferences) {
				lines << QStringLiteral("      -> %1").arg(reference);
			}
		}
	}
	return lines;
}

QStringList shaderStagePreviewLines(const ShaderDocument& document, const QString& shaderName, int stageIndex)
{
	QStringList lines;
	for (const ShaderDefinition& shader : document.shaders) {
		if (!shaderName.trimmed().isEmpty() && QString::compare(shader.name, shaderName, Qt::CaseInsensitive) != 0) {
			continue;
		}
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader: %1").arg(shader.name);
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Directives: %1").arg(shader.directives.isEmpty() ? QCoreApplication::translate("VibeStudioAdvancedStudio", "none") : shader.directives.join(QStringLiteral("; ")));
		for (const ShaderStage& stage : shader.stages) {
			if (stageIndex >= 0 && stage.id != stageIndex) {
				continue;
			}
			lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Stage %1").arg(stage.id);
			lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "  Map: %1").arg(stage.mapDirective.isEmpty() ? QCoreApplication::translate("VibeStudioAdvancedStudio", "none") : stage.mapDirective);
			lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "  Blend: %1").arg(stage.blendFunc.isEmpty() ? QCoreApplication::translate("VibeStudioAdvancedStudio", "default") : stage.blendFunc);
			lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "  RGB: %1").arg(stage.rgbGen.isEmpty() ? QCoreApplication::translate("VibeStudioAdvancedStudio", "identity") : stage.rgbGen);
			lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "  Alpha: %1").arg(stage.alphaGen.isEmpty() ? QCoreApplication::translate("VibeStudioAdvancedStudio", "default") : stage.alphaGen);
			lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "  TC Mod: %1").arg(stage.tcMod.isEmpty() ? QCoreApplication::translate("VibeStudioAdvancedStudio", "none") : stage.tcMod);
			lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "  Raw:");
			lines << stageTextLines(stage);
		}
	}
	if (lines.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "No matching shader stage.");
	}
	return lines;
}

QVector<ShaderReferenceValidation> validateShaderReferences(const ShaderDocument& document, const QStringList& mountedPackagePaths, QStringList* warnings)
{
	if (warnings) {
		warnings->clear();
	}
	QStringList references;
	for (const ShaderDefinition& shader : document.shaders) {
		for (const QString& reference : shader.textureReferences) {
			appendUnique(&references, reference);
		}
	}

	QVector<ShaderReferenceValidation> results;
	QSet<QString> available;
	QHash<QString, QString> sourceForPath;
	for (const QString& packagePath : mountedPackagePaths) {
		if (packagePath.trimmed().isEmpty()) {
			continue;
		}
		PackageArchive archive;
		QString error;
		if (!archive.load(packagePath, &error)) {
			if (warnings) {
				warnings->push_back(QCoreApplication::translate("VibeStudioAdvancedStudio", "Unable to mount %1: %2").arg(QDir::toNativeSeparators(packagePath), error));
			}
			continue;
		}
		for (const PackageEntry& entry : archive.entries()) {
			if (entry.kind != PackageEntryKind::File) {
				continue;
			}
			const QString normalized = normalizedVirtualPath(entry.virtualPath).toLower();
			available.insert(normalized);
			sourceForPath.insert(normalized, archive.sourcePath());
		}
	}
	if (mountedPackagePaths.isEmpty() && warnings) {
		warnings->push_back(QCoreApplication::translate("VibeStudioAdvancedStudio", "No package paths were supplied; references are listed but not resolved."));
	}

	const QStringList suffixes = {QString(), QStringLiteral(".tga"), QStringLiteral(".jpg"), QStringLiteral(".jpeg"), QStringLiteral(".png"), QStringLiteral(".dds")};
	for (const QString& reference : references) {
		ShaderReferenceValidation validation;
		validation.textureReference = reference;
		for (const QString& suffix : suffixes) {
			QString candidate = normalizedVirtualPath(reference);
			if (!suffix.isEmpty() && QFileInfo(candidate).suffix().isEmpty()) {
				candidate += suffix;
			}
			if (!validation.candidatePaths.contains(candidate)) {
				validation.candidatePaths << candidate;
			}
		}
		for (const QString& candidate : validation.candidatePaths) {
			const QString normalized = candidate.toLower();
			if (available.contains(normalized)) {
				validation.found = true;
				validation.foundInPackage = sourceForPath.value(normalized);
				break;
			}
		}
		results.push_back(validation);
	}
	return results;
}

QStringList shaderReferenceValidationLines(const QVector<ShaderReferenceValidation>& validation, const QStringList& warnings)
{
	QStringList lines;
	for (const ShaderReferenceValidation& result : validation) {
		lines << QStringLiteral("%1 [%2]").arg(result.textureReference, result.found ? QCoreApplication::translate("VibeStudioAdvancedStudio", "found") : QCoreApplication::translate("VibeStudioAdvancedStudio", "missing"));
		if (result.found) {
			lines << QStringLiteral("  %1").arg(QDir::toNativeSeparators(result.foundInPackage));
		} else {
			lines << QStringLiteral("  %1").arg(QCoreApplication::translate("VibeStudioAdvancedStudio", "Checked: %1").arg(result.candidatePaths.join(QStringLiteral(", "))));
		}
	}
	for (const QString& warning : warnings) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Warning: %1").arg(warning);
	}
	if (lines.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "No shader texture references.");
	}
	return lines;
}

ShaderSaveReport saveShaderScriptAs(const ShaderDocument& document, const QString& outputPath, bool dryRun, bool overwriteExisting)
{
	ShaderSaveReport report;
	report.sourcePath = document.sourcePath;
	report.outputPath = QFileInfo(outputPath).absoluteFilePath();
	report.dryRun = dryRun;
	report.editState = document.editState;
	if (outputPath.trimmed().isEmpty()) {
		report.errors << QCoreApplication::translate("VibeStudioAdvancedStudio", "Output path is required.");
		return report;
	}
	if (!dryRun && QFileInfo::exists(report.outputPath) && !overwriteExisting) {
		report.errors << QCoreApplication::translate("VibeStudioAdvancedStudio", "Output exists. Use overwrite to replace it.");
		return report;
	}
	const QString text = shaderDocumentText(document);
	report.summaryLines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Shaders: %1").arg(document.shaders.size());
	report.summaryLines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Bytes: %1").arg(text.toUtf8().size());
	if (dryRun) {
		report.summaryLines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Dry run: shader text would be written.");
		return report;
	}
	const QFileInfo outputInfo(report.outputPath);
	if (!QDir().mkpath(outputInfo.absolutePath())) {
		report.errors << QCoreApplication::translate("VibeStudioAdvancedStudio", "Unable to create output directory.");
		return report;
	}
	QSaveFile file(report.outputPath);
	if (!file.open(QIODevice::WriteOnly)) {
		report.errors << QCoreApplication::translate("VibeStudioAdvancedStudio", "Unable to open shader output.");
		return report;
	}
	const QByteArray bytes = text.toUtf8();
	if (file.write(bytes) != bytes.size() || !file.commit()) {
		report.errors << QCoreApplication::translate("VibeStudioAdvancedStudio", "Unable to write shader output.");
		return report;
	}
	report.written = true;
	report.editState = QStringLiteral("saved");
	report.summaryLines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader text written.");
	return report;
}

QString shaderDocumentReportText(const ShaderDocument& document, const QVector<ShaderReferenceValidation>& validation, const QStringList& validationWarnings)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader script");
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Source: %1").arg(document.sourcePath.isEmpty() ? QCoreApplication::translate("VibeStudioAdvancedStudio", "memory") : QDir::toNativeSeparators(document.sourcePath));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Shaders: %1").arg(document.shaders.size());
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Edit state: %1").arg(document.editState);
	lines << QString();
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Graph:");
	lines << shaderGraphLines(document);
	lines << QString();
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Preview:");
	lines << shaderStagePreviewLines(document);
	lines << QString();
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Reference validation:");
	lines << shaderReferenceValidationLines(validation, validationWarnings);
	if (!document.issues.isEmpty()) {
		lines << QString();
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Issues:");
		for (const AdvancedStudioIssue& issue : document.issues) {
			lines << issueLine(issue);
		}
	}
	return lines.join('\n');
}

QString shaderSaveReportText(const ShaderSaveReport& report)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader save-as");
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Source: %1").arg(QDir::toNativeSeparators(report.sourcePath));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Output: %1").arg(QDir::toNativeSeparators(report.outputPath));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Mode: %1").arg(report.dryRun ? QCoreApplication::translate("VibeStudioAdvancedStudio", "dry run") : QCoreApplication::translate("VibeStudioAdvancedStudio", "write"));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Written: %1").arg(report.written ? QCoreApplication::translate("VibeStudioAdvancedStudio", "yes") : QCoreApplication::translate("VibeStudioAdvancedStudio", "no"));
	lines << report.summaryLines;
	for (const QString& warning : report.warnings) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Warning: %1").arg(warning);
	}
	for (const QString& error : report.errors) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Error: %1").arg(error);
	}
	return lines.join('\n');
}

SpriteWorkflowPlan buildSpriteWorkflowPlan(const SpriteWorkflowRequest& request)
{
	SpriteWorkflowPlan plan;
	plan.engineFamily = normalizedId(request.engineFamily);
	plan.spriteName = request.spriteName.trimmed().isEmpty() ? QStringLiteral("SPRT") : request.spriteName.trimmed();
	plan.paletteId = request.paletteId.trimmed().isEmpty() ? QStringLiteral("generic") : request.paletteId.trimmed();
	plan.outputPackageRoot = normalizedVirtualPath(request.outputPackageRoot.trimmed().isEmpty() ? QStringLiteral("sprites") : request.outputPackageRoot);

	const int frameCount = std::clamp(request.frameCount, 1, 26);
	const int rotations = plan.engineFamily == QStringLiteral("doom") ? std::clamp(request.rotations, 0, 8) : 1;
	const QString prefix = safeSpritePrefix(plan.spriteName);
	if (request.frameCount != frameCount) {
		plan.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Frame count was clamped to the supported A-Z range.");
	}
	if (plan.engineFamily != QStringLiteral("doom") && plan.engineFamily != QStringLiteral("quake")) {
		plan.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Unknown sprite engine requested; using Quake-style package naming.");
		plan.engineFamily = QStringLiteral("quake");
	}

	for (int frame = 0; frame < frameCount; ++frame) {
		const QChar frameId(QLatin1Char('A' + frame));
		if (plan.engineFamily == QStringLiteral("doom")) {
			const int rotationCount = rotations == 0 ? 1 : rotations;
			for (int rotation = 0; rotation < rotationCount; ++rotation) {
				SpriteFramePlan item;
				item.index = plan.frames.size();
				item.frameId = QString(frameId);
				item.rotationId = rotations == 0 ? QStringLiteral("0") : QString::number(rotation + 1);
				item.lumpName = QStringLiteral("%1%2%3").arg(prefix, item.frameId, item.rotationId);
				item.sourcePath = request.sourceFramePaths.value(frame);
				item.virtualPath = QStringLiteral("%1/%2.lmp").arg(plan.outputPackageRoot, item.lumpName).toLower();
				item.paletteAction = QCoreApplication::translate("VibeStudioAdvancedStudio", "Convert to Doom PLAYPAL-indexed image or verify source palette.");
				plan.frames.push_back(item);
			}
		} else {
			SpriteFramePlan item;
			item.index = plan.frames.size();
			item.frameId = QString(frameId);
			item.rotationId = QStringLiteral("single");
			item.lumpName = QStringLiteral("%1_%2").arg(sanitizePathToken(plan.spriteName), QString(frameId).toLower());
			item.sourcePath = request.sourceFramePaths.value(frame);
			item.virtualPath = QStringLiteral("%1/%2/frame_%3.png").arg(plan.outputPackageRoot, sanitizePathToken(plan.spriteName), QString(frameId).toLower());
			item.paletteAction = QCoreApplication::translate("VibeStudioAdvancedStudio", "Prepare frame for Quake SPR packing; keep source image staged until SPR writer is selected.");
			plan.frames.push_back(item);
		}
	}

	if (plan.engineFamily == QStringLiteral("quake")) {
		plan.stagingLines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Quake SPR manifest: %1/%2.spr").arg(plan.outputPackageRoot, sanitizePathToken(plan.spriteName));
	}
	for (const SpriteFramePlan& frame : plan.frames) {
		plan.sequenceLines << QCoreApplication::translate("VibeStudioAdvancedStudio", "%1: frame %2 rotation %3 -> %4").arg(frame.index).arg(frame.frameId, frame.rotationId, frame.lumpName);
		if (request.stageForPackage) {
			plan.stagingLines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Stage %1 from %2").arg(frame.virtualPath, frame.sourcePath.isEmpty() ? QCoreApplication::translate("VibeStudioAdvancedStudio", "<choose frame source>") : QDir::toNativeSeparators(frame.sourcePath));
		}
	}
	plan.palettePreviewLines = {
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Palette: %1").arg(plan.paletteId),
		plan.engineFamily == QStringLiteral("doom") ? QCoreApplication::translate("VibeStudioAdvancedStudio", "Doom workflow: PLAYPAL index preview, fullbright ranges, transparent cyan policy review.") : QCoreApplication::translate("VibeStudioAdvancedStudio", "Quake workflow: palette 0 transparency and colormap-aware preview before SPR packing."),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Conversion remains staged; original source frames are preserved."),
	};
	plan.state = plan.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning;
	return plan;
}

QString spriteWorkflowPlanText(const SpriteWorkflowPlan& plan)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Sprite workflow");
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Engine: %1").arg(plan.engineFamily);
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Sprite: %1").arg(plan.spriteName);
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Frames: %1").arg(plan.frames.size());
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Palette:");
	lines << plan.palettePreviewLines;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Sequence:");
	lines << plan.sequenceLines;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Package staging:");
	lines << (plan.stagingLines.isEmpty() ? QStringList {QCoreApplication::translate("VibeStudioAdvancedStudio", "No package staging requested.")} : plan.stagingLines);
	for (const QString& warning : plan.warnings) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Warning: %1").arg(warning);
	}
	return lines.join('\n');
}

QVector<LanguageServiceHook> defaultLanguageServiceHooks()
{
	return {
		{QStringLiteral("quakec"), QCoreApplication::translate("VibeStudioAdvancedStudio", "QuakeC"), {QStringLiteral("qc"), QStringLiteral("qh")}, {QCoreApplication::translate("VibeStudioAdvancedStudio", "outline"), QCoreApplication::translate("VibeStudioAdvancedStudio", "symbols"), QCoreApplication::translate("VibeStudioAdvancedStudio", "brace diagnostics"), QCoreApplication::translate("VibeStudioAdvancedStudio", "build task handoff")}, QStringLiteral("fteqcc-lsp or custom QuakeC analyzer"), false, QCoreApplication::translate("VibeStudioAdvancedStudio", "Descriptor active; external language server not configured.")},
		{QStringLiteral("cpp"), QCoreApplication::translate("VibeStudioAdvancedStudio", "C and C++"), {QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"), QStringLiteral("h"), QStringLiteral("hpp")}, {QCoreApplication::translate("VibeStudioAdvancedStudio", "outline"), QCoreApplication::translate("VibeStudioAdvancedStudio", "symbols"), QCoreApplication::translate("VibeStudioAdvancedStudio", "compiler diagnostics")}, QStringLiteral("clangd"), false, QCoreApplication::translate("VibeStudioAdvancedStudio", "Hook defined; user toolchain selection remains explicit.")},
		{QStringLiteral("idtech3-shader"), QCoreApplication::translate("VibeStudioAdvancedStudio", "idTech3 Shader"), {QStringLiteral("shader")}, {QCoreApplication::translate("VibeStudioAdvancedStudio", "shader graph"), QCoreApplication::translate("VibeStudioAdvancedStudio", "texture refs"), QCoreApplication::translate("VibeStudioAdvancedStudio", "round-trip edits")}, QStringLiteral("vibestudio shader parser"), true, QCoreApplication::translate("VibeStudioAdvancedStudio", "Built-in shader parser available.")},
		{QStringLiteral("entity-definition"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Entity Definitions"), {QStringLiteral("def"), QStringLiteral("ent")}, {QCoreApplication::translate("VibeStudioAdvancedStudio", "outline"), QCoreApplication::translate("VibeStudioAdvancedStudio", "key/value snippets"), QCoreApplication::translate("VibeStudioAdvancedStudio", "spawnclass symbols")}, QStringLiteral("vibestudio entity analyzer"), true, QCoreApplication::translate("VibeStudioAdvancedStudio", "Built-in text analyzer available.")},
		{QStringLiteral("idtech-config"), QCoreApplication::translate("VibeStudioAdvancedStudio", "idTech Config"), {QStringLiteral("cfg"), QStringLiteral("arena"), QStringLiteral("skin")}, {QCoreApplication::translate("VibeStudioAdvancedStudio", "search"), QCoreApplication::translate("VibeStudioAdvancedStudio", "replace"), QCoreApplication::translate("VibeStudioAdvancedStudio", "launch profile hints")}, QStringLiteral("vibestudio config analyzer"), true, QCoreApplication::translate("VibeStudioAdvancedStudio", "Built-in text analyzer available.")},
	};
}

CodeWorkspaceIndex indexCodeWorkspace(const CodeWorkspaceIndexRequest& request)
{
	CodeWorkspaceIndex index;
	index.rootPath = QFileInfo(request.rootPath).absoluteFilePath();
	index.languageHooks = defaultLanguageServiceHooks();
	const QFileInfo rootInfo(index.rootPath);
	if (request.rootPath.trimmed().isEmpty() || !rootInfo.isDir() || rootInfo.isSymLink() || rootInfo.isJunction()) {
		index.state = OperationState::Failed;
		index.complete = false;
		index.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Project root must be an existing directory, not a link.");
		return index;
	}
	const int maxFiles = std::clamp(request.maxFiles, 1, 20000);
	const int maxEntries = std::clamp(request.maxEntries, 1, 100000);
	const int maxSymbols = std::clamp(request.maxSymbols, 1, 20000);
	const int maxDiagnostics = std::clamp(request.maxDiagnostics, 3, 10000);
	const qint64 maxFileBytes = std::clamp(request.maxFileBytes, 1ll, textDocumentByteLimit);
	const qint64 maxTotalBytes = std::clamp(request.maxTotalBytes, 1ll, 64ll * 1024 * 1024);
	const QString canonicalRoot = rootInfo.canonicalFilePath();
	const auto cancelled = [&]() {
		if (!request.isCancelled || !request.isCancelled()) { return false; }
		index.cancelled = true;
		index.complete = false;
		return true;
	};
	const auto markIncomplete = [&](const QString& reason) {
		index.complete = false;
		if (!index.warnings.contains(reason) && index.warnings.size() < 32) { index.warnings << reason; }
	};
	const auto pathKey = [](const QString& path) {
		const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
		return absolute.toCaseFolded();
#else
		return absolute;
#endif
	};
	const auto insideRoot = [&](const QString& path) {
		const QString relative = QDir(canonicalRoot).relativeFilePath(path);
		return !QDir::isAbsolutePath(relative) && relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../"));
	};
	QStringList extensions = request.extensions.isEmpty() ? defaultCodeExtensions() : request.extensions;
	for (QString& extension : extensions) {
		extension = extension.trimmed().toLower();
		if (extension.startsWith('.')) { extension.remove(0, 1); }
	}
	QHash<QString, QString> buffers;
	QHash<QString, QString> bufferErrors;
	qint64 bufferBytes = 0;
	for (const auto& buffer : request.buffers) {
		if (cancelled()) { break; }
		if (buffers.size() >= maxFiles) {
			markIncomplete(QCoreApplication::translate("VibeStudioAdvancedStudio", "Editor snapshots reached the source index file limit."));
			break;
		}
		const QFileInfo info(buffer.filePath);
		const QString parent = QFileInfo(info.absolutePath()).canonicalFilePath();
		if (buffer.filePath.isEmpty() || parent.isEmpty() || !insideRoot(parent)
			|| info.isSymLink() || info.isJunction() || (!info.canonicalFilePath().isEmpty() && !insideRoot(info.canonicalFilePath()))) {
			markIncomplete(QCoreApplication::translate("VibeStudioAdvancedStudio", "An editor snapshot is outside the source root or follows a link."));
			continue;
		}
		const qint64 size = buffer.text.size() * qint64(sizeof(QChar));
		if (!buffer.error.isEmpty()) {
			buffers.insert(pathKey(buffer.filePath), QString());
			bufferErrors.insert(pathKey(buffer.filePath), buffer.error);
			continue;
		}
		if (size > maxFileBytes * 2 || bufferBytes + size > maxTotalBytes) {
			markIncomplete(QCoreApplication::translate("VibeStudioAdvancedStudio", "Editor snapshots reached the source index size limit."));
			buffers.insert(pathKey(buffer.filePath), QString());
			bufferErrors.insert(pathKey(buffer.filePath), QCoreApplication::translate("VibeStudioAdvancedStudio", "Editor snapshot exceeds the index size limit."));
			continue;
		}
		buffers.insert(pathKey(buffer.filePath), buffer.text);
		bufferBytes += size;
	}
	QSet<QString> seen;
	int candidates = 0;
	bool stopped = false;
	const auto addFile = [&](const QString& path) {
		const QFileInfo info(path);
		const QString key = pathKey(path);
		if (seen.contains(key)) { return; }
		seen.insert(key);
		const QString suffix = info.fileName() == QStringLiteral("meson.build") ? QStringLiteral("meson") : info.suffix().toLower();
		if (!extensions.contains(suffix)) { return; }
		if (++candidates > maxFiles) {
			markIncomplete(QCoreApplication::translate("VibeStudioAdvancedStudio", "File scan stopped at %1 candidate files.").arg(maxFiles));
			stopped = true; return;
		}
		QString sourceText;
		qint64 sourceBytes = 0;
		const bool fromBuffer = buffers.contains(key);
		QString error;
		if (fromBuffer) {
			sourceText = buffers.value(key);
			sourceBytes = sourceText.size() * qint64(sizeof(QChar));
			error = bufferErrors.value(key);
		} else {
			QFile file(path);
			if (!info.isFile() || !file.open(QIODevice::ReadOnly)) {
				error = QCoreApplication::translate("VibeStudioAdvancedStudio", "Unable to read source file.");
			} else if (file.size() > maxFileBytes) {
				error = QCoreApplication::translate("VibeStudioAdvancedStudio", "Source file exceeds the index size limit.");
			} else if (file.size() > maxTotalBytes - index.bytesRead) {
				markIncomplete(QCoreApplication::translate("VibeStudioAdvancedStudio", "Source index reached its total input size limit."));
				stopped = true; return;
			} else {
				const QByteArray bytes = file.read(std::min(maxFileBytes, maxTotalBytes - index.bytesRead) + 1);
				sourceBytes = bytes.size();
				if (file.error() != QFileDevice::NoError || !file.atEnd() || sourceBytes > maxFileBytes) {
					error = QCoreApplication::translate("VibeStudioAdvancedStudio", "Source file could not be read completely within the index limit.");
				} else {
					const TextFileDocument document = vibestudio::decodeTextFile(bytes);
					if (!document.editable()) { error = document.error; }
					else { sourceText = document.text; }
				}
			}
		}
		if (sourceBytes > maxTotalBytes - index.bytesRead) {
			markIncomplete(QCoreApplication::translate("VibeStudioAdvancedStudio", "Source index reached its total input size limit."));
			stopped = true; return;
		}
		index.bytesRead += sourceBytes;
		if (!error.isEmpty()) {
			++index.filesSkipped;
			markIncomplete(QCoreApplication::translate("VibeStudioAdvancedStudio", "Some source files could not be indexed. Inspect the diagnostics."));
			if (index.diagnostics.size() < maxDiagnostics) { index.diagnostics.push_back({QStringLiteral("warning"), error, path, relativeToRoot(index.rootPath, path), 0, 0}); }
			return;
		}
		if (cancelled()) { return; }
		index.files.push_back({path, relativeToRoot(index.rootPath, path), languageForExtension(suffix), sourceBytes, int(sourceText.count('\n') + 1), fromBuffer});
		bool limited = false;
		index.symbols += symbolsFromText(index.rootPath, path, sourceText, request.symbolQuery, request.isCancelled, maxSymbols - int(index.symbols.size()), &limited);
		index.diagnostics += diagnosticsFromText(index.rootPath, path, sourceText, request.isCancelled, maxDiagnostics - int(index.diagnostics.size()), &limited);
		if (limited) { markIncomplete(QCoreApplication::translate("VibeStudioAdvancedStudio", "Symbol or diagnostic limits were reached, or a line exceeded 8192 characters.")); }
		if (request.progress) { request.progress(int(index.files.size()), int(index.symbols.size())); }
	};
	QStringList pending {index.rootPath};
	while (!pending.isEmpty() && !stopped && !cancelled()) {
		const QString directory = pending.takeLast();
		const QFileInfo directoryInfo(directory);
		if (directoryInfo.isSymLink() || directoryInfo.isJunction() || !insideRoot(directoryInfo.canonicalFilePath())) { continue; }
		QDirIterator iterator(directory, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
		while (iterator.hasNext() && !stopped && !cancelled()) {
			const QString path = iterator.next();
			if (++index.entriesVisited > maxEntries) {
				markIncomplete(QCoreApplication::translate("VibeStudioAdvancedStudio", "Source index reached its directory entry limit."));
				stopped = true; break;
			}
			const QFileInfo info = iterator.fileInfo();
			if (info.isSymLink() || info.isJunction()) { continue; }
			if (info.isDir()) {
				if (!shouldSkipSourceDirectory(path)) { pending << path; }
			} else if (info.isFile() && insideRoot(info.canonicalFilePath())) { addFile(path); }
		}
	}
	// An open file deleted on disk still has a navigable editor snapshot.
	for (const auto& buffer : request.buffers) {
		if (stopped || cancelled()) { break; }
		if (buffers.contains(pathKey(buffer.filePath))) { addFile(QFileInfo(buffer.filePath).absoluteFilePath()); }
	}
	cancelled();
	std::sort(index.files.begin(), index.files.end(), [](const CodeSourceFile& left, const CodeSourceFile& right) { return left.relativePath < right.relativePath; });
	for (const CodeSourceFile& file : index.files) { index.treeLines << QStringLiteral("%1 [%2, %3 lines]").arg(file.relativePath, file.languageId).arg(file.lineCount); }
	for (const CompilerProfileDescriptor& profile : compilerProfileDescriptors()) {
		index.buildTaskLines << QCoreApplication::translate("VibeStudioAdvancedStudio", "%1: %2").arg(profile.id, profile.displayName);
	}
	index.launchProfileLines = {
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Quake/idTech2 source port launch: select executable in installation profile, then pass +map <map>."),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Doom source port launch: select executable in installation profile, then pass -file <wad> -warp <map>."),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Quake III launch: select executable in installation profile, then pass +set fs_game <mod> +devmap <map>."),
	};
	index.state = index.cancelled ? OperationState::Cancelled : index.complete ? OperationState::Completed : OperationState::Warning;
	return index;
}

QString codeWorkspaceIndexText(const CodeWorkspaceIndex& index)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Code workspace");
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Root: %1").arg(QDir::toNativeSeparators(index.rootPath));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Files: %1").arg(index.files.size());
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Symbols: %1").arg(index.symbols.size());
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Diagnostics: %1").arg(index.diagnostics.size());
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Complete: %1; skipped files: %2; input bytes: %3").arg(index.complete ? QStringLiteral("true") : QStringLiteral("false")).arg(index.filesSkipped).arg(index.bytesRead);
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Source tree:");
	lines << (index.treeLines.isEmpty() ? QStringList {QCoreApplication::translate("VibeStudioAdvancedStudio", "No source files indexed.")} : index.treeLines.mid(0, 80));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Language hooks:");
	for (const LanguageServiceHook& hook : index.languageHooks) {
		lines << QStringLiteral("- %1 [%2]: %3").arg(hook.displayName, hook.languageId, hook.status);
	}
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Symbols:");
	for (const CodeSymbol& symbol : index.symbols.mid(0, 80)) {
		lines << QStringLiteral("- %1 %2 at %3:%4").arg(symbol.kind, symbol.name, symbol.relativePath).arg(symbol.line);
	}
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Build tasks:");
	lines << index.buildTaskLines;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Launch profiles:");
	lines << index.launchProfileLines;
	for (const CodeDiagnostic& diagnostic : index.diagnostics.mid(0, 80)) {
		lines << QStringLiteral("%1: %2:%3 %4").arg(diagnostic.severity, diagnostic.relativePath).arg(diagnostic.line).arg(diagnostic.message);
	}
	for (const QString& warning : index.warnings) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Warning: %1").arg(warning);
	}
	return lines.join('\n');
}

QStringList extensionTrustModelLines()
{
	return {
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Manifest schema: vibestudio.extension.json with id, version, trustLevel, sandbox, capabilities, commands, and generatedFiles."),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Trust levels: bundled, workspace, user-approved, disabled. Disabled extensions are discoverable but not executable."),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Sandbox models: metadata-only, command-plan, staged-files. File writes must be represented as staged generated files before package/project import."),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Command execution requires explicit allowExecution plus a non-disabled trust level; dry-run planning is the default."),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Programs and working directories are resolved relative to the extension root unless absolute paths are deliberately supplied by an approved manifest."),
	};
}

bool loadExtensionManifest(const QString& manifestPath, ExtensionManifest* manifest, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!manifest) {
		return false;
	}
	QFile file(manifestPath);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAdvancedStudio", "Unable to open extension manifest.");
		}
		return false;
	}
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAdvancedStudio", "Extension manifest is not a JSON object.");
		}
		return false;
	}
	const QJsonObject object = document.object();
	ExtensionManifest parsed;
	parsed.manifestPath = QFileInfo(manifestPath).absoluteFilePath();
	parsed.rootPath = QFileInfo(parsed.manifestPath).absolutePath();
	parsed.schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(ExtensionManifest::kSchemaVersion);
	parsed.id = normalizedId(object.value(QStringLiteral("id")).toString());
	parsed.displayName = object.value(QStringLiteral("displayName")).toString(parsed.id);
	parsed.version = object.value(QStringLiteral("version")).toString(QStringLiteral("0.0.0"));
	parsed.description = object.value(QStringLiteral("description")).toString();
	parsed.trustLevel = normalizedId(object.value(QStringLiteral("trustLevel")).toString(QStringLiteral("workspace")));
	parsed.sandboxModel = normalizedId(object.value(QStringLiteral("sandbox")).toString(object.value(QStringLiteral("sandboxModel")).toString(QStringLiteral("command-plan"))));
	parsed.capabilities = jsonStringArray(object.value(QStringLiteral("capabilities")));
	if (parsed.id.isEmpty()) {
		parsed.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Manifest id is missing.");
	}
	if (!QStringList {QStringLiteral("bundled"), QStringLiteral("workspace"), QStringLiteral("user-approved"), QStringLiteral("disabled")}.contains(parsed.trustLevel)) {
		parsed.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Unknown trust level; command execution will be blocked.");
		parsed.trustLevel = QStringLiteral("disabled");
	}
	if (!QStringList {QStringLiteral("metadata-only"), QStringLiteral("command-plan"), QStringLiteral("staged-files")}.contains(parsed.sandboxModel)) {
		parsed.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Unknown sandbox model; using metadata-only.");
		parsed.sandboxModel = QStringLiteral("metadata-only");
	}

	const QJsonArray commands = object.value(QStringLiteral("commands")).toArray();
	for (const QJsonValue& value : commands) {
		if (!value.isObject()) {
			continue;
		}
		const QJsonObject commandObject = value.toObject();
		ExtensionCommandDescriptor command;
		command.id = normalizedId(commandObject.value(QStringLiteral("id")).toString());
		command.displayName = commandObject.value(QStringLiteral("displayName")).toString(command.id);
		command.description = commandObject.value(QStringLiteral("description")).toString();
		command.program = commandObject.value(QStringLiteral("program")).toString();
		command.arguments = jsonStringArray(commandObject.value(QStringLiteral("arguments")));
		command.workingDirectory = commandObject.value(QStringLiteral("workingDirectory")).toString(QStringLiteral("${extensionRoot}"));
		command.capabilities = jsonStringArray(commandObject.value(QStringLiteral("capabilities")));
		command.requiresApproval = commandObject.value(QStringLiteral("requiresApproval")).toBool(true);
		for (const QJsonValue& fileValue : commandObject.value(QStringLiteral("generatedFiles")).toArray()) {
			if (fileValue.isObject()) {
				command.generatedFiles.push_back(generatedFileFromJson(fileValue.toObject()));
			}
		}
		if (command.id.isEmpty()) {
			parsed.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "An extension command is missing an id.");
			continue;
		}
		parsed.commands.push_back(command);
	}
	if (parsed.commands.isEmpty()) {
		parsed.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "No extension commands are declared.");
	}
	*manifest = parsed;
	return true;
}

ExtensionDiscoveryResult discoverExtensions(const QStringList& searchRoots)
{
	ExtensionDiscoveryResult result;
	result.searchRoots = searchRoots;
	for (const QString& root : searchRoots) {
		if (root.trimmed().isEmpty()) {
			continue;
		}
		const QFileInfo rootInfo(root);
		if (!rootInfo.exists()) {
			result.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Extension root does not exist: %1").arg(QDir::toNativeSeparators(root));
			continue;
		}
		if (rootInfo.isFile() && rootInfo.fileName() == QStringLiteral("vibestudio.extension.json")) {
			ExtensionManifest manifest;
			QString error;
			if (loadExtensionManifest(rootInfo.absoluteFilePath(), &manifest, &error)) {
				result.manifests.push_back(manifest);
			} else {
				result.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Unable to load %1: %2").arg(QDir::toNativeSeparators(rootInfo.absoluteFilePath()), error);
			}
			continue;
		}
		QDirIterator iterator(rootInfo.absoluteFilePath(), {QStringLiteral("vibestudio.extension.json")}, QDir::Files, QDirIterator::Subdirectories);
		while (iterator.hasNext()) {
			ExtensionManifest manifest;
			QString error;
			const QString path = iterator.next();
			if (loadExtensionManifest(path, &manifest, &error)) {
				result.manifests.push_back(manifest);
			} else {
				result.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Unable to load %1: %2").arg(QDir::toNativeSeparators(path), error);
			}
		}
	}
	result.state = result.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning;
	return result;
}

ExtensionCommandPlan buildExtensionCommandPlan(const ExtensionManifest& manifest, const QString& commandId, const QStringList& extraArguments, bool dryRun, bool allowExecution)
{
	ExtensionCommandPlan plan;
	plan.manifest = manifest;
	plan.dryRun = dryRun;
	plan.executionAllowed = allowExecution && !dryRun;
	const QString normalizedCommand = normalizedId(commandId);
	for (const ExtensionCommandDescriptor& command : manifest.commands) {
		if (command.id == normalizedCommand) {
			plan.command = command;
			break;
		}
	}
	if (plan.command.id.isEmpty()) {
		plan.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Extension command was not found.");
		plan.state = OperationState::Failed;
		return plan;
	}
	plan.program = substitutedExtensionValue(plan.command.program, manifest);
	for (const QString& argument : plan.command.arguments) {
		plan.arguments << substitutedExtensionValue(argument, manifest);
	}
	plan.arguments += extraArguments;
	plan.workingDirectory = substitutedExtensionValue(plan.command.workingDirectory, manifest);
	if (plan.workingDirectory.trimmed().isEmpty()) {
		plan.workingDirectory = manifest.rootPath;
	}
	if (manifest.trustLevel == QStringLiteral("disabled")) {
		plan.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Extension trust level is disabled; execution is blocked.");
		plan.executionAllowed = false;
	}
	if (manifest.sandboxModel == QStringLiteral("metadata-only")) {
		plan.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Metadata-only sandbox blocks command execution.");
		plan.executionAllowed = false;
	}
	if (plan.command.requiresApproval && !allowExecution) {
		plan.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Command requires approval; dry-run plan is available.");
	}
	if (plan.program.trimmed().isEmpty()) {
		plan.warnings << QCoreApplication::translate("VibeStudioAdvancedStudio", "Command program is empty.");
		plan.state = OperationState::Failed;
	} else {
		plan.state = plan.executionAllowed ? OperationState::Queued : OperationState::Warning;
	}
	for (const ExtensionGeneratedFile& file : plan.command.generatedFiles) {
		plan.stagingLines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Stage generated file %1: %2").arg(file.virtualPath, file.summary.isEmpty() ? file.sourceDescription : file.summary);
	}
	return plan;
}

ExtensionCommandResult runExtensionCommand(const ExtensionCommandPlan& plan, int timeoutMs)
{
	ExtensionCommandResult result;
	result.plan = plan;
	result.dryRun = plan.dryRun || !plan.executionAllowed;
	result.finishedUtc = QDateTime::currentDateTimeUtc();
	if (result.dryRun) {
		result.state = plan.state == OperationState::Failed ? OperationState::Failed : OperationState::Warning;
		result.error = plan.state == OperationState::Failed ? QCoreApplication::translate("VibeStudioAdvancedStudio", "Command plan is invalid.") : QCoreApplication::translate("VibeStudioAdvancedStudio", "Dry run only; command was not started.");
		return result;
	}
	QProcess process;
	process.setProgram(plan.program);
	process.setArguments(plan.arguments);
	process.setWorkingDirectory(plan.workingDirectory);
	process.start();
	if (!process.waitForStarted(5000)) {
		result.error = process.errorString();
		result.state = OperationState::Failed;
		return result;
	}
	result.started = true;
	if (!process.waitForFinished(timeoutMs)) {
		process.kill();
		process.waitForFinished(1000);
		result.error = QCoreApplication::translate("VibeStudioAdvancedStudio", "Extension command timed out.");
		result.state = OperationState::Failed;
		return result;
	}
	result.exitCode = process.exitCode();
	result.stdoutText = QString::fromUtf8(process.readAllStandardOutput());
	result.stderrText = QString::fromUtf8(process.readAllStandardError());
	result.state = result.exitCode == 0 ? OperationState::Completed : OperationState::Failed;
	return result;
}

QString extensionManifestText(const ExtensionManifest& manifest)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Extension: %1 [%2]").arg(manifest.displayName, manifest.id);
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Version: %1").arg(manifest.version);
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Trust: %1").arg(manifest.trustLevel);
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Sandbox: %1").arg(manifest.sandboxModel);
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Root: %1").arg(QDir::toNativeSeparators(manifest.rootPath));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Capabilities: %1").arg(manifest.capabilities.isEmpty() ? QCoreApplication::translate("VibeStudioAdvancedStudio", "none") : manifest.capabilities.join(QStringLiteral(", ")));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Commands:");
	for (const ExtensionCommandDescriptor& command : manifest.commands) {
		lines << QStringLiteral("- %1 [%2]: %3").arg(command.displayName, command.id, command.description);
		lines << QStringLiteral("  %1 %2").arg(command.program, command.arguments.join(QLatin1Char(' ')));
		for (const ExtensionGeneratedFile& file : command.generatedFiles) {
			lines << QStringLiteral("  %1").arg(QCoreApplication::translate("VibeStudioAdvancedStudio", "Stages: %1").arg(file.virtualPath));
		}
	}
	for (const QString& warning : manifest.warnings) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Warning: %1").arg(warning);
	}
	return lines.join('\n');
}

QString extensionDiscoveryText(const ExtensionDiscoveryResult& result)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Extension discovery");
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Roots: %1").arg(result.searchRoots.join(QStringLiteral("; ")));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Extensions: %1").arg(result.manifests.size());
	for (const ExtensionManifest& manifest : result.manifests) {
		lines << extensionManifestText(manifest);
	}
	for (const QString& warning : result.warnings) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Warning: %1").arg(warning);
	}
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Trust model:");
	lines << extensionTrustModelLines();
	return lines.join('\n');
}

QString extensionCommandPlanText(const ExtensionCommandPlan& plan)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Extension command plan");
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Extension: %1").arg(plan.manifest.id);
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Command: %1").arg(plan.command.id);
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Program: %1").arg(QDir::toNativeSeparators(plan.program));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Arguments: %1").arg(plan.arguments.join(QLatin1Char(' ')));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Working directory: %1").arg(QDir::toNativeSeparators(plan.workingDirectory));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Dry run: %1").arg(plan.dryRun ? QCoreApplication::translate("VibeStudioAdvancedStudio", "yes") : QCoreApplication::translate("VibeStudioAdvancedStudio", "no"));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Execution allowed: %1").arg(plan.executionAllowed ? QCoreApplication::translate("VibeStudioAdvancedStudio", "yes") : QCoreApplication::translate("VibeStudioAdvancedStudio", "no"));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Generated file staging:");
	lines << (plan.stagingLines.isEmpty() ? QStringList {QCoreApplication::translate("VibeStudioAdvancedStudio", "No generated files declared.")} : plan.stagingLines);
	for (const QString& warning : plan.warnings) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Warning: %1").arg(warning);
	}
	return lines.join('\n');
}

QString extensionCommandResultText(const ExtensionCommandResult& result)
{
	QStringList lines;
	lines << extensionCommandPlanText(result.plan);
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Started: %1").arg(result.started ? QCoreApplication::translate("VibeStudioAdvancedStudio", "yes") : QCoreApplication::translate("VibeStudioAdvancedStudio", "no"));
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Exit code: %1").arg(result.exitCode);
	if (!result.stdoutText.trimmed().isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Stdout:");
		lines << result.stdoutText.trimmed();
	}
	if (!result.stderrText.trimmed().isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Stderr:");
		lines << result.stderrText.trimmed();
	}
	if (!result.error.trimmed().isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Result: %1").arg(result.error);
	}
	return lines.join('\n');
}

static AiWorkflowResult makeCreationWorkflow(const QString& workflowId, const QString& title, const QString& prompt, const QString& capability, const QString& stagedId, const QString& stagedKind, const QString& stagedLabel, const QString& proposedPath, const QStringList& previewLines, const QStringList& actions, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	QString provider = providerId.trimmed().isEmpty() ? normalized.preferredCodingConnectorId : providerId;
	if (provider.trimmed().isEmpty()) {
		provider = normalized.aiFreeMode || normalized.projectAiFree ? QStringLiteral("local-offline") : defaultAiReasoningConnectorId();
	}
	QString model = modelId.trimmed().isEmpty() ? defaultAiModelId(provider, capability) : modelId;
	AiWorkflowResult result;
	result.title = title;
	result.summary = QCoreApplication::translate("VibeStudioAdvancedStudio", "Reviewable %1 proposal generated without writing files.").arg(title.toLower());
	result.reviewableText = previewLines.join('\n');
	result.nextActions = actions;
	result.diagnostics = {QCoreApplication::translate("VibeStudioAdvancedStudio", "No provider call was made; output is deterministic local scaffold text.")};
	result.manifest = defaultAiWorkflowManifest(workflowId, provider, model, prompt);
	result.manifest.contextSummary = QCoreApplication::translate("VibeStudioAdvancedStudio", "Prompt text, VibeStudio tool descriptors, and local project/package conventions only.");
	result.manifest.toolCalls.push_back({QStringLiteral("staged-text-edit"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Generated staged text output for review."), OperationState::Completed, {prompt}, {proposedPath}, {}, true, true});
	result.manifest.stagedOutputs.push_back({stagedId, stagedKind, stagedLabel, proposedPath, result.summary, previewLines, true});
	result.manifest.approvalState = QStringLiteral("review-required");
	return result;
}

AiWorkflowResult promptToShaderScaffoldAiExperiment(const QString& prompt, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString shaderName = safeShaderName(prompt);
	QStringList preview;
	preview << shaderName;
	preview << QStringLiteral("{");
	preview << QStringLiteral("\tqer_editorimage %1_editor").arg(shaderName);
	preview << QStringLiteral("\tsurfaceparm nomarks");
	preview << QStringLiteral("\t{");
	preview << QStringLiteral("\t\tmap %1_d").arg(shaderName);
	preview << QStringLiteral("\t\trgbGen identity");
	preview << QStringLiteral("\t}");
	preview << QStringLiteral("\t{");
	preview << QStringLiteral("\t\tmap %1_glow").arg(shaderName);
	preview << QStringLiteral("\t\tblendFunc GL_ONE GL_ONE");
	preview << QStringLiteral("\t\trgbGen wave sin 0.5 0.5 0 1");
	preview << QStringLiteral("\t}");
	preview << QStringLiteral("}");
	return makeCreationWorkflow(QStringLiteral("prompt-to-shader-scaffold"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader Scaffold"), prompt, QStringLiteral("coding"), QStringLiteral("shader-scaffold"), QStringLiteral("shader"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Shader scaffold"), QStringLiteral("scripts/vibestudio/generated.shader"), preview, {QCoreApplication::translate("VibeStudioAdvancedStudio", "Review shader name, qer_editorimage, stage maps, and blend modes."), QCoreApplication::translate("VibeStudioAdvancedStudio", "Run shader inspect and shader validate with mounted packages before staging.")}, preferences, providerId, modelId);
}

AiWorkflowResult promptToEntityDefinitionAiExperiment(const QString& prompt, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString entityName = QStringLiteral("vibestudio_%1").arg(sanitizePathToken(prompt).replace('-', '_').left(32));
	const QStringList preview = {
		QStringLiteral("/*QUAKED %1 (0.2 0.8 1.0) (-16 -16 -24) (16 16 48)").arg(entityName),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Review generated keys before copying into a .def or .ent file."),
		QStringLiteral("-------- KEYS --------"),
		QStringLiteral("\"targetname\" : \"Optional target name.\""),
		QStringLiteral("\"message\" : \"Editor-facing note or trigger text.\""),
		QStringLiteral("-------- SPAWNFLAGS --------"),
		QStringLiteral("1 : initially disabled"),
		QStringLiteral("*/"),
		QStringLiteral("{"),
		QStringLiteral("\"classname\" \"%1\"").arg(entityName),
		QStringLiteral("\"editor_usage\" \"%1\"").arg(prompt.left(96).replace('"', '\'')),
		QStringLiteral("}"),
	};
	return makeCreationWorkflow(QStringLiteral("prompt-to-entity-definition"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Entity Definition Snippet"), prompt, QStringLiteral("coding"), QStringLiteral("entity-definition"), QStringLiteral("entity-definition"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Entity definition snippet"), QStringLiteral("scripts/vibestudio/generated.def"), preview, {QCoreApplication::translate("VibeStudioAdvancedStudio", "Review bbox, keys, spawnflags, and classname against the target engine."), QCoreApplication::translate("VibeStudioAdvancedStudio", "Stage as a text edit only after package/project validation passes.")}, preferences, providerId, modelId);
}

AiWorkflowResult promptToPackageValidationPlanAiExperiment(const QString& prompt, const QString& packagePath, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString package = packagePath.trimmed().isEmpty() ? QStringLiteral("<package-path>") : packagePath;
	const QStringList preview = {
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Package validation plan"),
		QStringLiteral("vibestudio --cli package validate \"%1\" --json").arg(package),
		QStringLiteral("vibestudio --cli package list \"%1\" --json").arg(package),
		QStringLiteral("vibestudio --cli shader inspect <shader-file> --package \"%1\" --json").arg(package),
		QStringLiteral("vibestudio --cli asset find <project-root> --find \"%1\" --json").arg(prompt.left(32).replace('"', '\'')),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Review archive warnings, missing shader references, generated-file staging, and release manifest output before save-as."),
	};
	return makeCreationWorkflow(QStringLiteral("prompt-to-package-validation-plan"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Package Validation Plan"), prompt, QStringLiteral("reasoning"), QStringLiteral("package-validation-plan"), QStringLiteral("plan"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Package validation plan"), QStringLiteral(".vibestudio/ai-staged/package-validation-plan.txt"), preview, {QCoreApplication::translate("VibeStudioAdvancedStudio", "Run validation commands in dry-run or JSON mode first."), QCoreApplication::translate("VibeStudioAdvancedStudio", "Use package stage/save-as only after blockers are cleared.")}, preferences, providerId, modelId);
}

AiWorkflowResult promptToBatchConversionRecipeAiExperiment(const QString& prompt, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString lower = prompt.toLower();
	const QString format = lower.contains(QStringLiteral("jpg")) || lower.contains(QStringLiteral("jpeg")) ? QStringLiteral("jpg") : QStringLiteral("png");
	const QString palette = lower.contains(QStringLiteral("doom")) ? QStringLiteral("indexed") : lower.contains(QStringLiteral("gray")) ? QStringLiteral("grayscale") : QStringLiteral("indexed");
	const QStringList preview = {
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Batch conversion recipe"),
		QStringLiteral("vibestudio --cli asset convert <package> --output <converted-folder> --format %1 --palette %2 --dry-run --json").arg(format, palette),
		QStringLiteral("vibestudio --cli sprite plan --engine doom --name SPRT --frames 4 --rotations 8 --palette %1 --package-root sprites --json").arg(palette),
		QCoreApplication::translate("VibeStudioAdvancedStudio", "Review dimensions, palette loss, staged paths, and package save-as conflicts before writing converted files."),
	};
	return makeCreationWorkflow(QStringLiteral("prompt-to-batch-conversion-recipe"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Batch Conversion Recipe"), prompt, QStringLiteral("coding"), QStringLiteral("batch-conversion-recipe"), QStringLiteral("recipe"), QCoreApplication::translate("VibeStudioAdvancedStudio", "Batch conversion recipe"), QStringLiteral(".vibestudio/ai-staged/batch-conversion.txt"), preview, {QCoreApplication::translate("VibeStudioAdvancedStudio", "Run the conversion command in dry-run mode."), QCoreApplication::translate("VibeStudioAdvancedStudio", "Inspect generated images and sprite frame sequence before package staging.")}, preferences, providerId, modelId);
}

QString aiProposalReviewSurfaceText(const AiWorkflowResult& result)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "AI Proposal Review");
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Summary: %1").arg(result.summary);
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Provider/model: %1 / %2").arg(result.manifest.providerId, result.manifest.modelId);
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Approval: %1").arg(result.manifest.approvalState);
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Context used:");
	lines << result.manifest.contextSummary;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Generated actions:");
	for (const AiWorkflowToolCall& call : result.manifest.toolCalls) {
		lines << QStringLiteral("- %1: %2").arg(call.toolId, call.summary);
	}
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Staged outputs:");
	for (const AiStagedOutput& output : result.manifest.stagedOutputs) {
		lines << QStringLiteral("- %1 [%2] %3").arg(output.label, output.kind, output.proposedPath);
	}
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Prompt log:");
	lines << result.manifest.prompt;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Response preview:");
	lines << result.reviewableText;
	lines << QCoreApplication::translate("VibeStudioAdvancedStudio", "Validation:");
	lines << result.manifest.validationSummary;
	return lines.join('\n');
}

} // namespace vibestudio
