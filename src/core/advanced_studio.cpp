#include "core/advanced_studio.h"

#include "core/compiler_profiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
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

QString studioText(const char* source)
{
	return QCoreApplication::translate("VibeStudioAdvancedStudio", source);
}

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
		.arg(issue.line > 0 ? studioText(" (line %1)").arg(issue.line) : QString());
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
			*error = studioText("Unable to open file.");
		}
		return false;
	}
	const QByteArray bytes = file.readAll();
	QStringDecoder decoder(QStringDecoder::Utf8);
	const QString decoded = decoder.decode(bytes);
	if (decoder.hasError()) {
		if (error) {
			*error = studioText("File is not valid UTF-8 text.");
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
		|| normalized == QStringLiteral("qer-editorimage");
}

QStringList textureReferencesFromDirective(const QString& line)
{
	QStringList references;
	const QString token = normalizedId(firstToken(line));
	if (!shaderDirectiveLooksTexture(token)) {
		return references;
	}
	QString tail = directiveTail(line);
	if (token == QStringLiteral("animmap")) {
		QStringList parts = tail.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
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
	if (!tail.startsWith('$') && !tail.isEmpty()) {
		references << normalizedVirtualPath(tail.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).value(0));
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
	if (ext == QStringLiteral("def") || ext == QStringLiteral("ent")) {
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
	const QString name = QFileInfo(path).fileName().toLower();
	return QStringList {
		QStringLiteral(".git"),
		QStringLiteral(".vibestudio"),
		QStringLiteral("build"),
		QStringLiteral("builddir"),
		QStringLiteral("dist"),
		QStringLiteral("external"),
	}.contains(name);
}

QString relativeToRoot(const QString& rootPath, const QString& filePath)
{
	QString relative = QDir(rootPath).relativeFilePath(filePath);
	relative.replace('\\', '/');
	return relative;
}

QVector<CodeSymbol> symbolsFromText(const QString& rootPath, const QString& filePath, const QString& text, const QString& query)
{
	QVector<CodeSymbol> symbols;
	const QString relative = relativeToRoot(rootPath, filePath);
	const QStringList lines = text.split('\n');
	const QRegularExpression functionPattern(QStringLiteral(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*(?:\{|$))"));
	const QRegularExpression shaderPattern(QStringLiteral(R"(^\s*([A-Za-z0-9_./-]+)\s*$)"));
	const QRegularExpression entityPattern(QStringLiteral(R"(\b(classname|spawnclass|model)\b\s+\"?([A-Za-z0-9_./-]+)\"?)"));
	const QString suffix = QFileInfo(filePath).suffix().toLower();
	for (int index = 0; index < lines.size(); ++index) {
		const QString line = stripShaderComment(lines[index]).trimmed();
		if (line.isEmpty()) {
			continue;
		}
		CodeSymbol symbol;
		symbol.filePath = filePath;
		symbol.relativePath = relative;
		symbol.line = index + 1;
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
		if (symbol.name.isEmpty()) {
			const QRegularExpressionMatch function = functionPattern.match(line);
			if (function.hasMatch()) {
				const QString name = function.captured(1);
				if (!QStringList {QStringLiteral("if"), QStringLiteral("while"), QStringLiteral("for"), QStringLiteral("switch")}.contains(name)) {
					symbol.name = name;
					symbol.kind = QStringLiteral("function");
					symbol.column = function.capturedStart(1) + 1;
				}
			}
		}
		if (!symbol.name.isEmpty() && (query.trimmed().isEmpty() || symbol.name.contains(query, Qt::CaseInsensitive))) {
			symbols.push_back(symbol);
		}
	}
	return symbols;
}

QVector<CodeDiagnostic> diagnosticsFromText(const QString& rootPath, const QString& filePath, const QString& text)
{
	QVector<CodeDiagnostic> diagnostics;
	const QString relative = relativeToRoot(rootPath, filePath);
	int braceBalance = 0;
	const QStringList lines = text.split('\n');
	for (int index = 0; index < lines.size(); ++index) {
		const QString line = lines[index];
		braceBalance += line.count('{');
		braceBalance -= line.count('}');
		if (line.size() > 180) {
			diagnostics.push_back({QStringLiteral("warning"), studioText("Line may wrap in compact code surfaces."), filePath, relative, index + 1, 181});
		}
		if (line.contains(QStringLiteral("TODO"), Qt::CaseInsensitive) || line.contains(QStringLiteral("FIXME"), Qt::CaseInsensitive)) {
			const int markerColumn = static_cast<int>(line.indexOf(QRegularExpression(QStringLiteral("TODO|FIXME"), QRegularExpression::CaseInsensitiveOption))) + 1;
			diagnostics.push_back({QStringLiteral("info"), studioText("Task marker found."), filePath, relative, index + 1, std::max(1, markerColumn)});
		}
	}
	if (braceBalance != 0) {
		diagnostics.push_back({QStringLiteral("warning"), studioText("Brace balance is %1.").arg(braceBalance), filePath, relative, std::max(1, static_cast<int>(lines.size())), 1});
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
		studioText("Shader graph: parse idTech3 shader scripts, list stages, preview directives, edit blend/map directives, round-trip to text, and validate texture references against mounted packages."),
		studioText("Sprite creator: produce Doom lump naming plans, Quake sprite package plans, palette conversion previews, frame sequencing, and staged package virtual paths."),
		studioText("Code IDE: index source trees, expose language-service hook descriptors, collect lightweight diagnostics, search symbols, and list build/run profile handoffs."),
		studioText("AI-assisted creation: generate reviewable shader, entity definition, package validation, batch conversion, and CLI command proposals without writing files."),
		studioText("Extensions: parse manifests, describe trust/sandbox boundaries, discover extensions, plan/execute approved commands, and stage generated files."),
	};
}

ShaderDocument parseShaderScriptText(const QString& text, const QString& sourcePath)
{
	ShaderDocument document;
	document.sourcePath = sourcePath;
	document.originalText = text;

	const QStringList lines = text.split('\n');
	ShaderDefinition currentShader;
	ShaderStage currentStage;
	QString pendingShaderName;
	int depth = 0;
	bool inShader = false;
	bool inStage = false;
	QStringList shaderRaw;
	QStringList stageRaw;

	auto finishStage = [&]() {
		if (!inStage) {
			return;
		}
		currentStage.endLine = currentStage.endLine == 0 ? currentStage.startLine : currentStage.endLine;
		currentStage.rawText = stageRaw.join('\n');
		currentShader.stages.push_back(currentStage);
		currentShader.textureReferences += currentStage.textureReferences;
		currentStage = {};
		stageRaw.clear();
		inStage = false;
	};

	auto finishShader = [&]() {
		if (!inShader) {
			return;
		}
		finishStage();
		currentShader.endLine = currentShader.endLine == 0 ? currentShader.startLine : currentShader.endLine;
		currentShader.rawText = shaderRaw.join('\n');
		QStringList unique;
		for (const QString& reference : currentShader.textureReferences) {
			appendUnique(&unique, reference);
		}
		currentShader.textureReferences = unique;
		document.shaders.push_back(currentShader);
		currentShader = {};
		shaderRaw.clear();
		inShader = false;
	};

	for (int index = 0; index < lines.size(); ++index) {
		const QString rawLine = lines[index];
		const QString line = stripShaderComment(rawLine).trimmed();
		if (!inShader && line.isEmpty()) {
			continue;
		}

		if (!inShader) {
			if (line == QStringLiteral("{") || line == QStringLiteral("}")) {
				document.issues.push_back({QStringLiteral("warning"), QStringLiteral("shader-orphan-brace"), studioText("Shader brace appeared before a shader name."), QString(), index + 1});
				continue;
			}
			pendingShaderName = line;
			continue;
		}

		shaderRaw << rawLine;
		if (inStage) {
			stageRaw << rawLine;
		}

		if (!pendingShaderName.isEmpty() && line == QStringLiteral("{") && !inShader) {
			continue;
		}

		if (!inStage && line == QStringLiteral("{") && depth == 1) {
			inStage = true;
			currentStage = {};
			currentStage.id = currentShader.stages.size();
			currentStage.shaderName = currentShader.name;
			currentStage.startLine = index + 1;
			stageRaw = {rawLine};
			++depth;
			continue;
		}

		if (line == QStringLiteral("{")) {
			++depth;
			continue;
		}

		if (line == QStringLiteral("}")) {
			if (inStage && depth == 2) {
				currentStage.endLine = index + 1;
				finishStage();
				--depth;
				continue;
			}
			--depth;
			if (depth <= 0) {
				currentShader.endLine = index + 1;
				finishShader();
				depth = 0;
			}
			continue;
		}

		if (line.isEmpty()) {
			continue;
		}

		if (inStage) {
			currentStage.directives << line;
			const QString token = normalizedId(firstToken(line));
			if (token == QStringLiteral("map") || token == QStringLiteral("clampmap")) {
				currentStage.mapDirective = directiveTail(line).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).value(0);
			} else if (token == QStringLiteral("blendfunc")) {
				currentStage.blendFunc = directiveTail(line);
			} else if (token == QStringLiteral("rgbgen")) {
				currentStage.rgbGen = directiveTail(line);
			} else if (token == QStringLiteral("alphagen")) {
				currentStage.alphaGen = directiveTail(line);
			} else if (token == QStringLiteral("tcmod")) {
				currentStage.tcMod = directiveTail(line);
			}
			for (const QString& reference : textureReferencesFromDirective(line)) {
				appendUnique(&currentStage.textureReferences, reference);
			}
		} else {
			currentShader.directives << line;
			for (const QString& reference : textureReferencesFromDirective(line)) {
				appendUnique(&currentShader.textureReferences, reference);
			}
		}

		if (!pendingShaderName.isEmpty()) {
			continue;
		}
	}

	// Second pass with explicit shader starts. The first pass above keeps the
	// parser simple for nested stage bodies; this loop drives state changes when
	// a name line is followed by an opening brace.
	document.shaders.clear();
	document.issues.clear();
	currentShader = {};
	pendingShaderName.clear();
	depth = 0;
	inShader = false;
	inStage = false;
	shaderRaw.clear();
	stageRaw.clear();
	for (int index = 0; index < lines.size(); ++index) {
		const QString rawLine = lines[index];
		const QString line = stripShaderComment(rawLine).trimmed();
		if (!inShader) {
			if (line.isEmpty()) {
				continue;
			}
			if (pendingShaderName.isEmpty()) {
				if (line == QStringLiteral("{") || line == QStringLiteral("}")) {
					document.issues.push_back({QStringLiteral("warning"), QStringLiteral("shader-orphan-brace"), studioText("Shader brace appeared before a shader name."), QString(), index + 1});
					continue;
				}
				pendingShaderName = line;
				continue;
			}
			if (line == QStringLiteral("{")) {
				inShader = true;
				depth = 1;
				currentShader = {};
				currentShader.id = document.shaders.size();
				currentShader.name = pendingShaderName;
				currentShader.startLine = index;
				shaderRaw = {pendingShaderName, rawLine};
				pendingShaderName.clear();
				continue;
			}
			document.issues.push_back({QStringLiteral("warning"), QStringLiteral("shader-missing-brace"), studioText("Shader name was not followed by an opening brace."), pendingShaderName, index});
			pendingShaderName = line;
			continue;
		}

		shaderRaw << rawLine;
		if (inStage) {
			stageRaw << rawLine;
		}
		if (line.isEmpty()) {
			continue;
		}
		if (!inStage && line == QStringLiteral("{") && depth == 1) {
			inStage = true;
			currentStage = {};
			currentStage.id = currentShader.stages.size();
			currentStage.shaderName = currentShader.name;
			currentStage.startLine = index + 1;
			stageRaw = {rawLine};
			++depth;
			continue;
		}
		if (line == QStringLiteral("{")) {
			++depth;
			continue;
		}
		if (line == QStringLiteral("}")) {
			if (inStage && depth == 2) {
				currentStage.endLine = index + 1;
				finishStage();
				--depth;
				continue;
			}
			--depth;
			if (depth <= 0) {
				currentShader.endLine = index + 1;
				finishShader();
				depth = 0;
			}
			continue;
		}

		if (inStage) {
			currentStage.directives << line;
			const QString token = normalizedId(firstToken(line));
			if (token == QStringLiteral("map") || token == QStringLiteral("clampmap")) {
				currentStage.mapDirective = directiveTail(line).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).value(0);
			} else if (token == QStringLiteral("blendfunc")) {
				currentStage.blendFunc = directiveTail(line);
			} else if (token == QStringLiteral("rgbgen")) {
				currentStage.rgbGen = directiveTail(line);
			} else if (token == QStringLiteral("alphagen")) {
				currentStage.alphaGen = directiveTail(line);
			} else if (token == QStringLiteral("tcmod")) {
				currentStage.tcMod = directiveTail(line);
			}
			for (const QString& reference : textureReferencesFromDirective(line)) {
				appendUnique(&currentStage.textureReferences, reference);
			}
		} else {
			currentShader.directives << line;
			for (const QString& reference : textureReferencesFromDirective(line)) {
				appendUnique(&currentShader.textureReferences, reference);
			}
		}
	}
	if (inShader) {
		document.issues.push_back({QStringLiteral("error"), QStringLiteral("shader-unclosed"), studioText("Shader block ended before its closing brace."), currentShader.name, currentShader.startLine});
		currentShader.endLine = lines.size();
		finishShader();
	}
	if (!pendingShaderName.isEmpty()) {
		document.issues.push_back({QStringLiteral("warning"), QStringLiteral("shader-dangling-name"), studioText("Shader name has no body."), pendingShaderName, static_cast<int>(lines.size())});
	}
	if (document.shaders.isEmpty()) {
		document.issues.push_back({QStringLiteral("warning"), QStringLiteral("shader-empty"), studioText("No idTech3 shader definitions were parsed."), QString(), 0});
	}
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
			*error = studioText("Shader script path is empty.");
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
			*error = studioText("No shader document is loaded.");
		}
		return false;
	}
	if (shaderName.trimmed().isEmpty()) {
		if (error) {
			*error = studioText("Shader name is required.");
		}
		return false;
	}
	if (stageIndex < 0) {
		if (error) {
			*error = studioText("Stage index must be zero or greater.");
		}
		return false;
	}
	for (ShaderDefinition& shader : document->shaders) {
		if (QString::compare(shader.name, shaderName, Qt::CaseInsensitive) != 0) {
			continue;
		}
		if (stageIndex >= shader.stages.size()) {
			if (error) {
				*error = studioText("Shader stage index is out of range.");
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
		*error = studioText("Shader was not found.");
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
		lines << studioText("No shader graph nodes.");
		return lines;
	}
	for (const ShaderDefinition& shader : document.shaders) {
		lines << studioText("Shader %1: %2 stage(s), %3 texture reference(s)").arg(shader.name).arg(shader.stages.size()).arg(shader.textureReferences.size());
		for (const ShaderStage& stage : shader.stages) {
			lines << QStringLiteral("  [%1] map=%2 blend=%3 rgb=%4")
				.arg(stage.id)
				.arg(stage.mapDirective.isEmpty() ? studioText("none") : stage.mapDirective)
				.arg(stage.blendFunc.isEmpty() ? studioText("default") : stage.blendFunc)
				.arg(stage.rgbGen.isEmpty() ? studioText("identity") : stage.rgbGen);
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
		lines << studioText("Shader: %1").arg(shader.name);
		lines << studioText("Directives: %1").arg(shader.directives.isEmpty() ? studioText("none") : shader.directives.join(QStringLiteral("; ")));
		for (const ShaderStage& stage : shader.stages) {
			if (stageIndex >= 0 && stage.id != stageIndex) {
				continue;
			}
			lines << studioText("Stage %1").arg(stage.id);
			lines << studioText("  Map: %1").arg(stage.mapDirective.isEmpty() ? studioText("none") : stage.mapDirective);
			lines << studioText("  Blend: %1").arg(stage.blendFunc.isEmpty() ? studioText("default") : stage.blendFunc);
			lines << studioText("  RGB: %1").arg(stage.rgbGen.isEmpty() ? studioText("identity") : stage.rgbGen);
			lines << studioText("  Alpha: %1").arg(stage.alphaGen.isEmpty() ? studioText("default") : stage.alphaGen);
			lines << studioText("  TC Mod: %1").arg(stage.tcMod.isEmpty() ? studioText("none") : stage.tcMod);
			lines << studioText("  Raw:");
			lines << stageTextLines(stage);
		}
	}
	if (lines.isEmpty()) {
		lines << studioText("No matching shader stage.");
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
				warnings->push_back(studioText("Unable to mount %1: %2").arg(QDir::toNativeSeparators(packagePath), error));
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
		warnings->push_back(studioText("No package paths were supplied; references are listed but not resolved."));
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
		lines << QStringLiteral("%1 [%2]").arg(result.textureReference, result.found ? studioText("found") : studioText("missing"));
		if (result.found) {
			lines << QStringLiteral("  %1").arg(QDir::toNativeSeparators(result.foundInPackage));
		} else {
			lines << QStringLiteral("  %1").arg(studioText("Checked: %1").arg(result.candidatePaths.join(QStringLiteral(", "))));
		}
	}
	for (const QString& warning : warnings) {
		lines << studioText("Warning: %1").arg(warning);
	}
	if (lines.isEmpty()) {
		lines << studioText("No shader texture references.");
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
		report.errors << studioText("Output path is required.");
		return report;
	}
	if (!dryRun && QFileInfo::exists(report.outputPath) && !overwriteExisting) {
		report.errors << studioText("Output exists. Use overwrite to replace it.");
		return report;
	}
	const QString text = shaderDocumentText(document);
	report.summaryLines << studioText("Shaders: %1").arg(document.shaders.size());
	report.summaryLines << studioText("Bytes: %1").arg(text.toUtf8().size());
	if (dryRun) {
		report.summaryLines << studioText("Dry run: shader text would be written.");
		return report;
	}
	const QFileInfo outputInfo(report.outputPath);
	if (!QDir().mkpath(outputInfo.absolutePath())) {
		report.errors << studioText("Unable to create output directory.");
		return report;
	}
	QSaveFile file(report.outputPath);
	if (!file.open(QIODevice::WriteOnly)) {
		report.errors << studioText("Unable to open shader output.");
		return report;
	}
	const QByteArray bytes = text.toUtf8();
	if (file.write(bytes) != bytes.size() || !file.commit()) {
		report.errors << studioText("Unable to write shader output.");
		return report;
	}
	report.written = true;
	report.editState = QStringLiteral("saved");
	report.summaryLines << studioText("Shader text written.");
	return report;
}

QString shaderDocumentReportText(const ShaderDocument& document, const QVector<ShaderReferenceValidation>& validation, const QStringList& validationWarnings)
{
	QStringList lines;
	lines << studioText("Shader script");
	lines << studioText("Source: %1").arg(document.sourcePath.isEmpty() ? studioText("memory") : QDir::toNativeSeparators(document.sourcePath));
	lines << studioText("Shaders: %1").arg(document.shaders.size());
	lines << studioText("Edit state: %1").arg(document.editState);
	lines << QString();
	lines << studioText("Graph:");
	lines << shaderGraphLines(document);
	lines << QString();
	lines << studioText("Preview:");
	lines << shaderStagePreviewLines(document);
	lines << QString();
	lines << studioText("Reference validation:");
	lines << shaderReferenceValidationLines(validation, validationWarnings);
	if (!document.issues.isEmpty()) {
		lines << QString();
		lines << studioText("Issues:");
		for (const AdvancedStudioIssue& issue : document.issues) {
			lines << issueLine(issue);
		}
	}
	return lines.join('\n');
}

QString shaderSaveReportText(const ShaderSaveReport& report)
{
	QStringList lines;
	lines << studioText("Shader save-as");
	lines << studioText("Source: %1").arg(QDir::toNativeSeparators(report.sourcePath));
	lines << studioText("Output: %1").arg(QDir::toNativeSeparators(report.outputPath));
	lines << studioText("Mode: %1").arg(report.dryRun ? studioText("dry run") : studioText("write"));
	lines << studioText("Written: %1").arg(report.written ? studioText("yes") : studioText("no"));
	lines << report.summaryLines;
	for (const QString& warning : report.warnings) {
		lines << studioText("Warning: %1").arg(warning);
	}
	for (const QString& error : report.errors) {
		lines << studioText("Error: %1").arg(error);
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
		plan.warnings << studioText("Frame count was clamped to the supported A-Z range.");
	}
	if (plan.engineFamily != QStringLiteral("doom") && plan.engineFamily != QStringLiteral("quake")) {
		plan.warnings << studioText("Unknown sprite engine requested; using Quake-style package naming.");
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
				item.paletteAction = studioText("Convert to Doom PLAYPAL-indexed image or verify source palette.");
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
			item.paletteAction = studioText("Prepare frame for Quake SPR packing; keep source image staged until SPR writer is selected.");
			plan.frames.push_back(item);
		}
	}

	if (plan.engineFamily == QStringLiteral("quake")) {
		plan.stagingLines << studioText("Quake SPR manifest: %1/%2.spr").arg(plan.outputPackageRoot, sanitizePathToken(plan.spriteName));
	}
	for (const SpriteFramePlan& frame : plan.frames) {
		plan.sequenceLines << studioText("%1: frame %2 rotation %3 -> %4").arg(frame.index).arg(frame.frameId, frame.rotationId, frame.lumpName);
		if (request.stageForPackage) {
			plan.stagingLines << studioText("Stage %1 from %2").arg(frame.virtualPath, frame.sourcePath.isEmpty() ? studioText("<choose frame source>") : QDir::toNativeSeparators(frame.sourcePath));
		}
	}
	plan.palettePreviewLines = {
		studioText("Palette: %1").arg(plan.paletteId),
		plan.engineFamily == QStringLiteral("doom") ? studioText("Doom workflow: PLAYPAL index preview, fullbright ranges, transparent cyan policy review.") : studioText("Quake workflow: palette 0 transparency and colormap-aware preview before SPR packing."),
		studioText("Conversion remains staged; original source frames are preserved."),
	};
	plan.state = plan.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning;
	return plan;
}

QString spriteWorkflowPlanText(const SpriteWorkflowPlan& plan)
{
	QStringList lines;
	lines << studioText("Sprite workflow");
	lines << studioText("Engine: %1").arg(plan.engineFamily);
	lines << studioText("Sprite: %1").arg(plan.spriteName);
	lines << studioText("Frames: %1").arg(plan.frames.size());
	lines << studioText("Palette:");
	lines << plan.palettePreviewLines;
	lines << studioText("Sequence:");
	lines << plan.sequenceLines;
	lines << studioText("Package staging:");
	lines << (plan.stagingLines.isEmpty() ? QStringList {studioText("No package staging requested.")} : plan.stagingLines);
	for (const QString& warning : plan.warnings) {
		lines << studioText("Warning: %1").arg(warning);
	}
	return lines.join('\n');
}

QVector<LanguageServiceHook> defaultLanguageServiceHooks()
{
	return {
		{QStringLiteral("quakec"), studioText("QuakeC"), {QStringLiteral("qc"), QStringLiteral("qh")}, {studioText("outline"), studioText("symbols"), studioText("brace diagnostics"), studioText("build task handoff")}, QStringLiteral("fteqcc-lsp or custom QuakeC analyzer"), false, studioText("Descriptor active; external language server not configured.")},
		{QStringLiteral("cpp"), studioText("C and C++"), {QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"), QStringLiteral("h"), QStringLiteral("hpp")}, {studioText("outline"), studioText("symbols"), studioText("compiler diagnostics")}, QStringLiteral("clangd"), false, studioText("Hook defined; user toolchain selection remains explicit.")},
		{QStringLiteral("idtech3-shader"), studioText("idTech3 Shader"), {QStringLiteral("shader")}, {studioText("shader graph"), studioText("texture refs"), studioText("round-trip edits")}, QStringLiteral("vibestudio shader parser"), true, studioText("Built-in shader parser available.")},
		{QStringLiteral("entity-definition"), studioText("Entity Definitions"), {QStringLiteral("def"), QStringLiteral("ent")}, {studioText("outline"), studioText("key/value snippets"), studioText("spawnclass symbols")}, QStringLiteral("vibestudio entity analyzer"), true, studioText("Built-in text analyzer available.")},
		{QStringLiteral("idtech-config"), studioText("idTech Config"), {QStringLiteral("cfg"), QStringLiteral("arena"), QStringLiteral("skin")}, {studioText("search"), studioText("replace"), studioText("launch profile hints")}, QStringLiteral("vibestudio config analyzer"), true, studioText("Built-in text analyzer available.")},
	};
}

CodeWorkspaceIndex indexCodeWorkspace(const CodeWorkspaceIndexRequest& request)
{
	CodeWorkspaceIndex index;
	index.rootPath = QFileInfo(request.rootPath).absoluteFilePath();
	index.languageHooks = defaultLanguageServiceHooks();
	if (request.rootPath.trimmed().isEmpty() || !QFileInfo(index.rootPath).isDir()) {
		index.state = OperationState::Failed;
		index.warnings << studioText("Project root path is required and must be a directory.");
		return index;
	}
	QStringList extensions = request.extensions.isEmpty() ? defaultCodeExtensions() : request.extensions;
	for (QString& extension : extensions) {
		extension = extension.trimmed().toLower();
		if (extension.startsWith('.')) {
			extension.remove(0, 1);
		}
	}

	QDirIterator iterator(index.rootPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		const QString filePath = iterator.next();
		if (shouldSkipSourceDirectory(QFileInfo(filePath).absolutePath())) {
			continue;
		}
		const QFileInfo fileInfo(filePath);
		QString suffix = fileInfo.suffix().toLower();
		if (suffix.isEmpty() && fileInfo.fileName() == QStringLiteral("meson.build")) {
			suffix = QStringLiteral("meson");
		}
		if (!extensions.contains(suffix)) {
			continue;
		}
		if (index.files.size() >= request.maxFiles) {
			index.warnings << studioText("File scan stopped at %1 files.").arg(request.maxFiles);
			break;
		}
		QFile file(filePath);
		if (!file.open(QIODevice::ReadOnly)) {
			index.diagnostics.push_back({QStringLiteral("warning"), studioText("Unable to read source file."), filePath, relativeToRoot(index.rootPath, filePath), 0, 0});
			continue;
		}
		const QByteArray bytes = file.readAll();
		QStringDecoder decoder(QStringDecoder::Utf8);
		const QString text = decoder.decode(bytes);
		if (decoder.hasError()) {
			index.diagnostics.push_back({QStringLiteral("warning"), studioText("File is not UTF-8 text."), filePath, relativeToRoot(index.rootPath, filePath), 0, 0});
			continue;
		}
		CodeSourceFile source;
		source.filePath = filePath;
		source.relativePath = relativeToRoot(index.rootPath, filePath);
		source.languageId = languageForExtension(suffix);
		source.bytes = bytes.size();
		source.lineCount = text.count('\n') + 1;
		index.files.push_back(source);
		index.symbols += symbolsFromText(index.rootPath, filePath, text, request.symbolQuery);
		index.diagnostics += diagnosticsFromText(index.rootPath, filePath, text);
	}

	std::sort(index.files.begin(), index.files.end(), [](const CodeSourceFile& left, const CodeSourceFile& right) {
		return left.relativePath < right.relativePath;
	});
	for (const CodeSourceFile& file : index.files) {
		index.treeLines << QStringLiteral("%1 [%2, %3 lines]").arg(file.relativePath, file.languageId).arg(file.lineCount);
	}
	for (const CompilerProfileDescriptor& profile : compilerProfileDescriptors()) {
		index.buildTaskLines << studioText("%1: %2").arg(profile.id, profile.displayName);
	}
	index.launchProfileLines = {
		studioText("Quake/idTech2 source port launch: select executable in installation profile, then pass +map <map>."),
		studioText("Doom source port launch: select executable in installation profile, then pass -file <wad> -warp <map>."),
		studioText("Quake III launch: select executable in installation profile, then pass +set fs_game <mod> +devmap <map>."),
	};
	index.state = index.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning;
	return index;
}

QString codeWorkspaceIndexText(const CodeWorkspaceIndex& index)
{
	QStringList lines;
	lines << studioText("Code workspace");
	lines << studioText("Root: %1").arg(QDir::toNativeSeparators(index.rootPath));
	lines << studioText("Files: %1").arg(index.files.size());
	lines << studioText("Symbols: %1").arg(index.symbols.size());
	lines << studioText("Diagnostics: %1").arg(index.diagnostics.size());
	lines << studioText("Source tree:");
	lines << (index.treeLines.isEmpty() ? QStringList {studioText("No source files indexed.")} : index.treeLines.mid(0, 80));
	lines << studioText("Language hooks:");
	for (const LanguageServiceHook& hook : index.languageHooks) {
		lines << QStringLiteral("- %1 [%2]: %3").arg(hook.displayName, hook.languageId, hook.status);
	}
	lines << studioText("Symbols:");
	for (const CodeSymbol& symbol : index.symbols.mid(0, 80)) {
		lines << QStringLiteral("- %1 %2 at %3:%4").arg(symbol.kind, symbol.name, symbol.relativePath).arg(symbol.line);
	}
	lines << studioText("Build tasks:");
	lines << index.buildTaskLines;
	lines << studioText("Launch profiles:");
	lines << index.launchProfileLines;
	for (const CodeDiagnostic& diagnostic : index.diagnostics.mid(0, 80)) {
		lines << QStringLiteral("%1: %2:%3 %4").arg(diagnostic.severity, diagnostic.relativePath).arg(diagnostic.line).arg(diagnostic.message);
	}
	for (const QString& warning : index.warnings) {
		lines << studioText("Warning: %1").arg(warning);
	}
	return lines.join('\n');
}

QStringList extensionTrustModelLines()
{
	return {
		studioText("Manifest schema: vibestudio.extension.json with id, version, trustLevel, sandbox, capabilities, commands, and generatedFiles."),
		studioText("Trust levels: bundled, workspace, user-approved, disabled. Disabled extensions are discoverable but not executable."),
		studioText("Sandbox models: metadata-only, command-plan, staged-files. File writes must be represented as staged generated files before package/project import."),
		studioText("Command execution requires explicit allowExecution plus a non-disabled trust level; dry-run planning is the default."),
		studioText("Programs and working directories are resolved relative to the extension root unless absolute paths are deliberately supplied by an approved manifest."),
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
			*error = studioText("Unable to open extension manifest.");
		}
		return false;
	}
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		if (error) {
			*error = studioText("Extension manifest is not a JSON object.");
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
		parsed.warnings << studioText("Manifest id is missing.");
	}
	if (!QStringList {QStringLiteral("bundled"), QStringLiteral("workspace"), QStringLiteral("user-approved"), QStringLiteral("disabled")}.contains(parsed.trustLevel)) {
		parsed.warnings << studioText("Unknown trust level; command execution will be blocked.");
		parsed.trustLevel = QStringLiteral("disabled");
	}
	if (!QStringList {QStringLiteral("metadata-only"), QStringLiteral("command-plan"), QStringLiteral("staged-files")}.contains(parsed.sandboxModel)) {
		parsed.warnings << studioText("Unknown sandbox model; using metadata-only.");
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
			parsed.warnings << studioText("An extension command is missing an id.");
			continue;
		}
		parsed.commands.push_back(command);
	}
	if (parsed.commands.isEmpty()) {
		parsed.warnings << studioText("No extension commands are declared.");
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
			result.warnings << studioText("Extension root does not exist: %1").arg(QDir::toNativeSeparators(root));
			continue;
		}
		if (rootInfo.isFile() && rootInfo.fileName() == QStringLiteral("vibestudio.extension.json")) {
			ExtensionManifest manifest;
			QString error;
			if (loadExtensionManifest(rootInfo.absoluteFilePath(), &manifest, &error)) {
				result.manifests.push_back(manifest);
			} else {
				result.warnings << studioText("Unable to load %1: %2").arg(QDir::toNativeSeparators(rootInfo.absoluteFilePath()), error);
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
				result.warnings << studioText("Unable to load %1: %2").arg(QDir::toNativeSeparators(path), error);
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
		plan.warnings << studioText("Extension command was not found.");
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
		plan.warnings << studioText("Extension trust level is disabled; execution is blocked.");
		plan.executionAllowed = false;
	}
	if (manifest.sandboxModel == QStringLiteral("metadata-only")) {
		plan.warnings << studioText("Metadata-only sandbox blocks command execution.");
		plan.executionAllowed = false;
	}
	if (plan.command.requiresApproval && !allowExecution) {
		plan.warnings << studioText("Command requires approval; dry-run plan is available.");
	}
	if (plan.program.trimmed().isEmpty()) {
		plan.warnings << studioText("Command program is empty.");
		plan.state = OperationState::Failed;
	} else {
		plan.state = plan.executionAllowed ? OperationState::Queued : OperationState::Warning;
	}
	for (const ExtensionGeneratedFile& file : plan.command.generatedFiles) {
		plan.stagingLines << studioText("Stage generated file %1: %2").arg(file.virtualPath, file.summary.isEmpty() ? file.sourceDescription : file.summary);
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
		result.error = plan.state == OperationState::Failed ? studioText("Command plan is invalid.") : studioText("Dry run only; command was not started.");
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
		result.error = studioText("Extension command timed out.");
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
	lines << studioText("Extension: %1 [%2]").arg(manifest.displayName, manifest.id);
	lines << studioText("Version: %1").arg(manifest.version);
	lines << studioText("Trust: %1").arg(manifest.trustLevel);
	lines << studioText("Sandbox: %1").arg(manifest.sandboxModel);
	lines << studioText("Root: %1").arg(QDir::toNativeSeparators(manifest.rootPath));
	lines << studioText("Capabilities: %1").arg(manifest.capabilities.isEmpty() ? studioText("none") : manifest.capabilities.join(QStringLiteral(", ")));
	lines << studioText("Commands:");
	for (const ExtensionCommandDescriptor& command : manifest.commands) {
		lines << QStringLiteral("- %1 [%2]: %3").arg(command.displayName, command.id, command.description);
		lines << QStringLiteral("  %1 %2").arg(command.program, command.arguments.join(QLatin1Char(' ')));
		for (const ExtensionGeneratedFile& file : command.generatedFiles) {
			lines << QStringLiteral("  %1").arg(studioText("Stages: %1").arg(file.virtualPath));
		}
	}
	for (const QString& warning : manifest.warnings) {
		lines << studioText("Warning: %1").arg(warning);
	}
	return lines.join('\n');
}

QString extensionDiscoveryText(const ExtensionDiscoveryResult& result)
{
	QStringList lines;
	lines << studioText("Extension discovery");
	lines << studioText("Roots: %1").arg(result.searchRoots.join(QStringLiteral("; ")));
	lines << studioText("Extensions: %1").arg(result.manifests.size());
	for (const ExtensionManifest& manifest : result.manifests) {
		lines << extensionManifestText(manifest);
	}
	for (const QString& warning : result.warnings) {
		lines << studioText("Warning: %1").arg(warning);
	}
	lines << studioText("Trust model:");
	lines << extensionTrustModelLines();
	return lines.join('\n');
}

QString extensionCommandPlanText(const ExtensionCommandPlan& plan)
{
	QStringList lines;
	lines << studioText("Extension command plan");
	lines << studioText("Extension: %1").arg(plan.manifest.id);
	lines << studioText("Command: %1").arg(plan.command.id);
	lines << studioText("Program: %1").arg(QDir::toNativeSeparators(plan.program));
	lines << studioText("Arguments: %1").arg(plan.arguments.join(QLatin1Char(' ')));
	lines << studioText("Working directory: %1").arg(QDir::toNativeSeparators(plan.workingDirectory));
	lines << studioText("Dry run: %1").arg(plan.dryRun ? studioText("yes") : studioText("no"));
	lines << studioText("Execution allowed: %1").arg(plan.executionAllowed ? studioText("yes") : studioText("no"));
	lines << studioText("Generated file staging:");
	lines << (plan.stagingLines.isEmpty() ? QStringList {studioText("No generated files declared.")} : plan.stagingLines);
	for (const QString& warning : plan.warnings) {
		lines << studioText("Warning: %1").arg(warning);
	}
	return lines.join('\n');
}

QString extensionCommandResultText(const ExtensionCommandResult& result)
{
	QStringList lines;
	lines << extensionCommandPlanText(result.plan);
	lines << studioText("Started: %1").arg(result.started ? studioText("yes") : studioText("no"));
	lines << studioText("Exit code: %1").arg(result.exitCode);
	if (!result.stdoutText.trimmed().isEmpty()) {
		lines << studioText("Stdout:");
		lines << result.stdoutText.trimmed();
	}
	if (!result.stderrText.trimmed().isEmpty()) {
		lines << studioText("Stderr:");
		lines << result.stderrText.trimmed();
	}
	if (!result.error.trimmed().isEmpty()) {
		lines << studioText("Result: %1").arg(result.error);
	}
	return lines.join('\n');
}

static AiWorkflowResult makeCreationWorkflow(const QString& workflowId, const QString& title, const QString& prompt, const QString& capability, const QString& stagedId, const QString& stagedKind, const QString& stagedLabel, const QString& proposedPath, const QStringList& previewLines, const QStringList& actions, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	QString provider = providerId.trimmed().isEmpty() ? normalized.preferredCodingConnectorId : providerId;
	if (provider.trimmed().isEmpty()) {
		provider = normalized.aiFreeMode ? QStringLiteral("local-offline") : defaultAiReasoningConnectorId();
	}
	QString model = modelId.trimmed().isEmpty() ? defaultAiModelId(provider, capability) : modelId;
	AiWorkflowResult result;
	result.title = title;
	result.summary = studioText("Reviewable %1 proposal generated without writing files.").arg(title.toLower());
	result.reviewableText = previewLines.join('\n');
	result.nextActions = actions;
	result.diagnostics = {studioText("No provider call was made; output is deterministic local scaffold text.")};
	result.manifest = defaultAiWorkflowManifest(workflowId, provider, model, prompt);
	result.manifest.contextSummary = studioText("Prompt text, VibeStudio tool descriptors, and local project/package conventions only.");
	result.manifest.toolCalls.push_back({QStringLiteral("staged-text-edit"), studioText("Generated staged text output for review."), OperationState::Completed, {prompt}, {proposedPath}, {}, true, true});
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
	return makeCreationWorkflow(QStringLiteral("prompt-to-shader-scaffold"), studioText("Shader Scaffold"), prompt, QStringLiteral("coding"), QStringLiteral("shader-scaffold"), QStringLiteral("shader"), studioText("Shader scaffold"), QStringLiteral("scripts/vibestudio/generated.shader"), preview, {studioText("Review shader name, qer_editorimage, stage maps, and blend modes."), studioText("Run shader inspect and shader validate with mounted packages before staging.")}, preferences, providerId, modelId);
}

AiWorkflowResult promptToEntityDefinitionAiExperiment(const QString& prompt, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString entityName = QStringLiteral("vibestudio_%1").arg(sanitizePathToken(prompt).replace('-', '_').left(32));
	const QStringList preview = {
		QStringLiteral("/*QUAKED %1 (0.2 0.8 1.0) (-16 -16 -24) (16 16 48)").arg(entityName),
		studioText("Review generated keys before copying into a .def or .ent file."),
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
	return makeCreationWorkflow(QStringLiteral("prompt-to-entity-definition"), studioText("Entity Definition Snippet"), prompt, QStringLiteral("coding"), QStringLiteral("entity-definition"), QStringLiteral("entity-definition"), studioText("Entity definition snippet"), QStringLiteral("scripts/vibestudio/generated.def"), preview, {studioText("Review bbox, keys, spawnflags, and classname against the target engine."), studioText("Stage as a text edit only after package/project validation passes.")}, preferences, providerId, modelId);
}

AiWorkflowResult promptToPackageValidationPlanAiExperiment(const QString& prompt, const QString& packagePath, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString package = packagePath.trimmed().isEmpty() ? QStringLiteral("<package-path>") : packagePath;
	const QStringList preview = {
		studioText("Package validation plan"),
		QStringLiteral("vibestudio --cli package validate \"%1\" --json").arg(package),
		QStringLiteral("vibestudio --cli package list \"%1\" --json").arg(package),
		QStringLiteral("vibestudio --cli shader inspect <shader-file> --package \"%1\" --json").arg(package),
		QStringLiteral("vibestudio --cli asset find <project-root> --find \"%1\" --json").arg(prompt.left(32).replace('"', '\'')),
		studioText("Review archive warnings, missing shader references, generated-file staging, and release manifest output before save-as."),
	};
	return makeCreationWorkflow(QStringLiteral("prompt-to-package-validation-plan"), studioText("Package Validation Plan"), prompt, QStringLiteral("reasoning"), QStringLiteral("package-validation-plan"), QStringLiteral("plan"), studioText("Package validation plan"), QStringLiteral(".vibestudio/ai-staged/package-validation-plan.txt"), preview, {studioText("Run validation commands in dry-run or JSON mode first."), studioText("Use package stage/save-as only after blockers are cleared.")}, preferences, providerId, modelId);
}

AiWorkflowResult promptToBatchConversionRecipeAiExperiment(const QString& prompt, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString lower = prompt.toLower();
	const QString format = lower.contains(QStringLiteral("jpg")) || lower.contains(QStringLiteral("jpeg")) ? QStringLiteral("jpg") : QStringLiteral("png");
	const QString palette = lower.contains(QStringLiteral("doom")) ? QStringLiteral("indexed") : lower.contains(QStringLiteral("gray")) ? QStringLiteral("grayscale") : QStringLiteral("indexed");
	const QStringList preview = {
		studioText("Batch conversion recipe"),
		QStringLiteral("vibestudio --cli asset convert <package> --output <converted-folder> --format %1 --palette %2 --dry-run --json").arg(format, palette),
		QStringLiteral("vibestudio --cli sprite plan --engine doom --name SPRT --frames 4 --rotations 8 --palette %1 --package-root sprites --json").arg(palette),
		studioText("Review dimensions, palette loss, staged paths, and package save-as conflicts before writing converted files."),
	};
	return makeCreationWorkflow(QStringLiteral("prompt-to-batch-conversion-recipe"), studioText("Batch Conversion Recipe"), prompt, QStringLiteral("coding"), QStringLiteral("batch-conversion-recipe"), QStringLiteral("recipe"), studioText("Batch conversion recipe"), QStringLiteral(".vibestudio/ai-staged/batch-conversion.txt"), preview, {studioText("Run the conversion command in dry-run mode."), studioText("Inspect generated images and sprite frame sequence before package staging.")}, preferences, providerId, modelId);
}

QString aiProposalReviewSurfaceText(const AiWorkflowResult& result)
{
	QStringList lines;
	lines << studioText("AI Proposal Review");
	lines << studioText("Summary: %1").arg(result.summary);
	lines << studioText("Provider/model: %1 / %2").arg(result.manifest.providerId, result.manifest.modelId);
	lines << studioText("Approval: %1").arg(result.manifest.approvalState);
	lines << studioText("Context used:");
	lines << result.manifest.contextSummary;
	lines << studioText("Generated actions:");
	for (const AiWorkflowToolCall& call : result.manifest.toolCalls) {
		lines << QStringLiteral("- %1: %2").arg(call.toolId, call.summary);
	}
	lines << studioText("Staged outputs:");
	for (const AiStagedOutput& output : result.manifest.stagedOutputs) {
		lines << QStringLiteral("- %1 [%2] %3").arg(output.label, output.kind, output.proposedPath);
	}
	lines << studioText("Prompt log:");
	lines << result.manifest.prompt;
	lines << studioText("Response preview:");
	lines << result.reviewableText;
	lines << studioText("Validation:");
	lines << result.manifest.validationSummary;
	return lines.join('\n');
}

} // namespace vibestudio
