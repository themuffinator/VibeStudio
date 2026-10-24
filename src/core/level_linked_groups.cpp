#include "core/level_linked_groups.h"
#include "core/level_linked_objects.h"
#include "core/level_history_p.h"
#include "core/level_linked_groups_p.h"
#include "core/level_protected_properties.h"

#include "core/level_scene.h"
#include "core/level_scene_locks.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

// Linked groups follow TrenchBroom's (https://github.com/TrenchBroom/TrenchBroom,
// GPL-3.0-or-later, linked groups since v2023.1, nesting since v2024.1, as of
// 4c756be1 for Map_Groups.cpp, LinkedGroupUtils.cpp and
// UpdateLinkedGroupsHelper.cpp): groups that take on each other's edits while
// keeping their own places and frames, nested sets updated in order, and two
// changed members of one set refused. The design here is VibeStudio's own, on
// its scene layers and groups; no TrenchBroom code or file format is used.

namespace vibestudio {

namespace {

// The sync runs once for each edit. Its own steps, and the operations below,
// must not start it again.
thread_local int quietDepth = 0;
thread_local const LevelMapDocument* propagatingDocument = nullptr;

using Quiet = detail::LevelLinkedUpdateScope;

// Edits made for a link set may cross the open group's editing boundary.
struct Propagating {
	const LevelMapDocument* previous = propagatingDocument;
	explicit Propagating(const LevelMapDocument* current) { propagatingDocument = current; }
	~Propagating() { propagatingDocument = previous; }
	Propagating(const Propagating&) = delete;
	Propagating& operator=(const Propagating&) = delete;
};

QString message(const char* text)
{
	return QCoreApplication::translate("LevelLinkedGroups", text);
}

bool fail(QString* error, const char* text)
{
	if (error) {
		*error = message(text);
	}
	return false;
}

bool quakeFamily(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}

bool sceneEditable(const LevelMapDocument& document)
{
	return document.scene.opaqueMetadata.isEmpty() && document.scene.problem.isEmpty();
}

// A frame is an affine map, row by row: 3x4, translation last.
using Frame = std::array<double, 12>;
constexpr Frame kIdentity {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0};

// A value a hair from a whole number is that number, so quarter turns composed
// over and over stay exact and save cleanly.
double tidy(double value)
{
	const double whole = std::round(value);
	return std::abs(value - whole) < 1e-12 ? whole + 0.0 : value;
}

// `a` after `b`.
Frame multiply(const Frame& a, const Frame& b)
{
	Frame result {};
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 4; ++column) {
			double value = a[4 * row] * b[column] + a[4 * row + 1] * b[4 + column] + a[4 * row + 2] * b[8 + column];
			if (column == 3) {
				value += a[4 * row + 3];
			}
			result[4 * row + column] = tidy(value);
		}
	}
	return result;
}

double determinant(const Frame& m)
{
	return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
}

std::optional<Frame> invert(const Frame& m)
{
	const double d = determinant(m);
	if (!std::isfinite(d) || std::abs(d) < 1e-12) {
		return std::nullopt;
	}
	const std::array<double, 9> linear {(m[5] * m[10] - m[6] * m[9]) / d, (m[2] * m[9] - m[1] * m[10]) / d, (m[1] * m[6] - m[2] * m[5]) / d,
		(m[6] * m[8] - m[4] * m[10]) / d, (m[0] * m[10] - m[2] * m[8]) / d, (m[2] * m[4] - m[0] * m[6]) / d,
		(m[4] * m[9] - m[5] * m[8]) / d, (m[1] * m[8] - m[0] * m[9]) / d, (m[0] * m[5] - m[1] * m[4]) / d};
	Frame result {};
	for (int row = 0; row < 3; ++row) {
		result[4 * row] = tidy(linear[3 * row]);
		result[4 * row + 1] = tidy(linear[3 * row + 1]);
		result[4 * row + 2] = tidy(linear[3 * row + 2]);
		result[4 * row + 3] = tidy(-(linear[3 * row] * m[3] + linear[3 * row + 1] * m[7] + linear[3 * row + 2] * m[11]));
	}
	return result;
}

LevelMapVec3 apply(const Frame& m, const LevelMapVec3& v)
{
	return {m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3], m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7],
		m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11], v.valid};
}

Frame translation(double x, double y, double z)
{
	return {1.0, 0.0, 0.0, x, 0.0, 1.0, 0.0, y, 0.0, 0.0, 1.0, z};
}

// Whether the frame only moves content: its linear part is the identity.
bool movesOnly(const Frame& frame)
{
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			if (std::abs(frame[4 * row + column] - (row == column ? 1.0 : 0.0)) > 1e-9) {
				return false;
			}
		}
	}
	return true;
}

// Thousandths, so groups placed by fractional offsets still compare equal.
QByteArray number(double value)
{
	const double scaled = std::round(value * 1000.0);
	return QByteArray::number(static_cast<qint64>(scaled));
}

QByteArray relative(const LevelMapVec3& point, const LevelMapVec3& anchor)
{
	return number(point.x - anchor.x) + ',' + number(point.y - anchor.y) + ',' + number(point.z - anchor.z);
}

QByteArray direction(const LevelMapVec3& value)
{
	return number(value.x) + ',' + number(value.y) + ',' + number(value.z);
}

// One record as compared: where it is and what it is, then texture scale,
// rotation and axes, then texture offsets.
struct Lines {
	QByteArray geometry;
	QByteArray shape;
	QByteArray surface;
};

void sortLines(QVector<Lines>* lines)
{
	std::sort(lines->begin(), lines->end(), [](const Lines& a, const Lines& b) {
		if (a.geometry != b.geometry) {
			return a.geometry < b.geometry;
		}
		return a.shape != b.shape ? a.shape < b.shape : a.surface < b.surface;
	});
}

Lines brushLines(const LevelMapBrush& brush, const LevelMapVec3& anchor)
{
	QVector<Lines> faces;
	for (const LevelMapBrushFace& face : brush.faces) {
		Lines line;
		if (face.explicitPlane) {
			// n.x + d = 0 with x measured from the anchor: d grows by n.anchor.
			const double distance = face.planeDistance + face.planeNormal.x * anchor.x + face.planeNormal.y * anchor.y
				+ face.planeNormal.z * anchor.z;
			line.geometry = "plane " + direction(face.planeNormal) + ' ' + number(distance);
		} else {
			line.geometry = "points " + relative(face.p0, anchor) + ' ' + relative(face.p1, anchor) + ' ' + relative(face.p2, anchor);
		}
		line.geometry += ' ' + face.textureName.toUtf8() + " flags " + QByteArray::number(face.contentFlags) + ','
			+ QByteArray::number(face.surfaceFlags) + ',' + QByteArray::number(face.surfaceValue);
		line.shape = line.geometry + ' ' + number(face.rotation) + ' ' + number(face.scaleX) + ' ' + number(face.scaleY);
		if (face.explicitTextureAxes) {
			line.shape += " axes " + direction(face.uAxis) + ' ' + direction(face.vAxis);
		}
		if (face.explicitTextureMatrix) {
			line.shape += " matrix " + number(face.textureMatrix[0]) + ',' + number(face.textureMatrix[1]) + ','
				+ number(face.textureMatrix[3]) + ',' + number(face.textureMatrix[4]);
		}
		line.surface = number(face.shiftX) + ',' + number(face.shiftY) + ',' + number(face.uOffset) + ',' + number(face.vOffset);
		if (face.explicitTextureMatrix) {
			line.surface += ',' + number(face.textureMatrix[2]) + ',' + number(face.textureMatrix[5]);
		}
		faces << line;
	}
	sortLines(&faces);
	Lines result {"brush " + brush.primitiveKind.toUtf8() + ':', "brush:", "brush:"};
	for (const Lines& face : std::as_const(faces)) {
		result.geometry += face.geometry + ';';
		result.shape += face.shape + ';';
		result.surface += face.surface + ';';
	}
	return result;
}

Lines patchLines(const LevelMapPatch& patch, const LevelMapVec3& anchor)
{
	Lines line {"patch " + patch.textureName.toUtf8() + ' ' + QByteArray::number(patch.width) + 'x' + QByteArray::number(patch.height), {},
		"patch:"};
	if (patch.fixedSubdivisions) {
		line.geometry += " subdivisions " + QByteArray::number(patch.subdivisionsX) + 'x' + QByteArray::number(patch.subdivisionsY);
	}
	for (const LevelMapVec3& point : patch.controlPoints) {
		line.geometry += ' ' + relative(point, anchor);
	}
	line.shape = line.geometry;
	for (double u : patch.controlU) {
		line.surface += number(u) + ',';
	}
	for (double v : patch.controlV) {
		line.surface += number(v) + ',';
	}
	return line;
}

Lines entityLines(const LevelMapEntity& entity, QVector<Lines> owned, const LevelMapVec3& anchor)
{
	QStringList keys;
	for (const LevelMapProperty& property : entity.properties) {
		if (property.key.compare(QStringLiteral("origin"), Qt::CaseInsensitive) != 0) {
			keys << property.key + QLatin1Char('=') + property.value;
		}
	}
	keys.sort();
	Lines line {"entity " + entity.className.toUtf8() + ' ' + keys.join(QChar(0x1f)).toUtf8(), {}, "entity:"};
	if (entity.origin.valid) {
		line.geometry += " origin " + relative(entity.origin, anchor);
	}
	line.shape = line.geometry;
	sortLines(&owned);
	for (const Lines& part : std::as_const(owned)) {
		line.geometry += '{' + part.geometry + '}';
		line.shape += '{' + part.shape + '}';
		line.surface += part.surface + ';';
	}
	return line;
}

QByteArray digest(const QVector<Lines>& lines, const QVector<QByteArray>& structure, int which)
{
	QCryptographicHash hash(QCryptographicHash::Sha1);
	for (const Lines& line : lines) {
		hash.addData(which == 0 ? line.geometry : (which == 1 ? line.shape : line.surface));
		hash.addData(QByteArrayView("\n"));
	}
	if (which != 2) {
		for (const QByteArray& line : structure) {
			hash.addData(line);
			hash.addData(QByteArrayView("\n"));
		}
	}
	return hash.result();
}

QSet<int> idsOfKind(const QStringList& members, QStringView kind)
{
	QSet<int> ids;
	for (const QString& member : members) {
		const qsizetype colon = member.indexOf(QLatin1Char(':'));
		bool valid = false;
		const int id = member.mid(colon + 1).toInt(&valid);
		if (colon > 0 && valid && QStringView(member).left(colon) == kind) {
			ids.insert(id);
		}
	}
	return ids;
}

// A group nested in the content: what it is, and where its frame puts the
// shared content's origin.
struct Nested {
	QByteArray line;
	LevelMapVec3 origin;
};

// The content of `members`. A nested group's objects carry its label in
// `containers`, and `nested` describes the nested groups, so moving an
// object between them, adding a group or turning one is a change to the
// content.
LevelLinkedContent collect(const LevelMapDocument& document, const QStringList& members, const QHash<QString, QByteArray>& containers = {},
	const QVector<Nested>& nested = {})
{
	LevelLinkedContent content;
	const QSet<int> entityIds = idsOfKind(members, u"entity");
	const QSet<int> brushIds = idsOfKind(members, u"brush");
	const QSet<int> patchIds = idsOfKind(members, u"patch");
	QSet<int> memberEntities;
	for (const LevelMapEntity& entity : document.entities) {
		if (entityIds.contains(entity.id) && entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) != 0) {
			memberEntities.insert(entity.id);
		}
	}
	// The bounds first: every line is written relative to their centre, which
	// moves with the content.
	const auto include = [&content](const LevelMapVec3& point) {
		if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
			return;
		}
		if (!content.hasPoints) {
			content.mins = {point.x, point.y, point.z, true};
			content.maxs = content.mins;
			content.hasPoints = true;
			return;
		}
		content.mins = {std::min(content.mins.x, point.x), std::min(content.mins.y, point.y), std::min(content.mins.z, point.z), true};
		content.maxs = {std::max(content.maxs.x, point.x), std::max(content.maxs.y, point.y), std::max(content.maxs.z, point.z), true};
	};
	QVector<const LevelMapBrush*> brushes;
	QVector<const LevelMapPatch*> patches;
	for (const LevelMapBrush& brush : document.brushes) {
		if (!memberEntities.contains(brush.entityId) && !brushIds.contains(brush.id)) {
			continue;
		}
		brushes << &brush;
		for (const LevelMapBrushFace& face : brush.faces) {
			if (!face.explicitPlane) {
				include(face.p0);
				include(face.p1);
				include(face.p2);
			} else if (brush.mins.valid) {
				include(brush.mins);
				include(brush.maxs);
			}
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (memberEntities.contains(patch.entityId) || patchIds.contains(patch.id)) {
			patches << &patch;
			for (const LevelMapVec3& point : patch.controlPoints) {
				include(point);
			}
		}
	}
	for (const LevelMapEntity& entity : document.entities) {
		if (memberEntities.contains(entity.id) && entity.origin.valid) {
			include(entity.origin);
		}
	}
	if (content.hasPoints) {
		content.anchor = {(content.mins.x + content.maxs.x) / 2.0, (content.mins.y + content.maxs.y) / 2.0,
			(content.mins.z + content.maxs.z) / 2.0, true};
	}
	const auto labelled = [&containers](Lines line, const QString& object) {
		const QByteArray label = containers.value(object);
		if (!label.isEmpty()) {
			line.geometry = label + '|' + line.geometry;
			line.shape = label + '|' + line.shape;
		}
		return line;
	};
	QHash<int, QVector<Lines>> owned;
	QVector<Lines> lines;
	for (const LevelMapBrush* brush : std::as_const(brushes)) {
		if (memberEntities.contains(brush->entityId)) {
			owned[brush->entityId] << brushLines(*brush, content.anchor);
		} else {
			lines << labelled(brushLines(*brush, content.anchor), levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, brush->id}));
			content.roots.push_back({LevelMapSelectionKind::QuakeBrush, brush->id});
		}
	}
	for (const LevelMapPatch* patch : std::as_const(patches)) {
		if (memberEntities.contains(patch->entityId)) {
			owned[patch->entityId] << patchLines(*patch, content.anchor);
		} else {
			lines << labelled(patchLines(*patch, content.anchor), levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch, patch->id}));
			content.roots.push_back({LevelMapSelectionKind::QuakePatch, patch->id});
		}
	}
	for (const LevelMapEntity& entity : document.entities) {
		if (memberEntities.contains(entity.id)) {
			lines << labelled(entityLines(entity, owned.value(entity.id), content.anchor),
				levelMapSelectionRefId({LevelMapSelectionKind::Entity, entity.id}));
			content.roots.push_back({LevelMapSelectionKind::Entity, entity.id});
		}
	}
	sortLines(&lines);
	// Nested frames are measured from the anchor like everything else.
	QVector<QByteArray> structure;
	for (const Nested& group : nested) {
		structure << group.line + " at " + relative(group.origin, content.anchor);
	}
	std::sort(structure.begin(), structure.end());
	content.geometry = digest(lines, structure, 0);
	content.shape = digest(lines, structure, 1);
	content.surface = digest(lines, structure, 2);
	return content;
}

// How deep a node sits: the number of groups and layers above it.
int depthOf(const LevelSceneState& state, const QString& id)
{
	int depth = 0;
	const LevelSceneNode* node = levelSceneNode(state, id);
	while (node && !node->parentId.isEmpty() && depth < kLevelSceneMaxDepth) {
		node = levelSceneNode(state, node->parentId);
		++depth;
	}
	return depth;
}

// A group's content with the groups nested in it, against `state`.
LevelLinkedContent groupContent(const LevelMapDocument& document, const LevelSceneState& state, const QString& nodeId)
{
	const LevelSceneNode* node = levelSceneNode(state, nodeId);
	if (!node) {
		return {};
	}
	const QStringList descendants = levelLinkedGroupDescendants(state, nodeId);
	QHash<QString, QByteArray> labels {{nodeId, QByteArrayLiteral(".")}};
	for (const QString& id : descendants) {
		const LevelSceneNode* child = levelSceneNode(state, id);
		// A nested group is known by its link id, which its counterparts share.
		labels.insert(id, child->linkId.isEmpty() ? QByteArrayLiteral("new") : child->linkId.toUtf8());
	}
	QStringList members = node->objects;
	QHash<QString, QByteArray> containers;
	QVector<Nested> nested;
	for (const QString& id : descendants) {
		const LevelSceneNode* child = levelSceneNode(state, id);
		QByteArray line = "group " + labels.value(id) + " in " + labels.value(child->parentId) + " frame";
		for (int row = 0; row < 3; ++row) {
			line += ' ' + number(child->linkFrame[4 * row]) + ' ' + number(child->linkFrame[4 * row + 1]) + ' '
				+ number(child->linkFrame[4 * row + 2]);
		}
		nested.push_back({line, apply(child->linkFrame, {0.0, 0.0, 0.0, true})});
		for (const QString& object : child->objects) {
			containers.insert(object, labels.value(id));
			members << object;
		}
	}
	return collect(document, members, containers, nested);
}

void setBaseline(LevelSceneNode* node, const LevelLinkedContent& content)
{
	node->linkAnchor = {content.anchor.x, content.anchor.y, content.anchor.z};
	node->linkGeometry = content.geometry;
	node->linkShape = content.shape;
	node->linkSurface = content.surface;
}

void clearBaseline(LevelSceneNode* node)
{
	node->linkAnchor = {0.0, 0.0, 0.0};
	node->linkGeometry.clear();
	node->linkShape.clear();
	node->linkSurface.clear();
}

void clearLink(LevelSceneNode* node)
{
	node->linkId.clear();
	node->linkObjects.clear();
	node->linkFrame = kIdentity;
	node->linkOutOfSync = false;
	clearBaseline(node);
}

bool sameAnchor(const LevelMapVec3& now, const std::array<double, 3>& before)
{
	return number(now.x) == number(before[0]) && number(now.y) == number(before[1]) && number(now.z) == number(before[2]);
}

// Every linked node's baseline from what it holds now, or only those of
// `only`; a link left with one group is no link.
void rebaseline(const LevelMapDocument& document, LevelSceneState* state, const QSet<QString>* only = nullptr)
{
	QHash<QString, int> members;
	for (const LevelSceneNode& node : std::as_const(state->nodes)) {
		if (!node.linkId.isEmpty()) {
			++members[node.linkId];
		}
	}
	for (LevelSceneNode& node : state->nodes) {
		if (node.linkId.isEmpty()) {
			continue;
		}
		if (members.value(node.linkId) < 2) {
			clearLink(&node);
			continue;
		}
		if (!only || only->contains(node.id)) {
			setBaseline(&node, groupContent(document, *state, node.id));
		}
	}
}

// Undoes the steps a failed operation pushed, leaving no redo behind.
void rollBack(LevelMapDocument* document, int steps, const QVector<LevelMapSelectionRef>& selection)
{
	for (int step = 0; step < steps; ++step) {
		if (!undoLevelMapEdit(document)) {
			break;
		}
	}
	document->redoStack.clear();
	setLevelMapSelection(document, selection);
}

// "Pillar" and "Pillar 2" both continue as "Pillar 3" and on, among siblings.
QString copyName(const LevelSceneState& state, const QString& parentId, const QString& name)
{
	static const QRegularExpression numbered(QStringLiteral("^(.*\\S)\\s+(\\d+)$"));
	QString base = name;
	if (const auto match = numbered.match(name); match.hasMatch()) {
		base = match.captured(1);
	}
	QSet<QString> taken;
	for (const LevelSceneNode& node : state.nodes) {
		if (node.parentId == parentId) {
			taken.insert(node.name.toCaseFolded());
		}
	}
	for (int number = 2; number < 100000; ++number) {
		const QString suffix = QLatin1Char(' ') + QString::number(number);
		const QString candidate = base.left(128 - suffix.size()).trimmed() + suffix;
		if (!taken.contains(candidate.toCaseFolded())) {
			return candidate;
		}
	}
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// `name`, or a numbered version of it, unused among `parentId`'s children.
QString freeName(const LevelSceneState& state, const QString& parentId, const QString& name)
{
	const bool used = std::any_of(state.nodes.cbegin(), state.nodes.cend(), [&](const LevelSceneNode& node) {
		return node.parentId == parentId && node.name.compare(name, Qt::CaseInsensitive) == 0;
	});
	return used ? copyName(state, parentId, name) : name;
}

QString freshId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// Whether a node or anything nested in it is locked: an update would replace
// what the lock protects.
bool lockedSubtree(const LevelSceneState& state, const QSet<QString>& locked, const QString& id)
{
	if (locked.contains(id)) {
		return true;
	}
	const QStringList descendants = levelLinkedGroupDescendants(state, id);
	return std::any_of(descendants.cbegin(), descendants.cend(), [&locked](const QString& descendant) { return locked.contains(descendant); });
}

// What an update did to the scene beyond its targets.
struct Update {
	QStringList updated;
	QSet<QString> created;
	QSet<QString> removed;
	// Each nested group made, and the group it copies.
	QHash<QString, QString> copiedFrom;
};

bool anyLocked(const LevelMapDocument& document)
{
	return std::any_of(document.scene.nodes.cbegin(), document.scene.nodes.cend(), [](const LevelSceneNode& node) { return node.locked; });
}

// Copies of what a locked group holds are made in it before they move to
// their own groups. Where anything is locked, `operation` runs as one guarded
// edit, so the locks are checked against the map as it was before it, which
// that move leaves as it was. False leaves the map as it was.
std::optional<bool> guardLockedCopies(LevelMapDocument* document, QString* error, const std::function<bool(LevelMapDocument*)>& operation)
{
	return anyLocked(*document) ? guardLevelSceneEdit(document, error, operation) : std::nullopt;
}

// Makes each target hold the source's content and nested groups, taken out of
// the source's frame and into the target's. Counts the undo steps pushed.
bool propagateSteps(LevelMapDocument* document, const QString& sourceId, const QStringList& targets, Update* update, int* steps, QString* error)
{
	// Groups nested in the source are linked with their copies: those not
	// linked yet get a link of their own first.
	{
		LevelSceneState state = document->scene;
		bool changed = false;
		for (const QString& id : levelLinkedGroupDescendants(state, sourceId)) {
			for (LevelSceneNode& node : state.nodes) {
				if (node.id == id && node.linkId.isEmpty()) {
					node.linkId = freshId();
					node.linkObjects = levelLinkObjectIdentities(*document, state, node);
					changed = true;
				}
			}
		}
		if (changed) {
			const auto before = document->undoStack.size();
			if (!commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelLinkedGroups", "Link nested groups")), error)) {
				return false;
			}
			*steps += static_cast<int>(document->undoStack.size() - before);
		}
	}
	const LevelSceneNode* sourceNode = levelSceneNode(document->scene, sourceId);
	if (!sourceNode) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "The linked group no longer exists."));
	}
	const LevelSceneNode source = *sourceNode;
	const std::optional<Frame> sourceInverse = invert(source.linkFrame);
	if (!sourceInverse) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "The linked group's frame cannot be inverted."));
	}
	QVector<LevelSceneNode> nested;
	for (const QString& id : levelLinkedGroupDescendants(document->scene, sourceId)) {
		nested << *levelSceneNode(document->scene, id);
	}
	const QSet<QString> direct(source.objects.cbegin(), source.objects.cend());
	const auto correspondence = levelLinkObjectIdentities(*document, document->scene, source);
	QHash<QString, QMap<QString, QString>> nestedCorrespondence;
	for (const LevelSceneNode& node : std::as_const(nested)) {
		nestedCorrespondence.insert(node.id, levelLinkObjectIdentities(*document, document->scene, node));
	}
	const auto sourceProtection = document->scene.protectedProperties;
	LevelMapTextureLockOptions locked;
	locked.enabled = true;
	LevelMapTextureLockOptions unlocked;
	unlocked.enabled = false;
	for (const QString& targetId : targets) {
		const LevelSceneNode* targetNode = levelSceneNode(document->scene, targetId);
		if (!targetNode) {
			continue;
		}
		const LevelSceneNode target = *targetNode;
		const LevelLinkedContent old = groupContent(*document, document->scene, targetId);
		// Content leaves the source's frame for the target's.
		const Frame matrix = multiply(target.linkFrame, *sourceInverse);
		// The target's nested groups, by link, keep their names, visibility and
		// locks when their counterparts replace them.
		QHash<QString, QVector<LevelSceneNode>> previous;
		for (const QString& id : levelLinkedGroupDescendants(document->scene, targetId)) {
			const LevelSceneNode* node = levelSceneNode(document->scene, id);
			previous[node->linkId] << *node;
			update->removed.insert(id);
		}
		// Match existing entity properties before deleting the target. Keys
		// protected on either side preserve the target's value or absence, and
		// the target keeps its own protection flags (TB v2026.2 semantics).
		struct EntityProperties {
			QVector<LevelMapProperty> values;
			QStringList protection;
		};
		QHash<QString, EntityProperties> retained;
		for (const auto& entity : document->entities) {
			const auto object = levelMapSelectionRefId({LevelMapSelectionKind::Entity, entity.id});
			if (target.linkObjects.contains(object)) {
				retained.insert(target.linkObjects.value(object), {entity.properties, document->scene.protectedProperties.value(object)});
			}
		}
		const auto before = document->undoStack.size();
		if (!old.roots.isEmpty() && !deleteLevelMapObjects(document, old.roots, error)) {
			return false;
		}
		*steps += static_cast<int>(document->undoStack.size() - before);
		const LevelLinkedContent content = groupContent(*document, document->scene, sourceId);
		QHash<QString, QString> origins;
		if (!content.roots.isEmpty()) {
			// TB LinkedGroupUtils::cloneAndTransformRecursive constructs fresh
			// inherited children; it does not clone per-node visibility attributes.
			const auto copied = document->undoStack.size();
			if (!duplicateLevelMapObjects(document, content.roots, 0.0, 0.0, 0.0, locked, error, false, true)) {
				return false;
			}
			*steps += static_cast<int>(document->undoStack.size() - copied);
			origins = document->undoStack.last().sceneObjectOrigins;
			// Duplication left the copies selected; texture lock keeps their
			// textures in place unless the frame is one it cannot express.
			const auto placed = document->undoStack.size();
			QString problem;
			if (!transformLevelMapSelectionAffine(document, matrix, locked, &problem)
				&& !transformLevelMapSelectionAffine(document, matrix, unlocked, error)) {
				return false;
			}
			*steps += static_cast<int>(document->undoStack.size() - placed);
		}
		QHash<QString, QStringList> protections;
		struct PropertyRestore {
			int entity;
			QString key;
			std::optional<QString> value;
		};
		QVector<PropertyRestore> restorations;
		QMap<QString, QString> targetIdentities;
		for (auto it = origins.cbegin(); it != origins.cend(); ++it) {
			if (!correspondence.contains(it.value())) {
				continue;
			}
			const auto identity = correspondence.value(it.value());
			targetIdentities.insert(it.key(), identity);
			if (it.key().startsWith(QLatin1String("entity:")) && retained.contains(identity)) {
				const auto kept = retained.value(identity);
				protections.insert(it.key(), kept.protection);
				auto keys = sourceProtection.value(it.value()) + kept.protection;
				keys.removeDuplicates();
				for (const auto& key : keys) {
					std::optional<QString> value;
					for (const auto& property : kept.values) {
						if (property.key.compare(key, Qt::CaseInsensitive) == 0) {
							value = property.value;
							break;
						}
					}
					restorations << PropertyRestore {it.key().section(QLatin1Char(':'), 1).toInt(), key, value};
				}
			}
		}
		for (const auto& restore : restorations) {
			std::optional<QString> current;
			for (const auto& entity : document->entities) {
				if (entity.id != restore.entity) {
					continue;
				}
				for (const auto& property : entity.properties) {
					if (property.key.compare(restore.key, Qt::CaseInsensitive) == 0) {
						current = property.value;
						break;
					}
				}
				break;
			}
			if (current == restore.value) {
				continue;
			}
			const auto restored = document->undoStack.size();
			const bool applied = restore.value ? setLevelMapEntityProperty(document, restore.entity, restore.key, *restore.value, error)
											   : removeLevelMapEntityProperty(document, restore.entity, restore.key, error);
			if (!applied) {
				return false;
			}
			*steps += static_cast<int>(document->undoStack.size() - restored);
		}
		// The copies leave the source's groups they were made in for the target
		// and the copies of the source's nested groups.
		LevelSceneState state = document->scene;
		QHash<QString, QString> placedIn;
		for (auto it = origins.cbegin(); it != origins.cend(); ++it) {
			if (direct.contains(it.value())) {
				placedIn.insert(it.key(), targetId);
			}
		}
		QHash<QString, QString> copies {{sourceId, targetId}};
		QVector<LevelSceneNode> made;
		QHash<QString, QString> visibility;
		for (const LevelSceneNode& node : std::as_const(nested)) {
			LevelSceneNode copy;
			copy.id = freshId();
			copy.kind = LevelSceneNodeKind::Group;
			copy.parentId = copies.value(node.parentId, targetId);
			copy.linkId = node.linkId;
			copy.linkFrame = multiply(matrix, node.linkFrame);
			copy.visible = node.visible;
			copy.name = node.name;
			auto& counterparts = previous[node.linkId];
			if (!counterparts.isEmpty()) {
				const LevelSceneNode kept = counterparts.takeFirst();
				copy.name = kept.name;
				copy.visible = kept.visible;
				copy.locked = kept.locked;
				visibility.insert(QStringLiteral("group:") + copy.id, QStringLiteral("group:") + kept.id);
			} else {
				visibility.insert(QStringLiteral("group:") + copy.id, QStringLiteral("group:") + node.id);
			}
			const QSet<QString> held(node.objects.cbegin(), node.objects.cend());
			const auto& identities = nestedCorrespondence.value(node.id);
			for (auto it = origins.cbegin(); it != origins.cend(); ++it) {
				if (held.contains(it.value())) {
					placedIn.insert(it.key(), copy.id);
				}
				if (identities.contains(it.value())) {
					copy.linkObjects.insert(it.key(), identities.value(it.value()));
				}
			}
			copies.insert(node.id, copy.id);
			update->copiedFrom.insert(copy.id, node.id);
			made << copy;
		}
		QHash<QString, QStringList> joining;
		for (auto it = placedIn.cbegin(); it != placedIn.cend(); ++it) {
			joining[it.value()] << it.key();
		}
		for (LevelSceneNode& node : state.nodes) {
			node.objects.removeIf([&origins](const QString& object) { return origins.contains(object); });
			if (node.id == sourceId) {
				node.linkObjects = correspondence;
			} else if (nestedCorrespondence.contains(node.id)) {
				node.linkObjects = nestedCorrespondence.value(node.id);
			} else if (node.id == targetId) {
				node.linkObjects = targetIdentities;
				node.linkOutOfSync = false;
				QStringList added = joining.value(targetId);
				added.sort();
				node.objects += added;
			}
		}
		state.nodes.removeIf([update](const LevelSceneNode& node) { return update->removed.contains(node.id); });
		int at = -1;
		for (int index = 0; index < state.nodes.size(); ++index) {
			if (state.nodes.at(index).id == targetId) {
				at = index;
			}
		}
		for (LevelSceneNode& copy : made) {
			QStringList added = joining.value(copy.id);
			added.sort();
			copy.objects = added;
			copy.name = freeName(state, copy.parentId, copy.name);
			state.nodes.insert(++at, copy);
			update->created.insert(copy.id);
		}
		for (auto it = protections.cbegin(); it != protections.cend(); ++it) {
			if (it.value().isEmpty()) {
				state.protectedProperties.remove(it.key());
			} else {
				state.protectedProperties.insert(it.key(), it.value());
			}
		}
		const auto committed = document->undoStack.size();
		if (!commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelLinkedGroups", "Update linked group")), error, visibility)) {
			return false;
		}
		*steps += static_cast<int>(document->undoStack.size() - committed);
		update->updated << targetId;
	}
	return true;
}

// propagateSteps, guarded where anything is locked. A guarded failure leaves
// the map as it was and counts no steps; otherwise the caller rolls back the
// steps counted.
bool propagate(LevelMapDocument* document, const QString& sourceId, const QStringList& targets, Update* update, int* steps, QString* error)
{
	Update staged;
	int made = 0;
	const auto run = [&](LevelMapDocument* working) {
		const Propagating propagating(working);
		return propagateSteps(working, sourceId, targets, &staged, &made, error);
	};
	const auto guarded = guardLockedCopies(document, error, run);
	const bool done = guarded.has_value() ? *guarded : run(document);
	if (done || !guarded.has_value()) {
		*steps += made;
	}
	if (done) {
		update->updated += staged.updated;
		update->created.unite(staged.created);
		update->removed.unite(staged.removed);
		update->copiedFrom.insert(staged.copiedFrom);
	}
	return done;
}

bool samePoint(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return std::abs(a.x - b.x) < 0.01 && std::abs(a.y - b.y) < 0.01 && std::abs(a.z - b.z) < 0.01;
}

QStringList comparedKeys(const LevelMapEntity& entity, const QStringList& skipped = {})
{
	QStringList keys;
	for (const LevelMapProperty& property : entity.properties) {
		const QString key = property.key.toCaseFolded();
		// Placement and headings follow each group's frame.
		if (key != QLatin1String("origin") && key != QLatin1String("angle") && key != QLatin1String("angles") && key != QLatin1String("mangle")
			&& !skipped.contains(key)) {
			keys << key + QLatin1Char('=') + property.value;
		}
	}
	keys.sort();
	return keys;
}

// Whether two members of a link set that both changed changed the same way:
// corresponding objects (by identity) agree in the shared content, within a
// hundredth of a unit, and in what they are. Texture alignment is not
// compared: each frame aligns textures its own way. With `local`, keys either
// entity protects are not compared either.
bool sameChange(const LevelMapDocument& document, const LevelSceneNode& a, const LevelSceneNode& b, bool local = false)
{
	const auto inverseA = invert(a.linkFrame);
	const auto inverseB = invert(b.linkFrame);
	if (!inverseA || !inverseB || a.linkObjects.isEmpty() || b.linkObjects.isEmpty()) {
		return false;
	}
	const auto objectsA = levelLinkedObjects(document, levelLinkedGroupMembers(document.scene, a.id));
	const auto objectsB = levelLinkedObjects(document, levelLinkedGroupMembers(document.scene, b.id));
	if (objectsA.size() != objectsB.size() || objectsA.size() != a.linkObjects.size() || objectsB.size() != b.linkObjects.size()) {
		return false;
	}
	QHash<QString, QString> counterpart;
	for (auto it = b.linkObjects.cbegin(); it != b.linkObjects.cend(); ++it) {
		counterpart.insert(it.value(), it.key());
	}
	// Each point as it is in the shared content.
	const auto pointsMatch = [&](const LevelMapVec3& pa, const LevelMapVec3& pb) {
		return samePoint(apply(*inverseA, pa), apply(*inverseB, pb));
	};
	for (auto it = a.linkObjects.cbegin(); it != a.linkObjects.cend(); ++it) {
		const QString other = counterpart.value(it.value());
		if (other.isEmpty() || other.section(QLatin1Char(':'), 0, 0) != it.key().section(QLatin1Char(':'), 0, 0)) {
			return false;
		}
		const int idA = it.key().section(QLatin1Char(':'), 1).toInt();
		const int idB = other.section(QLatin1Char(':'), 1).toInt();
		if (it.key().startsWith(QLatin1String("entity:"))) {
			const auto ea = std::find_if(document.entities.cbegin(), document.entities.cend(), [idA](const auto& e) { return e.id == idA; });
			const auto eb = std::find_if(document.entities.cbegin(), document.entities.cend(), [idB](const auto& e) { return e.id == idB; });
			QStringList skipped;
			if (local) {
				skipped = document.scene.protectedProperties.value(it.key()) + document.scene.protectedProperties.value(other);
			}
			if (ea == document.entities.cend() || eb == document.entities.cend() || ea->className != eb->className
				|| comparedKeys(*ea, skipped) != comparedKeys(*eb, skipped) || ea->origin.valid != eb->origin.valid
				|| (ea->origin.valid && !pointsMatch(ea->origin, eb->origin))) {
				return false;
			}
		} else if (it.key().startsWith(QLatin1String("brush:"))) {
			const auto ba = std::find_if(document.brushes.cbegin(), document.brushes.cend(), [idA](const auto& x) { return x.id == idA; });
			const auto bb = std::find_if(document.brushes.cbegin(), document.brushes.cend(), [idB](const auto& x) { return x.id == idB; });
			if (ba == document.brushes.cend() || bb == document.brushes.cend() || ba->faces.size() != bb->faces.size()) {
				return false;
			}
			for (int face = 0; face < ba->faces.size(); ++face) {
				const auto& fa = ba->faces.at(face);
				const auto& fb = bb->faces.at(face);
				if (fa.textureName != fb.textureName || fa.contentFlags != fb.contentFlags || fa.surfaceFlags != fb.surfaceFlags
					|| fa.surfaceValue != fb.surfaceValue || fa.explicitPlane != fb.explicitPlane) {
					return false;
				}
				// A mirror swaps two of a face's points.
				if (!fa.explicitPlane && (!pointsMatch(fa.p0, fb.p0)
						|| !((pointsMatch(fa.p1, fb.p1) && pointsMatch(fa.p2, fb.p2)) || (pointsMatch(fa.p1, fb.p2) && pointsMatch(fa.p2, fb.p1))))) {
					return false;
				}
			}
		} else if (it.key().startsWith(QLatin1String("patch:"))) {
			const auto pa = std::find_if(document.patches.cbegin(), document.patches.cend(), [idA](const auto& x) { return x.id == idA; });
			const auto pb = std::find_if(document.patches.cbegin(), document.patches.cend(), [idB](const auto& x) { return x.id == idB; });
			if (pa == document.patches.cend() || pb == document.patches.cend() || pa->textureName != pb->textureName || pa->width != pb->width
				|| pa->height != pb->height || pa->controlPoints.size() != pb->controlPoints.size()) {
				return false;
			}
			// A mirror reverses a patch's columns.
			const auto rows = [&](bool reversed) {
				for (int row = 0; row < pa->height; ++row) {
					for (int column = 0; column < pa->width; ++column) {
						const int index = row * pa->width + column;
						const int other = row * pa->width + (reversed ? pa->width - 1 - column : column);
						if (index >= pa->controlPoints.size() || other >= pb->controlPoints.size()
							|| !pointsMatch(pa->controlPoints.at(index), pb->controlPoints.at(other))) {
							return false;
						}
					}
				}
				return true;
			};
			if (!rows(false) && !rows(true)) {
				return false;
			}
		}
	}
	return true;
}

// The linked groups nested in `id`'s link set's members and the members
// themselves, outermost set first.
QStringList outermostFirst(const LevelSceneState& state, const QStringList& links)
{
	QHash<QString, int> depth;
	QHash<QString, int> order;
	for (int index = 0; index < state.nodes.size(); ++index) {
		const LevelSceneNode& node = state.nodes.at(index);
		if (node.linkId.isEmpty()) {
			continue;
		}
		const int at = depthOf(state, node.id);
		if (!depth.contains(node.linkId) || at < depth.value(node.linkId)) {
			depth.insert(node.linkId, at);
		}
		if (!order.contains(node.linkId)) {
			order.insert(node.linkId, index);
		}
	}
	QStringList sorted = links;
	std::stable_sort(sorted.begin(), sorted.end(), [&](const QString& a, const QString& b) {
		return depth.value(a) != depth.value(b) ? depth.value(a) < depth.value(b) : order.value(a) < order.value(b);
	});
	return sorted;
}

} // namespace

QStringList levelLinkedGroupDescendants(const LevelSceneState& state, const QString& nodeId)
{
	QStringList descendants;
	if (nodeId.isEmpty()) {
		return descendants;
	}
	QHash<QString, QString> parents;
	for (const LevelSceneNode& node : state.nodes) {
		parents.insert(node.id, node.parentId);
	}
	// Parents come before their children, so copies can be made in order;
	// scene order is kept otherwise.
	QHash<QString, int> depths;
	for (const LevelSceneNode& node : state.nodes) {
		if (node.kind != LevelSceneNodeKind::Group || node.id == nodeId) {
			continue;
		}
		QString at = node.parentId;
		for (int depth = 0; !at.isEmpty() && depth < kLevelSceneMaxDepth; ++depth) {
			if (at == nodeId) {
				descendants << node.id;
				depths.insert(node.id, depth);
				break;
			}
			at = parents.value(at);
		}
	}
	std::stable_sort(descendants.begin(), descendants.end(),
		[&depths](const QString& a, const QString& b) { return depths.value(a) < depths.value(b); });
	return descendants;
}

QStringList levelLinkedGroupMembers(const LevelSceneState& state, const QString& nodeId)
{
	const LevelSceneNode* node = levelSceneNode(state, nodeId);
	if (!node) {
		return {};
	}
	QStringList members = node->objects;
	for (const QString& id : levelLinkedGroupDescendants(state, nodeId)) {
		members += levelSceneNode(state, id)->objects;
	}
	return members;
}

LevelLinkedContent levelLinkedContent(const LevelMapDocument& document, const QStringList& members)
{
	return collect(document, members);
}

LevelLinkedContent levelLinkedGroupContent(const LevelMapDocument& document, const QString& nodeId)
{
	return groupContent(document, document.scene, nodeId);
}

LevelLinkedContent levelLinkedGroupContent(const LevelMapDocument& document, const LevelSceneState& state, const QString& nodeId)
{
	return groupContent(document, state, nodeId);
}

bool levelLinkedGroupBounds(const LevelMapDocument& document, const QStringList& members, LevelMapVec3* mins, LevelMapVec3* maxs)
{
	LevelMapVec3 low;
	LevelMapVec3 high;
	const auto grow = [&low, &high](const LevelMapVec3& point) {
		if (!point.valid || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
			return;
		}
		if (!low.valid) {
			low = point;
			high = point;
			return;
		}
		low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z), true};
		high = {std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z), true};
	};
	const LevelLinkedContent content = levelLinkedContent(document, members);
	QSet<int> entities;
	for (const LevelMapSelectionRef& root : content.roots) {
		if (root.kind == LevelMapSelectionKind::Entity) {
			entities.insert(root.objectId);
		}
	}
	for (const LevelMapSelectionRef& root : content.roots) {
		if (root.kind == LevelMapSelectionKind::QuakeBrush) {
			for (const LevelMapBrush& brush : document.brushes) {
				if (brush.id == root.objectId) {
					grow(brush.mins);
					grow(brush.maxs);
				}
			}
		} else if (root.kind == LevelMapSelectionKind::QuakePatch) {
			for (const LevelMapPatch& patch : document.patches) {
				if (patch.id == root.objectId) {
					grow(patch.mins);
					grow(patch.maxs);
				}
			}
		}
	}
	for (const LevelMapBrush& brush : document.brushes) {
		if (entities.contains(brush.entityId)) {
			grow(brush.mins);
			grow(brush.maxs);
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (entities.contains(patch.entityId)) {
			grow(patch.mins);
			grow(patch.maxs);
		}
	}
	for (const LevelMapEntity& entity : document.entities) {
		if (entities.contains(entity.id)) {
			grow(entity.origin);
		}
	}
	if (mins) {
		*mins = low;
	}
	if (maxs) {
		*maxs = high;
	}
	return low.valid;
}

LevelMapVec3 levelLinkedDuplicateOffset(const LevelMapDocument& document, const QString& nodeId)
{
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (!levelSceneNode(document.scene, nodeId)
		|| !levelLinkedGroupBounds(document, levelLinkedGroupMembers(document.scene, nodeId), &mins, &maxs)) {
		return {64.0, 0.0, 0.0, true};
	}
	const double width = std::max(16.0, std::ceil((maxs.x - mins.x) / 16.0) * 16.0);
	return {width, 0.0, 0.0, true};
}

void refreshLevelLinkBaselines(const LevelMapDocument& document, LevelSceneState* state)
{
	if (!state) {
		return;
	}
	for (LevelSceneNode& node : state->nodes) {
		if (node.linkId.isEmpty()) {
			clearBaseline(&node);
		} else {
			setBaseline(&node, groupContent(document, *state, node.id));
		}
	}
}

void refreshLevelLinkBaselines(const LevelMapDocument& document, LevelSceneState* state, const QSet<QString>& nodes)
{
	if (!state) {
		return;
	}
	for (LevelSceneNode& node : state->nodes) {
		if (!nodes.contains(node.id)) {
			continue;
		}
		if (node.linkId.isEmpty()) {
			clearBaseline(&node);
		} else {
			setBaseline(&node, groupContent(document, *state, node.id));
		}
	}
}

bool levelLinkedGroupsAgree(const LevelMapDocument& document, const QString& a, const QString& b)
{
	const LevelSceneNode* first = levelSceneNode(document.scene, a);
	const LevelSceneNode* second = levelSceneNode(document.scene, b);
	return first && second && !first->linkId.isEmpty() && first->linkId == second->linkId && sameChange(document, *first, *second, true);
}

QStringList levelLinkedGroupNodes(const LevelSceneState& state, const QString& nodeId)
{
	const LevelSceneNode* node = levelSceneNode(state, nodeId);
	QStringList nodes;
	if (!node || node->linkId.isEmpty()) {
		return nodes;
	}
	for (const LevelSceneNode& other : state.nodes) {
		if (other.linkId == node->linkId) {
			nodes << other.id;
		}
	}
	return nodes;
}

QStringList levelSelectedSceneGroups(const LevelMapDocument& document)
{
	const LevelSceneSelectionContext context = levelSceneSelectionContext(document);
	QSet<QString> selected;
	for (const LevelMapSelectionRef& ref : document.selection) {
		selected.insert(levelSceneCanonicalObject(document, levelMapSelectionRefId(ref)));
	}
	QStringList groups;
	for (const LevelSceneNode& node : document.scene.nodes) {
		const auto members = context.groupSelections.value(node.id);
		if (!members.isEmpty() && std::all_of(members.cbegin(), members.cend(), [&](const LevelMapSelectionRef& ref) {
				return selected.contains(levelMapSelectionRefId(ref));
			})) {
			groups << node.id;
		}
	}
	return groups;
}

QStringList levelLinkedGroupsOfSelection(const LevelMapDocument& document)
{
	const LevelSceneSelectionContext context = levelSceneSelectionContext(document);
	QHash<QString, QString> owners;
	for (const LevelMapBrush& brush : document.brushes) {
		owners.insert(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, brush.id}), QStringLiteral("entity:%1").arg(brush.entityId));
	}
	for (const LevelMapPatch& patch : document.patches) {
		owners.insert(levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch, patch.id}), QStringLiteral("entity:%1").arg(patch.entityId));
	}
	QSet<QString> reached;
	for (const LevelMapSelectionRef& ref : document.selection) {
		const QString object = levelSceneCanonicalObject(document, levelMapSelectionRefId(ref));
		QString unit = context.closedGroups.value(object, context.closedGroups.value(owners.value(object)));
		if (!unit.isEmpty()) {
			reached.insert(unit);
			continue;
		}
		// An object of the open group itself reaches the innermost open
		// linked group around it.
		QString holder = levelSceneMembership(document.scene, object);
		if (holder.isEmpty()) {
			holder = levelSceneMembership(document.scene, owners.value(object));
		}
		for (int depth = 0; !holder.isEmpty() && depth < kLevelSceneMaxDepth; ++depth) {
			const LevelSceneNode* node = levelSceneNode(document.scene, holder);
			if (!node) {
				break;
			}
			if (!node->linkId.isEmpty()) {
				reached.insert(node->id);
				break;
			}
			holder = node->parentId;
		}
	}
	QStringList groups;
	for (const LevelSceneNode& node : document.scene.nodes) {
		if (!node.linkId.isEmpty() && reached.contains(node.id)) {
			groups << node.id;
		}
	}
	return groups;
}

QVector<LevelMapSelectionRef> levelLinkedGroupsSelection(const LevelMapDocument& document, const QStringList& groups)
{
	QVector<LevelMapSelectionRef> members;
	QSet<QString> seen;
	for (const QString& group : groups) {
		for (const QString& id : levelLinkedGroupNodes(document.scene, group)) {
			if (!seen.contains(id)) {
				seen.insert(id);
				members += levelSceneSelection(document, id);
			}
		}
	}
	return pickLevelSceneObjects(levelSceneSelectionContext(document), {}, members, LevelScenePick::Replace);
}

static bool createLinkedLevelGroupImpl(LevelMapDocument* document, const QString& nodeId, const LevelMapVec3& offset, QString* createdId, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document || !quakeFamily(*document)) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "Linked groups need a Quake-family map."));
	}
	if (!sceneEditable(*document)) {
		return fail(error,
			QT_TRANSLATE_NOOP("LevelLinkedGroups", "Reset the unrecognized or stale scene metadata before changing scene organization."));
	}
	const LevelSceneNode* node = levelSceneNode(document->scene, nodeId);
	if (!node || node->kind != LevelSceneNodeKind::Group) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "Choose a group to duplicate; layers cannot be linked."));
	}
	if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z)) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "The duplicate's offset must be a finite distance."));
	}
	const LevelLinkedContent content = groupContent(*document, document->scene, nodeId);
	if (content.roots.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "The group is empty: assign objects to it first."));
	}
	const QStringList descendants = levelLinkedGroupDescendants(document->scene, nodeId);
	if (document->scene.nodes.size() + 1 + descendants.size() > kLevelSceneMaxNodes) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "The duplicate would exceed the scene's 1,000-node limit."));
	}
	const LevelSceneNode source = *node;
	{
		const Quiet quiet;
		const detail::LevelMapHistoryScope history(*document);
		const auto before = document->undoStack.size();
		// Correspondence comes from the groups as they are, before the copies
		// join them.
		const auto correspondence = levelLinkObjectIdentities(*document, document->scene, source);
		QVector<LevelSceneNode> nested;
		QHash<QString, QMap<QString, QString>> nestedCorrespondence;
		for (const QString& id : descendants) {
			nested << *levelSceneNode(document->scene, id);
			nestedCorrespondence.insert(id, levelLinkObjectIdentities(*document, document->scene, nested.last()));
		}
		LevelMapTextureLockOptions textures;
		textures.enabled = true;
		if (!duplicateLevelMapObjects(document, content.roots, offset.x, offset.y, offset.z, textures, error, false)) {
			return false;
		}
		const QHash<QString, QString> origins = document->undoStack.last().sceneObjectOrigins;
		LevelSceneState state = document->scene;
		const QString link = source.linkId.isEmpty() ? freshId() : source.linkId;
		const QSet<QString> direct(source.objects.cbegin(), source.objects.cend());
		LevelSceneNode copy;
		copy.id = freshId();
		copy.name = copyName(state, source.parentId, source.name);
		copy.parentId = source.parentId;
		copy.kind = LevelSceneNodeKind::Group;
		copy.visible = source.visible;
		copy.linkId = link;
		// The duplicate's frame is the source's, moved by the offset.
		const Frame moved = translation(offset.x, offset.y, offset.z);
		copy.linkFrame = multiply(moved, source.linkFrame);
		QHash<QString, QString> copies {{source.id, copy.id}};
		QHash<QString, QString> visibility {{QStringLiteral("group:") + copy.id, QStringLiteral("group:") + source.id}};
		QVector<LevelSceneNode> made {copy};
		QHash<QString, QString> nestedLinks;
		for (const LevelSceneNode& child : std::as_const(nested)) {
			LevelSceneNode duplicate;
			duplicate.id = freshId();
			duplicate.name = child.name;
			duplicate.parentId = copies.value(child.parentId, copy.id);
			duplicate.kind = LevelSceneNodeKind::Group;
			duplicate.visible = child.visible;
			duplicate.linkId = child.linkId.isEmpty() ? freshId() : child.linkId;
			duplicate.linkFrame = multiply(moved, child.linkFrame);
			nestedLinks.insert(child.id, duplicate.linkId);
			copies.insert(child.id, duplicate.id);
			visibility.insert(QStringLiteral("group:") + duplicate.id, QStringLiteral("group:") + child.id);
			made << duplicate;
		}
		for (LevelSceneNode& each : made) {
			const QString original = copies.key(each.id);
			const LevelSceneNode* from = original == source.id ? &source : nullptr;
			for (const LevelSceneNode& child : std::as_const(nested)) {
				if (child.id == original) {
					from = &child;
				}
			}
			const QSet<QString> held(from->objects.cbegin(), from->objects.cend());
			const auto& identities = from == &source ? correspondence : nestedCorrespondence.value(from->id);
			for (auto it = origins.cbegin(); it != origins.cend(); ++it) {
				if (held.contains(it.value())) {
					each.objects << it.key();
				}
				if (identities.contains(it.value())) {
					each.linkObjects.insert(it.key(), identities.value(it.value()));
				}
			}
			each.objects.sort();
		}
		int sourceIndex = -1;
		for (int index = 0; index < state.nodes.size(); ++index) {
			LevelSceneNode& each = state.nodes[index];
			each.objects.removeIf([&origins](const QString& object) { return origins.contains(object); });
			if (each.id == nodeId) {
				each.linkId = link;
				each.linkObjects = correspondence;
				sourceIndex = index;
			} else if (nestedLinks.contains(each.id)) {
				each.linkId = nestedLinks.value(each.id);
				each.linkObjects = nestedCorrespondence.value(each.id);
			}
		}
		// Placed after the source, so linked groups list together.
		int at = sourceIndex;
		for (const LevelSceneNode& each : std::as_const(made)) {
			state.nodes.insert(++at, each);
		}
		QSet<QString> touched {nodeId};
		for (const LevelSceneNode& each : std::as_const(made)) {
			touched.insert(each.id);
		}
		for (const QString& id : descendants) {
			touched.insert(id);
		}
		// Groups around the source keep their baselines, so the sync below sees
		// that they changed.
		rebaseline(*document, &state, &touched);
		if (!commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelLinkedGroups", "Link the duplicate")), error, visibility)) {
			rollBack(document, static_cast<int>(document->undoStack.size() - before), {});
			return false;
		}
		collapseLevelMapUndoSteps(document, static_cast<int>(document->undoStack.size() - before),
			QCoreApplication::translate("LevelLinkedGroups", "Create linked duplicate of %1").arg(source.name),
			QCoreApplication::translate("LevelLinkedGroups", "Remove linked duplicate of %1").arg(source.name));
		if (createdId) {
			*createdId = copy.id;
		}
	}
	// A group nested in a linked group changed that group too.
	syncLevelLinkedGroups(document);
	return true;
}

bool createLinkedLevelGroup(LevelMapDocument* document, const QString& nodeId, const LevelMapVec3& offset, QString* createdId, QString* error)
{
	if (!document) {
		return createLinkedLevelGroupImpl(nullptr, nodeId, offset, nullptr, error);
	}
	// A failed metadata commit must not leave copied records, invalidate redo,
	// change selection or consume the caller's history. Qt records share storage
	// until the operation changes them.
	auto candidate = *document;
	QString created;
	const auto make = [&](LevelMapDocument* working) { return createLinkedLevelGroupImpl(working, nodeId, offset, &created, error); };
	const auto guarded = guardLockedCopies(&candidate, error, make);
	if (!(guarded.has_value() ? *guarded : make(&candidate))) {
		return false;
	}
	*document = std::move(candidate);
	if (createdId) {
		*createdId = created;
	}
	return true;
}

static bool updateLinkedLevelGroupsImpl(LevelMapDocument* document, const QString& nodeId, int* updated, QString* error)
{
	if (error) {
		error->clear();
	}
	if (updated) {
		*updated = 0;
	}
	if (!document || !quakeFamily(*document)) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "Linked groups need a Quake-family map."));
	}
	if (!sceneEditable(*document)) {
		return fail(error,
			QT_TRANSLATE_NOOP("LevelLinkedGroups", "Reset the unrecognized or stale scene metadata before changing scene organization."));
	}
	const LevelSceneNode* source = levelSceneNode(document->scene, nodeId);
	if (!source || source->linkId.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "Choose a linked group."));
	}
	const LevelLinkedContent content = groupContent(*document, document->scene, nodeId);
	if (content.roots.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "The group is empty: there is nothing to copy to the others."));
	}
	const QSet<QString> locked = levelSceneLockedNodes(document->scene);
	QStringList targets;
	QStringList passed;
	for (const QString& id : levelLinkedGroupNodes(document->scene, nodeId)) {
		if (id == nodeId) {
			continue;
		}
		if (lockedSubtree(document->scene, locked, id)) {
			passed << id;
		} else {
			targets << id;
		}
	}
	if (targets.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "The group has no unlocked linked groups to update."));
	}
	const QString name = source->name;
	const Quiet quiet;
	const Propagating propagating(document);
	const detail::LevelMapHistoryScope history(*document);
	const QVector<LevelMapSelectionRef> selection = document->selection;
	Update update;
	int steps = 0;
	if (!propagate(document, nodeId, targets, &update, &steps, error)) {
		rollBack(document, steps, selection);
		return false;
	}
	setLevelMapSelection(document, selection);
	LevelSceneState state = document->scene;
	QSet<QString> touched {nodeId};
	for (const QString& id : std::as_const(update.updated)) {
		touched.insert(id);
	}
	for (const QString& id : std::as_const(update.created)) {
		touched.insert(id);
	}
	// The groups holding updated ones changed with them.
	for (const QString& id : QSet<QString>(touched)) {
		for (QString at = levelSceneNode(state, id) ? levelSceneNode(state, id)->parentId : QString(); !at.isEmpty();) {
			touched.insert(at);
			const LevelSceneNode* parent = levelSceneNode(state, at);
			at = parent ? parent->parentId : QString();
		}
	}
	for (LevelSceneNode& node : state.nodes) {
		if (node.id == nodeId) {
			node.linkOutOfSync = false;
		} else if (passed.contains(node.id)) {
			node.linkOutOfSync = true;
		}
	}
	rebaseline(*document, &state, &touched);
	if (state != document->scene) {
		const auto before = document->undoStack.size();
		if (!commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelLinkedGroups", "Update linked groups")), error)) {
			rollBack(document, steps, selection);
			return false;
		}
		steps += static_cast<int>(document->undoStack.size() - before);
	}
	collapseLevelMapUndoSteps(document, steps, QCoreApplication::translate("LevelLinkedGroups", "Update linked groups of %1").arg(name),
		QCoreApplication::translate("LevelLinkedGroups", "Restore linked groups of %1").arg(name));
	if (updated) {
		*updated = static_cast<int>(update.updated.size());
	}
	return true;
}

bool updateLinkedLevelGroups(LevelMapDocument* document, const QString& nodeId, int* updated, QString* error)
{
	if (!document) {
		return updateLinkedLevelGroupsImpl(nullptr, nodeId, nullptr, error);
	}
	auto candidate = *document;
	int count = 0;
	if (!updateLinkedLevelGroupsImpl(&candidate, nodeId, &count, error)) {
		return false;
	}
	*document = std::move(candidate);
	if (updated) {
		*updated = count;
	}
	return true;
}

bool separateLinkedLevelGroups(LevelMapDocument* document, const QStringList& nodeIds, int* separated, QString* error)
{
	if (error) {
		error->clear();
	}
	if (separated) {
		*separated = 0;
	}
	if (!document || !sceneEditable(*document)) {
		return fail(error,
			QT_TRANSLATE_NOOP("LevelLinkedGroups", "Reset the unrecognized or stale scene metadata before changing scene organization."));
	}
	if (nodeIds.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "Choose the linked groups to separate."));
	}
	QMap<QString, QStringList> chosen;
	for (const QString& id : nodeIds) {
		const LevelSceneNode* node = levelSceneNode(document->scene, id);
		if (!node || node->linkId.isEmpty()) {
			return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "The group is not linked."));
		}
		if (!chosen[node->linkId].contains(id)) {
			chosen[node->linkId] << id;
		}
	}
	const Quiet quiet;
	LevelSceneState state = document->scene;
	int count = 0;
	QStringList names;
	for (auto it = chosen.cbegin(); it != chosen.cend(); ++it) {
		const int members = static_cast<int>(levelLinkedGroupNodes(state, it.value().first()).size());
		if (it.value().size() >= members) {
			continue;
		}
		// Several chosen from one set stay linked with each other.
		const QString link = it.value().size() > 1 ? freshId() : QString();
		for (LevelSceneNode& node : state.nodes) {
			if (!it.value().contains(node.id)) {
				continue;
			}
			names << node.name;
			if (link.isEmpty()) {
				clearLink(&node);
			} else {
				node.linkId = link;
			}
			++count;
		}
	}
	if (count == 0) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups",
			"These are all the linked groups of their set: choose fewer of them to separate, or leave them linked."));
	}
	rebaseline(*document, &state);
	const QString description = count == 1 ? QCoreApplication::translate("LevelLinkedGroups", "Separate %1").arg(names.first())
										   : QCoreApplication::translate("LevelLinkedGroups", "Separate %n linked group(s)", nullptr, count);
	if (!commitLevelSceneState(document, state, description, error)) {
		return false;
	}
	if (separated) {
		*separated = count;
	}
	return true;
}

bool unlinkLevelGroup(LevelMapDocument* document, const QString& nodeId, QString* error)
{
	return separateLinkedLevelGroups(document, {nodeId}, nullptr, error);
}

QString levelLinkFrameText(const std::array<double, 12>& frame)
{
	if (movesOnly(frame)) {
		return {};
	}
	const auto close = [](double a, double b) { return std::abs(a - b) <= 1e-6; };
	const std::array<double, 9> m {frame[0], frame[1], frame[2], frame[4], frame[5], frame[6], frame[8], frame[9], frame[10]};
	const double d = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
	const bool orthonormal = close(std::abs(d), 1.0) && close(m[0] * m[0] + m[1] * m[1] + m[2] * m[2], 1.0)
		&& close(m[3] * m[3] + m[4] * m[4] + m[5] * m[5], 1.0) && close(m[6] * m[6] + m[7] * m[7] + m[8] * m[8], 1.0)
		&& close(m[0] * m[3] + m[1] * m[4] + m[2] * m[5], 0.0) && close(m[0] * m[6] + m[1] * m[7] + m[2] * m[8], 0.0)
		&& close(m[3] * m[6] + m[4] * m[7] + m[5] * m[8], 0.0);
	if (!orthonormal) {
		return d < 0 ? QCoreApplication::translate("LevelLinkedGroups", "Mirrored, scaled or sheared")
					 : QCoreApplication::translate("LevelLinkedGroups", "Scaled or sheared");
	}
	// Turned about the vertical, or mirrored across a vertical plane: every
	// mirror that keeps up and down is one, whatever it was turned by too.
	if (close(m[2], 0.0) && close(m[5], 0.0) && close(m[6], 0.0) && close(m[7], 0.0) && close(m[8], 1.0)) {
		if (d < 0) {
			return QCoreApplication::translate("LevelLinkedGroups", "Mirrored");
		}
		double degrees = std::atan2(m[3], m[0]) * 180.0 / 3.14159265358979323846;
		degrees = std::fmod(degrees + 360.0, 360.0);
		return QCoreApplication::translate("LevelLinkedGroups", "Turned %1 degrees").arg(QString::number(std::round(degrees * 100.0) / 100.0));
	}
	return d < 0 ? QCoreApplication::translate("LevelLinkedGroups", "Mirrored and rotated")
				 : QCoreApplication::translate("LevelLinkedGroups", "Rotated");
}

std::array<double, 12> levelLinkFrameAfter(const std::array<double, 12>& map, const std::array<double, 12>& frame)
{
	return multiply(map, frame);
}

void syncLevelLinkedGroups(LevelMapDocument* document)
{
	if (quietDepth > 0 || !document || !quakeFamily(*document) || document->undoStack.isEmpty() || !sceneEditable(*document)
		|| std::none_of(document->scene.nodes.cbegin(), document->scene.nodes.cend(),
			[](const LevelSceneNode& node) { return !node.linkId.isEmpty(); })) {
		return;
	}
	const Quiet quiet;
	const Propagating propagating(document);
	enum class Change { Same, Transformed, Changed, Unknown };
	struct Member {
		QString id;
		QString link;
		Change change = Change::Same;
		LevelLinkedContent now;
		Frame frame;
		bool locked = false;
	};
	// What the edit did, when it moved records all the same way.
	const LevelMapUndoCommand& trigger = document->undoStack.last();
	const bool hinted = trigger.hasAffine;
	const QSet<QString> moved = trigger.affineObjects;
	const Frame motion = trigger.affine;
	const QSet<QString> open(document->openSceneGroups.cbegin(), document->openSceneGroups.cend());
	const LevelSceneState before = document->scene;
	const QSet<QString> locked = levelSceneLockedNodes(before);
	QVector<Member> members;
	QStringList links;
	for (const LevelSceneNode& node : before.nodes) {
		if (node.linkId.isEmpty()) {
			continue;
		}
		Member member {node.id, node.linkId, Change::Same, groupContent(*document, before, node.id), node.linkFrame,
			lockedSubtree(before, locked, node.id)};
		if (node.linkShape.isEmpty()) {
			member.change = Change::Unknown;
		} else if (member.now.shape == node.linkShape && sameAnchor(member.now.anchor, node.linkAnchor)
			&& member.now.surface == node.linkSurface) {
			member.change = Change::Same;
		} else if (open.contains(node.id)) {
			// Inside an open group every edit is to the shared content, even one
			// that moves all of it.
			member.change = Change::Changed;
		} else {
			const QSet<QString> objects = levelLinkedObjects(*document, levelLinkedGroupMembers(before, node.id));
			if (hinted && !objects.isEmpty() && std::all_of(objects.cbegin(), objects.cend(), [&moved](const QString& object) {
					return moved.contains(object);
				})) {
				member.change = Change::Transformed;
				member.frame = multiply(motion, node.linkFrame);
			} else if (member.now.shape == node.linkShape && !sameAnchor(member.now.anchor, node.linkAnchor)) {
				// Moved whole by an edit that records no map.
				member.change = Change::Transformed;
				member.frame = multiply(translation(member.now.anchor.x - node.linkAnchor[0], member.now.anchor.y - node.linkAnchor[1],
											member.now.anchor.z - node.linkAnchor[2]),
					node.linkFrame);
			} else {
				member.change = Change::Changed;
			}
		}
		if (!links.contains(member.link)) {
			links << member.link;
		}
		members << member;
	}
	const QVector<LevelMapSelectionRef> selection = document->selection;
	int steps = 0;
	// Groups transformed whole take their new frames first, so content copied
	// into their counterparts below lands in the right one.
	{
		LevelSceneState state = document->scene;
		bool changed = false;
		for (LevelSceneNode& node : state.nodes) {
			for (const Member& member : std::as_const(members)) {
				if (member.id == node.id && member.change == Change::Transformed && member.frame != node.linkFrame) {
					node.linkFrame = member.frame;
					changed = true;
				}
			}
		}
		if (changed) {
			const auto committed = document->undoStack.size();
			if (commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelLinkedGroups", "Turn linked group frames")), nullptr)) {
				steps += static_cast<int>(document->undoStack.size() - committed);
			}
		}
	}
	QSet<QString> fresh;
	QHash<QString, QString> copiedFrom;
	QSet<QString> removed;
	QSet<QString> inSync;
	QSet<QString> outOfSync;
	QSet<QString> unlinked;
	QStringList notices;
	for (const QString& link : outermostFirst(before, links)) {
		QStringList live;
		QVector<const Member*> changed;
		for (const Member& member : std::as_const(members)) {
			if (member.link != link || removed.contains(member.id)) {
				continue;
			}
			live << member.id;
			if (member.change == Change::Changed && !fresh.contains(member.id)) {
				changed << &member;
			}
		}
		for (const QString& id : levelLinkedGroupNodes(document->scene, live.value(0))) {
			if (fresh.contains(id) && !live.contains(id)) {
				live << id;
			}
		}
		// A group emptied of everything leaves its link rather than emptying the rest.
		changed.removeIf([&unlinked](const Member* member) {
			if (member->now.roots.isEmpty()) {
				unlinked.insert(member->id);
				return true;
			}
			return false;
		});
		if (changed.isEmpty()) {
			continue;
		}
		const Member* source = changed.first();
		const LevelSceneNode* sourceNode = levelSceneNode(document->scene, source->id);
		QSet<QString> agreeing {source->id};
		for (const Member* other : std::as_const(changed)) {
			const LevelSceneNode* otherNode = levelSceneNode(document->scene, other->id);
			if (other == source || !sourceNode || !otherNode) {
				continue;
			}
			// Keys a member protects are its own, whatever their values.
			if (!sameChange(*document, *sourceNode, *otherNode, true)) {
				agreeing.clear();
				break;
			}
			agreeing.insert(other->id);
		}
		if (agreeing.isEmpty()) {
			// TrenchBroom refuses this edit; here it stands, and the set says
			// it is out of sync until one of them is copied over the rest.
			for (const QString& id : std::as_const(live)) {
				outOfSync.insert(id);
			}
			notices << QCoreApplication::translate("LevelLinkedGroups",
				"Linked groups of %1 were changed in different ways at once, so none was copied over the others. They are out of sync: "
				"edit one again, or use Update Linked Groups on the one to keep.")
						   .arg(sourceNode ? sourceNode->name : source->id);
			continue;
		}
		QStringList targets;
		for (const QString& id : std::as_const(live)) {
			if (agreeing.contains(id) || fresh.contains(id)) {
				continue;
			}
			const auto it = std::find_if(members.cbegin(), members.cend(), [&id](const Member& member) { return member.id == id; });
			if (it != members.cend() && it->locked) {
				outOfSync.insert(id);
				const LevelSceneNode* node = levelSceneNode(document->scene, id);
				notices << QCoreApplication::translate("LevelLinkedGroups", "%1 is locked, so it kept its content and is out of sync.")
							   .arg(node ? node->name : id);
				continue;
			}
			targets << id;
		}
		// A copy an outer update made matches the group it copies as that was
		// then, so it takes what that group takes now: two members of this set
		// inside one outer member both reach the outer member's counterparts.
		for (const QString& id : std::as_const(live)) {
			if (!fresh.contains(id)) {
				continue;
			}
			QString origin = copiedFrom.value(id);
			for (int depth = 0; fresh.contains(origin) && depth < kLevelSceneMaxNodes; ++depth) {
				origin = copiedFrom.value(origin);
			}
			if (targets.contains(origin)) {
				targets << id;
			} else if (outOfSync.contains(origin)) {
				outOfSync.insert(id);
			}
		}
		for (const QString& id : std::as_const(agreeing)) {
			inSync.insert(id);
		}
		if (targets.isEmpty()) {
			continue;
		}
		QString error;
		const int done = steps;
		Update update;
		if (!propagate(document, source->id, targets, &update, &steps, &error)) {
			// The other groups keep what they had; the edit itself stands.
			rollBack(document, steps - done, selection);
			steps = done;
			for (const QString& id : std::as_const(targets)) {
				outOfSync.insert(id);
			}
			notices << QCoreApplication::translate("LevelLinkedGroups", "Could not update the groups linked with %1: %2")
						   .arg(sourceNode ? sourceNode->name : source->id, error);
			continue;
		}
		fresh.unite(update.created);
		copiedFrom.insert(update.copiedFrom);
		removed.unite(update.removed);
		for (const QString& id : std::as_const(update.updated)) {
			inSync.insert(id);
		}
	}
	setLevelMapSelection(document, selection);
	LevelSceneState state = document->scene;
	for (LevelSceneNode& node : state.nodes) {
		if (unlinked.contains(node.id)) {
			clearLink(&node);
		} else if (outOfSync.contains(node.id)) {
			node.linkOutOfSync = true;
		} else if (inSync.contains(node.id) || fresh.contains(node.id)) {
			node.linkOutOfSync = false;
		}
	}
	rebaseline(*document, &state);
	if (state != document->scene) {
		const auto committed = document->undoStack.size();
		if (commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelLinkedGroups", "Update linked groups")), nullptr)) {
			steps += static_cast<int>(document->undoStack.size() - committed);
		}
	}
	if (steps > 0) {
		foldLevelMapFollowUpEdits(document, steps);
	}
	document->linkedGroupNotices += notices;
}

bool levelLinkedGroupPropagationActive(const LevelMapDocument* document)
{
	return document && document == propagatingDocument;
}

detail::LevelLinkedUpdateScope::LevelLinkedUpdateScope() { ++quietDepth; }
detail::LevelLinkedUpdateScope::~LevelLinkedUpdateScope() { --quietDepth; }
bool detail::levelLinkedUpdatesDeferred() { return quietDepth > 0; }

} // namespace vibestudio
