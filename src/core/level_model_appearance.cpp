#include "core/level_model_appearance.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

namespace vibestudio {
namespace {
struct AppearanceText { Q_DECLARE_TR_FUNCTIONS(VibeStudioLevelModelAppearance) };
QString clean(QString path) { return path.replace(QLatin1Char('\\'), QLatin1Char('/')); }
QString property(const LevelMapEntity& entity, const QString& key)
{
	for (const auto& item : entity.properties) { if (item.key.compare(key, Qt::CaseInsensitive) == 0) { return item.value; } }
	return {};
}
QString preferred(const LevelMapEntity& entity, const QString& first, const QString& second)
{
	const auto value = property(entity, first);
	return value.isEmpty() ? property(entity, second) : value;
}
bool token(const QString& value, int maximum)
{
	if (value.isEmpty() || value.size() > maximum) { return false; }
	for (const auto c : value) {
		if (c.unicode() < 33 || c.unicode() > 126 || c == QLatin1Char('"') || c == QLatin1Char(';')) { return false; }
	}
	return true;
}
QString extensionless(QString path)
{
	const auto dot = path.lastIndexOf(QLatin1Char('.'));
	if (dot > path.lastIndexOf(QLatin1Char('/'))) { path.truncate(dot); }
	return path;
}

// Original implementation of the NetRadiant Custom q3map2 model.cpp contract,
// 68ecbed64b7be78741878c730279b5471d978c7c, GPL-2.0-or-later. Assimp's bundled
// MD3Loader uses shader slot zero (or dummy_texture.bmp); shaders/multipart are
// disabled by q3map2. See the pinned source links and licences in docs/CREDITS.md.
QString compilerMaterial(const QString& modelPath, QString material)
{
	material = extensionless(clean(material.isEmpty() ? QStringLiteral("dummy_texture.bmp") : material));
	const auto parent = packageVirtualPathParent(modelPath);
	const auto relative = [&](const QString& name) { return parent.isEmpty() ? name : parent + QLatin1Char('/') + name; };
	if (!material.contains(QLatin1Char('/'))) { return relative(material); }
	if (material.startsWith(QLatin1Char('/')) || (material.size() > 1 && material[1] == QLatin1Char(':')) || material.contains(QStringLiteral(".."))) {
		auto at = material.indexOf(QStringLiteral("/models/"), 0, Qt::CaseInsensitive);
		if (at < 0) { at = material.indexOf(QStringLiteral("/textures/"), 0, Qt::CaseInsensitive); }
		return at >= 0 ? material.mid(at + 1) : relative(material.section(QLatin1Char('/'), -1));
	}
	return material;
}
bool parseSkin(QByteArray bytes, QVector<LevelModelRemap>* mappings, QString* error, const ModelWorkControl& control)
{
	if (bytes.contains('\0')) { *error = AppearanceText::tr("Compiler skin files cannot contain NUL bytes."); return false; }
	if (bytes.startsWith("\xef\xbb\xbf")) { *error = AppearanceText::tr("Save compiler skins as plain text without a byte-order mark."); return false; }
	// q3map2 searches for CR before LF. Mixed newline conventions would consume
	// different logical lines; reject them instead of pretending to agree.
	if (bytes.contains('\r')) {
		auto remaining = bytes; remaining.replace("\r\n", ""); remaining.replace("\r", "");
		if (remaining.contains('\n')) { *error = AppearanceText::tr("Compiler skin files must use one newline convention."); return false; }
	}
	bytes.replace("\r\n", "\n"); bytes.replace('\r', '\n');
	int lineNumber = 0;
	for (const auto& line : bytes.split('\n')) {
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, ++lineNumber, 0, error)) { return false; }
		const auto trimmed = line.trimmed();
		if (trimmed.isEmpty() || trimmed.startsWith("//")) { continue; }
		// The compiler accepts replace FROM TO and FROM,TO. Diagnose oversized
		// fields and trailing tokens instead of reproducing sscanf truncation.
		static const QRegularExpression replaceLine(QStringLiteral("^replace\\s*(\\S+)\\s+(\\S+)(.*)$"));
		static const QRegularExpression commaLine(QStringLiteral("^\\s*([^, ]+) *,\\s*(\\S+)(.*)$"));
		const auto text = QString::fromLatin1(line);
		auto match = replaceLine.match(text);
		if (!match.hasMatch()) { match = commaLine.match(text); }
		const auto tail = match.captured(3).trimmed();
		const auto source = match.captured(1), target = match.captured(2);
		if (!match.hasMatch() || (!tail.isEmpty() && !tail.startsWith(QStringLiteral("//"))) || !token(source, 1023)
			|| !token(target, 63) || !isSafePackageVirtualPath(target) || mappings->size() >= 4096) {
			*error = AppearanceText::tr("Invalid or oversized compiler skin mapping on line %1.").arg(lineNumber); return false;
		}
		mappings->append({QString::number(lineNumber), source, target});
	}
	return true;
}
} // namespace

LevelModelAppearance levelModelAppearance(const LevelMapDocument& document, const LevelMapEntity& entity)
{
	LevelModelAppearance result;
	const auto rawPath = property(entity, QStringLiteral("model"));
	result.modelPath = clean(rawPath.trimmed());
	result.cacheKey = result.modelPath.toCaseFolded();
	result.compiler = document.format == LevelMapFormat::Quake3Map && entity.className.compare(QStringLiteral("misc_model"), Qt::CaseInsensitive) == 0;
	if (result.modelPath.isEmpty() || result.modelPath.startsWith(QLatin1Char('*')) || !result.compiler) { return result; }
	result.skin = preferred(entity, QStringLiteral("_skin"), QStringLiteral("skin"));
	const auto parent = packageVirtualPathParent(result.modelPath);
	const auto filename = result.modelPath.section(QLatin1Char('/'), -1);
	const auto underscore = filename.lastIndexOf(QLatin1Char('_'));
	result.defaultSkinPath = (parent.isEmpty() ? QString() : parent + QLatin1Char('/'))
		+ (underscore < 0 ? extensionless(filename) : filename.left(underscore)) + QStringLiteral("_default.skin");
	const auto frame = preferred(entity, QStringLiteral("_frame"), QStringLiteral("frame"));
	QJsonArray remaps;
	for (const auto& item : entity.properties) {
		if (!item.key.startsWith(QStringLiteral("_remap"), Qt::CaseInsensitive)) { continue; }
		remaps.append(QJsonArray{item.key, item.value});
		const auto separator = item.value.indexOf(QLatin1Char(';'));
		const auto from = item.value.left(separator), to = separator < 0 ? QString() : item.value.mid(separator + 1);
		if (separator <= 0 || item.value.toUtf8().size() >= 1024 || !token(from, 1023) || !token(to, 63) || !isSafePackageVirtualPath(to)
			|| result.remaps.size() >= 256) {
			result.error = AppearanceText::tr("Invalid compiler material remap %1; use FROM;TO with a safe material path of at most 63 ASCII bytes.").arg(item.key);
		} else { result.remaps.append({item.key, from, to}); }
	}
	const QJsonArray identity{rawPath == rawPath.trimmed() ? result.modelPath.toCaseFolded() : rawPath, frame, result.skin, remaps};
	result.cacheKey += QStringLiteral("#q3map2:") + QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
	if (!isSafePackageVirtualPath(result.modelPath) || rawPath != rawPath.trimmed()) { result.error = AppearanceText::tr("The model requires a safe package path without surrounding spaces."); }
	if (!frame.isEmpty()) {
		bool ok = false;
		result.frame = frame.toInt(&ok);
		if (!ok || result.frame < 0 || !QRegularExpression(QStringLiteral("^[0-9]+$")).match(frame).hasMatch()) {
			result.error = AppearanceText::tr("The model frame must be a nonnegative whole number.");
		}
	}
	if (!result.skin.isEmpty()) {
		if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_.-]{1,128}$")).match(result.skin).hasMatch() || result.skin == QStringLiteral(".") || result.skin == QStringLiteral("..")) {
			result.error = AppearanceText::tr("The compiler skin is a filename suffix; use letters, numbers, underscores, dots or hyphens.");
		} else {
			const bool numeric = QRegularExpression(QStringLiteral("^[0-9]+$")).match(result.skin).hasMatch();
			result.skinPath = (numeric ? result.modelPath : extensionless(result.modelPath)) + QLatin1Char('_') + result.skin + QStringLiteral(".skin");
		}
	}
	return result;
}

LevelModelAppearanceResult prepareLevelModelAppearance(const ModelMesh& source, const LevelModelAppearance& appearance,
	const PackageArchiveReader& archive, const ModelWorkControl& control)
{
	LevelModelAppearanceResult result;
	result.receipt = {{QStringLiteral("key"), appearance.cacheKey}, {QStringLiteral("modelPath"), appearance.modelPath},
		{QStringLiteral("contract"), appearance.compiler ? QStringLiteral("q3map2-nrc-md3-v1") : QStringLiteral("native-default")},
		{QStringLiteral("frame"), appearance.frame}, {QStringLiteral("skin"), appearance.skin}, {QStringLiteral("skinPath"), appearance.skinPath}};
	const auto fail = [&](const QString& error) {
		result.error = error;
		result.cancelled = control.cancelled && control.cancelled();
		result.receipt.insert(QStringLiteral("status"), result.cancelled ? QStringLiteral("cancelled") : QStringLiteral("unavailable"));
		result.receipt.insert(QStringLiteral("error"), error);
		return result;
	};
	QString error;
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, &error)) { return fail(error); }
	if (!appearance.error.isEmpty()) { return fail(appearance.error); }
	if (!source.geometryAvailable || !source.error.isEmpty() || source.frames.isEmpty()) { return fail(AppearanceText::tr("The placed model has no supported geometry.")); }
	const bool md3 = appearance.compiler && source.format == ModelMeshFormat::Quake3Md3;
	if (appearance.compiler && !md3 && (appearance.frame != 0 || !appearance.skin.isEmpty() || !appearance.remaps.isEmpty())) {
		return fail(AppearanceText::tr("Compiler appearance overrides currently require an MD3 model."));
	}
	if (appearance.frame < 0 || appearance.frame >= source.frames.size()) { return fail(AppearanceText::tr("Frame %1 is outside this model's %2 frames.").arg(appearance.frame).arg(source.frames.size())); }
	// MD3Loader.cpp validates configFrameID but never offsets pcVertices by it.
	// A real NRC q3map2 BSP test confirms frame zero. An assembly pose bake is
	// the remedy; do not display an intended pose as verified compiler geometry.
	if (md3 && appearance.frame != 0) {
		return fail(AppearanceText::tr("This q3map2 MD3 importer reads frame 0 even when another frame is requested. Bake the requested pose as a static MD3, then place that asset with frame 0."));
	}
	QVector<LevelModelRemap> mappings, defaults;
	QJsonArray inputs;
	const auto readSkin = [&](const QString& path, bool optional, QByteArray* bytes) {
		const auto entries = archive.entries();
		qsizetype selected = -1;
		for (qsizetype i = 0; i < entries.size(); ++i) {
			if ((i & 255) == 0 && !modelWorkCheckpoint(control, ModelWorkPhase::Reading, i, entries.size(), &error)) { return false; }
			if (entries[i].kind != PackageEntryKind::File || clean(entries[i].virtualPath).compare(path, Qt::CaseInsensitive) != 0) { continue; }
			if (selected >= 0) { error = AppearanceText::tr("The compiler skin path is ambiguous in this package."); return false; }
			selected = i;
		}
		if (selected < 0) {
			if (optional) { return true; }
			error = AppearanceText::tr("The compiler skin file is missing: %1").arg(path); return false;
		}
		const auto& entry = entries[selected];
		if (!entry.readable || !isSafePackageVirtualPath(entry.virtualPath) || entry.sizeBytes > 256 * 1024) { error = AppearanceText::tr("The compiler skin is unreadable, unsafe, or larger than 256 KiB."); return false; }
		if (!archive.readEntryAt(selected, bytes, &error, 256 * 1024 + 1) || bytes->size() != qint64(entry.sizeBytes)) {
			if (error.isEmpty()) { error = AppearanceText::tr("The compiler skin changed or could not be read completely."); }
			return false;
		}
		inputs.append(QJsonObject{{QStringLiteral("path"), entry.virtualPath}, {QStringLiteral("entryIndex"), selected},
			{QStringLiteral("sourceOrdinal"), entry.sourceOrdinal}, {QStringLiteral("role"), optional ? QStringLiteral("importer-default-skin") : QStringLiteral("compiler-skin")},
			{QStringLiteral("layer"), entry.layerId.isEmpty() ? entry.sourceArchiveId : entry.layerId}, {QStringLiteral("sizeBytes"), qint64(bytes->size())},
			{QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256).toHex())}});
		result.receipt.insert(QStringLiteral("inputs"), inputs);
		return modelWorkCheckpoint(control, ModelWorkPhase::Reading, 1, 1, &error);
	};
	if (md3) {
		QByteArray bytes;
		if (!readSkin(appearance.defaultSkinPath, true, &bytes)) { return fail(error); }
		// Assimp loads an optional *_default.skin even when shader loading is off.
		// It removes commas and reads whitespace-delimited pairs, with case-sensitive
		// surface names and standalone tag_ tokens. Never use the native Q3 parser.
		if (bytes.contains('\0')) { return fail(AppearanceText::tr("Default compiler skin files cannot contain NUL bytes.")); }
		bytes.replace(',', ' ');
		const auto words = QString::fromLatin1(bytes).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
		for (qsizetype i = 0; i < words.size(); ++i) {
			const auto from = words[i];
			if (from.startsWith(QStringLiteral("tag_"))) { continue; }
			if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, i, words.size(), &error)) { return fail(error); }
			if (++i >= words.size() || !token(from, 63) || from.startsWith(QStringLiteral("//")) || !token(words[i], 63)
				|| !isSafePackageVirtualPath(words[i]) || defaults.size() >= 4096) {
				return fail(AppearanceText::tr("The importer's default skin requires plain surface/material pairs without comments or quotes."));
			}
			defaults.append({{}, from, words[i]});
		}
	}
	if (!appearance.skinPath.isEmpty()) {
		QByteArray bytes;
		if (!readSkin(appearance.skinPath, false, &bytes)) { return fail(error); }
		if (!parseSkin(bytes, &mappings, &error, control)) { return fail(error); }
	}
	ModelMesh prepared = source;
	prepared.surfaces.clear(); prepared.skinPaths.clear(); prepared.tags.clear(); prepared.animations.clear(); prepared.collisionBoxes.clear();
	prepared.frames = {source.frames[appearance.frame]}; prepared.frameCount = 1; prepared.tagCount = 0;
	prepared.vertexCount = 0; prepared.triangleCount = 0;
	QJsonArray surfaces, remaps;
	for (const auto& remap : appearance.remaps) { remaps.append(QJsonObject{{QStringLiteral("key"), remap.key}, {QStringLiteral("from"), remap.from}, {QStringLiteral("to"), remap.to}}); }
	for (const auto& surface : source.surfaces) {
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, surfaces.size(), source.surfaces.size(), &error)) { return fail(error); }
		if (appearance.frame >= surface.frames.size()) { return fail(AppearanceText::tr("A model surface does not contain the selected frame.")); }
		QString material = surface.skinPaths.value(0, source.skinPaths.value(0));
		if (md3) {
			material = surface.skinPaths.value(0);
			for (const auto& mapping : defaults) { if (mapping.from == surface.name) { material = mapping.to; break; } }
			material = compilerMaterial(appearance.modelPath, material);
		}
		const auto original = material;
		bool retained = mappings.isEmpty();
		for (const auto& mapping : mappings) {
			if (material.compare(mapping.from, Qt::CaseInsensitive) == 0) { material = mapping.to; retained = true; break; }
		}
		if (retained) {
			QString replacement;
			qsizetype longest = 0;
			for (const auto& remap : appearance.remaps) {
				if (remap.from == QStringLiteral("*") && longest == 0) { replacement = remap.to; }
				else if (remap.from.size() > longest && material.endsWith(remap.from, Qt::CaseInsensitive)) { replacement = remap.to; longest = remap.from.size(); }
			}
			if (!replacement.isEmpty()) { material = replacement; }
			if (md3) { material = extensionless(clean(material)); }
			if (md3 && (!token(material, 63) || !isSafePackageVirtualPath(material))) { return fail(AppearanceText::tr("The effective model material is unsafe or exceeds 63 ASCII bytes: %1").arg(material)); }
			auto selected = surface;
			selected.frames = {surface.frames[appearance.frame]};
			selected.skinPaths = material.isEmpty() ? QStringList{} : QStringList{material};
			prepared.vertexCount += selected.vertexCount; prepared.triangleCount += selected.triangles.size();
			prepared.surfaces.append(std::move(selected));
			if (!material.isEmpty() && !prepared.skinPaths.contains(material)) { prepared.skinPaths << material; }
		}
		surfaces.append(QJsonObject{{QStringLiteral("surface"), surface.index}, {QStringLiteral("name"), surface.name},
			{QStringLiteral("sourceMaterial"), original}, {QStringLiteral("material"), retained ? material : QString()}, {QStringLiteral("retained"), retained}});
	}
	prepared.surfaceCount = prepared.surfaces.size(); prepared.skinCount = prepared.skinPaths.size();
	prepared.mins = prepared.frames.first().mins; prepared.maxs = prepared.frames.first().maxs;
	result.receipt.insert(QStringLiteral("surfaces"), surfaces); result.receipt.insert(QStringLiteral("remaps"), remaps);
	result.receipt.insert(QStringLiteral("mappingCount"), mappings.size());
	result.receipt.insert(QStringLiteral("omittedSurfaces"), source.surfaces.size() - prepared.surfaces.size());
	result.receipt.insert(QStringLiteral("triangles"), prepared.triangleCount);
	result.receipt.insert(QStringLiteral("status"), QStringLiteral("ready"));
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, 1, 1, &error)) { return fail(error); }
	result.mesh = std::move(prepared);
	return result;
}
} // namespace vibestudio
