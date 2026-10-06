// Layout, geometry, entities, and the map writers of the level generator
// (core/level_generation.h).

#include "core/level_generation.h"

#include "core/level_document.h"
#include "core/level_generation_p.h"
#include "core/level_map.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <QQueue>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <functional>
#include <queue>

namespace vibestudio {

using namespace levelgen;

namespace {

constexpr int kCellUnits = 32;
constexpr int kStepRise = 16;
constexpr int kLevelUnits = 64;

struct GameRules {
	int corridorWidth = 3;
	int corridorHeight = 128;
	int roomHeightBonus = 0;
	// Highest step a player walks up.
	int maxStep = 18;
	// Origin height above the floor for players and things.
	int spawnLift = 24;
	int itemLift = 24;
	int lightScale = 100;
};

GameRules rulesFor(const QString& game)
{
	GameRules rules;
	if (game == QStringLiteral("quake3")) {
		rules.corridorWidth = 4;
		rules.corridorHeight = 160;
		rules.roomHeightBonus = 64;
		rules.spawnLift = 32;
		rules.itemLift = 16;
		rules.lightScale = 220;
	} else if (game == QStringLiteral("doom")) {
		rules.maxStep = 24;
		rules.spawnLift = 0;
		rules.itemLift = 0;
	}
	return rules;
}

int minX(const QRect& rect)
{
	return rect.x();
}
int maxX(const QRect& rect)
{
	return rect.x() + rect.width() - 1;
}
int minY(const QRect& rect)
{
	return rect.y();
}
int maxY(const QRect& rect)
{
	return rect.y() + rect.height() - 1;
}
QRect grown(const QRect& rect, int by)
{
	return QRect(rect.x() - by, rect.y() - by, rect.width() + 2 * by, rect.height() + 2 * by);
}

struct ThemeTextures {
	QString wall;
	QString floor;
	QString ceiling;
	QString trim;
	QString water;
	QString slime;
	QString lava;
	QString trigger;
	QString caulk;
	QString exitFloor;

	[[nodiscard]] QString liquid(const QString& kind) const
	{
		return kind == QStringLiteral("lava") ? lava : kind == QStringLiteral("slime") ? slime : water;
	}
};

ThemeTextures defaultTextures(const QString& game, const QString& theme)
{
	// The games' own texture names, which need the game's textures to show:
	// worldspawn's WAD for Quake, pak0's textures/ for Quake II and III, the
	// IWAD for Doom. Projects can pass their own (availableTextures, textures).
	ThemeTextures t;
	if (game == QStringLiteral("quake")) {
		t.water = QStringLiteral("*water0");
		t.slime = QStringLiteral("*slime0");
		t.lava = QStringLiteral("*lava1");
		t.trigger = QStringLiteral("trigger");
		if (theme == QStringLiteral("medieval")) {
			t.wall = QStringLiteral("wbrick1_5"), t.floor = QStringLiteral("city4_6"), t.ceiling = QStringLiteral("city2_3"), t.trim = QStringLiteral("wizmet1_2");
		} else if (theme == QStringLiteral("metal")) {
			t.wall = QStringLiteral("metal4_2"), t.floor = QStringLiteral("metal1_2"), t.ceiling = QStringLiteral("metal5_1"), t.trim = QStringLiteral("metal6_1");
		} else if (theme == QStringLiteral("hell")) {
			t.wall = QStringLiteral("rock3_8"), t.floor = QStringLiteral("rock4_1"), t.ceiling = QStringLiteral("rock1_2"), t.trim = QStringLiteral("wizmet1_2");
		} else if (theme == QStringLiteral("cave")) {
			t.wall = QStringLiteral("rock1_2"), t.floor = QStringLiteral("rock3_2"), t.ceiling = QStringLiteral("rock4_1"), t.trim = QStringLiteral("rock5_2");
		} else {
			t.wall = QStringLiteral("tech06_1"), t.floor = QStringLiteral("floor01_5"), t.ceiling = QStringLiteral("tech03_1"), t.trim = QStringLiteral("metal5_1");
		}
		t.exitFloor = t.trim;
	} else if (game == QStringLiteral("quake2")) {
		t.wall = QStringLiteral("e1u1/metal2_2");
		t.floor = QStringLiteral("e1u1/floor1_3");
		t.ceiling = QStringLiteral("e1u1/ceil1_1");
		t.trim = QStringLiteral("e1u1/grate1_3");
		t.water = t.slime = t.lava = QStringLiteral("e1u1/water1_8");
		t.trigger = QStringLiteral("e1u1/trigger");
		t.exitFloor = t.trim;
	} else if (game == QStringLiteral("quake3")) {
		const bool gothic = theme == QStringLiteral("medieval") || theme == QStringLiteral("hell") || theme == QStringLiteral("cave");
		t.wall = gothic ? QStringLiteral("gothic_block/blocks18c") : QStringLiteral("base_wall/concrete");
		t.floor = gothic ? QStringLiteral("gothic_floor/largerblock3b") : QStringLiteral("base_floor/diamond2c");
		t.ceiling = t.wall;
		t.trim = gothic ? QStringLiteral("gothic_trim/baseboard09_e") : QStringLiteral("base_floor/diamond2c");
		t.water = QStringLiteral("liquids/clear_calm1");
		t.slime = QStringLiteral("liquids/slime1");
		t.lava = QStringLiteral("liquids/lavahell");
		t.trigger = QStringLiteral("common/trigger");
		t.caulk = QStringLiteral("common/caulk");
		t.exitFloor = t.trim;
	} else {
		t.water = QStringLiteral("FWATER1");
		t.slime = QStringLiteral("NUKAGE1");
		t.lava = QStringLiteral("LAVA1");
		t.exitFloor = QStringLiteral("FLOOR1_6");
		if (theme == QStringLiteral("medieval")) {
			t.wall = QStringLiteral("STONE2"), t.floor = QStringLiteral("FLOOR7_1"), t.ceiling = QStringLiteral("CEIL5_1");
		} else if (theme == QStringLiteral("metal")) {
			t.wall = QStringLiteral("GRAY1"), t.floor = QStringLiteral("FLOOR5_1"), t.ceiling = QStringLiteral("CEIL3_5");
		} else if (theme == QStringLiteral("hell")) {
			t.wall = QStringLiteral("SP_ROCK1"), t.floor = QStringLiteral("FLOOR6_1"), t.ceiling = QStringLiteral("CEIL5_2");
		} else if (theme == QStringLiteral("cave")) {
			t.wall = QStringLiteral("BROWN1"), t.floor = QStringLiteral("FLAT10"), t.ceiling = QStringLiteral("CEIL5_2");
		} else {
			t.wall = QStringLiteral("STARTAN3"), t.floor = QStringLiteral("FLOOR4_8"), t.ceiling = QStringLiteral("CEIL3_5");
		}
		t.trim = t.wall;
	}
	return t;
}

// The project texture best matching a role, by words in its name.
QString matchedTexture(const QString& current, const QStringList& available, const QStringList& words, bool liquid)
{
	if (available.isEmpty()) {
		return current;
	}
	for (const QString& name : available) {
		if (name.compare(current, Qt::CaseInsensitive) == 0) {
			return name;
		}
	}
	QString best;
	int bestScore = 0;
	QStringList sorted = available;
	sorted.sort(Qt::CaseInsensitive);
	for (const QString& name : sorted) {
		const QString lower = name.toLower();
		const bool isLiquid = lower.startsWith(QLatin1Char('*')) || lower.contains(QStringLiteral("liquid"));
		if (isLiquid != liquid || lower.startsWith(QStringLiteral("sky")) || lower.contains(QStringLiteral("clip")) || lower.contains(QStringLiteral("trigger"))
			|| lower.startsWith(QStringLiteral("common/"))) {
			continue;
		}
		int score = 1;
		for (const QString& word : words) {
			if (lower.contains(word)) {
				score += 10;
			}
		}
		if (score > bestScore) {
			bestScore = score;
			best = name;
		}
	}
	return best.isEmpty() ? current : best;
}

ThemeTextures texturesFor(const LevelGenerationSpec& spec)
{
	ThemeTextures t = defaultTextures(spec.game, spec.theme);
	const QStringList& available = spec.availableTextures;
	// Doom's wall textures are TEXTURE1 composites, not lumps a package lists,
	// so a project's flats only stand in for floors, ceilings and liquids.
	const QStringList walls = spec.game == QStringLiteral("doom") ? QStringList() : available;
	t.wall = matchedTexture(t.wall, walls, {QStringLiteral("wall"), QStringLiteral("brick"), QStringLiteral("stone"), QStringLiteral("tech"), QStringLiteral("rock"), QStringLiteral("block")}, false);
	t.floor = matchedTexture(t.floor, available, {QStringLiteral("floor"), QStringLiteral("ground"), QStringLiteral("tile"), QStringLiteral("grate"), QStringLiteral("flat")}, false);
	t.ceiling = matchedTexture(t.ceiling, available, {QStringLiteral("ceil"), QStringLiteral("roof"), QStringLiteral("metal"), QStringLiteral("tech")}, false);
	t.trim = matchedTexture(t.trim, walls, {QStringLiteral("trim"), QStringLiteral("metal"), QStringLiteral("step"), QStringLiteral("plat")}, false);
	t.water = matchedTexture(t.water, available, {QStringLiteral("water")}, true);
	t.slime = matchedTexture(t.slime, available, {QStringLiteral("slime"), QStringLiteral("nukage")}, true);
	t.lava = matchedTexture(t.lava, available, {QStringLiteral("lava")}, true);
	const auto override = [&spec](QString* field, const QString& role) {
		const QString value = spec.textures.value(role).trimmed();
		if (!value.isEmpty()) {
			*field = value;
		}
	};
	override(&t.wall, QStringLiteral("wall"));
	override(&t.floor, QStringLiteral("floor"));
	override(&t.ceiling, QStringLiteral("ceiling"));
	override(&t.trim, QStringLiteral("trim"));
	if (!spec.textures.value(QStringLiteral("liquid")).trimmed().isEmpty()) {
		t.water = t.slime = t.lava = spec.textures.value(QStringLiteral("liquid")).trimmed();
	}
	return t;
}

// ---------------------------------------------------------------------------
// Layout in plan space (cells; y grows north).

struct Corridor {
	int from = -1;
	int to = -1;
	QVector<QPoint> centers;
	QVector<int> floors;
	int width = 3;
	// Its cells, each with the floor of the first centre that covers it.
	QHash<QPoint, int> cells;
};

struct RoomBox {
	int plan = -1;
	QRect rect;
	int floor = 0;
	int ceiling = 0;
	bool placed = false;
};

// The footprint of a corridor centre: `width` cells square.
QRect footprint(QPoint centre, int width)
{
	const int low = (width - 1) / 2;
	return QRect(centre.x() - low, centre.y() - low, width, width);
}

class Occupancy {
public:
	void addRect(const QRect& rect, int owner)
	{
		for (int y = minY(rect); y <= maxY(rect); ++y) {
			for (int x = minX(rect); x <= maxX(rect); ++x) {
				m_cells.insert(QPoint(x, y), owner);
			}
		}
	}
	void addCell(QPoint cell, int owner)
	{
		m_cells.insert(cell, owner);
	}
	// The owner of what lies within `distance` cells (Chebyshev), or -1.
	[[nodiscard]] bool near(QPoint cell, int distance, const QSet<int>& except) const
	{
		for (int dy = -distance; dy <= distance; ++dy) {
			for (int dx = -distance; dx <= distance; ++dx) {
				const auto found = m_cells.constFind(QPoint(cell.x() + dx, cell.y() + dy));
				if (found != m_cells.constEnd() && !except.contains(found.value())) {
					return true;
				}
			}
		}
		return false;
	}
	[[nodiscard]] bool occupied(QPoint cell) const
	{
		return m_cells.contains(cell);
	}
	[[nodiscard]] int owner(QPoint cell) const
	{
		return m_cells.value(cell, -1);
	}
	[[nodiscard]] QRect bounds() const
	{
		QRect box;
		for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
			box = box.isNull() ? QRect(it.key(), QSize(1, 1)) : box.united(QRect(it.key(), QSize(1, 1)));
		}
		return box;
	}

private:
	QHash<QPoint, int> m_cells;
};

// Owners: rooms are their index, corridors 1000 + their index.
constexpr int kCorridorOwner = 1000;

QSize roomSize(const LevelPlanRoom& room, LevelRandom* random)
{
	int low = 9;
	int high = 12;
	if (room.size == QStringLiteral("small")) {
		low = 6, high = 8;
	} else if (room.size == QStringLiteral("large")) {
		low = 13, high = 16;
	}
	int width = random->range(low, high);
	int height = random->range(low, high);
	if (room.role == QStringLiteral("hall")) {
		// Long and narrow, either way.
		if (random->chance(0.5)) {
			width = std::max(6, width * 2 / 3), height = std::min(20, height * 3 / 2);
		} else {
			height = std::max(6, height * 2 / 3), width = std::min(20, width * 3 / 2);
		}
	}
	if (room.shape == QStringLiteral("pit") || room.shape == QStringLiteral("platform")) {
		width = std::max(width, 10);
		height = std::max(height, 10);
	}
	return QSize(width, height);
}

// Where centres may climb: the middle of five centres in a straight line.
QVector<int> stairPositions(const QVector<QPoint>& centres)
{
	QVector<int> positions;
	for (int index = 2; index + 2 < centres.size(); ++index) {
		const QPoint before = centres[index] - centres[index - 1];
		bool straight = true;
		for (int offset = -1; offset <= 2 && straight; ++offset) {
			straight = centres[index + offset] - centres[index + offset - 1] == before;
		}
		if (straight) {
			positions << index;
		}
	}
	return positions;
}

// Floors for each centre climbing `rise` (a multiple of the step rise) in
// steps placed as one flight in the longest straight stretch, else spread.
bool corridorFloors(const QVector<QPoint>& centres, int start, int rise, QVector<int>* floors)
{
	const int steps = std::abs(rise) / kStepRise;
	floors->fill(start, centres.size());
	if (steps == 0) {
		return true;
	}
	const QVector<int> positions = stairPositions(centres);
	if (positions.size() < steps) {
		return false;
	}
	// The run of consecutive positions that can hold the flight, centred.
	QVector<int> chosen;
	int runStart = 0;
	for (int index = 1; index <= positions.size(); ++index) {
		if (index == positions.size() || positions[index] != positions[index - 1] + 1) {
			if (index - runStart >= steps) {
				const int first = runStart + (index - runStart - steps) / 2;
				for (int step = 0; step < steps; ++step) {
					chosen << positions[first + step];
				}
				break;
			}
			runStart = index;
		}
	}
	if (chosen.isEmpty()) {
		// Landings between shorter flights.
		for (int step = 0; step < steps; ++step) {
			chosen << positions[step * positions.size() / steps];
		}
	}
	const int direction = rise > 0 ? kStepRise : -kStepRise;
	int floor = start;
	int next = 0;
	for (int index = 0; index < centres.size(); ++index) {
		if (next < chosen.size() && chosen[next] == index) {
			floor += direction;
			++next;
		}
		(*floors)[index] = floor;
	}
	return floor == start + rise;
}

void fillCorridorCells(Corridor* corridor)
{
	corridor->cells.clear();
	for (int index = 0; index < corridor->centers.size(); ++index) {
		const QRect area = footprint(corridor->centers[index], corridor->width);
		for (int y = minY(area); y <= maxY(area); ++y) {
			for (int x = minX(area); x <= maxX(area); ++x) {
				if (!corridor->cells.contains(QPoint(x, y))) {
					corridor->cells.insert(QPoint(x, y), corridor->floors[index]);
				}
			}
		}
	}
}

enum class Side { East, West, North, South };

QPoint sideStep(Side side)
{
	switch (side) {
	case Side::East:
		return QPoint(1, 0);
	case Side::West:
		return QPoint(-1, 0);
	case Side::North:
		return QPoint(0, 1);
	case Side::South:
		return QPoint(0, -1);
	}
	return QPoint(1, 0);
}

// The first corridor centre outside a room's side, its footprint touching
// the wall, at `along` across the side.
QPoint doorCentre(const QRect& room, Side side, int along, int width)
{
	const int low = (width - 1) / 2;
	const int high = width - 1 - low;
	switch (side) {
	case Side::East:
		return QPoint(maxX(room) + 1 + low, along);
	case Side::West:
		return QPoint(minX(room) - 1 - high, along);
	case Side::North:
		return QPoint(along, maxY(room) + 1 + low);
	case Side::South:
		return QPoint(along, minY(room) - 1 - high);
	}
	return QPoint();
}

// Across positions on a side where a corridor of `width` fits, with a cell
// spare at each corner.
QPair<int, int> doorSpan(const QRect& room, Side side, int width)
{
	const int low = (width - 1) / 2;
	const int high = width - 1 - low;
	const bool vertical = side == Side::East || side == Side::West;
	const int first = (vertical ? minY(room) : minX(room)) + 1 + low;
	const int last = (vertical ? maxY(room) : maxX(room)) - 1 - high;
	return {first, last};
}

// Whether a corridor centre's footprint is free, with one solid cell between
// it and anything else: anything but the rooms in `except`, which only the
// corridor's end centres may touch.
bool corridorCentreClear(const Occupancy& occupancy, QPoint centre, int width, const QSet<int>& except)
{
	const QRect area = footprint(centre, width);
	for (int y = minY(area); y <= maxY(area); ++y) {
		for (int x = minX(area); x <= maxX(area); ++x) {
			const QPoint cell(x, y);
			if (occupancy.occupied(cell) || occupancy.near(cell, 1, except)) {
				return false;
			}
		}
	}
	return true;
}

// A* over corridor centres from one room's door to another's.
QVector<QPoint> routeCorridor(const Occupancy& occupancy, const QRect& bounds, QPoint start, QPoint goal, int width, int roomA, int roomB)
{
	const QSet<int> none;
	const QSet<int> ends {roomA, roomB};
	const auto valid = [&](QPoint centre, bool end) { return bounds.contains(centre) && corridorCentreClear(occupancy, centre, width, end ? ends : none); };
	if (!valid(start, true) || !valid(goal, true)) {
		return {};
	}
	struct Node {
		int cost;
		int estimate;
		QPoint point;
		int direction;
		bool operator>(const Node& other) const
		{
			return cost + estimate > other.cost + other.estimate;
		}
	};
	// State: point and direction of arrival, so turns can cost.
	const auto key = [](QPoint point, int direction) { return qint64(point.x() + 100000) * 1000000 + qint64(point.y() + 100000) * 5 + direction; };
	std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
	QHash<qint64, int> best;
	QHash<qint64, qint64> parent;
	QHash<qint64, QPoint> pointOf;
	const auto heuristic = [goal](QPoint point) { return std::abs(point.x() - goal.x()) + std::abs(point.y() - goal.y()); };
	open.push({0, heuristic(start), start, 4});
	best.insert(key(start, 4), 0);
	pointOf.insert(key(start, 4), start);
	static const QPoint moves[4] = {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)};
	int expanded = 0;
	qint64 found = -1;
	while (!open.empty() && expanded < 60000) {
		const Node node = open.top();
		open.pop();
		const qint64 nodeKey = key(node.point, node.direction);
		if (best.value(nodeKey, INT_MAX) < node.cost) {
			continue;
		}
		++expanded;
		if (node.point == goal) {
			found = nodeKey;
			break;
		}
		for (int direction = 0; direction < 4; ++direction) {
			const QPoint next = node.point + moves[direction];
			if (!valid(next, next == goal)) {
				continue;
			}
			// Turns cost: straight runs read better and leave room for stairs.
			const int cost = node.cost + 1 + (node.direction != 4 && node.direction != direction ? 6 : 0);
			const qint64 nextKey = key(next, direction);
			if (cost < best.value(nextKey, INT_MAX)) {
				best.insert(nextKey, cost);
				parent.insert(nextKey, nodeKey);
				pointOf.insert(nextKey, next);
				open.push({cost, heuristic(next), next, direction});
			}
		}
	}
	if (found < 0) {
		return {};
	}
	QVector<QPoint> path;
	for (qint64 at = found;; at = parent.value(at)) {
		path.prepend(pointOf.value(at));
		if (!parent.contains(at)) {
			break;
		}
	}
	return path;
}

struct LayoutWork {
	const LevelGenerationSpec* spec = nullptr;
	const LevelSemanticPlan* plan = nullptr;
	GameRules rules;
	QVector<RoomBox> rooms;
	QVector<Corridor> corridors;
	Occupancy occupancy;
	QStringList dropped;
	QStringList notes;
};

// A doorway straight through the short gap between two rooms on one floor
// that face each other: how neighbouring rooms join without a corridor.
bool tryDoorway(const LayoutWork& work, int a, int b, int width, Corridor* out)
{
	const RoomBox& first = work.rooms[a];
	const RoomBox& second = work.rooms[b];
	if (first.floor != second.floor) {
		return false;
	}
	QRect cells;
	for (int axis = 0; axis < 2 && cells.isNull(); ++axis) {
		const bool across = axis == 0;
		const QRect& low = (across ? minX(first.rect) < minX(second.rect) : minY(first.rect) < minY(second.rect)) ? first.rect : second.rect;
		const QRect& high = &low == &first.rect ? second.rect : first.rect;
		const int gapStart = (across ? maxX(low) : maxY(low)) + 1;
		const int gapEnd = (across ? minX(high) : minY(high)) - 1;
		if (gapEnd < gapStart || gapEnd - gapStart + 1 > 6) {
			continue;
		}
		const int spanFirst = std::max(across ? minY(low) : minX(low), across ? minY(high) : minX(high)) + 1;
		const int spanLast = std::min(across ? maxY(low) : maxX(low), across ? maxY(high) : maxX(high)) - 1;
		if (spanLast - spanFirst + 1 < width) {
			continue;
		}
		const int door = (spanFirst + spanLast + 1 - width) / 2;
		cells = across ? QRect(gapStart, door, gapEnd - gapStart + 1, width) : QRect(door, gapStart, width, gapEnd - gapStart + 1);
	}
	if (cells.isNull()) {
		return false;
	}
	const QSet<int> ends {a, b};
	for (int y = minY(cells); y <= maxY(cells); ++y) {
		for (int x = minX(cells); x <= maxX(cells); ++x) {
			if (work.occupancy.occupied(QPoint(x, y)) || work.occupancy.near(QPoint(x, y), 1, ends)) {
				return false;
			}
		}
	}
	out->from = a;
	out->to = b;
	out->width = width;
	out->cells.clear();
	for (int y = minY(cells); y <= maxY(cells); ++y) {
		for (int x = minX(cells); x <= maxX(cells); ++x) {
			out->cells.insert(QPoint(x, y), first.floor);
		}
	}
	out->centers = {cells.center()};
	out->floors = {first.floor};
	return true;
}

int roomFloor(const LevelPlanRoom& room)
{
	return room.level * kLevelUnits;
}

int roomCeiling(const LevelPlanRoom& room, const GameRules& rules)
{
	int height = levelForHeight(room.height) + rules.roomHeightBonus;
	if (room.shape == QStringLiteral("platform")) {
		height = std::max(height, 192 + rules.roomHeightBonus);
	}
	if (room.size == QStringLiteral("large") && room.height != QStringLiteral("low")) {
		height += 64;
	}
	return roomFloor(room) + height;
}

// A straight corridor from `parent`'s side to a room placed beyond it.
bool tryPlaceBeside(LayoutWork* work, int roomIndex, int parentIndex, Side side, QSize size, int width, LevelRandom* random, Corridor* corridorOut, QRect* rectOut)
{
	const RoomBox& parent = work->rooms[parentIndex];
	const LevelPlanRoom& planRoom = work->plan->rooms[work->rooms[roomIndex].plan];
	const int rise = roomFloor(planRoom) - parent.floor;
	const int steps = std::abs(rise) / kStepRise;
	// Centres between the doors; stairs need two straight centres either side.
	const int centres = std::max(steps + (steps > 0 ? 4 : 0), random->range(1, 4));
	const int low = (width - 1) / 2;
	const int high = width - 1 - low;
	const QPair<int, int> span = doorSpan(parent.rect, side, width);
	if (span.first > span.second) {
		return false;
	}
	const bool vertical = side == Side::East || side == Side::West;
	const int along = random->range(span.first, span.second);
	// The new room's position across the side keeps the door inside it.
	const int extent = vertical ? size.height() : size.width();
	const int acrossMin = along + high + 2 - extent;
	const int acrossMax = along - low - 2;
	if (acrossMin > acrossMax) {
		return false;
	}
	const int across = random->range(acrossMin, acrossMax);
	Corridor corridor;
	corridor.width = width;
	const QPoint step = sideStep(side);
	QPoint centre = doorCentre(parent.rect, side, along, width);
	for (int index = 0; index < centres; ++index) {
		corridor.centers << centre;
		centre += step;
	}
	const QPoint last = corridor.centers.last();
	QRect rect;
	switch (side) {
	case Side::East:
		rect = QRect(last.x() + high + 1, across, size.width(), size.height());
		break;
	case Side::West:
		rect = QRect(last.x() - low - size.width(), across, size.width(), size.height());
		break;
	case Side::North:
		rect = QRect(across, last.y() + high + 1, size.width(), size.height());
		break;
	case Side::South:
		rect = QRect(across, last.y() - low - size.height(), size.width(), size.height());
		break;
	}
	// Rooms keep five cells apart, the width a walled corridor needs, so later
	// links can run between them; a room's own corridor sets its distance
	// from its parent. One cell between a room and another's corridor.
	for (int other = 0; other < work->rooms.size(); ++other) {
		if (work->rooms[other].placed && grown(rect, other == parentIndex ? 2 : width + 2).intersects(work->rooms[other].rect)) {
			return false;
		}
	}
	for (int y = minY(rect) - 1; y <= maxY(rect) + 1; ++y) {
		for (int x = minX(rect) - 1; x <= maxX(rect) + 1; ++x) {
			if (work->occupancy.owner(QPoint(x, y)) >= kCorridorOwner) {
				return false;
			}
		}
	}
	if (!corridorFloors(corridor.centers, parent.floor, rise, &corridor.floors)) {
		return false;
	}
	fillCorridorCells(&corridor);
	const QSet<int> ends {parentIndex};
	for (auto it = corridor.cells.constBegin(); it != corridor.cells.constEnd(); ++it) {
		if (work->occupancy.occupied(it.key()) || rect.contains(it.key())) {
			return false;
		}
		// The corridor touches only its own two rooms, at its ends.
		const bool atEnd = footprint(corridor.centers.first(), width).contains(it.key()) || footprint(corridor.centers.last(), width).contains(it.key());
		if (work->occupancy.near(it.key(), 1, atEnd ? ends : QSet<int>())) {
			return false;
		}
		if (!atEnd && grown(rect, 1).contains(it.key())) {
			return false;
		}
	}
	*corridorOut = corridor;
	*rectOut = rect;
	return true;
}

void layoutRooms(LayoutWork* work, LevelRandom* random)
{
	const LevelSemanticPlan& plan = *work->plan;
	const int count = int(plan.rooms.size());
	work->rooms.resize(count);
	QVector<QSize> sizes(count);
	for (int index = 0; index < count; ++index) {
		work->rooms[index].plan = index;
		work->rooms[index].floor = roomFloor(plan.rooms[index]);
		work->rooms[index].ceiling = roomCeiling(plan.rooms[index], work->rules);
		sizes[index] = roomSize(plan.rooms[index], random);
	}
	QHash<QString, int> indexOf;
	for (int index = 0; index < count; ++index) {
		indexOf.insert(plan.rooms[index].id, index);
	}
	QVector<QVector<int>> neighbours(count);
	QVector<int> connectionWidth;
	for (const LevelPlanConnection& connection : plan.connections) {
		const int a = indexOf.value(connection.from, -1);
		const int b = indexOf.value(connection.to, -1);
		if (a >= 0 && b >= 0) {
			neighbours[a] << b;
			neighbours[b] << a;
		}
	}
	int start = 0;
	for (int index = 0; index < count; ++index) {
		if (plan.rooms[index].role == QStringLiteral("start")) {
			start = index;
			break;
		}
	}
	// Breadth first from the start, each room beside one already placed.
	QVector<int> order {start};
	QVector<bool> queued(count, false);
	queued[start] = true;
	for (int head = 0; head < order.size(); ++head) {
		for (int next : neighbours[order[head]]) {
			if (!queued[next]) {
				queued[next] = true;
				order << next;
			}
		}
	}
	work->rooms[start].rect = QRect(QPoint(0, 0), sizes[start]);
	work->rooms[start].placed = true;
	work->occupancy.addRect(work->rooms[start].rect, start);
	QSet<QPair<int, int>> treeLinks;
	for (int position = 1; position < order.size(); ++position) {
		const int room = order[position];
		bool placed = false;
		QVector<int> parents;
		for (int other : neighbours[room]) {
			if (work->rooms[other].placed && !parents.contains(other)) {
				parents << other;
			}
		}
		for (int parent : parents) {
			QString kind = QStringLiteral("corridor");
			for (const LevelPlanConnection& connection : plan.connections) {
				if ((indexOf.value(connection.from) == parent && indexOf.value(connection.to) == room)
					|| (indexOf.value(connection.from) == room && indexOf.value(connection.to) == parent)) {
					kind = connection.kind;
				}
			}
			const int width = work->rules.corridorWidth + (kind == QStringLiteral("open") ? 2 : 0);
			double bestScore = 1e18;
			Corridor bestCorridor;
			QRect bestRect;
			QVector<Side> sides {Side::East, Side::West, Side::North, Side::South};
			for (int attempt = 0; attempt < 48; ++attempt) {
				if (attempt % 4 == 0) {
					random->shuffle(&sides);
				}
				Corridor corridor;
				QRect rect;
				if (!tryPlaceBeside(work, room, parent, sides[attempt % 4], sizes[room], width, random, &corridor, &rect)) {
					continue;
				}
				// Keep the level compact and square: a long side costs more
				// than area, or each room just extends a line.
				QRect all = rect;
				for (const RoomBox& box : work->rooms) {
					if (box.placed) {
						all = all.united(box.rect);
					}
				}
				const double longest = std::max(all.width(), all.height());
				const double score = longest * longest + 0.25 * double(all.width()) * all.height() + random->range(0, 60);
				if (score < bestScore) {
					bestScore = score;
					bestCorridor = corridor;
					bestRect = rect;
				}
			}
			if (bestScore < 1e18) {
				bestCorridor.from = parent;
				bestCorridor.to = room;
				work->rooms[room].rect = bestRect;
				work->rooms[room].placed = true;
				work->occupancy.addRect(bestRect, room);
				const int corridorIndex = int(work->corridors.size());
				for (auto it = bestCorridor.cells.constBegin(); it != bestCorridor.cells.constEnd(); ++it) {
					work->occupancy.addCell(it.key(), kCorridorOwner + corridorIndex);
				}
				work->corridors << bestCorridor;
				treeLinks.insert({std::min(parent, room), std::max(parent, room)});
				placed = true;
				break;
			}
		}
		if (!placed) {
			work->dropped << QCoreApplication::translate("VibeStudioLevelGeneration", "Room %1 did not fit beside the rooms it joins and was left out.").arg(plan.rooms[room].id);
		}
	}
	for (int index = 0; index < count; ++index) {
		if (!queued[index]) {
			work->dropped << QCoreApplication::translate("VibeStudioLevelGeneration", "Room %1 is not linked to the start and was left out.").arg(plan.rooms[index].id);
		}
	}

	// The other links: routed around everything already there.
	for (const LevelPlanConnection& connection : plan.connections) {
		const int a = indexOf.value(connection.from, -1);
		const int b = indexOf.value(connection.to, -1);
		if (a < 0 || b < 0 || !work->rooms[a].placed || !work->rooms[b].placed || treeLinks.contains({std::min(a, b), std::max(a, b)})) {
			continue;
		}
		const int width = work->rules.corridorWidth;
		const RoomBox& from = work->rooms[a];
		const RoomBox& to = work->rooms[b];
		QRect bounds = grown(work->occupancy.bounds(), 12);
		struct Pair {
			Side fromSide;
			Side toSide;
			int distance;
		};
		QVector<Pair> pairs;
		const QVector<Side> sides {Side::East, Side::West, Side::North, Side::South};
		for (Side fromSide : sides) {
			for (Side toSide : sides) {
				const QPair<int, int> fromSpan = doorSpan(from.rect, fromSide, width);
				const QPair<int, int> toSpan = doorSpan(to.rect, toSide, width);
				if (fromSpan.first > fromSpan.second || toSpan.first > toSpan.second) {
					continue;
				}
				const QPoint p = doorCentre(from.rect, fromSide, (fromSpan.first + fromSpan.second) / 2, width);
				const QPoint q = doorCentre(to.rect, toSide, (toSpan.first + toSpan.second) / 2, width);
				pairs.push_back({fromSide, toSide, std::abs(p.x() - q.x()) + std::abs(p.y() - q.y())});
			}
		}
		std::stable_sort(pairs.begin(), pairs.end(), [](const Pair& left, const Pair& right) { return left.distance < right.distance; });
		bool routed = false;
		Corridor doorway;
		if (tryDoorway(*work, a, b, width, &doorway)) {
			const int corridorIndex = int(work->corridors.size());
			for (auto it = doorway.cells.constBegin(); it != doorway.cells.constEnd(); ++it) {
				work->occupancy.addCell(it.key(), kCorridorOwner + corridorIndex);
			}
			work->corridors << doorway;
			routed = true;
		}
		const QSet<int> ends {a, b};
		// The clear door on a side nearest a target: other corridors often
		// already leave by the obvious spot, so every spot along it is tried.
		const auto clearDoor = [&](const QRect& room, Side side, QPoint target, QPoint* door) {
			const QPair<int, int> span = doorSpan(room, side, width);
			const bool vertical = side == Side::East || side == Side::West;
			const int wanted = vertical ? target.y() : target.x();
			int best = INT_MAX;
			for (int along = span.first; along <= span.second; ++along) {
				const QPoint centre = doorCentre(room, side, along, width);
				if (std::abs(along - wanted) < best && bounds.contains(centre) && corridorCentreClear(work->occupancy, centre, width, ends)) {
					best = std::abs(along - wanted);
					*door = centre;
				}
			}
			return best != INT_MAX;
		};
		int searches = 0;
		for (int attempt = 0; attempt < pairs.size() && !routed && searches < 8; ++attempt) {
			const Pair& pair = pairs[attempt];
			QPoint start;
			QPoint goal;
			if (!clearDoor(from.rect, pair.fromSide, to.rect.center(), &start) || !clearDoor(to.rect, pair.toSide, from.rect.center(), &goal)) {
				continue;
			}
			{
				++searches;
				QVector<QPoint> path = routeCorridor(work->occupancy, bounds, start, goal, width, a, b);
				if (path.size() < 2) {
					continue;
				}
				Corridor corridor;
				corridor.from = a;
				corridor.to = b;
				corridor.width = width;
				corridor.centers = path;
				if (!corridorFloors(path, from.floor, to.floor - from.floor, &corridor.floors)) {
					continue;
				}
				fillCorridorCells(&corridor);
				const int corridorIndex = int(work->corridors.size());
				for (auto it = corridor.cells.constBegin(); it != corridor.cells.constEnd(); ++it) {
					work->occupancy.addCell(it.key(), kCorridorOwner + corridorIndex);
				}
				work->corridors << corridor;
				routed = true;
			}
		}
		if (!routed) {
			work->dropped << QCoreApplication::translate("VibeStudioLevelGeneration", "The link %1 to %2 could not be routed and was left out.").arg(connection.from, connection.to);
		}
	}
}

// ---------------------------------------------------------------------------
// Cells, features, and entities.

void stampLayout(LayoutWork* work, LevelLayout* layout, LevelRandom* random)
{
	const LevelSemanticPlan& plan = *work->plan;
	QRect bounds;
	for (const RoomBox& room : work->rooms) {
		if (room.placed) {
			bounds = bounds.isNull() ? room.rect : bounds.united(room.rect);
		}
	}
	for (const Corridor& corridor : work->corridors) {
		for (auto it = corridor.cells.constBegin(); it != corridor.cells.constEnd(); ++it) {
			bounds = bounds.united(QRect(it.key(), QSize(1, 1)));
		}
	}
	constexpr int pad = 2;
	layout->cellSize = kCellUnits;
	layout->width = bounds.width() + 2 * pad;
	layout->height = bounds.height() + 2 * pad;
	layout->cells = QVector<LevelCell>(layout->width * layout->height);
	const QPoint shift(pad - bounds.x(), pad - bounds.y());
	// Centre the map on the origin, on a 64-unit grid.
	layout->origin = QPoint(-(layout->width / 2) * kCellUnits, -(layout->height / 2) * kCellUnits);

	for (const RoomBox& room : work->rooms) {
		if (!room.placed) {
			continue;
		}
		const LevelPlanRoom& planRoom = plan.rooms[room.plan];
		LevelLayoutRoom out;
		out.id = planRoom.id;
		out.role = planRoom.role;
		out.rect = room.rect.translated(shift);
		out.floor = room.floor;
		out.ceiling = room.ceiling;
		out.shape = planRoom.shape;
		out.liquid = planRoom.liquid;
		out.lighting = planRoom.lighting;
		out.name = planRoom.name;
		for (int y = minY(out.rect); y <= maxY(out.rect); ++y) {
			for (int x = minX(out.rect); x <= maxX(out.rect); ++x) {
				LevelCell& cell = layout->cell(x, y);
				cell.open = true;
				cell.floor = room.floor;
				cell.ceiling = room.ceiling;
				cell.owner = out.id;
			}
		}
		layout->rooms << out;
	}
	for (int index = 0; index < work->corridors.size(); ++index) {
		const Corridor& corridor = work->corridors[index];
		LevelLayoutCorridor out;
		out.from = plan.rooms[work->rooms[corridor.from].plan].id;
		out.to = plan.rooms[work->rooms[corridor.to].plan].id;
		out.width = corridor.width;
		for (const QPoint& centre : corridor.centers) {
			out.path << centre + shift;
		}
		for (auto it = corridor.cells.constBegin(); it != corridor.cells.constEnd(); ++it) {
			const QPoint at = it.key() + shift;
			LevelCell& cell = layout->cell(at.x(), at.y());
			if (cell.open) {
				continue;
			}
			cell.open = true;
			cell.floor = it.value();
			cell.ceiling = it.value() + work->rules.corridorHeight;
			cell.owner = QStringLiteral("c:%1").arg(index);
		}
		layout->corridors << out;
	}

	// Features, kept clear of the doorways.
	for (const LevelLayoutRoom& room : layout->rooms) {
		QSet<QPoint> doorway;
		for (int y = minY(room.rect); y <= maxY(room.rect); ++y) {
			for (int x = minX(room.rect); x <= maxX(room.rect); ++x) {
				static const QPoint around[4] = {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)};
				for (const QPoint& step : around) {
					const QPoint next(x + step.x(), y + step.y());
					if (layout->contains(next.x(), next.y()) && layout->cell(next.x(), next.y()).owner.startsWith(QStringLiteral("c:"))) {
						doorway.insert(QPoint(x, y));
					}
				}
			}
		}
		const auto nearDoor = [&doorway](QPoint cell, int distance) {
			for (const QPoint& door : doorway) {
				if (std::abs(door.x() - cell.x()) <= distance && std::abs(door.y() - cell.y()) <= distance) {
					return true;
				}
			}
			return false;
		};
		const int width = room.rect.width();
		const int height = room.rect.height();
		if (room.shape == QStringLiteral("pillars") && std::min(width, height) >= 9) {
			const int spacing = std::min(width, height) >= 13 ? 5 : 4;
			for (int y = minY(room.rect) + 2; y + 1 <= maxY(room.rect) - 2; y += spacing) {
				for (int x = minX(room.rect) + 2; x + 1 <= maxX(room.rect) - 2; x += spacing) {
					if (nearDoor(QPoint(x, y), 2) || nearDoor(QPoint(x + 1, y + 1), 2)) {
						continue;
					}
					for (int py = y; py <= y + 1; ++py) {
						for (int px = x; px <= x + 1; ++px) {
							layout->cell(px, py).open = false;
							layout->cell(px, py).owner.clear();
						}
					}
				}
			}
		} else if (room.shape == QStringLiteral("pit") && std::min(width, height) >= 8) {
			const QRect pit = grown(room.rect, -2);
			const int depth = 64;
			for (int y = minY(pit); y <= maxY(pit); ++y) {
				for (int x = minX(pit); x <= maxX(pit); ++x) {
					LevelCell& cell = layout->cell(x, y);
					cell.floor = room.floor - depth;
					cell.liquid = room.liquid == QStringLiteral("none") ? QStringLiteral("water") : room.liquid;
					cell.liquidTop = room.floor - 8;
				}
			}
		} else if (room.shape == QStringLiteral("platform") && std::min(width, height) >= 9) {
			// A stepped dais, climbable from every side.
			QRect ring = grown(room.rect, -2);
			for (int step = 1; step <= 4 && ring.width() >= 2 && ring.height() >= 2; ++step) {
				for (int y = minY(ring); y <= maxY(ring); ++y) {
					for (int x = minX(ring); x <= maxX(ring); ++x) {
						layout->cell(x, y).floor = room.floor + step * kStepRise;
					}
				}
				ring = grown(ring, -1);
			}
		}
	}
	Q_UNUSED(random);
}

double angleTowards(QPointF from, QPointF to)
{
	const double degrees = std::atan2(to.y() - from.y(), to.x() - from.x()) * 180.0 / 3.14159265358979323846;
	const int snapped = int(std::lround(degrees / 45.0)) * 45;
	return double((snapped % 360 + 360) % 360);
}

QPointF cellCentre(const LevelLayout& layout, QPoint cell)
{
	return QPointF(layout.origin.x() + (cell.x() + 0.5) * layout.cellSize, layout.origin.y() + (cell.y() + 0.5) * layout.cellSize);
}

void placeEntities(const LayoutWork& work, LevelLayout* layout, LevelRandom* random)
{
	const LevelGenerationSpec& spec = *work.spec;
	const LevelSemanticPlan& plan = *work.plan;
	const GameRules& rules = work.rules;
	QHash<QString, int> roomIndex;
	for (int index = 0; index < layout->rooms.size(); ++index) {
		roomIndex.insert(layout->rooms[index].id, index);
	}
	QSet<QPoint> used;
	const auto clearance = [layout](QPoint cell) {
		// Distance to the nearest closed cell or level change, in cells.
		for (int distance = 1; distance <= 3; ++distance) {
			for (int dy = -distance; dy <= distance; ++dy) {
				for (int dx = -distance; dx <= distance; ++dx) {
					const QPoint other(cell.x() + dx, cell.y() + dy);
					if (!layout->contains(other.x(), other.y())) {
						return distance - 1;
					}
					const LevelCell& here = layout->cell(cell.x(), cell.y());
					const LevelCell& there = layout->cell(other.x(), other.y());
					if (!there.open || there.floor != here.floor || there.owner != here.owner) {
						return distance - 1;
					}
				}
			}
		}
		return 3;
	};
	// A free cell of the room's floor, far from what is already placed.
	const auto chooseCell = [&](const LevelLayoutRoom& room, int wanted) {
		QVector<QPoint> candidates;
		for (int y = minY(room.rect); y <= maxY(room.rect); ++y) {
			for (int x = minX(room.rect); x <= maxX(room.rect); ++x) {
				const LevelCell& cell = layout->cell(x, y);
				if (cell.open && cell.liquid.isEmpty() && !used.contains(QPoint(x, y)) && clearance(QPoint(x, y)) >= wanted) {
					candidates << QPoint(x, y);
				}
			}
		}
		if (candidates.isEmpty() && wanted > 0) {
			for (int y = minY(room.rect); y <= maxY(room.rect); ++y) {
				for (int x = minX(room.rect); x <= maxX(room.rect); ++x) {
					const LevelCell& cell = layout->cell(x, y);
					if (cell.open && cell.liquid.isEmpty() && !used.contains(QPoint(x, y))) {
						candidates << QPoint(x, y);
					}
				}
			}
		}
		if (candidates.isEmpty()) {
			return QPoint(-1, -1);
		}
		QPoint best = candidates.first();
		double bestDistance = -1.0;
		for (const QPoint& candidate : candidates) {
			double nearest = 1e9;
			for (const QPoint& taken : used) {
				nearest = std::min(nearest, std::hypot(double(taken.x() - candidate.x()), double(taken.y() - candidate.y())));
			}
			nearest += random->range(0, 100) / 1000.0;
			if (nearest > bestDistance) {
				bestDistance = nearest;
				best = candidate;
			}
		}
		// Keep a cell around everything placed.
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				used.insert(QPoint(best.x() + dx, best.y() + dy));
			}
		}
		return best;
	};
	const auto doorOf = [layout](const LevelLayoutRoom& room) {
		for (const LevelLayoutCorridor& corridor : layout->corridors) {
			if (corridor.from == room.id && !corridor.path.isEmpty()) {
				return corridor.path.first();
			}
			if (corridor.to == room.id && !corridor.path.isEmpty()) {
				return corridor.path.last();
			}
		}
		return room.rect.center();
	};

	// Defaults when a placement leaves the item to the generator.
	const auto defaultItem = [&spec, random](const QString& kind) {
		const QStringList items = levelGenerationItemIds(spec.game, kind);
		if (items.isEmpty()) {
			return QString();
		}
		static const QHash<QString, QStringList> preferred = {
			{QStringLiteral("ammo"), {QStringLiteral("shells"), QStringLiteral("nails"), QStringLiteral("bullets"), QStringLiteral("clip")}},
			{QStringLiteral("health"), {QStringLiteral("health"), QStringLiteral("medikit"), QStringLiteral("stimpack")}},
			{QStringLiteral("armor"), {QStringLiteral("yellow"), QStringLiteral("combat"), QStringLiteral("green")}},
			{QStringLiteral("powerup"), {QStringLiteral("quad"), QStringLiteral("berserk")}},
			{QStringLiteral("monster"), {QStringLiteral("grunt"), QStringLiteral("guard"), QStringLiteral("imp")}},
			{QStringLiteral("weapon"), {QStringLiteral("supershotgun"), QStringLiteral("shotgun")}},
		};
		const QStringList wanted = preferred.value(kind);
		QStringList present;
		for (const QString& id : wanted) {
			if (items.contains(id)) {
				present << id;
			}
		}
		return present.isEmpty() ? items[random->range(0, int(items.size()) - 1)] : present[random->range(0, int(present.size()) - 1)];
	};

	for (const LevelPlanPlacement& placement : plan.placements) {
		const int index = roomIndex.value(placement.room, -1);
		if (index < 0) {
			continue;
		}
		const LevelLayoutRoom& room = layout->rooms[index];
		const QString item = placement.item.isEmpty() ? defaultItem(placement.kind) : placement.item;
		ItemEntry entry;
		if (!itemEntryFor(spec.game, placement.kind, item, &entry) && !itemEntryFor(spec.game, placement.kind, QString(), &entry)) {
			continue;
		}
		const bool big = placement.kind == QStringLiteral("monster")
			&& QStringList {QStringLiteral("shambler"), QStringLiteral("tank"), QStringLiteral("baron"), QStringLiteral("mancubus"), QStringLiteral("arachnotron"),
				   QStringLiteral("cyberdemon"), QStringLiteral("gladiator"), QStringLiteral("vore")}
				   .contains(item);
		const int count = placement.kind == QStringLiteral("exit") ? 1 : placement.count;
		for (int copy = 0; copy < count; ++copy) {
			const QPoint cell = chooseCell(room, placement.kind == QStringLiteral("exit") ? 1 : big ? 2 : 1);
			if (cell.x() < 0) {
				break;
			}
			LevelPlacedEntity entity;
			entity.kind = placement.kind;
			entity.item = item;
			entity.room = room.id;
			entity.className = entry.className;
			entity.doomType = entry.doomType;
			const QPointF centre = cellCentre(*layout, cell);
			entity.x = centre.x();
			entity.y = centre.y();
			const int floor = layout->cell(cell.x(), cell.y()).floor;
			const bool spawn = placement.kind == QStringLiteral("player-start") || placement.kind == QStringLiteral("deathmatch-start");
			entity.z = floor + (spawn || placement.kind == QStringLiteral("monster") ? rules.spawnLift : rules.itemLift);
			if (spawn) {
				entity.angle = int(angleTowards(centre, cellCentre(*layout, room.rect.center())));
			} else if (placement.kind == QStringLiteral("monster")) {
				entity.angle = int(angleTowards(centre, cellCentre(*layout, doorOf(room))));
			}
			for (const QString& pair : entry.extra.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
				const qsizetype equals = pair.indexOf(QLatin1Char('='));
				if (equals > 0) {
					entity.keys.insert(pair.left(equals), pair.mid(equals + 1));
				}
			}
			if (placement.kind == QStringLiteral("exit")) {
				// The trigger sits on the floor of the exit room; Doom gets an
				// exit pad sector instead.
				entity.z = floor;
				layout->cell(cell.x(), cell.y()).tag = 1;
			}
			layout->entities << entity;
		}
	}

	// Quake III's scoreboard camera.
	if (spec.game == QStringLiteral("quake3") && !layout->rooms.isEmpty()) {
		const LevelLayoutRoom* arena = &layout->rooms.first();
		for (const LevelLayoutRoom& room : layout->rooms) {
			if (room.role == QStringLiteral("arena")) {
				arena = &room;
				break;
			}
		}
		LevelPlacedEntity camera;
		camera.kind = QStringLiteral("camera");
		camera.className = QStringLiteral("info_player_intermission");
		camera.room = arena->id;
		const QPointF corner = cellCentre(*layout, QPoint(minX(arena->rect) + 1, minY(arena->rect) + 1));
		camera.x = corner.x();
		camera.y = corner.y();
		camera.z = arena->ceiling - 48;
		camera.angle = int(angleTowards(corner, cellCentre(*layout, arena->rect.center())));
		layout->entities << camera;
	}

	// Lights: a grid under each room's ceiling, and along the corridors.
	if (spec.game != QStringLiteral("doom")) {
		for (const LevelLayoutRoom& room : layout->rooms) {
			const int value = (room.lighting == QStringLiteral("dim") ? 200 : room.lighting == QStringLiteral("bright") ? 380 : 280) * rules.lightScale / 100;
			const int across = std::max(1, int(std::lround(room.rect.width() * double(kCellUnits) / 224.0)));
			const int down = std::max(1, int(std::lround(room.rect.height() * double(kCellUnits) / 224.0)));
			for (int j = 0; j < down; ++j) {
				for (int i = 0; i < across; ++i) {
					QPoint cell(minX(room.rect) + (2 * i + 1) * room.rect.width() / (2 * across), minY(room.rect) + (2 * j + 1) * room.rect.height() / (2 * down));
					if (!layout->cell(cell.x(), cell.y()).open) {
						cell += QPoint(1, 0);
					}
					if (!layout->cell(cell.x(), cell.y()).open) {
						continue;
					}
					LevelPlacedEntity light;
					light.kind = QStringLiteral("light");
					light.className = QStringLiteral("light");
					light.room = room.id;
					const QPointF centre = cellCentre(*layout, cell);
					light.x = centre.x();
					light.y = centre.y();
					light.z = room.ceiling - 24;
					light.keys.insert(QStringLiteral("light"), QString::number(value));
					layout->entities << light;
				}
			}
		}
		for (const LevelLayoutCorridor& corridor : layout->corridors) {
			for (int index = corridor.path.size() / 2 % 6; index < corridor.path.size(); index += 6) {
				const QPoint cell = corridor.path[index];
				const LevelCell& at = layout->cell(cell.x(), cell.y());
				LevelPlacedEntity light;
				light.kind = QStringLiteral("light");
				light.className = QStringLiteral("light");
				const QPointF centre = cellCentre(*layout, cell);
				light.x = centre.x();
				light.y = centre.y();
				light.z = at.ceiling - 16;
				light.keys.insert(QStringLiteral("light"), QString::number(180 * rules.lightScale / 100));
				layout->entities << light;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Quake-family brushes.

struct BoxBrush {
	int x0, y0, z0, x1, y1, z1;
	QString top;
	QString bottom;
	QString sides;
	// Quake II face flags: contents, surface, value.
	int contents = 0;
	int surface = 0;
};

struct BrushPlan {
	QString kind;
	int bottom = 0;
	int top = 0;
	QString topTexture;
	QString bottomTexture;
	QString sideTexture;
	int contents = 0;
	int surface = 0;

	bool operator==(const BrushPlan& other) const
	{
		return kind == other.kind && bottom == other.bottom && top == other.top && topTexture == other.topTexture && bottomTexture == other.bottomTexture
			&& sideTexture == other.sideTexture && contents == other.contents && surface == other.surface;
	}
};

QVector<BoxBrush> buildBrushes(const LevelLayout& layout, const LevelGenerationSpec& spec, const ThemeTextures& textures)
{
	int lowest = INT_MAX;
	int highest = INT_MIN;
	for (const LevelCell& cell : layout.cells) {
		if (cell.open) {
			lowest = std::min(lowest, cell.floor);
			highest = std::max(highest, cell.ceiling);
		}
	}
	const int bottom = lowest - 16;
	const int top = highest + 16;
	const bool quake3 = spec.game == QStringLiteral("quake3");
	const QString hidden = quake3 ? textures.caulk : textures.wall;
	const auto floorTexture = [&textures](const LevelCell& cell) {
		if (cell.owner.startsWith(QStringLiteral("c:"))) {
			return textures.trim;
		}
		return cell.tag ? textures.exitFloor : textures.floor;
	};
	// One brush plan per cell and layer, then merged into rectangles.
	const auto layer = [&](const std::function<bool(int, int, BrushPlan*)>& planFor) {
		QVector<BoxBrush> brushes;
		QVector<BrushPlan> plans(layout.width * layout.height);
		QVector<bool> wanted(layout.width * layout.height, false);
		for (int y = 0; y < layout.height; ++y) {
			for (int x = 0; x < layout.width; ++x) {
				wanted[y * layout.width + x] = planFor(x, y, &plans[y * layout.width + x]);
			}
		}
		QVector<bool> done(wanted.size(), false);
		for (int y = 0; y < layout.height; ++y) {
			for (int x = 0; x < layout.width; ++x) {
				const int at = y * layout.width + x;
				if (!wanted[at] || done[at]) {
					continue;
				}
				const BrushPlan& plan = plans[at];
				int width = 1;
				while (x + width < layout.width && wanted[at + width] && !done[at + width] && plans[at + width] == plan) {
					++width;
				}
				int height = 1;
				bool grow = true;
				while (grow && y + height < layout.height) {
					for (int dx = 0; dx < width; ++dx) {
						const int next = (y + height) * layout.width + x + dx;
						if (!wanted[next] || done[next] || !(plans[next] == plan)) {
							grow = false;
							break;
						}
					}
					if (grow) {
						++height;
					}
				}
				for (int dy = 0; dy < height; ++dy) {
					for (int dx = 0; dx < width; ++dx) {
						done[(y + dy) * layout.width + x + dx] = true;
					}
				}
				BoxBrush brush;
				brush.x0 = layout.origin.x() + x * layout.cellSize;
				brush.y0 = layout.origin.y() + y * layout.cellSize;
				brush.x1 = brush.x0 + width * layout.cellSize;
				brush.y1 = brush.y0 + height * layout.cellSize;
				brush.z0 = plan.bottom;
				brush.z1 = plan.top;
				brush.top = plan.topTexture;
				brush.bottom = plan.bottomTexture;
				brush.sides = plan.sideTexture;
				brush.contents = plan.contents;
				brush.surface = plan.surface;
				brushes << brush;
			}
		}
		return brushes;
	};
	QVector<BoxBrush> brushes;
	// Walls: every closed cell beside an open one is a full column.
	brushes += layer([&](int x, int y, BrushPlan* plan) {
		if (layout.cell(x, y).open) {
			return false;
		}
		bool beside = false;
		for (int dy = -1; dy <= 1 && !beside; ++dy) {
			for (int dx = -1; dx <= 1 && !beside; ++dx) {
				beside = layout.contains(x + dx, y + dy) && layout.cell(x + dx, y + dy).open;
			}
		}
		if (!beside) {
			return false;
		}
		*plan = {QStringLiteral("wall"), bottom, top, hidden, hidden, textures.wall, 0, 0};
		return true;
	});
	// Floors and ceilings of every open cell.
	brushes += layer([&](int x, int y, BrushPlan* plan) {
		const LevelCell& cell = layout.cell(x, y);
		if (!cell.open) {
			return false;
		}
		*plan = {QStringLiteral("floor"), bottom, cell.floor, floorTexture(cell), hidden, textures.wall, 0, 0};
		return true;
	});
	brushes += layer([&](int x, int y, BrushPlan* plan) {
		const LevelCell& cell = layout.cell(x, y);
		if (!cell.open) {
			return false;
		}
		*plan = {QStringLiteral("ceiling"), cell.ceiling, top, hidden, textures.ceiling, textures.wall, 0, 0};
		return true;
	});
	// Liquids fill pits up to their surface.
	brushes += layer([&](int x, int y, BrushPlan* plan) {
		const LevelCell& cell = layout.cell(x, y);
		if (!cell.open || cell.liquid.isEmpty() || cell.liquidTop <= cell.floor) {
			return false;
		}
		const QString texture = textures.liquid(cell.liquid);
		// Quake II's qfiles.h: CONTENTS_LAVA 8, SLIME 16, WATER 32;
		// SURF_WARP 8, SURF_TRANS33 16.
		int contents = 0;
		int surface = 0;
		if (spec.game == QStringLiteral("quake2")) {
			contents = cell.liquid == QStringLiteral("lava") ? 8 : cell.liquid == QStringLiteral("slime") ? 16 : 32;
			surface = 8 | (cell.liquid == QStringLiteral("lava") ? 0 : 16);
		}
		*plan = {QStringLiteral("liquid"), cell.floor, cell.liquidTop, texture, texture, texture, contents, surface};
		return true;
	});
	return brushes;
}

QString number(double value)
{
	return QString::number(value, 'f', value == std::floor(value) ? 0 : 2);
}

// One face line, its three points wound so (p0 - p1) x (p2 - p1) faces out
// of the brush, as qbsp, q3map2 and TrenchBroom read them.
QString faceLine(const QString& game, const std::array<std::array<int, 3>, 3>& points, const QString& texture, int contents, int surface)
{
	QString line;
	for (const auto& point : points) {
		line += QStringLiteral("( %1 %2 %3 ) ").arg(point[0]).arg(point[1]).arg(point[2]);
	}
	line += texture + QStringLiteral(" 0 0 0 1 1");
	if (game == QStringLiteral("quake2") || game == QStringLiteral("quake3")) {
		line += QStringLiteral(" %1 %2 0").arg(contents).arg(surface);
	}
	return line;
}

QString brushText(const QString& game, const BoxBrush& b, int index)
{
	QStringList lines;
	lines << QStringLiteral("// brush %1").arg(index) << QStringLiteral("{");
	lines << faceLine(game, {{{b.x0, b.y0, b.z0}, {b.x0, b.y0 + 1, b.z0}, {b.x0, b.y0, b.z0 + 1}}}, b.sides, b.contents, b.surface);
	lines << faceLine(game, {{{b.x0, b.y0, b.z0}, {b.x0, b.y0, b.z0 + 1}, {b.x0 + 1, b.y0, b.z0}}}, b.sides, b.contents, b.surface);
	lines << faceLine(game, {{{b.x0, b.y0, b.z0}, {b.x0 + 1, b.y0, b.z0}, {b.x0, b.y0 + 1, b.z0}}}, b.bottom, b.contents, b.surface);
	lines << faceLine(game, {{{b.x1, b.y1, b.z1}, {b.x1, b.y1 + 1, b.z1}, {b.x1 + 1, b.y1, b.z1}}}, b.top, b.contents, b.surface);
	lines << faceLine(game, {{{b.x1, b.y1, b.z1}, {b.x1 + 1, b.y1, b.z1}, {b.x1, b.y1, b.z1 + 1}}}, b.sides, b.contents, b.surface);
	lines << faceLine(game, {{{b.x1, b.y1, b.z1}, {b.x1, b.y1, b.z1 + 1}, {b.x1, b.y1 + 1, b.z1}}}, b.sides, b.contents, b.surface);
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}

QString quoted(const QString& key, const QString& value)
{
	QString clean = value;
	clean.replace(QLatin1Char('"'), QLatin1Char('\''));
	clean.replace(QLatin1Char('\n'), QLatin1Char(' '));
	return QStringLiteral("\"%1\" \"%2\"").arg(key, clean);
}

QByteArray writeQuakeMap(const LevelGenerationResult& result, const ThemeTextures& textures, const QVector<BoxBrush>& brushes, int* entityCount)
{
	const LevelGenerationSpec& spec = result.spec;
	const LevelLayout& layout = result.layout;
	QStringList out;
	if (spec.game == QStringLiteral("quake2")) {
		// The first line keeps Quake II's target when VibeStudio reopens it.
		out << QString::fromLatin1(kQuake2MapTargetHeader).trimmed();
	} else if (spec.game == QStringLiteral("quake3")) {
		out << QStringLiteral("// Quake III Arena map in the Q3Radiant format, generated by VibeStudio");
	}
	out << QStringLiteral("// Generated by VibeStudio's level generator: %1, seed %2, planner %3").arg(spec.game).arg(spec.seed).arg(result.plan.planner);
	out << QStringLiteral("// entity 0") << QStringLiteral("{") << quoted(QStringLiteral("classname"), QStringLiteral("worldspawn"));
	out << quoted(QStringLiteral("message"), result.plan.title.isEmpty() ? spec.title : result.plan.title);
	out << quoted(QStringLiteral("_vibestudio_seed"), QString::number(spec.seed));
	if (spec.game == QStringLiteral("quake")) {
		// worldtype picks the key models: 0 medieval, 1 metal (runic), 2 base.
		const int worldtype = spec.theme == QStringLiteral("base") ? 2 : spec.theme == QStringLiteral("metal") || spec.theme == QStringLiteral("hell") ? 1 : 0;
		out << quoted(QStringLiteral("worldtype"), QString::number(worldtype));
		if (!spec.wad.trimmed().isEmpty()) {
			out << quoted(QStringLiteral("wad"), spec.wad.trimmed());
		}
	} else if (spec.game == QStringLiteral("quake2")) {
		out << quoted(QStringLiteral("sky"), QStringLiteral("unit1_"));
	}
	for (int index = 0; index < brushes.size(); ++index) {
		out << brushText(spec.game, brushes[index], index);
	}
	out << QStringLiteral("}");
	int entityIndex = 1;
	int exits = 0;
	QStringList brushEntities;
	for (const LevelPlacedEntity& entity : layout.entities) {
		if (entity.className.isEmpty()) {
			continue;
		}
		if (entity.kind == QStringLiteral("exit")) {
			// A trigger over the exit spot.
			const int half = layout.cellSize;
			BoxBrush trigger {int(entity.x) - half, int(entity.y) - half, int(entity.z), int(entity.x) + half, int(entity.y) + half, int(entity.z) + 96,
				textures.trigger, textures.trigger, textures.trigger, 0, 0};
			QStringList block;
			block << QStringLiteral("// entity %1").arg(entityIndex++) << QStringLiteral("{");
			if (spec.game == QStringLiteral("quake2")) {
				const QString name = QStringLiteral("vs_exit%1").arg(++exits);
				block << quoted(QStringLiteral("classname"), QStringLiteral("trigger_multiple")) << quoted(QStringLiteral("target"), name);
				block << brushText(spec.game, trigger, 0) << QStringLiteral("}");
				block << QStringLiteral("// entity %1").arg(entityIndex++) << QStringLiteral("{") << quoted(QStringLiteral("classname"), QStringLiteral("target_changelevel"))
					  << quoted(QStringLiteral("targetname"), name) << quoted(QStringLiteral("map"), QStringLiteral("base1"))
					  << quoted(QStringLiteral("origin"), QStringLiteral("%1 %2 %3").arg(number(entity.x), number(entity.y), number(entity.z + 32))) << QStringLiteral("}");
			} else {
				block << quoted(QStringLiteral("classname"), QStringLiteral("trigger_changelevel")) << quoted(QStringLiteral("map"), QStringLiteral("start"));
				block << brushText(spec.game, trigger, 0) << QStringLiteral("}");
			}
			brushEntities << block.join(QLatin1Char('\n'));
			continue;
		}
		QStringList block;
		block << QStringLiteral("// entity %1").arg(entityIndex++) << QStringLiteral("{") << quoted(QStringLiteral("classname"), entity.className);
		block << quoted(QStringLiteral("origin"), QStringLiteral("%1 %2 %3").arg(number(entity.x), number(entity.y), number(entity.z)));
		if (entity.kind != QStringLiteral("light") && entity.angle != 0) {
			block << quoted(QStringLiteral("angle"), QString::number(entity.angle));
		}
		QStringList keys = entity.keys.keys();
		keys.sort();
		for (const QString& key : keys) {
			block << quoted(key, entity.keys.value(key));
		}
		block << QStringLiteral("}");
		out << block.join(QLatin1Char('\n'));
	}
	out += brushEntities;
	if (entityCount) {
		*entityCount = entityIndex;
	}
	return (out.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
}

// ---------------------------------------------------------------------------
// Doom: sectors traced from the cells into a PWAD.

struct DoomBuild {
	QByteArray wad;
	int sectors = 0;
	int linedefs = 0;
	int things = 0;
};

void putShort(QByteArray* bytes, int value)
{
	const qint16 v = qint16(std::clamp(value, -32768, 32767));
	char data[2];
	qToLittleEndian(v, data);
	bytes->append(data, 2);
}

void putName(QByteArray* bytes, const QString& name)
{
	QByteArray latin = name.toUpper().toLatin1().left(8);
	latin.resize(8, '\0');
	bytes->append(latin);
}

DoomBuild writeDoomMap(const LevelGenerationResult& result, const ThemeTextures& textures)
{
	const LevelLayout& layout = result.layout;
	struct SectorKey {
		int floor;
		int ceiling;
		QString floorTexture;
		QString ceilingTexture;
		int light;
		int special;
		int tag;
		bool operator==(const SectorKey& other) const
		{
			return floor == other.floor && ceiling == other.ceiling && floorTexture == other.floorTexture && ceilingTexture == other.ceilingTexture && light == other.light
				&& special == other.special && tag == other.tag;
		}
	};
	QHash<QString, QString> lighting;
	for (const LevelLayoutRoom& room : layout.rooms) {
		lighting.insert(room.id, room.lighting);
	}
	const auto keyFor = [&](const LevelCell& cell) {
		SectorKey key;
		key.floor = cell.floor;
		key.ceiling = cell.ceiling;
		key.floorTexture = cell.tag ? textures.exitFloor : cell.liquid.isEmpty() ? textures.floor : textures.liquid(cell.liquid);
		key.ceilingTexture = textures.ceiling;
		const QString mood = lighting.value(cell.owner, QStringLiteral("corridor"));
		key.light = mood == QStringLiteral("dim") ? 128 : mood == QStringLiteral("bright") ? 224 : mood == QStringLiteral("corridor") ? 144 : 176;
		// Doom sector specials: 16 hurts 20%, 7 hurts 5% (nukage).
		key.special = cell.liquid == QStringLiteral("lava") ? 16 : cell.liquid == QStringLiteral("slime") ? 7 : 0;
		key.tag = cell.tag;
		return key;
	};
	// Sectors: connected cells with the same key.
	QVector<int> sectorOf(layout.cells.size(), -1);
	QVector<SectorKey> sectors;
	for (int y = 0; y < layout.height; ++y) {
		for (int x = 0; x < layout.width; ++x) {
			if (!layout.cell(x, y).open || sectorOf[y * layout.width + x] >= 0) {
				continue;
			}
			const SectorKey key = keyFor(layout.cell(x, y));
			const int id = int(sectors.size());
			sectors << key;
			QQueue<QPoint> queue;
			queue.enqueue(QPoint(x, y));
			sectorOf[y * layout.width + x] = id;
			while (!queue.isEmpty()) {
				const QPoint at = queue.dequeue();
				static const QPoint around[4] = {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)};
				for (const QPoint& step : around) {
					const QPoint next = at + step;
					if (!layout.contains(next.x(), next.y()) || sectorOf[next.y() * layout.width + next.x()] >= 0 || !layout.cell(next.x(), next.y()).open) {
						continue;
					}
					if (keyFor(layout.cell(next.x(), next.y())) == key) {
						sectorOf[next.y() * layout.width + next.x()] = id;
						queue.enqueue(next);
					}
				}
			}
		}
	}
	// Edges between a sector and the void or another sector, each walked with
	// its front sector on the right (Doom's front side).
	struct Edge {
		QPoint a;
		QPoint b;
		int front;
		int back;
	};
	QVector<Edge> edges;
	const auto worldPoint = [&layout](int cx, int cy) { return QPoint(layout.origin.x() + cx * layout.cellSize, layout.origin.y() + cy * layout.cellSize); };
	for (int y = 0; y < layout.height; ++y) {
		for (int x = 0; x < layout.width; ++x) {
			const int sector = sectorOf[y * layout.width + x];
			if (sector < 0) {
				continue;
			}
			const auto other = [&](int nx, int ny) { return layout.contains(nx, ny) ? sectorOf[ny * layout.width + nx] : -1; };
			// East, west, north, south, each clockwise around this cell.
			const struct {
				int nx, ny;
				QPoint a, b;
			} sides[4] = {
				{x + 1, y, worldPoint(x + 1, y + 1), worldPoint(x + 1, y)},
				{x - 1, y, worldPoint(x, y), worldPoint(x, y + 1)},
				{x, y + 1, worldPoint(x, y + 1), worldPoint(x + 1, y + 1)},
				{x, y - 1, worldPoint(x + 1, y), worldPoint(x, y)},
			};
			for (const auto& side : sides) {
				const int neighbour = other(side.nx, side.ny);
				if (neighbour == sector) {
					continue;
				}
				// A two-sided edge once, from the lower-numbered sector.
				if (neighbour >= 0 && neighbour < sector) {
					continue;
				}
				edges.push_back({side.a, side.b, sector, neighbour});
			}
		}
	}
	// Every point an edge ends at, so merged lines break where lines meet.
	QHash<QPoint, int> endpointUse;
	for (const Edge& edge : edges) {
		endpointUse[edge.a] += 1;
		endpointUse[edge.b] += 1;
	}
	// Merge runs of edges in one direction between the same sectors; a point
	// more than two edges meet at ends a run.
	QHash<QPoint, QVector<int>> startingAt;
	QHash<QPoint, QVector<int>> endingAt;
	for (int index = 0; index < edges.size(); ++index) {
		startingAt[edges[index].a] << index;
		endingAt[edges[index].b] << index;
	}
	const auto continues = [&edges](int from, int to) {
		return edges[from].front == edges[to].front && edges[from].back == edges[to].back && edges[from].b - edges[from].a == edges[to].b - edges[to].a;
	};
	QVector<bool> merged(edges.size(), false);
	QVector<Edge> lines;
	for (int index = 0; index < edges.size(); ++index) {
		if (merged[index]) {
			continue;
		}
		// Walk back to the run's first edge.
		int first = index;
		for (bool moved = true; moved && endpointUse.value(edges[first].a) <= 2;) {
			moved = false;
			for (int candidate : endingAt.value(edges[first].a)) {
				if (!merged[candidate] && candidate != first && continues(candidate, first)) {
					first = candidate;
					moved = true;
					break;
				}
			}
		}
		Edge line = edges[first];
		merged[first] = true;
		for (bool grew = true; grew && endpointUse.value(line.b) <= 2;) {
			grew = false;
			for (int candidate : startingAt.value(line.b)) {
				if (!merged[candidate] && continues(first, candidate)) {
					line.b = edges[candidate].b;
					merged[candidate] = true;
					grew = true;
					break;
				}
			}
		}
		lines << line;
	}
	// Vertices, sidedefs, linedefs.
	QHash<QPoint, int> vertexIndex;
	QVector<QPoint> vertices;
	const auto vertex = [&](QPoint point) {
		const auto found = vertexIndex.constFind(point);
		if (found != vertexIndex.constEnd()) {
			return found.value();
		}
		vertexIndex.insert(point, int(vertices.size()));
		vertices << point;
		return int(vertices.size()) - 1;
	};
	QByteArray vertexBytes, lineBytes, sideBytes, sectorBytes, thingBytes;
	int sideCount = 0;
	const auto addSide = [&](int sector, const QString& upper, const QString& lower, const QString& middle) {
		putShort(&sideBytes, 0);
		putShort(&sideBytes, 0);
		putName(&sideBytes, upper);
		putName(&sideBytes, lower);
		putName(&sideBytes, middle);
		putShort(&sideBytes, sector);
		return sideCount++;
	};
	for (const Edge& line : lines) {
		const int v1 = vertex(line.a);
		const int v2 = vertex(line.b);
		const SectorKey& front = sectors[line.front];
		int flags = 0;
		int special = 0;
		int frontSide = -1;
		int backSide = -1;
		if (line.back < 0) {
			// ML_BLOCKING 1.
			flags = 1;
			frontSide = addSide(line.front, QStringLiteral("-"), QStringLiteral("-"), textures.wall);
		} else {
			const SectorKey& back = sectors[line.back];
			// ML_TWOSIDED 4; the risers and lintels each side sees.
			flags = 4;
			frontSide = addSide(line.front, back.ceiling < front.ceiling ? textures.wall : QStringLiteral("-"), back.floor > front.floor ? textures.wall : QStringLiteral("-"),
				QStringLiteral("-"));
			backSide = addSide(line.back, front.ceiling < back.ceiling ? textures.wall : QStringLiteral("-"), front.floor > back.floor ? textures.wall : QStringLiteral("-"),
				QStringLiteral("-"));
			// W1 Exit Level (52) on the lines around an exit pad.
			if (front.tag != back.tag) {
				special = 52;
			}
		}
		putShort(&lineBytes, v1);
		putShort(&lineBytes, v2);
		putShort(&lineBytes, flags);
		putShort(&lineBytes, special);
		putShort(&lineBytes, 0);
		putShort(&lineBytes, frontSide);
		// No back side is 0xFFFF, which -1 writes as a little-endian short.
		putShort(&lineBytes, backSide < 0 ? -1 : backSide);
	}
	for (const QPoint& point : vertices) {
		putShort(&vertexBytes, point.x());
		putShort(&vertexBytes, point.y());
	}
	for (const SectorKey& sector : sectors) {
		putShort(&sectorBytes, sector.floor);
		putShort(&sectorBytes, sector.ceiling);
		putName(&sectorBytes, sector.floorTexture);
		putName(&sectorBytes, sector.ceilingTexture);
		putShort(&sectorBytes, sector.light);
		putShort(&sectorBytes, sector.special);
		putShort(&sectorBytes, 0);
	}
	int things = 0;
	for (const LevelPlacedEntity& entity : layout.entities) {
		if (entity.doomType <= 0) {
			continue;
		}
		putShort(&thingBytes, int(std::lround(entity.x)));
		putShort(&thingBytes, int(std::lround(entity.y)));
		putShort(&thingBytes, entity.angle);
		putShort(&thingBytes, entity.doomType);
		// Easy, medium and hard skills.
		putShort(&thingBytes, 7);
		++things;
	}
	// The node builder fills SEGS, SSECTORS, NODES, REJECT and BLOCKMAP.
	const QVector<QPair<QString, QByteArray>> lumps = {
		{result.mapName, QByteArray()},
		{QStringLiteral("THINGS"), thingBytes},
		{QStringLiteral("LINEDEFS"), lineBytes},
		{QStringLiteral("SIDEDEFS"), sideBytes},
		{QStringLiteral("VERTEXES"), vertexBytes},
		{QStringLiteral("SEGS"), QByteArray()},
		{QStringLiteral("SSECTORS"), QByteArray()},
		{QStringLiteral("NODES"), QByteArray()},
		{QStringLiteral("SECTORS"), sectorBytes},
		{QStringLiteral("REJECT"), QByteArray()},
		{QStringLiteral("BLOCKMAP"), QByteArray()},
	};
	QByteArray data;
	QByteArray directory;
	for (const auto& lump : lumps) {
		char field[4];
		qToLittleEndian(qint32(12 + data.size()), field);
		directory.append(field, 4);
		qToLittleEndian(qint32(lump.second.size()), field);
		directory.append(field, 4);
		putName(&directory, lump.first);
		data += lump.second;
	}
	DoomBuild build;
	build.wad = QByteArrayLiteral("PWAD");
	char field[4];
	qToLittleEndian(qint32(lumps.size()), field);
	build.wad.append(field, 4);
	qToLittleEndian(qint32(12 + data.size()), field);
	build.wad.append(field, 4);
	build.wad += data;
	build.wad += directory;
	build.sectors = int(sectors.size());
	build.linedefs = int(lines.size());
	build.things = things;
	return build;
}

// Rooms the start reaches on foot or swimming: steps up to the game's
// highest walkable step, any drop down, and through water.
int reachableRooms(const LevelLayout& layout, const GameRules& rules, QStringList* unreached)
{
	QPoint start(-1, -1);
	for (const LevelPlacedEntity& entity : layout.entities) {
		if (entity.kind == QStringLiteral("player-start") || entity.kind == QStringLiteral("deathmatch-start")) {
			start = QPoint(int(std::floor((entity.x - layout.origin.x()) / layout.cellSize)), int(std::floor((entity.y - layout.origin.y()) / layout.cellSize)));
			break;
		}
	}
	if (!layout.contains(start.x(), start.y())) {
		return 0;
	}
	QVector<bool> seen(layout.cells.size(), false);
	QQueue<QPoint> queue;
	queue.enqueue(start);
	seen[start.y() * layout.width + start.x()] = true;
	QSet<QString> reached;
	while (!queue.isEmpty()) {
		const QPoint at = queue.dequeue();
		const LevelCell& here = layout.cell(at.x(), at.y());
		reached.insert(here.owner);
		static const QPoint around[4] = {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)};
		for (const QPoint& step : around) {
			const QPoint next = at + step;
			if (!layout.contains(next.x(), next.y()) || seen[next.y() * layout.width + next.x()]) {
				continue;
			}
			const LevelCell& there = layout.cell(next.x(), next.y());
			if (!there.open || there.liquid == QStringLiteral("lava") || there.liquid == QStringLiteral("slime")) {
				continue;
			}
			const int standHere = here.liquid == QStringLiteral("water") ? here.liquidTop : here.floor;
			const int standThere = there.liquid == QStringLiteral("water") ? there.liquidTop : there.floor;
			if (standThere - standHere > rules.maxStep) {
				continue;
			}
			seen[next.y() * layout.width + next.x()] = true;
			queue.enqueue(next);
		}
	}
	int count = 0;
	for (const LevelLayoutRoom& room : layout.rooms) {
		if (reached.contains(room.id)) {
			++count;
		} else if (unreached) {
			*unreached << room.id;
		}
	}
	return count;
}

} // namespace

LevelGenerationResult generateLevel(const LevelGenerationSpec& input, const LevelSemanticPlan* givenPlan)
{
	LevelGenerationResult result;
	result.spec = normalizedLevelGenerationSpec(input);
	const LevelGenerationSpec& spec = result.spec;
	result.plan = givenPlan ? *givenPlan : rulesLevelPlan(spec);
	if (result.plan.planner.isEmpty()) {
		result.plan.planner = QStringLiteral("supplied");
	}
	repairLevelSemanticPlan(&result.plan, spec);
	const GameRules rules = rulesFor(spec.game);
	const ThemeTextures textures = texturesFor(spec);

	LayoutWork work;
	work.spec = &result.spec;
	work.plan = &result.plan;
	work.rules = rules;
	LevelRandom random(quint64(spec.seed) * 0x9E3779B97F4A7C15ull + 0x6c61796f7574ull);
	layoutRooms(&work, &random);
	stampLayout(&work, &result.layout, &random);
	result.layout.dropped = work.dropped;
	placeEntities(work, &result.layout, &random);
	result.warnings += work.dropped;

	const LevelLayout& layout = result.layout;
	LevelGenerationStatistics& stats = result.statistics;
	stats.rooms = int(layout.rooms.size());
	stats.corridors = int(layout.corridors.size());
	for (const LevelCell& cell : layout.cells) {
		stats.openCells += cell.open ? 1 : 0;
	}
	for (const LevelPlacedEntity& entity : layout.entities) {
		if (entity.kind == QStringLiteral("light")) {
			++stats.lights;
		} else if (entity.kind == QStringLiteral("monster")) {
			++stats.monsters;
		} else if (QStringList {QStringLiteral("weapon"), QStringLiteral("ammo"), QStringLiteral("health"), QStringLiteral("armor"), QStringLiteral("powerup")}.contains(entity.kind)) {
			++stats.items;
		}
	}
	stats.extentUnits = QSize(layout.width * layout.cellSize, layout.height * layout.cellSize);
	QString suffix = spec.theme;
	result.suggestedFileName = QStringLiteral("generated_%1_%2.%3").arg(suffix).arg(spec.seed).arg(spec.game == QStringLiteral("doom") ? QStringLiteral("wad") : QStringLiteral("map"));

	if (stats.rooms == 0) {
		result.error = QCoreApplication::translate("VibeStudioLevelGeneration", "No room could be laid out.");
		return result;
	}
	QStringList unreached;
	stats.reachableRooms = reachableRooms(layout, rules, &unreached);
	if (!unreached.isEmpty()) {
		result.warnings << QCoreApplication::translate("VibeStudioLevelGeneration", "The start does not reach %1 on foot.").arg(unreached.join(QStringLiteral(", ")));
	}

	// Write the game's map, then read it back with the editor's own parser.
	LevelMapLoadRequest load;
	if (spec.game == QStringLiteral("doom")) {
		result.format = QStringLiteral("doom-wad");
		result.mapName = QStringLiteral("MAP01");
		const DoomBuild build = writeDoomMap(result, textures);
		result.mapBytes = build.wad;
		stats.sectors = build.sectors;
		stats.linedefs = build.linedefs;
		stats.entities = build.things;
		load.path = result.suggestedFileName;
		load.mapName = result.mapName;
		load.engineHint = QStringLiteral("doom");
		result.notes << QCoreApplication::translate("VibeStudioLevelGeneration", "MAP01 has no nodes yet: build it (ZDBSP) before playing. Doom II things and textures are used.");
	} else {
		result.format = QStringLiteral("%1-map").arg(spec.game);
		const QVector<BoxBrush> brushes = buildBrushes(layout, spec, textures);
		stats.brushes = int(brushes.size());
		int entities = 0;
		result.mapBytes = writeQuakeMap(result, textures, brushes, &entities);
		stats.entities = entities;
		load.path = result.suggestedFileName;
		load.engineHint = spec.game == QStringLiteral("quake3") ? QStringLiteral("idTech3") : QStringLiteral("idTech2");
		if (spec.game == QStringLiteral("quake") && spec.wad.trimmed().isEmpty()) {
			result.notes << QCoreApplication::translate("VibeStudioLevelGeneration",
				"Set worldspawn's \"wad\" key to the WAD holding %1, %2 and %3 (or pass a WAD) before compiling.").arg(textures.wall, textures.floor, textures.ceiling);
		}
	}
	LevelMapDocument document;
	QString error;
	if (!loadLevelMapBytes(load, result.mapBytes, &document, &error)) {
		result.error = QCoreApplication::translate("VibeStudioLevelGeneration", "The generated map does not read back: %1").arg(error);
		return result;
	}
	const LevelMapStatistics read = levelMapStatistics(document);
	if (read.errorCount > 0 || read.degenerateBrushCount > 0) {
		QStringList problems;
		for (const LevelMapIssue& issue : document.issues) {
			if (issue.severity == LevelMapIssueSeverity::Error) {
				problems << issue.message;
			}
		}
		result.error = QCoreApplication::translate("VibeStudioLevelGeneration", "The generated map has %n error(s): %1", nullptr, std::max(read.errorCount, read.degenerateBrushCount))
						   .arg(problems.mid(0, 5).join(QStringLiteral("; ")));
		return result;
	}
	if (spec.game != QStringLiteral("doom") && read.brushCount < stats.brushes) {
		result.warnings << QCoreApplication::translate("VibeStudioLevelGeneration", "The parser read %1 of %2 brushes.").arg(read.brushCount).arg(stats.brushes);
	}
	for (const QString& repair : result.plan.repairs) {
		result.notes << repair;
	}
	result.ok = true;
	return result;
}

bool levelGenerationDocument(const LevelGenerationResult& result, LevelMapDocument* document, QString* error)
{
	if (!document || !result.ok || result.mapBytes.isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelGeneration", "There is no generated level to open.");
		}
		return false;
	}
	LevelMapLoadRequest request;
	request.path = result.suggestedFileName;
	request.mapName = result.mapName;
	request.engineHint = result.spec.game == QStringLiteral("doom") ? QStringLiteral("doom") : result.spec.game == QStringLiteral("quake3") ? QStringLiteral("idTech3")
																																			: QStringLiteral("idTech2");
	LevelMapDocument loaded;
	if (!loadLevelMapBytes(request, result.mapBytes, &loaded, error)) {
		return false;
	}
	// As createLevelMap leaves a new map: no file until Save As.
	loaded.sourcePath.clear();
	loaded.sourceContentHash.clear();
	loaded.outputPath.clear();
	loaded.undoStack.clear();
	loaded.redoStack.clear();
	loaded.savedUndoDepth = -1;
	loaded.editState = QStringLiteral("modified");
	loaded.revision = 1;
	if (result.spec.game != QStringLiteral("doom")) {
		loaded.mapName = result.plan.title.isEmpty() ? result.spec.title : result.plan.title;
	}
	clearLevelMapSelection(&loaded);
	*document = std::move(loaded);
	return true;
}

QImage renderLevelLayoutPreview(const LevelGenerationResult& result, QSize size)
{
	const LevelLayout& layout = result.layout;
	QImage image(size.isValid() && !size.isEmpty() ? size : QSize(640, 640), QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(24, 26, 31));
	if (layout.width <= 0 || layout.height <= 0) {
		return image;
	}
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, true);
	const double margin = 12.0;
	const double scale = std::min((image.width() - 2 * margin) / layout.width, (image.height() - 2 * margin) / layout.height);
	const double left = (image.width() - scale * layout.width) / 2.0;
	const double top = (image.height() - scale * layout.height) / 2.0;
	// North is up: grid y grows north, image y grows down.
	const auto cellRect = [&](int x, int y) { return QRectF(left + x * scale, top + (layout.height - 1 - y) * scale, scale, scale); };
	int lowest = INT_MAX;
	int highest = INT_MIN;
	for (const LevelCell& cell : layout.cells) {
		if (cell.open) {
			lowest = std::min(lowest, cell.floor);
			highest = std::max(highest, cell.floor);
		}
	}
	for (int y = 0; y < layout.height; ++y) {
		for (int x = 0; x < layout.width; ++x) {
			const LevelCell& cell = layout.cell(x, y);
			if (!cell.open) {
				bool wall = false;
				for (int dy = -1; dy <= 1 && !wall; ++dy) {
					for (int dx = -1; dx <= 1 && !wall; ++dx) {
						wall = layout.contains(x + dx, y + dy) && layout.cell(x + dx, y + dy).open;
					}
				}
				if (wall) {
					painter.fillRect(cellRect(x, y), QColor(58, 63, 74));
				}
				continue;
			}
			const double t = highest > lowest ? double(cell.floor - lowest) / double(highest - lowest) : 0.5;
			QColor colour = QColor::fromRgbF(0.36 + 0.42 * t, 0.42 + 0.40 * t, 0.48 + 0.38 * t);
			if (cell.liquid == QStringLiteral("lava")) {
				colour = QColor(224, 88, 43);
			} else if (cell.liquid == QStringLiteral("slime")) {
				colour = QColor(111, 191, 60);
			} else if (cell.liquid == QStringLiteral("water")) {
				colour = QColor(63, 127, 208);
			}
			if (cell.tag) {
				colour = QColor(196, 72, 196);
			}
			painter.fillRect(cellRect(x, y), colour);
		}
	}
	// Room outlines, and names where there are fonts to draw them.
	painter.setPen(QPen(QColor(240, 200, 120, 200), std::max(1.0, scale / 6.0)));
	for (const LevelLayoutRoom& room : layout.rooms) {
		const QRectF area = cellRect(minX(room.rect), maxY(room.rect)).united(cellRect(maxX(room.rect), minY(room.rect)));
		painter.drawRect(area);
		if (qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
			QFont font = painter.font();
			font.setPixelSize(std::clamp(int(scale * 1.2), 9, 16));
			painter.setFont(font);
			painter.setPen(QColor(245, 245, 245));
			painter.drawText(area.adjusted(3, 2, -3, -2), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("%1 %2").arg(room.id, room.role));
			painter.setPen(QPen(QColor(240, 200, 120, 200), std::max(1.0, scale / 6.0)));
		}
	}
	// Entities.
	painter.setPen(Qt::NoPen);
	for (const LevelPlacedEntity& entity : layout.entities) {
		const QPointF at(left + (entity.x - layout.origin.x()) / layout.cellSize * scale, top + (layout.height - (entity.y - layout.origin.y()) / layout.cellSize) * scale);
		const double radius = std::max(2.0, scale * 0.45);
		if (entity.kind == QStringLiteral("light")) {
			painter.setBrush(QColor(255, 250, 220, 140));
			painter.drawEllipse(at, radius * 0.45, radius * 0.45);
		} else if (entity.kind == QStringLiteral("player-start") || entity.kind == QStringLiteral("deathmatch-start")) {
			painter.setBrush(entity.kind == QStringLiteral("player-start") ? QColor(80, 220, 120) : QColor(80, 200, 230));
			QPainterPath arrow;
			const double angle = entity.angle * 3.14159265358979323846 / 180.0;
			arrow.moveTo(at + QPointF(std::cos(angle), -std::sin(angle)) * radius * 1.6);
			arrow.lineTo(at + QPointF(std::cos(angle + 2.4), -std::sin(angle + 2.4)) * radius);
			arrow.lineTo(at + QPointF(std::cos(angle - 2.4), -std::sin(angle - 2.4)) * radius);
			arrow.closeSubpath();
			painter.drawPath(arrow);
		} else if (entity.kind == QStringLiteral("monster")) {
			painter.setBrush(QColor(230, 60, 60));
			painter.drawEllipse(at, radius, radius);
		} else if (entity.kind == QStringLiteral("weapon")) {
			painter.setBrush(QColor(255, 160, 40));
			painter.drawRect(QRectF(at.x() - radius, at.y() - radius * 0.5, radius * 2, radius));
		} else if (entity.kind == QStringLiteral("exit")) {
			painter.setBrush(QColor(230, 90, 230));
			painter.drawRect(QRectF(at.x() - radius, at.y() - radius, radius * 2, radius * 2));
		} else if (entity.kind != QStringLiteral("camera")) {
			painter.setBrush(QColor(250, 220, 90));
			painter.drawRect(QRectF(at.x() - radius * 0.6, at.y() - radius * 0.6, radius * 1.2, radius * 1.2));
		}
	}
	painter.end();
	return image;
}

QJsonObject levelGenerationSpecJson(const LevelGenerationSpec& spec)
{
	QJsonObject textures;
	for (auto it = spec.textures.constBegin(); it != spec.textures.constEnd(); ++it) {
		textures.insert(it.key(), it.value());
	}
	return QJsonObject {
		{QStringLiteral("prompt"), spec.prompt},
		{QStringLiteral("game"), spec.game},
		{QStringLiteral("mode"), spec.mode},
		{QStringLiteral("theme"), spec.theme},
		{QStringLiteral("rooms"), spec.rooms},
		{QStringLiteral("size"), spec.size},
		{QStringLiteral("verticality"), spec.verticality},
		{QStringLiteral("players"), spec.players},
		{QStringLiteral("monsters"), spec.monsters},
		{QStringLiteral("liquid"), spec.liquid},
		{QStringLiteral("title"), spec.title},
		{QStringLiteral("seed"), double(spec.seed)},
		{QStringLiteral("textures"), textures},
		{QStringLiteral("wad"), spec.wad},
	};
}

QJsonObject levelGenerationReportJson(const LevelGenerationResult& result)
{
	QJsonObject plan = levelSemanticPlanJson(result.plan);
	plan.insert(QStringLiteral("planner"), result.plan.planner);
	plan.insert(QStringLiteral("repairs"), QJsonArray::fromStringList(result.plan.repairs));
	QJsonArray rooms;
	for (const LevelLayoutRoom& room : result.layout.rooms) {
		const int size = result.layout.cellSize;
		rooms.append(QJsonObject {
			{QStringLiteral("id"), room.id},
			{QStringLiteral("role"), room.role},
			{QStringLiteral("x"), result.layout.origin.x() + room.rect.x() * size},
			{QStringLiteral("y"), result.layout.origin.y() + room.rect.y() * size},
			{QStringLiteral("width"), room.rect.width() * size},
			{QStringLiteral("depth"), room.rect.height() * size},
			{QStringLiteral("floor"), room.floor},
			{QStringLiteral("ceiling"), room.ceiling},
			{QStringLiteral("shape"), room.shape},
		});
	}
	const LevelGenerationStatistics& stats = result.statistics;
	return QJsonObject {
		{QStringLiteral("ok"), result.ok},
		{QStringLiteral("error"), result.error},
		{QStringLiteral("format"), result.format},
		{QStringLiteral("suggestedFileName"), result.suggestedFileName},
		{QStringLiteral("mapName"), result.mapName},
		{QStringLiteral("spec"), levelGenerationSpecJson(result.spec)},
		{QStringLiteral("plan"), plan},
		{QStringLiteral("layout"), QJsonObject {
			{QStringLiteral("cellSize"), result.layout.cellSize},
			{QStringLiteral("width"), result.layout.width},
			{QStringLiteral("height"), result.layout.height},
			{QStringLiteral("rooms"), rooms},
			{QStringLiteral("corridors"), int(result.layout.corridors.size())},
			{QStringLiteral("dropped"), QJsonArray::fromStringList(result.layout.dropped)},
		}},
		{QStringLiteral("statistics"), QJsonObject {
			{QStringLiteral("rooms"), stats.rooms},
			{QStringLiteral("reachableRooms"), stats.reachableRooms},
			{QStringLiteral("corridors"), stats.corridors},
			{QStringLiteral("brushes"), stats.brushes},
			{QStringLiteral("entities"), stats.entities},
			{QStringLiteral("lights"), stats.lights},
			{QStringLiteral("monsters"), stats.monsters},
			{QStringLiteral("items"), stats.items},
			{QStringLiteral("sectors"), stats.sectors},
			{QStringLiteral("linedefs"), stats.linedefs},
			{QStringLiteral("widthUnits"), stats.extentUnits.width()},
			{QStringLiteral("depthUnits"), stats.extentUnits.height()},
		}},
		{QStringLiteral("notes"), QJsonArray::fromStringList(result.notes)},
		{QStringLiteral("warnings"), QJsonArray::fromStringList(result.warnings)},
	};
}

QStringList levelGenerationSummaryLines(const LevelGenerationResult& result)
{
	const LevelGenerationStatistics& stats = result.statistics;
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioLevelGeneration", "%1 (%2, %3, %4 theme, seed %5), planned by %6.")
				 .arg(result.plan.title, result.spec.game, result.spec.mode, result.spec.theme)
				 .arg(result.spec.seed)
				 .arg(result.plan.planner);
	lines << QCoreApplication::translate("VibeStudioLevelGeneration", "%1 rooms and %2 corridors over %3 x %4 units; the start reaches %5 of the rooms.")
				 .arg(stats.rooms)
				 .arg(stats.corridors)
				 .arg(stats.extentUnits.width())
				 .arg(stats.extentUnits.height())
				 .arg(stats.reachableRooms);
	if (result.spec.game == QStringLiteral("doom")) {
		lines << QCoreApplication::translate("VibeStudioLevelGeneration", "%1 sectors, %2 linedefs, %3 things.").arg(stats.sectors).arg(stats.linedefs).arg(stats.entities);
	} else {
		lines << QCoreApplication::translate("VibeStudioLevelGeneration", "%1 brushes, %2 entities (%3 lights, %4 monsters, %5 items).")
					 .arg(stats.brushes)
					 .arg(stats.entities)
					 .arg(stats.lights)
					 .arg(stats.monsters)
					 .arg(stats.items);
	}
	return lines;
}

} // namespace vibestudio
