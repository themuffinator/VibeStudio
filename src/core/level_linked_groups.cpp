#include "core/level_linked_groups.h"

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
// GPL-3.0-or-later, linked groups since v2023.1): copies that take on each
// other's edits while keeping their own places and turns. The design here is
// VibeStudio's own, on its scene layers and groups; no TrenchBroom code or file
// format is used.

namespace vibestudio {

namespace {

// The sync runs once for each edit. Its own steps, and the operations below,
// must not start it again.
thread_local int quietDepth = 0;

struct Quiet {
	Quiet() { ++quietDepth; }
	~Quiet() { --quietDepth; }
	Quiet(const Quiet&) = delete;
	Quiet& operator=(const Quiet&) = delete;
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

// A copy's frame: mirrored across x first when `mirror`, then turned
// `turns` quarter turns anticlockwise seen from above, as
// turnLevelMapSelection() applies it.
struct Frame {
	int turns = 0;
	bool mirror = false;
	bool operator==(const Frame&) const = default;
};

// `a` after `b`. A mirror reverses the turns that come before it.
Frame compose(const Frame& a, const Frame& b)
{
	return {(((a.turns + (a.mirror ? -b.turns : b.turns)) % 4) + 4) % 4, a.mirror != b.mirror};
}

Frame inverse(const Frame& frame)
{
	return frame.mirror ? frame : Frame {(4 - frame.turns) % 4, false};
}

Frame frameOf(const LevelSceneNode& node)
{
	return {((node.linkTurn % 4) + 4) % 4, node.linkMirror};
}

// Thousandths, so copies placed by fractional offsets still compare equal.
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

QByteArray digest(const QVector<Lines>& lines, int which)
{
	QCryptographicHash hash(QCryptographicHash::Sha1);
	for (const Lines& line : lines) {
		hash.addData(which == 0 ? line.geometry : (which == 1 ? line.shape : line.surface));
		hash.addData(QByteArrayView("\n"));
	}
	return hash.result();
}

void setBaseline(LevelSceneNode* node, const LevelLinkedContent& content)
{
	node->linkAnchor = {content.anchor.x, content.anchor.y, content.anchor.z};
	node->linkExtent = {content.maxs.x - content.mins.x, content.maxs.y - content.mins.y, content.maxs.z - content.mins.z};
	node->linkGeometry = content.geometry;
	node->linkShape = content.shape;
	node->linkSurface = content.surface;
}

void clearBaseline(LevelSceneNode* node)
{
	node->linkAnchor = {0.0, 0.0, 0.0};
	node->linkExtent = {0.0, 0.0, 0.0};
	node->linkGeometry.clear();
	node->linkShape.clear();
	node->linkSurface.clear();
}

void clearLink(LevelSceneNode* node)
{
	node->linkId.clear();
	node->linkTurn = 0;
	node->linkMirror = false;
	clearBaseline(node);
}

bool sameAnchor(const LevelMapVec3& now, const std::array<double, 3>& before)
{
	return number(now.x) == number(before[0]) && number(now.y) == number(before[1]) && number(now.z) == number(before[2]);
}

LevelMapVec3 anchorOf(const LevelSceneNode& node)
{
	return {node.linkAnchor[0], node.linkAnchor[1], node.linkAnchor[2], true};
}

// Every linked node's baseline from what it holds now; a link left with one
// copy is no link.
void rebaseline(const LevelMapDocument& document, LevelSceneState* state)
{
	QHash<QString, int> copies;
	for (const LevelSceneNode& node : std::as_const(state->nodes)) {
		if (!node.linkId.isEmpty()) {
			++copies[node.linkId];
		}
	}
	for (LevelSceneNode& node : state->nodes) {
		if (node.linkId.isEmpty()) {
			continue;
		}
		if (copies.value(node.linkId) < 2) {
			clearLink(&node);
			continue;
		}
		setBaseline(&node, levelLinkedContent(document, node.objects));
	}
}

// Whether a copy whose shape changed was turned or mirrored as a whole: its
// content, turned back about its centre, is what it was. The frame that
// turned it, or nullopt.
std::optional<Frame> wholeTurn(const LevelMapDocument& document, const LevelSceneNode& node, const LevelLinkedContent& now)
{
	if (!now.hasPoints || now.roots.isEmpty() || node.linkGeometry.isEmpty()) {
		return std::nullopt;
	}
	const auto close = [](double a, double b) { return std::abs(a - b) < 0.01; };
	const double width = now.maxs.x - now.mins.x;
	const double depth = now.maxs.y - now.mins.y;
	const double height = now.maxs.z - now.mins.z;
	if (!close(height, node.linkExtent[2])) {
		return std::nullopt;
	}
	for (int turns = 0; turns < 4; ++turns) {
		for (const bool mirror : {false, true}) {
			const Frame candidate {turns, mirror};
			if (candidate == Frame {}) {
				continue;
			}
			// A quarter turn swaps the box's width and depth.
			const bool swapped = turns % 2 == 1;
			if (!close(swapped ? depth : width, node.linkExtent[0]) || !close(swapped ? width : depth, node.linkExtent[1])) {
				continue;
			}
			LevelMapDocument probe = document;
			if (!setLevelMapSelection(&probe, now.roots)) {
				continue;
			}
			const Frame back = inverse(candidate);
			if (!turnLevelMapSelection(&probe, back.turns, back.mirror, now.anchor, {false, false})) {
				continue;
			}
			if (levelLinkedContent(probe, node.objects).geometry == node.linkGeometry) {
				return candidate;
			}
		}
	}
	return std::nullopt;
}

struct Target {
	QString id;
	LevelMapVec3 centre;
	Frame frame;
};

// Rebuilds each target from the source's content, placed by the difference
// between the target's centre and the source's and turned from the source's
// frame into the target's, counting the undo steps pushed. `moves` gains the
// copies each target now owns.
bool propagate(LevelMapDocument* document, const QStringList& sourceMembers, const LevelMapVec3& sourceCentre, const Frame& sourceFrame,
	const QVector<Target>& targets, QHash<QString, QString>* moves, int* steps, QString* error)
{
	const LevelLinkedContent content = levelLinkedContent(*document, sourceMembers);
	const QSet<QString> members(sourceMembers.cbegin(), sourceMembers.cend());
	LevelMapTextureLockOptions textures;
	textures.enabled = true;
	for (const Target& target : targets) {
		const LevelSceneNode* node = levelSceneNode(document->scene, target.id);
		if (!node) {
			continue;
		}
		const LevelLinkedContent old = levelLinkedContent(*document, node->objects);
		if (!old.roots.isEmpty()) {
			if (!deleteLevelMapObjects(document, old.roots, error)) {
				return false;
			}
			++*steps;
		}
		if (content.roots.isEmpty()) {
			continue;
		}
		if (!duplicateLevelMapObjects(document, content.roots, target.centre.x - sourceCentre.x, target.centre.y - sourceCentre.y,
				target.centre.z - sourceCentre.z, textures, error)) {
			return false;
		}
		++*steps;
		QStringList copies;
		const LevelMapUndoCommand& copy = document->undoStack.last();
		for (auto it = copy.sceneObjectOrigins.cbegin(); it != copy.sceneObjectOrigins.cend(); ++it) {
			if (members.contains(it.value())) {
				moves->insert(it.key(), target.id);
			}
		}
		// Turned from the source's frame into the target's, about its centre;
		// duplication left the copies selected.
		const Frame turn = compose(target.frame, inverse(sourceFrame));
		if (!(turn == Frame {})) {
			if (!turnLevelMapSelection(document, turn.turns, turn.mirror, target.centre, textures, error)) {
				return false;
			}
			++*steps;
		}
	}
	return true;
}

// The copies leave the source they were made from and join their targets.
void applyMoves(LevelSceneState* state, const QHash<QString, QString>& moves)
{
	if (moves.isEmpty()) {
		return;
	}
	QHash<QString, QStringList> joining;
	for (auto it = moves.cbegin(); it != moves.cend(); ++it) {
		joining[it.value()] << it.key();
	}
	for (LevelSceneNode& node : state->nodes) {
		node.objects.removeIf([&moves](const QString& object) { return moves.contains(object); });
	}
	for (LevelSceneNode& node : state->nodes) {
		if (joining.contains(node.id)) {
			QStringList added = joining.value(node.id);
			added.sort();
			node.objects += added;
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

} // namespace

LevelLinkedContent levelLinkedContent(const LevelMapDocument& document, const QStringList& members)
{
	LevelLinkedContent content;
	QSet<int> entityIds;
	QSet<int> brushIds;
	QSet<int> patchIds;
	for (const QString& member : members) {
		const qsizetype colon = member.indexOf(QLatin1Char(':'));
		bool valid = false;
		const int id = member.mid(colon + 1).toInt(&valid);
		if (colon <= 0 || !valid) {
			continue;
		}
		const QStringView kind = QStringView(member).left(colon);
		if (kind == QLatin1String("entity")) {
			entityIds.insert(id);
		} else if (kind == QLatin1String("brush")) {
			brushIds.insert(id);
		} else if (kind == QLatin1String("patch")) {
			patchIds.insert(id);
		}
	}
	QSet<int> memberEntities;
	for (const LevelMapEntity& entity : document.entities) {
		if (entityIds.contains(entity.id) && entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) != 0) {
			memberEntities.insert(entity.id);
		}
	}
	// The bounds first: every line is written relative to their centre, which
	// moves, turns and mirrors with the content.
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
		if (memberEntities.contains(brush.entityId) || brushIds.contains(brush.id)) {
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
	QHash<int, QVector<Lines>> owned;
	QVector<Lines> lines;
	for (const LevelMapBrush* brush : std::as_const(brushes)) {
		if (memberEntities.contains(brush->entityId)) {
			owned[brush->entityId] << brushLines(*brush, content.anchor);
		} else {
			lines << brushLines(*brush, content.anchor);
			content.roots.push_back({LevelMapSelectionKind::QuakeBrush, brush->id});
		}
	}
	for (const LevelMapPatch* patch : std::as_const(patches)) {
		if (memberEntities.contains(patch->entityId)) {
			owned[patch->entityId] << patchLines(*patch, content.anchor);
		} else {
			lines << patchLines(*patch, content.anchor);
			content.roots.push_back({LevelMapSelectionKind::QuakePatch, patch->id});
		}
	}
	for (const LevelMapEntity& entity : document.entities) {
		if (memberEntities.contains(entity.id)) {
			lines << entityLines(entity, owned.value(entity.id), content.anchor);
			content.roots.push_back({LevelMapSelectionKind::Entity, entity.id});
		}
	}
	sortLines(&lines);
	content.geometry = digest(lines, 0);
	content.shape = digest(lines, 1);
	content.surface = digest(lines, 2);
	return content;
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

LevelMapVec3 levelLinkedCopyOffset(const LevelMapDocument& document, const QString& nodeId)
{
	const LevelSceneNode* node = levelSceneNode(document.scene, nodeId);
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (!node || !levelLinkedGroupBounds(document, node->objects, &mins, &maxs)) {
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
			setBaseline(&node, levelLinkedContent(document, node.objects));
		}
	}
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

QStringList levelLinkedGroupsOfSelection(const LevelMapDocument& document)
{
	QSet<QString> objects;
	for (const LevelMapSelectionRef& ref : document.selection) {
		objects.insert(levelSceneCanonicalObject(document, levelMapSelectionRefId(ref)));
		int owner = -1;
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			for (const LevelMapBrush& brush : document.brushes) {
				if (brush.id == ref.objectId) {
					owner = brush.entityId;
					break;
				}
			}
		} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
			for (const LevelMapPatch& patch : document.patches) {
				if (patch.id == ref.objectId) {
					owner = patch.entityId;
					break;
				}
			}
		}
		if (owner >= 0) {
			objects.insert(QStringLiteral("entity:%1").arg(owner));
		}
	}
	QStringList groups;
	for (const LevelSceneNode& node : document.scene.nodes) {
		if (!node.linkId.isEmpty()
			&& std::any_of(node.objects.cbegin(), node.objects.cend(), [&objects](const QString& object) { return objects.contains(object); })) {
			groups << node.id;
		}
	}
	return groups;
}

bool createLinkedLevelGroup(LevelMapDocument* document, const QString& nodeId, const LevelMapVec3& offset, QString* createdId, QString* error)
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
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "Choose a group to copy; layers cannot be linked."));
	}
	if (std::any_of(document->scene.nodes.cbegin(), document->scene.nodes.cend(),
			[&nodeId](const LevelSceneNode& other) { return other.parentId == nodeId; })) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "A linked group cannot hold other layers or groups; move them out first."));
	}
	if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z)) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "The copy's offset must be a finite distance."));
	}
	const LevelLinkedContent content = levelLinkedContent(*document, node->objects);
	if (content.roots.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "The group is empty: assign objects to it first."));
	}
	const Quiet quiet;
	const QSet<QString> members(node->objects.cbegin(), node->objects.cend());
	const QString sourceName = node->name;
	const QString parentId = node->parentId;
	const QString existingLink = node->linkId;
	const bool visible = node->visible;
	const Frame frame = frameOf(*node);
	LevelMapTextureLockOptions textures;
	textures.enabled = true;
	if (!duplicateLevelMapObjects(document, content.roots, offset.x, offset.y, offset.z, textures, error)) {
		return false;
	}
	QStringList copies;
	const LevelMapUndoCommand& duplicate = document->undoStack.last();
	for (auto it = duplicate.sceneObjectOrigins.cbegin(); it != duplicate.sceneObjectOrigins.cend(); ++it) {
		if (members.contains(it.value())) {
			copies << it.key();
		}
	}
	copies.sort();
	LevelSceneState state = document->scene;
	const QString link = existingLink.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : existingLink;
	LevelSceneNode copy;
	copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	copy.name = copyName(state, parentId, sourceName);
	copy.parentId = parentId;
	copy.kind = LevelSceneNodeKind::Group;
	copy.visible = visible;
	copy.linkId = link;
	// The copy starts in the source's frame: same turn, same mirror.
	copy.linkTurn = frame.turns;
	copy.linkMirror = frame.mirror;
	copy.objects = copies;
	const QSet<QString> copied(copies.cbegin(), copies.cend());
	int sourceIndex = -1;
	for (int index = 0; index < state.nodes.size(); ++index) {
		LevelSceneNode& each = state.nodes[index];
		each.objects.removeIf([&copied](const QString& object) { return copied.contains(object); });
		if (each.id == nodeId) {
			each.linkId = link;
			sourceIndex = index;
		}
	}
	// Placed after the source, so the copies list together.
	state.nodes.insert(sourceIndex + 1, copy);
	rebaseline(*document, &state);
	if (!commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelLinkedGroups", "Link the copy")), error)) {
		rollBack(document, 1, {});
		return false;
	}
	collapseLevelMapUndoSteps(document, 2,
		QCoreApplication::translate("LevelLinkedGroups", "Create linked copy of %1").arg(sourceName),
		QCoreApplication::translate("LevelLinkedGroups", "Remove linked copy of %1").arg(sourceName));
	if (createdId) {
		*createdId = copy.id;
	}
	return true;
}

bool updateLinkedLevelGroups(LevelMapDocument* document, const QString& nodeId, int* updated, QString* error)
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
	const LevelLinkedContent content = levelLinkedContent(*document, source->objects);
	if (content.roots.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "The group is empty: there is nothing to copy to the others."));
	}
	const QSet<QString> locked = levelSceneLockedNodes(document->scene);
	QVector<Target> targets;
	for (const LevelSceneNode& other : document->scene.nodes) {
		if (other.id == nodeId || other.linkId != source->linkId || locked.contains(other.id)) {
			continue;
		}
		const LevelLinkedContent now = levelLinkedContent(*document, other.objects);
		targets.push_back({other.id, now.hasPoints ? now.anchor : anchorOf(other), frameOf(other)});
	}
	if (targets.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "The group has no unlocked copies to update."));
	}
	const Quiet quiet;
	const QString name = source->name;
	const QStringList members = source->objects;
	const Frame sourceFrame = frameOf(*source);
	const QVector<LevelMapSelectionRef> selection = document->selection;
	QHash<QString, QString> moves;
	int steps = 0;
	if (!propagate(document, members, content.anchor, sourceFrame, targets, &moves, &steps, error)) {
		rollBack(document, steps, selection);
		return false;
	}
	setLevelMapSelection(document, selection);
	LevelSceneState state = document->scene;
	applyMoves(&state, moves);
	rebaseline(*document, &state);
	if (state != document->scene) {
		if (!commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelLinkedGroups", "Update linked copies")), error)) {
			rollBack(document, steps, selection);
			return false;
		}
		++steps;
	}
	collapseLevelMapUndoSteps(document, steps, QCoreApplication::translate("LevelLinkedGroups", "Update linked copies of %1").arg(name),
		QCoreApplication::translate("LevelLinkedGroups", "Restore linked copies of %1").arg(name));
	if (updated) {
		*updated = static_cast<int>(targets.size());
	}
	return true;
}

bool unlinkLevelGroup(LevelMapDocument* document, const QString& nodeId, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document || !sceneEditable(*document)) {
		return fail(error,
			QT_TRANSLATE_NOOP("LevelLinkedGroups", "Reset the unrecognized or stale scene metadata before changing scene organization."));
	}
	const LevelSceneNode* node = levelSceneNode(document->scene, nodeId);
	if (!node || node->linkId.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedGroups", "The group is not linked."));
	}
	const Quiet quiet;
	const QString name = node->name;
	LevelSceneState state = document->scene;
	for (LevelSceneNode& each : state.nodes) {
		if (each.id == nodeId) {
			clearLink(&each);
		}
	}
	rebaseline(*document, &state);
	return commitLevelSceneState(document, state, QCoreApplication::translate("LevelLinkedGroups", "Unlink %1").arg(name), error);
}

void syncLevelLinkedGroups(LevelMapDocument* document)
{
	if (quietDepth > 0 || !document || !quakeFamily(*document) || document->undoStack.isEmpty() || !sceneEditable(*document)
		|| std::none_of(document->scene.nodes.cbegin(), document->scene.nodes.cend(),
			[](const LevelSceneNode& node) { return !node.linkId.isEmpty(); })) {
		return;
	}
	const Quiet quiet;
	enum class Change { Same, Moved, Turned, Changed, Unknown };
	struct Copy {
		QString id;
		QString link;
		Change change = Change::Same;
		LevelLinkedContent now;
		Frame frame;
		bool locked = false;
	};
	const LevelSceneState before = document->scene;
	const QSet<QString> locked = levelSceneLockedNodes(before);
	QVector<Copy> copies;
	QStringList links;
	for (const LevelSceneNode& node : before.nodes) {
		if (node.linkId.isEmpty()) {
			continue;
		}
		Copy copy {node.id, node.linkId, Change::Same, levelLinkedContent(*document, node.objects), frameOf(node), locked.contains(node.id)};
		if (node.linkShape.isEmpty()) {
			copy.change = Change::Unknown;
		} else if (copy.now.shape == node.linkShape) {
			// Moved as a whole: texture lock may have shifted its textures.
			if (copy.now.hasPoints && !sameAnchor(copy.now.anchor, node.linkAnchor)) {
				copy.change = Change::Moved;
			} else if (copy.now.surface != node.linkSurface) {
				copy.change = Change::Changed;
			}
		} else if (copy.now.geometry == node.linkGeometry) {
			// The same things in the same places with other texture scales or turns.
			copy.change = Change::Changed;
		} else if (const std::optional<Frame> turned = wholeTurn(*document, node, copy.now)) {
			copy.change = Change::Turned;
			copy.frame = compose(*turned, copy.frame);
		} else {
			copy.change = Change::Changed;
		}
		if (!links.contains(copy.link)) {
			links << copy.link;
		}
		copies << copy;
	}
	const QVector<LevelMapSelectionRef> selection = document->selection;
	QHash<QString, QString> moves;
	QSet<QString> unlinked;
	int steps = 0;
	for (const QString& link : std::as_const(links)) {
		const Copy* source = nullptr;
		int changed = 0;
		for (const Copy& copy : std::as_const(copies)) {
			if (copy.link == link && copy.change == Change::Changed) {
				source = &copy;
				++changed;
			}
		}
		// Nothing to copy, or copies changed in different ways at once.
		if (changed != 1) {
			continue;
		}
		// A copy emptied of everything leaves its link rather than emptying the rest.
		if (source->now.roots.isEmpty()) {
			unlinked.insert(source->id);
			continue;
		}
		QVector<Target> targets;
		for (const Copy& copy : std::as_const(copies)) {
			if (copy.link == link && copy.id != source->id && !copy.locked && copy.now.hasPoints) {
				targets.push_back({copy.id, copy.now.anchor, copy.frame});
			}
		}
		const LevelSceneNode* sourceNode = levelSceneNode(document->scene, source->id);
		const LevelSceneNode* sourceBefore = levelSceneNode(before, source->id);
		if (targets.isEmpty() || !sourceNode || !sourceBefore) {
			continue;
		}
		QString error;
		const int done = steps;
		if (!propagate(document, sourceNode->objects, anchorOf(*sourceBefore), source->frame, targets, &moves, &steps, &error)) {
			// The copies keep what they had; the edit itself stands.
			rollBack(document, steps - done, selection);
			steps = done;
		}
	}
	setLevelMapSelection(document, selection);
	LevelSceneState state = document->scene;
	applyMoves(&state, moves);
	for (LevelSceneNode& node : state.nodes) {
		if (unlinked.contains(node.id)) {
			clearLink(&node);
		}
		for (const Copy& copy : std::as_const(copies)) {
			if (copy.id == node.id && copy.change == Change::Turned) {
				node.linkTurn = copy.frame.turns;
				node.linkMirror = copy.frame.mirror;
			}
		}
	}
	rebaseline(*document, &state);
	if (state != document->scene
		&& commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelLinkedGroups", "Update linked copies")), nullptr)) {
		++steps;
	}
	if (steps > 0) {
		foldLevelMapFollowUpEdits(document, steps);
	}
}

} // namespace vibestudio
