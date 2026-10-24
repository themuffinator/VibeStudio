#include "core/level_selection_tools.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <limits>

namespace vibestudio {
namespace {

struct Numbered {
	LevelMapSelectionRef ref;
	int line = 0;
	int order = 0;
};

bool sooner(const Numbered& a, const Numbered& b)
{
	return a.line != b.line ? a.line < b.line : a.order < b.order;
}

// Saved objects by their place in the file; new ones after them, in the
// order they were made, as save writes them.
int fileLine(int startLine) { return startLine > 0 ? startLine : std::numeric_limits<int>::max(); }

QVector<Numbered> entitiesInOrder(const LevelMapDocument& document)
{
	QVector<Numbered> result;
	for (int index = 0; index < document.entities.size(); ++index) {
		const auto& entity = document.entities.at(index);
		result.push_back({{LevelMapSelectionKind::Entity, entity.id}, fileLine(entity.startLine), index});
	}
	std::stable_sort(result.begin(), result.end(), sooner);
	return result;
}

QVector<Numbered> primitivesOf(const LevelMapDocument& document, int entityId)
{
	QVector<Numbered> result;
	for (int index = 0; index < document.brushes.size(); ++index) {
		const auto& brush = document.brushes.at(index);
		if (brush.entityId == entityId) {
			// A brush rewritten in place of a source brush is saved, and so
			// numbered, where that brush stood.
			result.push_back({{LevelMapSelectionKind::QuakeBrush, brush.id}, fileLine(levelMapBrushSaveLine(document, brush)), index});
		}
	}
	for (int index = 0; index < document.patches.size(); ++index) {
		const auto& patch = document.patches.at(index);
		if (patch.entityId == entityId) {
			result.push_back({{LevelMapSelectionKind::QuakePatch, patch.id}, fileLine(patch.startLine), static_cast<int>(document.brushes.size()) + index});
		}
	}
	std::stable_sort(result.begin(), result.end(), sooner);
	return result;
}

} // namespace

QVector<LevelMapSelectionRef> levelMapSelectionWholeEntities(const LevelMapDocument& document)
{
	int world = -1;
	for (const auto& entity : document.entities) {
		if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
			world = entity.id;
			break;
		}
	}
	QHash<int, int> brushOwners, patchOwners;
	for (const auto& brush : document.brushes) {
		brushOwners.insert(brush.id, brush.entityId);
	}
	for (const auto& patch : document.patches) {
		patchOwners.insert(patch.id, patch.entityId);
	}
	QVector<LevelMapSelectionRef> result;
	QSet<QPair<int, int>> seen;
	const auto add = [&result, &seen](const LevelMapSelectionRef& ref) {
		if (!seen.contains({static_cast<int>(ref.kind), ref.objectId})) {
			seen.insert({static_cast<int>(ref.kind), ref.objectId});
			result.push_back(ref);
		}
	};
	for (const auto& ref : document.selection) {
		int owner = -1;
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			owner = brushOwners.value(ref.objectId, -1);
		} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
			owner = patchOwners.value(ref.objectId, -1);
		}
		if (owner >= 0 && owner != world) {
			add({LevelMapSelectionKind::Entity, owner});
		} else {
			add(ref);
		}
	}
	return result;
}

bool levelMapObjectAtNumber(const LevelMapDocument& document, int entity, int primitive, LevelMapSelectionRef* ref, QString* error)
{
	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return fail(QCoreApplication::translate("VibeStudioLevelSelection", "Entity and brush numbers belong to Quake-family maps."));
	}
	const QVector<Numbered> entities = entitiesInOrder(document);
	if (entity < 0 || entity >= entities.size()) {
		return fail(QCoreApplication::translate("VibeStudioLevelSelection", "The map has no entity %1; it has %n entit(y)(ies), counted from 0.",
			nullptr, static_cast<int>(entities.size()))
						.arg(entity));
	}
	const LevelMapSelectionRef owner = entities.at(entity).ref;
	if (primitive < 0) {
		if (ref) {
			*ref = owner;
		}
		return true;
	}
	const QVector<Numbered> primitives = primitivesOf(document, owner.objectId);
	if (primitive >= primitives.size()) {
		return fail(QCoreApplication::translate("VibeStudioLevelSelection", "Entity %1 has no brush %2; it has %n brush(es) and patch(es), counted from 0.",
			nullptr, static_cast<int>(primitives.size()))
						.arg(entity)
						.arg(primitive));
	}
	if (ref) {
		*ref = primitives.at(primitive).ref;
	}
	return true;
}

bool levelMapObjectNumber(const LevelMapDocument& document, const LevelMapSelectionRef& ref, int* entity, int* primitive)
{
	int owner = -1;
	if (ref.kind == LevelMapSelectionKind::Entity) {
		owner = ref.objectId;
	} else if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
		for (const auto& brush : document.brushes) {
			if (brush.id == ref.objectId) {
				owner = brush.entityId;
				break;
			}
		}
	} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
		for (const auto& patch : document.patches) {
			if (patch.id == ref.objectId) {
				owner = patch.entityId;
				break;
			}
		}
	}
	if (owner < 0) {
		return false;
	}
	const QVector<Numbered> entities = entitiesInOrder(document);
	const auto at = std::find_if(entities.cbegin(), entities.cend(), [owner](const Numbered& n) { return n.ref.objectId == owner; });
	if (at == entities.cend()) {
		return false;
	}
	int number = -1;
	if (ref.kind != LevelMapSelectionKind::Entity) {
		const QVector<Numbered> primitives = primitivesOf(document, owner);
		for (int index = 0; index < primitives.size(); ++index) {
			if (primitives.at(index).ref == ref) {
				number = index;
				break;
			}
		}
		if (number < 0) {
			return false;
		}
	}
	if (entity) {
		*entity = static_cast<int>(at - entities.cbegin());
	}
	if (primitive) {
		*primitive = number;
	}
	return true;
}

bool parseLevelMapObjectNumber(const QString& text, int* entity, int* primitive)
{
	static const QRegularExpression pattern(QStringLiteral("\\bentity\\s*#?\\s*(\\d+)(?:\\s*[,;:]?\\s*(?:brush|primitive|patch)\\s*#?\\s*(\\d+))?"),
		QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch match = pattern.match(text);
	if (!match.hasMatch()) {
		return false;
	}
	bool ok = false;
	const int e = match.captured(1).toInt(&ok);
	if (!ok) {
		return false;
	}
	int p = -1;
	if (match.hasCaptured(2) && !match.captured(2).isEmpty()) {
		p = match.captured(2).toInt(&ok);
		if (!ok) {
			return false;
		}
	}
	if (entity) {
		*entity = e;
	}
	if (primitive) {
		*primitive = p;
	}
	return true;
}

} // namespace vibestudio
