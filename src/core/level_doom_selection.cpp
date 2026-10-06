#include "core/level_doom_selection.h"
#include "core/level_placement_control_p.h"
#include <QCoreApplication>
#include <utility>

namespace vibestudio {
namespace {
QString text(const char* value) { return QCoreApplication::translate("LevelDoomSelection", value); }
} // namespace
bool selectConnectedLevelMapDoomGeometry(LevelMapDocument* document, QString* error) {
	const auto fail = [&](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (error) {
		error->clear();
	}
	if (!document || document->format != LevelMapFormat::DoomWad || (document->doomFormat == LevelMapDoomFormat::Udmf && !document->doomUdmf)) {
		return fail(text(QT_TRANSLATE_NOOP("LevelDoomSelection", "Connected geometry selection requires a loaded Doom, Hexen or UDMF map.")));
	}
	auto candidate = *document;
	if (!setLevelMapSelection(&candidate, document->selection, error)) {
		return false;
	}
	QSet<int> vertices, lines, sectors;
	QVector<LevelMapSelectionRef> things;
	for (const auto& ref : document->selection) {
		switch (ref.kind) {
		case LevelMapSelectionKind::DoomVertex:
			vertices.insert(ref.objectId);
			break;
		case LevelMapSelectionKind::DoomLinedef:
			lines.insert(ref.objectId);
			break;
		case LevelMapSelectionKind::DoomSector:
			sectors.insert(ref.objectId);
			break;
		case LevelMapSelectionKind::DoomThing:
		case LevelMapSelectionKind::Entity:
			things << ref;
			break;
		default:
			break;
		}
	}
	QHash<int, int> sideSectors;
	for (const auto& side : document->doomSidedefs) {
		detail::placementCancellationCheckpoint();
		sideSectors.insert(side.id, side.sector);
	}
	QHash<int, QVector<int>> neighbours;
	for (const auto& line : document->doomLinedefs) {
		detail::placementCancellationCheckpoint();
		neighbours[line.startVertex] << line.endVertex;
		neighbours[line.endVertex] << line.startVertex;
		if (lines.contains(line.id) || sectors.contains(sideSectors.value(line.frontSidedef, -1)) ||
			sectors.contains(sideSectors.value(line.backSidedef, -1))) {
			vertices.insert(line.startVertex);
			vertices.insert(line.endVertex);
		}
	}
	if (vertices.isEmpty()) {
		return fail(text(QT_TRANSLATE_NOOP("LevelDoomSelection", "Select vertices, linedefs or sectors to expand connected geometry.")));
	}
	QVector<int> pending(vertices.cbegin(), vertices.cend());
	for (qsizetype at = 0; at < pending.size(); ++at) {
		detail::placementCancellationCheckpoint();
		for (const auto next : neighbours.value(pending[at])) {
			detail::placementCancellationCheckpoint();
			if (!vertices.contains(next)) {
				vertices.insert(next);
				pending << next;
			}
		}
	}
	QVector<LevelMapSelectionRef> selection;
	QSet<int> referenced;
	for (const auto& line : document->doomLinedefs) {
		detail::placementCancellationCheckpoint();
		if (vertices.contains(line.startVertex)) {
			selection << LevelMapSelectionRef{LevelMapSelectionKind::DoomLinedef, line.id};
			referenced.insert(line.startVertex);
			referenced.insert(line.endVertex);
		}
	}
	QSet<int> existingVertices;
	for (const auto& vertex : document->doomVertices) {
		detail::placementCancellationCheckpoint();
		existingVertices.insert(vertex.id);
		if (vertices.contains(vertex.id) && !referenced.contains(vertex.id)) {
			selection << LevelMapSelectionRef{LevelMapSelectionKind::DoomVertex, vertex.id};
		}
	}
	selection += things;
	if (!existingVertices.contains(vertices)) {
		return fail(text(QT_TRANSLATE_NOOP(
			"LevelDoomSelection",
			"Connected geometry contains a missing vertex. Repair its linedef references before expanding the selection.")));
	}
	if (!setLevelMapSelection(&candidate, selection, error)) {
		return false;
	}
	detail::placementCancellationCheckpoint();
	*document = std::move(candidate);
	return true;
}
bool prepareLevelMapDoomMirrorLines(const LevelMapDocument& document, const QSet<int>& vertices, QVector<LevelMapDoomLinedef>* before,
									QVector<LevelMapDoomLinedef>* after, QString* error) {
	QVector<LevelMapDoomLinedef> source, result;
	if (error) {
		error->clear();
	}
	if (!before || !after) {
		return false;
	}
	for (const auto& line : document.doomLinedefs) {
		detail::placementCancellationCheckpoint();
		const bool start = vertices.contains(line.startVertex), end = vertices.contains(line.endVertex);
		if (start != end) {
			if (error) {
				*error = text(QT_TRANSLATE_NOOP("LevelDoomSelection", "The selection is attached to unselected geometry at linedef %1. Use "
																	  "Select Connected Geometry before mirroring."))
							 .arg(line.id);
			}
			return false;
		}
		if (!start) {
			continue;
		}
		// Workflow reference: Ultimate Doom Builder EditSelectionMode.FlipLinedefs,
		// 6d9f6038db30adfee0edd74221b74b2de4837f6f, GPL-3.0; copyright Pascal vd
		// Heiden (2007). A detached reflection reverses endpoints while retaining
		// side ownership. No C# implementation copied; details in docs/CREDITS.md.
		auto reflected = line;
		std::swap(reflected.startVertex, reflected.endVertex);
		source << line;
		result << reflected;
	}
	*before = std::move(source);
	*after = std::move(result);
	return true;
}
} // namespace vibestudio
