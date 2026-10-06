#include "core/level_prefab.h"
#include "core/level_document.h"
#include "core/level_patch.h"
#include "core/level_texture_mapping.h"
#include "core/package_publication.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &text)
{
	if (error) {
		*error = text;
	}
	return false;
}
bool check(const LevelPrefabCancellation &cancelled, QString *error)
{
	return !cancelled || !cancelled() || fail(error, QCoreApplication::translate("LevelPrefab", "Prefab operation cancelled."));
}
bool finite(const LevelMapVec3 &p, double limit = 32768)
{
	return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
		   std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) <= limit;
}
bool textMap(const LevelMapDocument &map) { return map.format == LevelMapFormat::QuakeMap || map.format == LevelMapFormat::Quake3Map; }
QString property(const LevelMapEntity &entity, const QString &key)
{
	for (const auto &p : entity.properties) {
		if (p.key.compare(key, Qt::CaseInsensitive) == 0) {
			return p.value;
		}
	}
	return {};
}
bool world(const LevelMapEntity &entity)
{
	return property(entity, QStringLiteral("classname")).compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0;
}
bool targetKey(const QString &key) { return levelMapTargetPropertyKeys().contains(key.toLower()); }
QVector<LevelMapSelectionRef> contentSelection(const LevelMapDocument &map)
{
	QVector<LevelMapSelectionRef> selection;
	for (const auto &entity : map.entities) {
		if (!world(entity)) {
			selection << LevelMapSelectionRef{LevelMapSelectionKind::Entity, entity.id};
		} else {
			for (const auto &brush : map.brushes) {
				if (brush.entityId == entity.id) {
					selection << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, brush.id};
				}
			}
			for (const auto &patch : map.patches) {
				if (patch.entityId == entity.id) {
					selection << LevelMapSelectionRef{LevelMapSelectionKind::QuakePatch, patch.id};
				}
			}
		}
	}
	return selection;
}
// Bound parser/convex-solver work before invoking the shared map reader.
// Ignore quoted values and comments, so material/entity text cannot disguise
// unbounded geometry. Parenthesis roots bound plane counts inside each block.
bool boundedText(const QString &text, QString *error, const LevelPrefabCancellation &cancelled)
{
	if (text.isEmpty() || text.size() > levelPrefabByteLimit / 2 || text.contains(QChar::Null)) {
		return fail(error, QCoreApplication::translate("LevelPrefab",
													   "Prefab map text is empty, contains NUL, or exceeds four million characters."));
	}
	QVector<int> roots{0};
	int parens = 0, total = 0, blocks = 0;
	bool quoted = false, lineComment = false, blockComment = false, escaped = false;
	for (qsizetype i = 0; i < text.size(); ++i) {
		if ((i & 4095) == 0 && !check(cancelled, error)) {
			return false;
		}
		const QChar c = text[i], next = i + 1 < text.size() ? text[i + 1] : QChar();
		if (lineComment) {
			if (c == '\n') {
				lineComment = false;
			}
			continue;
		}
		if (blockComment) {
			if (c == '*' && next == '/') {
				blockComment = false;
				++i;
			}
			continue;
		}
		if (quoted) {
			if (escaped) {
				escaped = false;
				continue;
			}
			if (c == '\\') {
				escaped = true;
			} else if (c == '"') {
				quoted = false;
			}
			continue;
		}
		if (c == '/' && next == '/') {
			lineComment = true;
			++i;
			continue;
		}
		if (c == '/' && next == '*') {
			blockComment = true;
			++i;
			continue;
		}
		if (c == '"') {
			quoted = true;
			continue;
		}
		if (c == '{') {
			if (parens || roots.size() >= 8 || ++blocks > 8192) {
				return fail(error,
							QCoreApplication::translate("LevelPrefab", "Prefab nesting or block count exceeds the supported limit."));
			}
			roots << 0;
		} else if (c == '}') {
			if (parens || roots.size() == 1) {
				return fail(error, QCoreApplication::translate("LevelPrefab", "Prefab map braces are unbalanced."));
			}
			roots.removeLast();
		} else if (c == '(') {
			if ((parens == 0 && ++roots.last() > 512) || ++parens > 64 || ++total > 250000) {
				return fail(error, QCoreApplication::translate("LevelPrefab", "Prefab geometry exceeds the bounded parser limits."));
			}
		} else if (c == ')' && --parens < 0) {
			return fail(error, QCoreApplication::translate("LevelPrefab", "Prefab map parentheses are unbalanced."));
		}
	}
	return (!quoted && !blockComment && roots.size() == 1 && parens == 0) ||
		   fail(error, QCoreApplication::translate("LevelPrefab", "Prefab map text ends inside a string, comment or geometry block."));
}
void reportLinks(const LevelMapDocument &map, LevelPrefabReport *report)
{
	QSet<QString> names, external;
	for (const auto &entity : map.entities) {
		const auto name = property(entity, QStringLiteral("targetname"));
		if (!name.isEmpty()) {
			names.insert(name);
		}
	}
	for (const auto &entity : map.entities) {
		for (const auto &p : entity.properties) {
			if (targetKey(p.key) && !p.value.isEmpty() && !names.contains(p.value)) {
				external.insert(p.value);
			}
			if (p.key.contains(QStringLiteral("target"), Qt::CaseInsensitive) && !targetKey(p.key) &&
				p.key.compare(QStringLiteral("targetname"), Qt::CaseInsensitive) != 0) {
				report->warnings << QCoreApplication::translate("LevelPrefab",
																"Custom target property %1 on entity %2 is preserved without remapping.")
										.arg(p.key)
										.arg(entity.id);
			}
		}
	}
	report->externalTargets = external.values();
	report->externalTargets.sort();
	if (!external.isEmpty()) {
		report->warnings << QCoreApplication::translate("LevelPrefab", "External target links keep their names: %1")
								.arg(report->externalTargets.join(QStringLiteral(", ")));
	}
}
} // namespace

bool inspectLevelPrefab(const LevelPrefab &prefab, LevelMapDocument *definition, LevelPrefabReport *report, QString *error,
						LevelPrefabCancellation cancelled)
{
	if (error) {
		error->clear();
	}
	if (prefab.name.trimmed().isEmpty() || prefab.name.size() > 128 || prefab.description.size() > 4096 || !finite(prefab.anchor)) {
		return fail(error, QCoreApplication::translate(
							   "LevelPrefab",
							   "A prefab needs a name of 1–128 characters, a description up to 4096 characters, and a finite anchor within "
							   "the map bounds."));
	}
	if (prefab.engineFamily.compare(QStringLiteral("idTech2"), Qt::CaseInsensitive) != 0 &&
		prefab.engineFamily.compare(QStringLiteral("idTech3"), Qt::CaseInsensitive) != 0) {
		return fail(
			error, QCoreApplication::translate("LevelPrefab", "Prefabs currently support Quake-family idTech2 and idTech3 map documents."));
	}
	if (!boundedText(prefab.mapText, error, cancelled)) {
		return false;
	}
	LevelMapDocument parsed;
	if (!loadLevelMapBytes({QStringLiteral("prefab.map"), {}, prefab.engineFamily}, prefab.mapText.toUtf8(), &parsed, error)) {
		return false;
	}
	if (!textMap(parsed)) {
		return fail(error, QCoreApplication::translate("LevelPrefab", "The prefab does not contain a Quake-family map."));
	}
	for (const auto &issue : parsed.issues) {
		if (issue.severity == LevelMapIssueSeverity::Error) {
			return fail(error, QCoreApplication::translate("LevelPrefab", "Invalid prefab geometry: %1").arg(issue.message));
		}
	}
	if (parsed.entities.size() > 4096 || parsed.brushes.size() > 4096 || parsed.patches.size() > 256) {
		return fail(error,
					QCoreApplication::translate("LevelPrefab", "A prefab supports at most 4096 entities, 4096 brushes and 256 patches."));
	}
	int worldCount = 0, faces = 0;
	for (const auto &entity : parsed.entities) {
		worldCount += world(entity) ? 1 : 0;
		QSet<QString> keys;
		for (const auto &p : entity.properties) {
			const auto key = p.key.toLower();
			if (keys.contains(key)) {
				return fail(
					error,
					QCoreApplication::translate("LevelPrefab", "Prefab entity %1 has duplicate property %2.").arg(entity.id).arg(p.key));
			}
			keys.insert(key);
		}
		if (entity.origin.valid && !finite(entity.origin)) {
			return fail(error, QCoreApplication::translate("LevelPrefab", "Prefab entity %1 is outside the map bounds.").arg(entity.id));
		}
		if (property(entity, QStringLiteral("model")).startsWith('*')) {
			return fail(error,
						QCoreApplication::translate("LevelPrefab", "Compiled inline model references cannot be reused as prefab assets."));
		}
	}
	if (worldCount != 1) {
		return fail(error, QCoreApplication::translate("LevelPrefab", "A prefab definition requires exactly one worldspawn."));
	}
	for (const auto &brush : parsed.brushes) {
		if (!check(cancelled, error)) {
			return false;
		}
		faces += static_cast<int>(brush.faces.size());
		if (!brush.boundsSolved || brush.faces.size() > 128 || faces > 16384 || !finite(brush.mins) || !finite(brush.maxs)) {
			return fail(error,
						QCoreApplication::translate(
							"LevelPrefab",
							"Prefab brushes must be closed, within map bounds, and limited to 128 faces each and 16384 faces in total."));
		}
	}
	for (const auto &patch : parsed.patches) {
		if (!check(cancelled, error) || !validateLevelPatch(patch, error)) {
			return false;
		}
		if (!finite(patch.mins) || !finite(patch.maxs)) {
			return fail(error, QCoreApplication::translate("LevelPrefab", "Prefab patch %1 is outside the map bounds.").arg(patch.id));
		}
	}
	LevelPrefabReport summary;
	summary.statistics = levelMapStatistics(parsed);
	summary.dialect = levelMapBrushDialect(parsed);
	if (summary.dialect == QStringLiteral("mixed")) {
		return fail(error, QCoreApplication::translate("LevelPrefab", "A prefab cannot mix brush texture dialects."));
	}
	const auto selection = contentSelection(parsed);
	if (selection.isEmpty()) {
		return fail(error, QCoreApplication::translate("LevelPrefab", "The prefab contains no placeable objects."));
	}
	if (!setLevelMapSelection(&parsed, selection, error)) {
		return false;
	}
	// Reusing the clipboard preflight also proves each source block is writable.
	if (levelMapSelectionText(parsed, error).isEmpty()) {
		return fail(error, error && !error->isEmpty()
							   ? *error
							   : QCoreApplication::translate("LevelPrefab", "The prefab objects cannot be serialized."));
	}
	reportLinks(parsed, &summary);
	if (!check(cancelled, error)) {
		return false;
	}
	if (definition) {
		*definition = std::move(parsed);
	}
	if (report) {
		*report = std::move(summary);
	}
	return true;
}

bool createLevelPrefab(const LevelMapDocument &source, const LevelPrefabCreateRequest &request, LevelPrefab *prefab,
					   LevelPrefabReport *report, QString *error, LevelPrefabCancellation cancelled)
{
	if (error) {
		error->clear();
	}
	if (!prefab || !textMap(source) || source.selection.isEmpty()) {
		return fail(error, QCoreApplication::translate("LevelPrefab", "Select Quake-family map objects to create a prefab."));
	}
	auto selected = source;
	QStringList expanded;
	for (const auto &ref : source.selection) {
		if (!levelMapObjectExists(source, ref)) {
			return fail(error, QCoreApplication::translate("LevelPrefab", "A selected prefab object no longer exists."));
		}
		int owner = -1;
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			for (const auto &b : source.brushes) {
				if (b.id == ref.objectId) {
					owner = b.entityId;
					break;
				}
			}
		}
		if (ref.kind == LevelMapSelectionKind::QuakePatch) {
			for (const auto &p : source.patches) {
				if (p.id == ref.objectId) {
					owner = p.entityId;
					break;
				}
			}
		}
		for (const auto &entity : source.entities) {
			if (entity.id == owner && !world(entity)) {
				const LevelMapSelectionRef whole{LevelMapSelectionKind::Entity, owner};
				if (!selected.selection.contains(whole)) {
					selected.selection << whole;
					expanded << QString::number(owner);
				}
			}
		}
	}
	if (!check(cancelled, error)) {
		return false;
	}
	const auto text = levelMapSelectionText(selected, error);
	if (text.isEmpty()) {
		return false;
	}
	LevelMapDocument definition;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	create.game = source.engineFamily.compare(QStringLiteral("idTech3"), Qt::CaseInsensitive) == 0 ? QStringLiteral("quake3")
																								   : QStringLiteral("quake2");
	if (!createLevelMap(create, &definition, error) || !pasteLevelMapText(&definition, text, error)) {
		return false;
	}
	const auto serialized = serializeLevelMap(definition);
	if (!serialized.succeeded()) {
		return fail(error, serialized.errors.join('\n'));
	}
	LevelPrefab result;
	result.name = request.name.trimmed();
	result.description = request.description;
	result.engineFamily = source.engineFamily;
	result.mapText = QString::fromUtf8(serialized.bytes);
	result.anchor = request.anchor;
	if (!result.anchor.valid) {
		LevelMapVec3 low, high;
		if (!levelMapSelectionBounds(definition, &low, &high)) {
			return fail(error, QCoreApplication::translate("LevelPrefab", "The selection has no position for a prefab anchor."));
		}
		result.anchor = {(low.x + high.x) / 2, (low.y + high.y) / 2, low.z, true};
	}
	LevelPrefabReport summary;
	if (!inspectLevelPrefab(result, nullptr, &summary, error, cancelled)) {
		return false;
	}
	if (!expanded.isEmpty()) {
		summary.warnings << QCoreApplication::translate("LevelPrefab", "Included complete brush entities to preserve ownership: %1")
								.arg(expanded.join(QStringLiteral(", ")));
	}
	*prefab = std::move(result);
	if (report) {
		*report = std::move(summary);
	}
	return true;
}

QByteArray serializeLevelPrefab(const LevelPrefab &prefab, QString *error, LevelPrefabCancellation cancelled)
{
	if (!inspectLevelPrefab(prefab, nullptr, nullptr, error, cancelled)) {
		return {};
	}
	const QJsonObject object{{QStringLiteral("format"), QStringLiteral("VibeStudioPrefab")},
							 {QStringLiteral("version"), 1},
							 {QStringLiteral("name"), prefab.name},
							 {QStringLiteral("description"), prefab.description},
							 {QStringLiteral("engineFamily"), prefab.engineFamily},
							 {QStringLiteral("anchor"), QJsonArray{prefab.anchor.x, prefab.anchor.y, prefab.anchor.z}},
							 {QStringLiteral("map"), prefab.mapText}};
	const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
	if (bytes.size() > levelPrefabByteLimit) {
		fail(error, QCoreApplication::translate("LevelPrefab", "The serialized prefab exceeds eight MiB."));
		return {};
	}
	return bytes;
}
bool parseLevelPrefab(const QByteArray &bytes, LevelPrefab *prefab, QString *error, LevelPrefabCancellation cancelled)
{
	if (error) {
		error->clear();
	}
	if (!prefab || bytes.isEmpty() || bytes.size() > levelPrefabByteLimit) {
		return fail(error, QCoreApplication::translate("LevelPrefab", "A prefab file must contain at most eight MiB."));
	}
	QJsonParseError parse;
	const auto json = QJsonDocument::fromJson(bytes, &parse);
	const auto object = json.object();
	if (parse.error != QJsonParseError::NoError || !json.isObject() ||
		object.value(QStringLiteral("format")).toString() != QStringLiteral("VibeStudioPrefab") ||
		object.value(QStringLiteral("version")).toDouble() != 1) {
		return fail(error, QCoreApplication::translate("LevelPrefab", "Unsupported or invalid VibeStudio prefab file."));
	}
	LevelPrefab result;
	for (const auto &key : {QStringLiteral("name"), QStringLiteral("description"), QStringLiteral("engineFamily"), QStringLiteral("map")}) {
		if (!object.value(key).isString()) {
			return fail(error, QCoreApplication::translate("LevelPrefab", "Prefab property %1 must be a string.").arg(key));
		}
	}
	result.name = object.value(QStringLiteral("name")).toString();
	result.description = object.value(QStringLiteral("description")).toString();
	result.engineFamily = object.value(QStringLiteral("engineFamily")).toString();
	result.mapText = object.value(QStringLiteral("map")).toString();
	const auto anchor = object.value(QStringLiteral("anchor")).toArray();
	if (anchor.size() != 3 || !anchor[0].isDouble() || !anchor[1].isDouble() || !anchor[2].isDouble()) {
		return fail(error, QCoreApplication::translate("LevelPrefab", "The prefab anchor requires three coordinates."));
	}
	result.anchor = {anchor[0].toDouble(), anchor[1].toDouble(), anchor[2].toDouble(), true};
	if (!inspectLevelPrefab(result, nullptr, nullptr, error, cancelled)) {
		return false;
	}
	*prefab = std::move(result);
	return true;
}
bool readLevelPrefab(const QString &path, LevelPrefab *prefab, QString *error, LevelPrefabCancellation cancelled)
{
	QFile file(path);
	if (!check(cancelled, error)) {
		return false;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		return fail(error, file.errorString());
	}
	if (file.size() > levelPrefabByteLimit) {
		return fail(error, QCoreApplication::translate("LevelPrefab", "The prefab exceeds eight MiB."));
	}
	const auto bytes = file.read(levelPrefabByteLimit + 1);
	if (file.error() != QFileDevice::NoError) {
		return fail(error, file.errorString());
	}
	return parseLevelPrefab(bytes, prefab, error, cancelled);
}
bool writeLevelPrefab(const LevelPrefab &prefab, const QString &path, bool overwrite, bool dryRun, LevelPrefabWriteReport *report,
					  QString *error, LevelPrefabCancellation cancelled)
{
	const auto bytes = serializeLevelPrefab(prefab, error, cancelled);
	if (bytes.isEmpty() || !check(cancelled, error)) {
		return false;
	}
	const QFileInfo target(path);
	if (path.trimmed().isEmpty() || target.isDir() || target.isSymLink() || !target.dir().exists()) {
		return fail(error, QCoreApplication::translate("LevelPrefab", "Choose a regular prefab output in an existing directory."));
	}
	if (target.exists() && !overwrite) {
		return fail(error, QCoreApplication::translate("LevelPrefab", "The prefab output exists. Explicit overwrite is required."));
	}
	if (report) {
		*report = {};
		report->path = target.absoluteFilePath();
		report->dryRun = dryRun;
	}
	if (dryRun) {
		return true;
	}
	PackagePublicationOptions publicationOptions;
	publicationOptions.destinationPath = target.absoluteFilePath();
	publicationOptions.allowOverwrite = overwrite;
	publicationOptions.isCancelled = cancelled;
	PackagePublication publication(publicationOptions);
	if (!publication.begin(error)) {
		return false;
	}
	if (publication.device()->write(bytes) != bytes.size()) {
		return fail(error, QCoreApplication::translate("LevelPrefab", "Unable to write the complete prefab."));
	}
	const auto result =
		publication.commit(bytes.size(), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
	if (report) {
		report->committed = result.committed;
		report->backupPath = result.backupPath;
		report->warnings = result.warnings;
		report->recoveryPaths = result.recoveryPaths;
	}
	if (!result.committed) {
		QStringList details{result.error};
		details += result.warnings;
		if (!result.backupPath.isEmpty()) {
			details << QCoreApplication::translate("LevelPrefab", "Backup: %1").arg(result.backupPath);
		}
		for (const auto &path : result.recoveryPaths) {
			details << QCoreApplication::translate("LevelPrefab", "Recovery file: %1").arg(path);
		}
		return fail(error, details.join('\n'));
	}
	return true;
}

bool insertLevelPrefab(LevelMapDocument *destination, const LevelPrefab &prefab, const LevelPrefabPlacement &placement,
					   LevelPrefabReport *report, QString *error, LevelPrefabCancellation cancelled)
{
	if (error) {
		error->clear();
	}
	if (!destination || !textMap(*destination) || destination->engineFamily.compare(prefab.engineFamily, Qt::CaseInsensitive) != 0) {
		return fail(error,
					QCoreApplication::translate("LevelPrefab", "Insert the prefab into a Quake-family map with the same engine family."));
	}
	if (!finite(placement.position) || !finite(placement.rotation, 360000)) {
		return fail(error,
					QCoreApplication::translate("LevelPrefab", "Prefab position and rotation must be finite and within supported limits."));
	}
	LevelMapDocument definition;
	LevelPrefabReport summary;
	if (!inspectLevelPrefab(prefab, &definition, &summary, error, cancelled)) {
		return false;
	}
	const auto existingDialect = levelMapBrushDialect(*destination);
	if (existingDialect == QStringLiteral("mixed") ||
		(!existingDialect.isEmpty() && !summary.dialect.isEmpty() && existingDialect != summary.dialect)) {
		return fail(error,
					QCoreApplication::translate(
						"LevelPrefab", "Prefab and destination brush dialects differ. Convert them to the same format before insertion."));
	}
	// Preflight integer allocation before the clipboard service allocates IDs.
	const auto capacity = [&](const auto &objects, qsizetype added) {
		int highest = -1;
		for (const auto &item : objects) {
			highest = std::max(highest, item.id);
		}
		return static_cast<qint64>(highest) + added < std::numeric_limits<int>::max();
	};
	if (!capacity(destination->entities, definition.entities.size()) || !capacity(destination->brushes, definition.brushes.size()) ||
		!capacity(destination->patches, definition.patches.size())) {
		return fail(error, QCoreApplication::translate("LevelPrefab", "The map has no remaining object identifiers for this prefab."));
	}
	QSet<QString> reserved, names;
	for (const auto &entity : destination->entities) {
		for (const auto &p : entity.properties) {
			if (p.key.compare(QStringLiteral("targetname"), Qt::CaseInsensitive) == 0 || targetKey(p.key)) {
				reserved.insert(p.value);
			}
		}
	}
	for (const auto &entity : definition.entities) {
		const auto name = property(entity, QStringLiteral("targetname"));
		if (!name.isEmpty()) {
			names.insert(name);
		}
	}
	// A new internal name must not capture one of the prefab's external links.
	for (const auto &name : summary.externalTargets) {
		reserved.insert(name);
	}
	const auto available = [&](const QString &prefix) {
		for (const auto &name : names) {
			if (reserved.contains(prefix + name)) {
				return false;
			}
		}
		return true;
	};
	summary.prefix = placement.targetPrefix;
	if (!summary.prefix.isEmpty() &&
		!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{0,31}$")).match(summary.prefix).hasMatch()) {
		return fail(error,
					QCoreApplication::translate(
						"LevelPrefab",
						"A target prefix must start with a letter or underscore and contain at most 32 letters, digits or underscores."));
	}
	if (summary.prefix.isEmpty()) {
		for (int index = 1; index <= 100000; ++index) {
			if (!check(cancelled, error)) {
				return false;
			}
			const auto prefix = QStringLiteral("prefab%1_").arg(index);
			if (available(prefix)) {
				summary.prefix = prefix;
				break;
			}
		}
	}
	if (summary.prefix.isEmpty() || !available(summary.prefix)) {
		return fail(
			error, QCoreApplication::translate("LevelPrefab", "The target prefix would collide with existing entity names or references."));
	}
	for (const auto &name : names) {
		if ((summary.prefix + name).toUtf8().size() > 255) {
			return fail(error, QCoreApplication::translate("LevelPrefab", "A namespaced prefab target exceeds 255 UTF-8 bytes."));
		}
		summary.renamedTargets.insert(name, summary.prefix + name);
	}
	// Use normal property services so source-line rewrites, comments and cached
	// entity fields remain consistent. These edits happen only in the isolated draft.
	const auto entities = definition.entities;
	for (const auto &entity : entities) {
		for (const auto &p : entity.properties) {
			if ((p.key.compare(QStringLiteral("targetname"), Qt::CaseInsensitive) == 0 || targetKey(p.key)) &&
				summary.renamedTargets.contains(p.value)) {
				if (!setLevelMapEntityProperty(&definition, entity.id, p.key, summary.renamedTargets.value(p.value), error)) {
					return false;
				}
			}
		}
	}
	if (!setLevelMapSelection(&definition, contentSelection(definition), error)) {
		return false;
	}
	const double degrees[3]{placement.rotation.x, placement.rotation.y, placement.rotation.z};
	for (int axis = 0; axis < 3; ++axis) {
		if (!check(cancelled, error)) {
			return false;
		}
		if (std::abs(std::remainder(degrees[axis], 360.0)) < 1e-9) {
			continue;
		}
		if (!rotateLevelMapSelection(&definition, {axis, degrees[axis], prefab.anchor, placement.textureLock, false}, error)) {
			return false;
		}
	}
	const LevelMapVec3 delta{placement.position.x - prefab.anchor.x, placement.position.y - prefab.anchor.y,
							 placement.position.z - prefab.anchor.z, true};
	if ((delta.x != 0 || delta.y != 0 || delta.z != 0) &&
		!moveLevelMapSelection(&definition, delta.x, delta.y, delta.z, {placement.textureLock, false}, error)) {
		return false;
	}
	const auto text = levelMapSelectionText(definition, error);
	if (text.isEmpty() || !check(cancelled, error)) {
		return false;
	}
	auto candidate = *destination;
	if (!pasteLevelMapText(&candidate, text, error)) {
		return false;
	}
	if (!check(cancelled, error)) {
		return false;
	}
	summary.inserted = candidate.selection;
	candidate.undoStack.last().selectionSnapshot = destination->selection;
	candidate.undoStack.last().selectionResult = candidate.selection;
	candidate.undoStack.last().description = QCoreApplication::translate("LevelPrefab", "Insert prefab %1").arg(prefab.name);
	candidate.undoStack.last().undoDescription = QCoreApplication::translate("LevelPrefab", "Remove prefab %1").arg(prefab.name);
	*destination = std::move(candidate);
	if (report) {
		*report = std::move(summary);
	}
	return true;
}
QJsonObject levelPrefabReportJson(const LevelPrefabReport &report)
{
	QJsonObject names;
	for (auto it = report.renamedTargets.cbegin(); it != report.renamedTargets.cend(); ++it) {
		names.insert(it.key(), it.value());
	}
	QJsonArray objects;
	for (const auto &ref : report.inserted) {
		objects << levelMapSelectionRefId(ref);
	}
	return {{QStringLiteral("entities"), report.statistics.entityCount - 1},
			{QStringLiteral("brushes"), report.statistics.brushCount},
			{QStringLiteral("patches"), report.statistics.patchCount},
			{QStringLiteral("dialect"), report.dialect},
			{QStringLiteral("targetPrefix"), report.prefix},
			{QStringLiteral("renamedTargets"), names},
			{QStringLiteral("externalTargets"), QJsonArray::fromStringList(report.externalTargets)},
			{QStringLiteral("warnings"), QJsonArray::fromStringList(report.warnings)},
			{QStringLiteral("inserted"), objects}};
}
} // namespace vibestudio
