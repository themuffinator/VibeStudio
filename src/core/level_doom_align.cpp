#include "core/level_doom_align.h"

#include <QCoreApplication>
#include <QMultiHash>
#include <QPointF>

#include <algorithm>
#include <cmath>
#include <numbers>

// The walk and the offsets follow Ultimate Doom Builder's auto-align
// (https://github.com/UltimateDoomBuilder/UltimateDoomBuilder, GPL-3.0,
// BuilderModes VisualModes "Auto-align textures"), and the vertical rule the
// Doom renderer's pegging in linuxdoom-1.10 r_segs.c (GPL-2.0 source release).
// Behaviour only; no code is copied.

namespace vibestudio {

namespace {

constexpr int kUpperUnpegged = 8;
constexpr int kLowerUnpegged = 16;

bool fail(QString* error, const char* text)
{
	if (error) {
		*error = QCoreApplication::translate("LevelDoomAlign", text);
	}
	return false;
}

const QString& partTexture(const LevelMapDoomSidedef& side, LevelDoomWallPart part)
{
	switch (part) {
	case LevelDoomWallPart::Upper:
		return side.upperTexture;
	case LevelDoomWallPart::Lower:
		return side.lowerTexture;
	case LevelDoomWallPart::Middle:
		break;
	}
	return side.middleTexture;
}

bool shows(const QString& texture)
{
	const QString name = texture.trimmed();
	return !name.isEmpty() && name != QStringLiteral("-");
}

// One side of a linedef as a wall: its texture runs from `from` to `to`.
struct Wall {
	int line = -1;
	int side = -1;
	int other = -1;
	int from = -1;
	int to = -1;
	int flags = 0;
};

} // namespace

bool levelDoomSideTexturedPart(const LevelMapDocument& document, int sidedef, LevelDoomWallPart* part)
{
	for (const LevelMapDoomSidedef& side : document.doomSidedefs) {
		if (side.id != sidedef) {
			continue;
		}
		for (const LevelDoomWallPart each : {LevelDoomWallPart::Middle, LevelDoomWallPart::Upper, LevelDoomWallPart::Lower}) {
			if (shows(partTexture(side, each))) {
				if (part) {
					*part = each;
				}
				return true;
			}
		}
		return false;
	}
	return false;
}

bool alignLevelMapDoomWallTextures(LevelMapDocument* document, const LevelDoomAlignRequest& request, int* aligned, QString* error)
{
	if (error) {
		error->clear();
	}
	if (aligned) {
		*aligned = 0;
	}
	if (!document || document->format != LevelMapFormat::DoomWad) {
		return fail(error, QT_TRANSLATE_NOOP("LevelDoomAlign", "Align wall textures on a Doom or Hexen map."));
	}
	if (document->doomUdmf) {
		return fail(error, QT_TRANSLATE_NOOP("LevelDoomAlign", "Use the UDMF property editor to set this map's texture offsets."));
	}
	if (!request.alignX && !request.alignY) {
		return fail(error, QT_TRANSLATE_NOOP("LevelDoomAlign", "Choose X, Y or both to align."));
	}
	QHash<int, const LevelMapDoomSidedef*> sides;
	for (const LevelMapDoomSidedef& side : document->doomSidedefs) {
		sides.insert(side.id, &side);
	}
	QHash<int, const LevelMapDoomSector*> sectors;
	for (const LevelMapDoomSector& sector : document->doomSectors) {
		sectors.insert(sector.id, &sector);
	}
	QHash<int, QPointF> vertices;
	for (const LevelMapDoomVertex& vertex : document->doomVertices) {
		vertices.insert(vertex.id, {vertex.x, vertex.y});
	}
	QVector<Wall> walls;
	QMultiHash<int, int> leaving;
	int start = -1;
	for (const LevelMapDoomLinedef& line : document->doomLinedefs) {
		for (const bool front : {true, false}) {
			const int side = front ? line.frontSidedef : line.backSidedef;
			if (!sides.contains(side)) {
				continue;
			}
			const Wall wall {line.id, side, front ? line.backSidedef : line.frontSidedef, front ? line.startVertex : line.endVertex,
				front ? line.endVertex : line.startVertex, line.flags};
			if (side == request.sidedef) {
				start = static_cast<int>(walls.size());
			}
			leaving.insert(wall.from, static_cast<int>(walls.size()));
			walls.push_back(wall);
		}
	}
	if (start < 0) {
		return fail(error, QT_TRANSLATE_NOOP("LevelDoomAlign", "Choose a wall: its side was not found."));
	}
	const QString texture = partTexture(*sides.value(request.sidedef), request.part).trimmed();
	if (!shows(texture)) {
		return fail(error, QT_TRANSLATE_NOOP("LevelDoomAlign", "That part of the wall shows no texture."));
	}
	const auto length = [&vertices](const Wall& wall) {
		const QPointF a = vertices.value(wall.from);
		const QPointF b = vertices.value(wall.to);
		return std::hypot(b.x() - a.x(), b.y() - a.y());
	};
	const auto heading = [&vertices](const Wall& wall) {
		const QPointF a = vertices.value(wall.from);
		const QPointF b = vertices.value(wall.to);
		return std::atan2(b.y() - a.y(), b.x() - a.x());
	};
	// Where the texture's top row would be, as a height; see the header.
	const auto anchor = [&](const Wall& wall) {
		const LevelMapDoomSidedef* own = sides.value(wall.side);
		const LevelMapDoomSidedef* far = sides.value(wall.other);
		const LevelMapDoomSector* front = own ? sectors.value(own->sector) : nullptr;
		const LevelMapDoomSector* back = far ? sectors.value(far->sector) : nullptr;
		if (!front) {
			return 0.0;
		}
		switch (request.part) {
		case LevelDoomWallPart::Upper:
			return back && !(wall.flags & kUpperUnpegged) ? back->ceilingHeight : front->ceilingHeight;
		case LevelDoomWallPart::Lower:
			if (!back) {
				return front->floorHeight;
			}
			return wall.flags & kLowerUnpegged ? front->ceilingHeight : back->floorHeight;
		case LevelDoomWallPart::Middle:
			break;
		}
		if (!back) {
			return wall.flags & kLowerUnpegged ? front->floorHeight : front->ceilingHeight;
		}
		return wall.flags & kLowerUnpegged ? std::max(front->floorHeight, back->floorHeight)
										   : std::min(front->ceilingHeight, back->ceilingHeight);
	};
	const auto eligible = [&](const Wall& wall, const QSet<int>& visitedLines) {
		const LevelMapDoomSidedef* side = sides.value(wall.side);
		return side && !visitedLines.contains(wall.line) && (request.within.isEmpty() || request.within.contains(wall.line))
			&& partTexture(*side, request.part).trimmed().compare(texture, Qt::CaseInsensitive) == 0;
	};
	// At a junction the wall turning least goes on.
	const auto straightest = [&](const QList<int>& candidates, const Wall& from, const QSet<int>& visitedLines, bool forward) {
		int best = -1;
		double bestTurn = 0.0;
		for (int index : candidates) {
			const Wall& wall = walls.at(index);
			if (!eligible(wall, visitedLines)) {
				continue;
			}
			double turn = std::remainder(forward ? heading(wall) - heading(from) : heading(from) - heading(wall), 2.0 * std::numbers::pi);
			turn = std::abs(turn);
			if (best < 0 || turn < bestTurn) {
				best = index;
				bestTurn = turn;
			}
		}
		return best;
	};
	struct Step {
		int side = -1;
		double offsetX = 0.0;
		double offsetY = 0.0;
	};
	const Wall& first = walls.at(start);
	const LevelMapDoomSidedef* origin = sides.value(first.side);
	const double startAnchor = anchor(first);
	QVector<Step> steps;
	QSet<int> visited {first.line};
	// Forward: walls leaving where the last one ends.
	double offset = origin->offsetX + length(first);
	int at = start;
	while (true) {
		const int next = straightest(leaving.values(walls.at(at).to), walls.at(at), visited, true);
		if (next < 0) {
			break;
		}
		const Wall& wall = walls.at(next);
		visited.insert(wall.line);
		steps.push_back({wall.side, offset, origin->offsetY + startAnchor - anchor(wall)});
		offset += length(wall);
		at = next;
	}
	// Back: walls ending where the first one starts.
	offset = origin->offsetX;
	at = start;
	while (true) {
		QList<int> arriving;
		for (int index = 0; index < walls.size(); ++index) {
			if (walls.at(index).to == walls.at(at).from) {
				arriving << index;
			}
		}
		const int previous = straightest(arriving, walls.at(at), visited, false);
		if (previous < 0) {
			break;
		}
		const Wall& wall = walls.at(previous);
		visited.insert(wall.line);
		offset -= length(wall);
		steps.push_back({wall.side, offset, origin->offsetY + startAnchor - anchor(wall)});
		at = previous;
	}
	if (steps.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelDoomAlign", "No wall joined to this one shows the same texture."));
	}
	const int width = request.widths.value(texture.toUpper(), 0);
	const auto wrapped = [width](double value) {
		int whole = static_cast<int>(std::lround(value));
		if (width > 0) {
			whole = ((whole % width) + width) % width;
		} else if (whole > 32767 || whole < -32768) {
			whole = ((whole % 4096) + 4096) % 4096;
		}
		return whole;
	};
	int done = 0;
	int changed = 0;
	const auto rollBack = [document, &done]() {
		for (; done > 0; --done) {
			undoLevelMapEdit(document);
		}
		document->redoStack.clear();
	};
	for (const Step& step : std::as_const(steps)) {
		const LevelMapDoomSidedef* side = sides.value(step.side);
		const int x = wrapped(step.offsetX);
		const int y = static_cast<int>(std::lround(step.offsetY));
		bool touched = false;
		// Each set may move the sidedef records, so the current values are read first.
		const int oldX = static_cast<int>(std::lround(side->offsetX));
		const int oldY = static_cast<int>(std::lround(side->offsetY));
		if (request.alignX && x != oldX) {
			if (!setLevelMapSidedefProperty(document, step.side, QStringLiteral("offsetx"), QString::number(x), error)) {
				rollBack();
				return false;
			}
			++done;
			touched = true;
		}
		if (request.alignY && y != oldY) {
			if (!setLevelMapSidedefProperty(document, step.side, QStringLiteral("offsety"), QString::number(y), error)) {
				rollBack();
				return false;
			}
			++done;
			touched = true;
		}
		changed += touched ? 1 : 0;
		// The sidedef vector may have been copied on write; look the sides up again.
		sides.clear();
		for (const LevelMapDoomSidedef& each : document->doomSidedefs) {
			sides.insert(each.id, &each);
		}
	}
	if (done > 0) {
		collapseLevelMapUndoSteps(document, done,
			QCoreApplication::translate("LevelDoomAlign", "Align %1 along %n walls", nullptr, changed).arg(texture),
			QCoreApplication::translate("LevelDoomAlign", "Restore the offsets of %1", nullptr).arg(texture));
	}
	if (aligned) {
		*aligned = changed;
	}
	return true;
}

} // namespace vibestudio
