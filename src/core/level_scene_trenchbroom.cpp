#include "core/level_scene_trenchbroom.h"
#include "core/level_linked_groups.h"
#include "core/level_scene.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>

// TrenchBroom's keys for layers, groups, linked groups and protected
// properties, implemented independently from its manual ("Linked Groups in
// the Map File", https://trenchbroom.github.io/manual/latest/) and the maps it
// writes. TrenchBroom is GPL-3.0-or-later; none of its code is used. See
// docs/CREDITS.md.
namespace vibestudio {
namespace {

constexpr QLatin1String kType("_tb_type");
constexpr QLatin1String kLayerType("_tb_layer");
constexpr QLatin1String kGroupType("_tb_group");
constexpr QLatin1String kId("_tb_id");
constexpr QLatin1String kName("_tb_name");
constexpr QLatin1String kLayer("_tb_layer");
constexpr QLatin1String kGroup("_tb_group");
constexpr QLatin1String kLink("_tb_linked_group_id");
constexpr QLatin1String kTransformation("_tb_transformation");
constexpr QLatin1String kSortIndex("_tb_layer_sort_index");
constexpr QLatin1String kHidden("_tb_layer_hidden");
constexpr QLatin1String kLocked("_tb_layer_locked");
constexpr QLatin1String kProtected("_tb_protected_properties");

bool sameKey(const QString& key, QLatin1String name)
{
	return key.compare(name, Qt::CaseInsensitive) == 0;
}

std::optional<QString> valueOf(const LevelMapEntity& entity, QLatin1String key)
{
	for (const LevelMapProperty& property : entity.properties) {
		if (sameKey(property.key, key)) {
			return property.value;
		}
	}
	return std::nullopt;
}

QString kept(const QMap<QString, QString>& properties, QLatin1String key)
{
	for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
		if (sameKey(it.key(), key)) {
			return it.value();
		}
	}
	return {};
}

bool isWorld(const LevelMapEntity& entity)
{
	return entity.className.compare(QLatin1String("worldspawn"), Qt::CaseInsensitive) == 0;
}

// The node a TrenchBroom layer or group entity stands for.
std::optional<LevelSceneNodeKind> wrapperKind(const LevelMapEntity& entity)
{
	if (entity.className.compare(QLatin1String("func_group"), Qt::CaseInsensitive) != 0) {
		return std::nullopt;
	}
	const QString type = valueOf(entity, kType).value_or(QString()).trimmed();
	if (type == kLayerType) {
		return LevelSceneNodeKind::Layer;
	}
	if (type == kGroupType) {
		return LevelSceneNodeKind::Group;
	}
	return std::nullopt;
}

// Keys an entity names its layer, group and protected keys with.
bool memberKey(const QString& key)
{
	return sameKey(key, kGroup) || sameKey(key, kLayer) || sameKey(key, kProtected);
}

// Ids worked out from the keys: name-based UUIDs in a namespace of our own.
QString stableId(const QString& seed)
{
	static const QUuid space = QUuid::fromString(QLatin1String("7b1e5d3a-90c4-4f62-8e1d-2a6c9b07f345"));
	return QUuid::createUuidV5(space, seed.toUtf8()).toString(QUuid::WithoutBraces);
}

QString canonicalLink(const QString& raw)
{
	const QUuid uuid = QUuid::fromString(raw.trimmed());
	return uuid.isNull() ? stableId(QStringLiteral("link:") + raw.trimmed()) : uuid.toString(QUuid::WithoutBraces);
}

// A line before any `//` comment that is not inside quotes.
QString code(const QString& line)
{
	bool quoted = false;
	for (qsizetype index = 0; index < line.size(); ++index) {
		const QChar ch = line.at(index);
		if (ch == QLatin1Char('"')) {
			quoted = !quoted;
		} else if (!quoted && ch == QLatin1Char('/') && index + 1 < line.size() && line.at(index + 1) == QLatin1Char('/')) {
			return line.left(index);
		}
	}
	return line;
}

// The opening and closing quote of each string on a line, up to a comment.
QVector<std::pair<qsizetype, qsizetype>> quotedSpans(const QString& line)
{
	QVector<std::pair<qsizetype, qsizetype>> spans;
	const QString text = code(line);
	for (qsizetype index = 0; index < text.size(); ++index) {
		if (text.at(index) != QLatin1Char('"')) {
			continue;
		}
		const qsizetype close = text.indexOf(QLatin1Char('"'), index + 1);
		if (close < 0) {
			break;
		}
		spans.push_back({index, close});
		index = close;
	}
	return spans;
}

// Takes the "key" "value" pairs whose keys `drop` names off a line, with the
// spaces after them. False when such a key's value is not on the same line.
bool dropPairs(QString* line, const std::function<bool(const QString&)>& drop)
{
	const auto spans = quotedSpans(*line);
	QVector<std::pair<qsizetype, qsizetype>> removals;
	for (int index = 0; index < spans.size(); index += 2) {
		const QString key = line->mid(spans.at(index).first + 1, spans.at(index).second - spans.at(index).first - 1);
		if (!drop(key)) {
			continue;
		}
		if (index + 1 >= spans.size()) {
			return false;
		}
		removals.push_back({spans.at(index).first, spans.at(index + 1).second + 1});
	}
	for (auto it = removals.crbegin(); it != removals.crend(); ++it) {
		qsizetype start = it->first;
		qsizetype end = it->second;
		while (end < line->size() && line->at(end).isSpace()) {
			++end;
		}
		if (end == line->size()) {
			while (start > 0 && line->at(start - 1).isSpace()) {
				--start;
			}
		}
		line->remove(start, end - start);
	}
	return true;
}

// Radiant and TrenchBroom head entities and brushes with "// entity N" or
// "// brush N"; such a comment goes with its object.
bool markerComment(const QString& line)
{
	static const QRegularExpression marker(QStringLiteral(R"(^\s*//\s*(entity|brush|patch)\s+\d+\s*$)"),
		QRegularExpression::CaseInsensitiveOption);
	return marker.match(line).hasMatch();
}

// "_tb_transformation": 16 numbers, a 4x4 matrix row by row whose last row is
// 0 0 0 1. The top three rows are the frame.
bool readFrame(const QString& text, std::array<double, 12>* frame)
{
	const QStringList parts = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (parts.size() != 16) {
		return false;
	}
	std::array<double, 16> m {};
	for (int index = 0; index < 16; ++index) {
		bool ok = false;
		m[index] = parts.at(index).toDouble(&ok);
		if (!ok || !std::isfinite(m[index]) || std::abs(m[index]) > 1e6) {
			return false;
		}
	}
	if (std::abs(m[12]) > 1e-9 || std::abs(m[13]) > 1e-9 || std::abs(m[14]) > 1e-9 || std::abs(m[15] - 1.0) > 1e-9) {
		return false;
	}
	const double determinant = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
	if (!std::isfinite(determinant) || std::abs(determinant) < 1e-9) {
		return false;
	}
	std::copy(m.cbegin(), m.cbegin() + 12, frame->begin());
	return true;
}

QString number(double value)
{
	return QString::number(std::abs(value) < 1e-12 ? 0.0 : value, 'g', 17);
}

QString frameText(const std::array<double, 12>& frame)
{
	QStringList parts;
	for (double value : frame) {
		parts << number(value);
	}
	parts << QStringLiteral("0") << QStringLiteral("0") << QStringLiteral("0") << QStringLiteral("1");
	return parts.join(QLatin1Char(' '));
}

// "_tb_protected_properties": keys separated by semicolons, a backslash
// escaping a semicolon or backslash in a key.
QStringList splitKeys(const QString& text)
{
	QStringList keys;
	QString current;
	bool escaped = false;
	for (const QChar ch : text) {
		if (escaped) {
			current += ch;
			escaped = false;
		} else if (ch == QLatin1Char('\\')) {
			escaped = true;
		} else if (ch == QLatin1Char(';')) {
			keys << current;
			current.clear();
		} else {
			current += ch;
		}
	}
	keys << current;
	return keys;
}

QString joinKeys(const QStringList& keys)
{
	QStringList escaped;
	for (QString key : keys) {
		key.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
		key.replace(QLatin1Char(';'), QStringLiteral("\\;"));
		escaped << key;
	}
	return escaped.join(QLatin1Char(';'));
}

// A protected key in the property editor's canonical spelling, or empty.
QString canonicalKey(const QString& key)
{
	const QString folded = key.trimmed().toCaseFolded();
	const bool usable = !folded.isEmpty() && folded.size() <= 1024 && std::none_of(folded.cbegin(), folded.cend(), [](QChar c) {
		return c == QLatin1Char('"') || c.category() == QChar::Other_Control || c == QChar::LineSeparator || c == QChar::ParagraphSeparator;
	});
	return usable ? folded : QString();
}

// A scene name: no control characters or surrounding spaces, at most 128
// characters, never empty.
QString sceneName(const QString& name, LevelSceneNodeKind kind)
{
	QString cleaned;
	for (const QChar ch : name) {
		if (ch.category() != QChar::Other_Control && ch != QChar::LineSeparator && ch != QChar::ParagraphSeparator) {
			cleaned += ch;
		}
	}
	cleaned = cleaned.trimmed().left(128).trimmed();
	if (!cleaned.isEmpty()) {
		return cleaned;
	}
	return kind == LevelSceneNodeKind::Layer ? QCoreApplication::translate("LevelTrenchBroom", "Layer")
											 : QCoreApplication::translate("LevelTrenchBroom", "Group");
}

// The name to write for a node: the name TrenchBroom gave it while VibeStudio
// only numbered it to tell siblings apart, else its own. Map strings cannot
// hold double quotes.
QString writtenName(const LevelSceneNode& node)
{
	const QString original = kept(node.trenchBroomProperties, kName);
	QString name = node.name;
	if (!original.isEmpty()) {
		const QString base = sceneName(original, node.kind);
		static const QRegularExpression numbered(QStringLiteral(R"(^(.*\S) \d+$)"));
		const auto match = numbered.match(node.name);
		const QString stem = match.hasMatch() ? match.captured(1) : QString();
		if (node.name == base || stem == base || (!stem.isEmpty() && base.size() > 120 && base.startsWith(stem))) {
			name = original;
		}
	}
	return name.replace(QLatin1Char('"'), QLatin1Char('\''));
}

QString pairLine(const QString& key, const QString& value)
{
	return QStringLiteral("\"%1\" \"%2\"").arg(key, value);
}

bool blockOwnsLines(const QStringList& lines, int start, int end)
{
	return start > 0 && end >= start && end <= lines.size() && code(lines.at(start - 1)).trimmed().startsWith(QLatin1Char('{'))
		&& code(lines.at(end - 1)).trimmed().endsWith(QLatin1Char('}'));
}

QString selector(const char* kind, int id)
{
	return QLatin1String(kind) + QLatin1Char(':') + QString::number(id);
}

} // namespace

QString levelSceneStorageId(LevelSceneStorage storage)
{
	return storage == LevelSceneStorage::TrenchBroom ? QStringLiteral("trenchbroom") : QStringLiteral("vibestudio");
}

bool levelSceneStorageFromId(const QString& id, LevelSceneStorage* storage)
{
	const QString folded = id.trimmed().toLower();
	if (folded != QLatin1String("trenchbroom") && folded != QLatin1String("vibestudio")) {
		return false;
	}
	if (storage) {
		*storage = folded == QLatin1String("trenchbroom") ? LevelSceneStorage::TrenchBroom : LevelSceneStorage::VibeStudio;
	}
	return true;
}

LevelTrenchBroomImport readLevelTrenchBroomScene(const LevelMapDocument& source)
{
	LevelTrenchBroomImport result;
	if (source.format != LevelMapFormat::QuakeMap && source.format != LevelMapFormat::Quake3Map) {
		return result;
	}
	const QStringList& lines = source.textLines;
	const auto& entities = source.entities;
	int world = -1;
	QVector<int> wrappers;
	bool memberKeys = false;
	for (int index = 0; index < entities.size(); ++index) {
		const LevelMapEntity& entity = entities.at(index);
		if (isWorld(entity)) {
			world = world < 0 ? index : world;
		} else if (wrapperKind(entity)) {
			wrappers << index;
		} else {
			memberKeys = memberKeys || std::any_of(entity.properties.cbegin(), entity.properties.cend(),
				[](const LevelMapProperty& property) { return memberKey(property.key); });
		}
	}
	if (wrappers.isEmpty() && !memberKeys) {
		return result;
	}
	result.found = true;
	const auto refuse = [](const QString& why) {
		LevelTrenchBroomImport refused;
		refused.found = true;
		refused.warnings << QCoreApplication::translate("LevelTrenchBroom",
			"TrenchBroom's layers and groups could not be read: %1 The map opens with them as func_group entities.").arg(why);
		return refused;
	};
	const auto lineAt = [&lines](int line) { return line > 0 && line <= lines.size() ? lines.at(line - 1) : QString(); };

	// The brushes and patches of each layer and group, in file order.
	QHash<int, QVector<std::pair<int, int>>> blocks;
	QSet<int> wrapperIds;
	for (int index : std::as_const(wrappers)) {
		const LevelMapEntity& entity = entities.at(index);
		if (!blockOwnsLines(lines, entity.startLine, entity.endLine)) {
			return refuse(QCoreApplication::translate("LevelTrenchBroom", "a layer or group entity shares its lines with other text."));
		}
		wrapperIds.insert(entity.id);
	}
	const auto collect = [&](int entityId, int start, int end) {
		if (wrapperIds.contains(entityId)) {
			blocks[entityId].push_back({start, end});
		}
	};
	for (const LevelMapBrush& brush : source.brushes) {
		collect(brush.entityId, brush.startLine, brush.endLine);
	}
	for (const LevelMapPatch& patch : source.patches) {
		collect(patch.entityId, patch.startLine, patch.endLine);
	}
	for (auto it = blocks.begin(); it != blocks.end(); ++it) {
		std::sort(it->begin(), it->end());
		const LevelMapEntity* owner = nullptr;
		for (const LevelMapEntity& entity : entities) {
			if (entity.id == it.key()) {
				owner = &entity;
			}
		}
		for (const auto& [start, end] : std::as_const(*it)) {
			if (!blockOwnsLines(lines, start, end) || !owner || start <= owner->startLine || end >= owner->endLine) {
				return refuse(QCoreApplication::translate("LevelTrenchBroom", "a brush or patch of a layer or group shares its lines with other text."));
			}
		}
	}
	const LevelMapEntity* worldEntity = world >= 0 ? &entities.at(world) : nullptr;
	if (!blocks.isEmpty()
		&& (!worldEntity || !blockOwnsLines(lines, worldEntity->startLine, worldEntity->endLine)
			|| code(lineAt(worldEntity->endLine)).trimmed() != QLatin1String("}"))) {
		return refuse(QCoreApplication::translate("LevelTrenchBroom", "the map has no worldspawn entity whose closing brace stands on its own line."));
	}

	// The layer and group entities go, with the comments heading them, and
	// the member keys come off their lines.
	QSet<int> dropped;
	for (int index : std::as_const(wrappers)) {
		const LevelMapEntity& entity = entities.at(index);
		const int first = entity.startLine > 1 && markerComment(lineAt(entity.startLine - 1)) ? entity.startLine - 1 : entity.startLine;
		for (int line = first; line <= entity.endLine; ++line) {
			dropped.insert(line);
		}
	}
	QHash<int, QString> edited;
	for (const LevelMapEntity& entity : entities) {
		if (isWorld(entity) || wrapperIds.contains(entity.id)) {
			continue;
		}
		for (const LevelMapProperty& property : entity.properties) {
			if (!memberKey(property.key)) {
				continue;
			}
			QString text = edited.value(property.line, lineAt(property.line));
			if (property.line <= 0 || property.line > lines.size() || !dropPairs(&text, memberKey)) {
				return refuse(QCoreApplication::translate("LevelTrenchBroom", "the value of a TrenchBroom key on line %1 is not on the same line.")
						.arg(property.line));
			}
			edited.insert(property.line, text);
		}
	}
	for (auto it = edited.cbegin(); it != edited.cend(); ++it) {
		if (code(it.value()).trimmed().isEmpty()) {
			dropped.insert(it.key());
		}
	}

	// Their brushes and patches join worldspawn's, ahead of its closing brace.
	QStringList out;
	out.reserve(lines.size());
	QHash<int, int> placed;
	const auto push = [&](int line) {
		out << edited.value(line, lineAt(line));
		result.sourceLines << line;
		placed.insert(line, static_cast<int>(out.size()));
	};
	const int worldEnd = worldEntity ? worldEntity->endLine : -1;
	for (int line = 1; line <= lines.size(); ++line) {
		if (line == worldEnd) {
			for (int index : std::as_const(wrappers)) {
				for (const auto& [start, end] : blocks.value(entities.at(index).id)) {
					for (int moved = start; moved <= end; ++moved) {
						push(moved);
					}
				}
			}
		}
		if (!dropped.contains(line)) {
			push(line);
		}
	}
	result.text = out.join(source.lineEnding.isEmpty() ? QStringLiteral("\n") : source.lineEnding);

	// Layers in their sort order, then groups in file order.
	struct Found {
		int entity = -1;
		int sort = 0;
		QString number;
		LevelTrenchBroomNode node;
	};
	QVector<Found> layers;
	QVector<Found> groups;
	for (int index : std::as_const(wrappers)) {
		const LevelMapEntity& entity = entities.at(index);
		Found found;
		found.entity = index;
		found.node.kind = *wrapperKind(entity);
		found.number = valueOf(entity, kId).value_or(QString()).trimmed();
		found.node.name = valueOf(entity, kName).value_or(QString());
		const bool layer = found.node.kind == LevelSceneNodeKind::Layer;
		for (const LevelMapProperty& property : entity.properties) {
			const QString& key = property.key;
			const bool read = sameKey(key, QLatin1String("classname")) || sameKey(key, kType) || sameKey(key, kName)
				|| (layer ? sameKey(key, kSortIndex) || sameKey(key, kHidden) || sameKey(key, kLocked)
						  : sameKey(key, kGroup) || sameKey(key, kLayer) || sameKey(key, kLink) || sameKey(key, kTransformation));
			if (!read) {
				found.node.properties.insert(key, property.value);
			}
		}
		if (layer) {
			bool sorted = false;
			found.sort = valueOf(entity, kSortIndex).value_or(QString()).trimmed().toInt(&sorted);
			found.sort = sorted ? found.sort : std::numeric_limits<int>::max();
			found.node.visible = valueOf(entity, kHidden).value_or(QString()).trimmed() != QLatin1String("1");
			found.node.locked = valueOf(entity, kLocked).value_or(QString()).trimmed() == QLatin1String("1");
			layers << found;
		} else {
			found.node.link = valueOf(entity, kLink).value_or(QString()).trimmed();
			if (const auto transformation = valueOf(entity, kTransformation); transformation && !found.node.link.isEmpty()) {
				found.node.frameValid = readFrame(*transformation, &found.node.frame);
			}
			groups << found;
		}
	}
	std::stable_sort(layers.begin(), layers.end(), [](const Found& a, const Found& b) { return a.sort < b.sort; });
	QVector<Found> all = layers + groups;
	QHash<int, int> nodeOfEntity;
	QHash<QString, int> layerNumbers;
	QHash<QString, int> groupNumbers;
	for (int index = 0; index < all.size(); ++index) {
		Found& found = all[index];
		nodeOfEntity.insert(entities.at(found.entity).id, index);
		auto& numbers = found.node.kind == LevelSceneNodeKind::Layer ? layerNumbers : groupNumbers;
		const int line = entities.at(found.entity).startLine;
		if (found.number.isEmpty() || numbers.contains(found.number)) {
			result.warnings << (found.number.isEmpty()
				? QCoreApplication::translate("LevelTrenchBroom", "The TrenchBroom layer or group on line %1 has no _tb_id; nothing else can be placed in it.").arg(line)
				: QCoreApplication::translate("LevelTrenchBroom", "The TrenchBroom layer or group on line %1 has the same _tb_id as another; what names %2 goes to the first.")
					  .arg(line).arg(found.number));
			for (auto it = found.node.properties.begin(); it != found.node.properties.end();) {
				it = sameKey(it.key(), kId) ? found.node.properties.erase(it) : std::next(it);
			}
			continue;
		}
		numbers.insert(found.number, index);
	}
	// Where each group sits: in the group or layer it names, else at the top.
	for (Found& found : all) {
		if (found.node.kind != LevelSceneNodeKind::Group) {
			continue;
		}
		const LevelMapEntity& entity = entities.at(found.entity);
		const QString inGroup = valueOf(entity, kGroup).value_or(QString()).trimmed();
		const QString inLayer = valueOf(entity, kLayer).value_or(QString()).trimmed();
		const QString wanted = inGroup.isEmpty() ? inLayer : inGroup;
		if (wanted.isEmpty()) {
			continue;
		}
		found.node.parent = (inGroup.isEmpty() ? layerNumbers : groupNumbers).value(wanted, -1);
		if (found.node.parent < 0) {
			result.warnings << QCoreApplication::translate("LevelTrenchBroom",
				"The TrenchBroom group on line %1 names a layer or group (%2) the map does not have; it is placed at the top.").arg(entity.startLine).arg(wanted);
		}
	}
	for (int index = 0; index < all.size(); ++index) {
		int at = all.at(index).node.parent;
		for (int depth = 0; at >= 0; ++depth) {
			if (at == index || depth >= kLevelSceneMaxDepth) {
				all[index].node.parent = -1;
				result.warnings << QCoreApplication::translate("LevelTrenchBroom",
					"The TrenchBroom group on line %1 is nested in itself or too deeply; it is placed at the top.").arg(entities.at(all.at(index).entity).startLine);
				break;
			}
			at = all.at(at).node.parent;
		}
	}
	for (const Found& found : std::as_const(all)) {
		result.nodes << found.node;
	}

	// What each node holds: its own brushes and patches, and the entities that
	// name it. An entity's protected keys go with it.
	for (int index : std::as_const(wrappers)) {
		const int node = nodeOfEntity.value(entities.at(index).id);
		for (const auto& [start, end] : blocks.value(entities.at(index).id)) {
			result.members.insert(placed.value(start), node);
		}
	}
	for (const LevelMapEntity& entity : entities) {
		if (isWorld(entity) || wrapperIds.contains(entity.id) || !placed.contains(entity.startLine)) {
			continue;
		}
		const QString inGroup = valueOf(entity, kGroup).value_or(QString()).trimmed();
		const QString inLayer = valueOf(entity, kLayer).value_or(QString()).trimmed();
		if (!inGroup.isEmpty() || !inLayer.isEmpty()) {
			const QString wanted = inGroup.isEmpty() ? inLayer : inGroup;
			const int node = (inGroup.isEmpty() ? layerNumbers : groupNumbers).value(wanted, -1);
			if (node >= 0) {
				result.members.insert(placed.value(entity.startLine), node);
			} else {
				result.warnings << QCoreApplication::translate("LevelTrenchBroom",
					"The entity on line %1 names a TrenchBroom layer or group (%2) the map does not have; it stays in Default.").arg(entity.startLine).arg(wanted);
			}
		}
		if (const auto keys = valueOf(entity, kProtected)) {
			QStringList list;
			for (const QString& key : splitKeys(*keys)) {
				if (!key.trimmed().isEmpty()) {
					list << key;
				}
			}
			if (!list.isEmpty()) {
				result.protectedKeys.insert(placed.value(entity.startLine), list);
			}
		}
	}
	return result;
}

bool applyLevelTrenchBroomScene(LevelMapDocument* document, const LevelTrenchBroomImport& import, QStringList* warnings, QString* error)
{
	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& text) {
		if (error) {
			*error = text;
		}
		return false;
	};
	if (!document || !import.found || import.text.isEmpty()) {
		return fail(QCoreApplication::translate("LevelTrenchBroom", "The map has no TrenchBroom layers or groups to read."));
	}
	if (import.nodes.size() > kLevelSceneMaxNodes) {
		return fail(QCoreApplication::translate("LevelTrenchBroom", "A scene supports at most 1,000 layers and groups."));
	}
	const auto warn = [warnings](const QString& text) {
		if (warnings) {
			*warnings << text;
		}
	};
	LevelSceneState state;
	QVector<QString> ids;
	QSet<QString> used;
	for (int index = 0; index < import.nodes.size(); ++index) {
		const LevelTrenchBroomNode& node = import.nodes.at(index);
		const QString number = kept(node.properties, kId);
		const QString seed = (node.kind == LevelSceneNodeKind::Layer ? QStringLiteral("layer:") : QStringLiteral("group:"))
			+ (number.isEmpty() ? QStringLiteral("node-") + QString::number(index) : number);
		QString id = stableId(seed);
		for (int again = 2; used.contains(id); ++again) {
			id = stableId(seed + QLatin1Char('#') + QString::number(again));
		}
		used.insert(id);
		ids << id;
	}
	// Siblings need distinct names: later ones are numbered, and remember the
	// name TrenchBroom had for them.
	QSet<QString> names;
	for (int index = 0; index < import.nodes.size(); ++index) {
		const LevelTrenchBroomNode& source = import.nodes.at(index);
		LevelSceneNode node;
		node.id = ids.at(index);
		node.kind = source.kind;
		node.parentId = source.kind == LevelSceneNodeKind::Group && source.parent >= 0 ? ids.at(source.parent) : QString();
		node.visible = source.visible;
		node.locked = source.locked;
		node.trenchBroomProperties = source.properties;
		const QString name = sceneName(source.name, source.kind);
		QString unique = name;
		for (int again = 2; names.contains(node.parentId + QLatin1Char('/') + unique.toCaseFolded()); ++again) {
			const QString suffix = QLatin1Char(' ') + QString::number(again);
			unique = name.left(128 - suffix.size()).trimmed() + suffix;
		}
		names.insert(node.parentId + QLatin1Char('/') + unique.toCaseFolded());
		node.name = unique;
		if (unique != source.name) {
			node.trenchBroomProperties.insert(kName, source.name);
		}
		if (!source.link.isEmpty()) {
			node.linkId = canonicalLink(source.link);
			if (source.frameValid) {
				node.linkFrame = source.frame;
			} else {
				node.linkOutOfSync = true;
				warn(QCoreApplication::translate("LevelTrenchBroom",
					"Linked group %1 has a _tb_transformation that is not a usable frame; it is taken as unmoved and marked out of sync.").arg(node.name));
			}
		}
		state.nodes << node;
	}
	// A linked group cannot hold a group it is linked with.
	for (LevelSceneNode& node : state.nodes) {
		if (node.linkId.isEmpty()) {
			continue;
		}
		QString at = node.parentId;
		for (int depth = 0; !at.isEmpty() && depth < kLevelSceneMaxDepth; ++depth) {
			const LevelSceneNode* ancestor = levelSceneNode(state, at);
			if (!ancestor) {
				break;
			}
			if (ancestor->linkId == node.linkId) {
				warn(QCoreApplication::translate("LevelTrenchBroom", "Group %1 is linked with a group that holds it; it is no longer linked.").arg(node.name));
				node.linkId.clear();
				node.linkFrame = LevelSceneNode {}.linkFrame;
				node.linkOutOfSync = false;
				break;
			}
			at = ancestor->parentId;
		}
	}

	// Members: entities that named a node, and the brushes and patches of the
	// layer and group entities, now in worldspawn.
	QSet<int> world;
	for (const LevelMapEntity& entity : document->entities) {
		if (isWorld(entity)) {
			world.insert(entity.id);
		}
	}
	for (const LevelMapEntity& entity : document->entities) {
		if (world.contains(entity.id) || entity.startLine <= 0) {
			continue;
		}
		const QString object = selector("entity", entity.id);
		if (const int at = import.members.value(entity.startLine, -1); at >= 0) {
			state.nodes[at].objects << object;
		}
		if (import.protectedKeys.contains(entity.startLine)) {
			QStringList keys;
			for (const QString& key : import.protectedKeys.value(entity.startLine)) {
				const QString canonical = canonicalKey(key);
				if (canonical.isEmpty()) {
					warn(QCoreApplication::translate("LevelTrenchBroom", "Protected key \"%1\" of %2 cannot be used and was left out.").arg(key, object));
				} else {
					keys << canonical;
				}
			}
			keys.sort();
			keys.removeDuplicates();
			if (!keys.isEmpty()) {
				state.protectedProperties.insert(object, keys);
			}
		}
	}
	const auto place = [&](const auto& primitives, const char* kind) {
		for (const auto& primitive : primitives) {
			if (world.contains(primitive.entityId) && primitive.startLine > 0) {
				if (const int at = import.members.value(primitive.startLine, -1); at >= 0) {
					state.nodes[at].objects << selector(kind, primitive.id);
				}
			}
		}
	};
	place(document->brushes, "brush");
	place(document->patches, "patch");

	// Corresponding objects, as Update Linked Groups leaves them: TrenchBroom
	// keeps linked groups alike in structure, so an object's place in its
	// group (its group nested within, entity, brush or patch, by order) is its
	// identity in the link set.
	QHash<int, QVector<int>> ownedBrushes;
	QHash<int, QVector<int>> ownedPatches;
	for (const LevelMapBrush& brush : document->brushes) {
		if (!world.contains(brush.entityId)) {
			ownedBrushes[brush.entityId] << brush.id;
		}
	}
	for (const LevelMapPatch& patch : document->patches) {
		if (!world.contains(patch.entityId)) {
			ownedPatches[patch.entityId] << patch.id;
		}
	}
	std::function<void(const QString&, const QString&, const QString&, QMap<QString, QString>*)> walk;
	walk = [&](const QString& nodeId, const QString& path, const QString& link, QMap<QString, QString>* identities) {
		const LevelSceneNode* node = levelSceneNode(state, nodeId);
		QVector<int> entities;
		QVector<int> brushes;
		QVector<int> patches;
		for (const QString& object : node->objects) {
			const int id = object.section(QLatin1Char(':'), 1).toInt();
			(object.startsWith(QLatin1String("entity:")) ? entities : (object.startsWith(QLatin1String("brush:")) ? brushes : patches)) << id;
		}
		std::sort(entities.begin(), entities.end());
		std::sort(brushes.begin(), brushes.end());
		std::sort(patches.begin(), patches.end());
		const auto name = [&link](const QString& place) { return stableId(link + QLatin1Char('/') + place); };
		for (int index = 0; index < entities.size(); ++index) {
			const QString at = path + QLatin1Char('e') + QString::number(index);
			identities->insert(selector("entity", entities.at(index)), name(at));
			const QVector<int> owned = ownedBrushes.value(entities.at(index));
			for (int brush = 0; brush < owned.size(); ++brush) {
				identities->insert(selector("brush", owned.at(brush)), name(at + QStringLiteral("/b") + QString::number(brush)));
			}
			const QVector<int> ownedPatchIds = ownedPatches.value(entities.at(index));
			for (int patch = 0; patch < ownedPatchIds.size(); ++patch) {
				identities->insert(selector("patch", ownedPatchIds.at(patch)), name(at + QStringLiteral("/p") + QString::number(patch)));
			}
		}
		for (int index = 0; index < brushes.size(); ++index) {
			identities->insert(selector("brush", brushes.at(index)), name(path + QLatin1Char('b') + QString::number(index)));
		}
		for (int index = 0; index < patches.size(); ++index) {
			identities->insert(selector("patch", patches.at(index)), name(path + QLatin1Char('p') + QString::number(index)));
		}
		int nested = 0;
		for (const LevelSceneNode& child : state.nodes) {
			if (child.parentId == nodeId && child.kind == LevelSceneNodeKind::Group) {
				walk(child.id, path + QLatin1Char('g') + QString::number(nested++) + QLatin1Char('/'), link, identities);
			}
		}
	};
	QHash<QString, QMap<QString, QString>> identities;
	for (const LevelSceneNode& node : std::as_const(state.nodes)) {
		if (!node.linkId.isEmpty()) {
			walk(node.id, {}, node.linkId, &identities[node.id]);
		}
	}
	for (LevelSceneNode& node : state.nodes) {
		node.linkObjects = identities.value(node.id);
	}

	// Members that do not match the rest of their set are out of sync: the
	// largest agreeing part of the set, or the first, is taken as it should be.
	const LevelSceneState previous = document->scene;
	document->scene = state;
	QSet<QString> links;
	for (const LevelSceneNode& node : std::as_const(state.nodes)) {
		if (node.linkId.isEmpty() || links.contains(node.linkId)) {
			continue;
		}
		links.insert(node.linkId);
		QVector<QStringList> agreeing;
		for (const QString& member : levelLinkedGroupNodes(document->scene, node.id)) {
			auto match = std::find_if(agreeing.begin(), agreeing.end(),
				[&](const QStringList& part) { return levelLinkedGroupsAgree(*document, part.first(), member); });
			if (match == agreeing.end()) {
				agreeing.push_back({member});
			} else {
				*match << member;
			}
		}
		const auto largest = std::max_element(agreeing.cbegin(), agreeing.cend(), [](const QStringList& a, const QStringList& b) { return a.size() < b.size(); });
		for (auto part = agreeing.cbegin(); part != agreeing.cend(); ++part) {
			if (part == largest) {
				continue;
			}
			for (const QString& member : *part) {
				for (LevelSceneNode& each : document->scene.nodes) {
					if (each.id == member && !each.linkOutOfSync) {
						each.linkOutOfSync = true;
						warn(QCoreApplication::translate("LevelTrenchBroom",
							"Linked group %1 does not match the groups it is linked with. Edit one of them or use Update Linked Groups.").arg(each.name));
					}
				}
			}
		}
	}
	QString invalid;
	if (!validateLevelScene(*document, document->scene, &invalid)) {
		document->scene = previous;
		return fail(invalid);
	}
	LevelSceneState measured = document->scene;
	refreshLevelLinkBaselines(*document, &measured);
	document->scene = std::move(measured);
	document->sceneStorage = LevelSceneStorage::TrenchBroom;
	return true;
}

LevelTrenchBroomExport writeLevelTrenchBroomScene(const LevelMapDocument& source, const LevelSceneState& scene)
{
	LevelTrenchBroomExport result;
	const QStringList& lines = source.textLines;
	const auto refuse = [](const QString& why) {
		LevelTrenchBroomExport refused;
		refused.errors << QCoreApplication::translate("LevelTrenchBroom", "The map cannot be saved with TrenchBroom groups: %1").arg(why);
		return refused;
	};
	const auto lineAt = [&lines](int line) { return line > 0 && line <= lines.size() ? lines.at(line - 1) : QString(); };
	const LevelMapEntity* world = nullptr;
	for (const LevelMapEntity& entity : source.entities) {
		if (isWorld(entity) && !world) {
			world = &entity;
		} else if (wrapperKind(entity)) {
			return refuse(QCoreApplication::translate("LevelTrenchBroom",
				"a func_group entity has a _tb_type key, which TrenchBroom reads as a layer or group. Remove that key first."));
		}
	}
	if (!world || !blockOwnsLines(lines, world->startLine, world->endLine) || code(lineAt(world->endLine)).trimmed() != QLatin1String("}")) {
		return refuse(QCoreApplication::translate("LevelTrenchBroom", "the map has no worldspawn entity whose closing brace stands on its own line."));
	}

	// TrenchBroom numbers: those it gave, while free, then new ones.
	QHash<QString, QString> numbers;
	QSet<qint64> taken;
	for (const LevelSceneNode& node : scene.nodes) {
		bool valid = false;
		const qint64 value = kept(node.trenchBroomProperties, kId).trimmed().toLongLong(&valid);
		if (valid && value > 0 && !taken.contains(value)) {
			taken.insert(value);
			numbers.insert(node.id, QString::number(value));
		}
	}
	qint64 next = taken.isEmpty() ? 1 : *std::max_element(taken.cbegin(), taken.cend()) + 1;
	for (const LevelSceneNode& node : scene.nodes) {
		if (!numbers.contains(node.id)) {
			numbers.insert(node.id, QString::number(next++));
		}
	}
	// Layers in scene order, then groups, each after the group holding it.
	QStringList order;
	for (const LevelSceneNode& node : scene.nodes) {
		if (node.kind == LevelSceneNodeKind::Layer) {
			order << node.id;
		}
	}
	QSet<QString> written(order.cbegin(), order.cend());
	for (bool progress = true; progress;) {
		progress = false;
		for (const LevelSceneNode& node : scene.nodes) {
			if (node.kind == LevelSceneNodeKind::Group && !written.contains(node.id)
				&& (node.parentId.isEmpty() || written.contains(node.parentId) || !levelSceneNode(scene, node.parentId))) {
				order << node.id;
				written.insert(node.id);
				progress = true;
			}
		}
	}

	// What holds what. The brushes and patches of worldspawn in a layer or
	// group move into its entity; those of brush entities follow their entity.
	QHash<QString, QString> owner;
	for (const LevelSceneNode& node : scene.nodes) {
		for (const QString& object : node.objects) {
			owner.insert(object, node.id);
		}
	}
	QHash<QString, QVector<std::pair<int, int>>> moved;
	QSet<int> movedLines;
	bool entityPrimitive = false;
	const auto gather = [&](const auto& primitives, const char* kind) {
		for (const auto& primitive : primitives) {
			const QString object = selector(kind, primitive.id);
			if (!owner.contains(object)) {
				continue;
			}
			if (primitive.entityId != world->id) {
				entityPrimitive = true;
				continue;
			}
			moved[owner.value(object)].push_back({primitive.startLine, primitive.endLine});
		}
	};
	gather(source.brushes, "brush");
	gather(source.patches, "patch");
	if (entityPrimitive) {
		result.warnings << QCoreApplication::translate("LevelTrenchBroom",
			"Brushes and patches of brush entities follow their entity in TrenchBroom's groups; the layers or groups they have of their own are not written for TrenchBroom.");
	}
	for (auto it = moved.begin(); it != moved.end(); ++it) {
		std::sort(it->begin(), it->end());
		for (const auto& [start, end] : std::as_const(*it)) {
			if (!blockOwnsLines(lines, start, end) || start <= world->startLine || end >= world->endLine) {
				return refuse(QCoreApplication::translate("LevelTrenchBroom", "a brush or patch shares its lines with other text."));
			}
			const int first = start > 1 && markerComment(lineAt(start - 1)) ? start - 1 : start;
			for (int line = first; line <= end; ++line) {
				movedLines.insert(line);
			}
		}
	}

	// Each entity names its layer or group, and lists its protected keys,
	// after its last key; any TrenchBroom keys it had come off first.
	QHash<int, QString> edited;
	QHash<int, QStringList> after;
	for (const LevelMapEntity& entity : source.entities) {
		if (isWorld(entity)) {
			continue;
		}
		int anchor = entity.startLine;
		for (const LevelMapProperty& property : entity.properties) {
			anchor = std::max(anchor, property.line);
			if (!memberKey(property.key)) {
				continue;
			}
			QString text = edited.value(property.line, lineAt(property.line));
			if (property.line <= 0 || !dropPairs(&text, memberKey)) {
				return refuse(QCoreApplication::translate("LevelTrenchBroom", "an entity's TrenchBroom key is split across lines."));
			}
			edited.insert(property.line, text);
		}
		const QString object = selector("entity", entity.id);
		QStringList add;
		if (const LevelSceneNode* node = levelSceneNode(scene, owner.value(object))) {
			add << pairLine(node->kind == LevelSceneNodeKind::Layer ? kLayer : kGroup, numbers.value(node->id));
		}
		if (scene.protectedProperties.contains(object)) {
			QStringList keys;
			for (const QString& key : scene.protectedProperties.value(object)) {
				QString spelled = key;
				for (const LevelMapProperty& property : entity.properties) {
					if (property.key.compare(key, Qt::CaseInsensitive) == 0) {
						spelled = property.key;
						break;
					}
				}
				keys << spelled;
			}
			add << pairLine(kProtected, joinKeys(keys));
		}
		if (add.isEmpty()) {
			continue;
		}
		if (entity.startLine <= 0 || anchor >= entity.endLine) {
			// One line holds the whole entity: the keys go on it.
			QString text = edited.value(anchor, lineAt(anchor));
			const auto spans = quotedSpans(text);
			const qsizetype at = spans.isEmpty() ? text.indexOf(QLatin1Char('{')) + 1 : spans.constLast().second + 1;
			if (at <= 0) {
				return refuse(QCoreApplication::translate("LevelTrenchBroom", "an entity's keys could not be placed."));
			}
			text.insert(at, QLatin1Char(' ') + add.join(QLatin1Char(' ')));
			edited.insert(anchor, text);
			continue;
		}
		const QString text = edited.value(anchor, lineAt(anchor));
		const auto spans = quotedSpans(text);
		const QString rest = code(text).mid(spans.isEmpty() ? 0 : spans.constLast().second + 1).trimmed();
		if (rest.isEmpty() || (anchor == entity.startLine && rest == QLatin1String("{"))) {
			QString indent;
			const QString& original = lineAt(anchor);
			for (const QChar ch : original) {
				if (!ch.isSpace()) {
					break;
				}
				indent += ch;
			}
			for (const QString& line : std::as_const(add)) {
				after[anchor] << indent + line;
			}
		} else {
			QString inline_ = text;
			const qsizetype at = spans.isEmpty() ? inline_.indexOf(QLatin1Char('{')) + 1 : spans.constLast().second + 1;
			inline_.insert(at, QLatin1Char(' ') + add.join(QLatin1Char(' ')));
			edited.insert(anchor, inline_);
		}
	}

	// The layer and group entities, after worldspawn.
	QStringList wrappers;
	int sortIndex = 0;
	for (const QString& id : std::as_const(order)) {
		const LevelSceneNode* node = levelSceneNode(scene, id);
		const bool layer = node->kind == LevelSceneNodeKind::Layer;
		wrappers << QStringLiteral("{") << pairLine(QStringLiteral("classname"), QStringLiteral("func_group"))
				 << pairLine(kType, layer ? kLayerType : kGroupType) << pairLine(kName, writtenName(*node)) << pairLine(kId, numbers.value(id));
		if (layer) {
			wrappers << pairLine(kSortIndex, QString::number(sortIndex++));
			if (!node->visible) {
				wrappers << pairLine(kHidden, QStringLiteral("1"));
			}
			if (node->locked) {
				wrappers << pairLine(kLocked, QStringLiteral("1"));
			}
		} else {
			if (const LevelSceneNode* parent = levelSceneNode(scene, node->parentId)) {
				wrappers << pairLine(parent->kind == LevelSceneNodeKind::Layer ? kLayer : kGroup, numbers.value(parent->id));
			}
			if (!node->linkId.isEmpty()) {
				wrappers << pairLine(kLink, QLatin1Char('{') + node->linkId + QLatin1Char('}')) << pairLine(kTransformation, frameText(node->linkFrame));
			}
		}
		// The keys VibeStudio does not edit, such as a layer's colour.
		static const QSet<QString> written {QStringLiteral("classname"), kType, kId, kName, kSortIndex, kHidden, kLocked, kGroup, kLayer, kLink,
			kTransformation};
		for (auto it = node->trenchBroomProperties.cbegin(); it != node->trenchBroomProperties.cend(); ++it) {
			if (!written.contains(it.key().toCaseFolded())) {
				wrappers << pairLine(it.key(), QString(it.value()).replace(QLatin1Char('"'), QLatin1Char('\'')));
			}
		}
		for (const auto& [start, end] : moved.value(id)) {
			for (int line = start; line <= end; ++line) {
				wrappers << lineAt(line);
			}
		}
		wrappers << QStringLiteral("}");
	}
	QStringList out;
	out.reserve(lines.size() + wrappers.size());
	for (int line = 1; line <= lines.size(); ++line) {
		if (!movedLines.contains(line)) {
			const QString text = edited.value(line, lineAt(line));
			if (!edited.contains(line) || !code(text).trimmed().isEmpty() || code(lineAt(line)).trimmed().isEmpty()) {
				out << text;
			}
		}
		out << after.value(line);
		if (line == world->endLine) {
			out << wrappers;
		}
	}
	result.text = out.join(source.lineEnding.isEmpty() ? QStringLiteral("\n") : source.lineEnding);

	// Opening the result puts the layer and group brushes back at the end of
	// worldspawn, in the order they were written; everything else keeps its
	// place. That gives the numbers the objects will have.
	for (const LevelMapEntity& entity : source.entities) {
		result.objects.insert(selector("entity", entity.id), selector("entity", entity.id));
	}
	QSet<int> movedStarts;
	for (auto it = moved.cbegin(); it != moved.cend(); ++it) {
		for (const auto& block : it.value()) {
			movedStarts.insert(block.first);
		}
	}
	const auto renumber = [&](const auto& primitives, const char* kind) {
		int next = 0;
		QHash<int, int> startingAt;
		for (const auto& primitive : primitives) {
			if (!movedStarts.contains(primitive.startLine) && primitive.startLine < world->endLine) {
				result.objects.insert(selector(kind, primitive.id), selector(kind, next++));
			}
			startingAt.insert(primitive.startLine, primitive.id);
		}
		for (const QString& id : std::as_const(order)) {
			for (const auto& block : moved.value(id)) {
				if (startingAt.contains(block.first)) {
					result.objects.insert(selector(kind, startingAt.value(block.first)), selector(kind, next++));
				}
			}
		}
		for (const auto& primitive : primitives) {
			if (!movedStarts.contains(primitive.startLine) && primitive.startLine > world->endLine) {
				result.objects.insert(selector(kind, primitive.id), selector(kind, next++));
			}
		}
	};
	renumber(source.brushes, "brush");
	renumber(source.patches, "patch");
	return result;
}

} // namespace vibestudio
