#include "core/level_doom_shapes.h"

#include <QCoreApplication>
#include <QRectF>

#include <cmath>

namespace vibestudio {

namespace {

bool fail(QString* error, const char* message)
{
	if (error) {
		*error = QCoreApplication::translate("VibeStudioLevelDoomShapes", message);
	}
	return false;
}

// Takes back the edits already made, so a failure leaves the map as it was.
void undoSteps(LevelMapDocument* document, int steps)
{
	for (int step = 0; step < steps; ++step) {
		QString ignored;
		if (!undoLevelMapEdit(document, &ignored)) {
			break;
		}
		if (!document->redoStack.isEmpty()) {
			document->redoStack.removeLast();
		}
	}
}

// Draws each cell as a sector, one undo step each, counted in `done`.
bool drawRectangles(LevelMapDocument* document, const QVector<QRectF>& cells, QVector<int>* sectorIds, int* done, QString* error)
{
	QVector<int> ids;
	for (const QRectF& cell : cells) {
		// Clockwise from the lower left, as Draw Sector traces a room.
		const QVector<LevelMapVec3> corners {{cell.left(), cell.top(), 0.0, true}, {cell.left(), cell.bottom(), 0.0, true},
			{cell.right(), cell.bottom(), 0.0, true}, {cell.right(), cell.top(), 0.0, true}};
		int sector = -1;
		if (!drawLevelMapDoomSector(document, corners, &sector, error)) {
			undoSteps(document, *done);
			return false;
		}
		++*done;
		ids.push_back(sector);
	}
	if (sectorIds) {
		*sectorIds = ids;
	}
	return true;
}

bool validFootprint(const LevelMapDocument* document, double minX, double minY, double maxX, double maxY, QString* error)
{
	if (!document || document->format != LevelMapFormat::DoomWad || document->doomFormat == LevelMapDoomFormat::Udmf) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelDoomShapes", "Sector shapes are for Doom and Hexen format maps."));
	}
	const auto inRange = [](double value) { return std::isfinite(value) && std::abs(value) <= 32767; };
	if (!inRange(minX) || !inRange(minY) || !inRange(maxX) || !inRange(maxY) || maxX <= minX || maxY <= minY) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelDoomShapes", "The footprint must lie within -32767 to 32767 and have an area."));
	}
	return true;
}

} // namespace

bool drawLevelMapDoomStairs(LevelMapDocument* document, const LevelDoomStairsRequest& request, QVector<int>* sectorIds, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!validFootprint(document, request.minX, request.minY, request.maxX, request.maxY, error)) {
		return false;
	}
	if (request.steps < 2 || request.steps > 64) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelDoomShapes", "Stairs need 2 to 64 steps."));
	}
	if (request.stepHeight == 0 || std::abs(request.stepHeight) > 1024) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelDoomShapes", "Each step rises 1 to 1024 units, or falls as much."));
	}
	const QString rise = request.rise.trimmed().toLower();
	if (rise != QStringLiteral("auto") && rise != QStringLiteral("+x") && rise != QStringLiteral("-x") && rise != QStringLiteral("+y")
		&& rise != QStringLiteral("-y")) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelDoomShapes", "Stairs climb towards auto, +x, -x, +y or -y."));
	}
	const double x0 = std::round(request.minX);
	const double y0 = std::round(request.minY);
	const double x1 = std::round(request.maxX);
	const double y1 = std::round(request.maxY);
	const bool alongX = rise == QStringLiteral("auto") ? x1 - x0 >= y1 - y0 : rise.endsWith(QLatin1Char('x'));
	const bool backwards = rise.startsWith(QLatin1Char('-'));
	const double run0 = alongX ? x0 : y0;
	const double run1 = alongX ? x1 : y1;
	if (run1 - run0 < 8.0 * request.steps) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelDoomShapes", "Each step needs at least 8 units of run; use fewer steps or a longer footprint."));
	}
	QVector<QRectF> cells;
	for (int step = 0; step < request.steps; ++step) {
		const double near = backwards ? std::round(run1 - (run1 - run0) * (step + 1) / request.steps) : std::round(run0 + (run1 - run0) * step / request.steps);
		const double far = backwards ? std::round(run1 - (run1 - run0) * step / request.steps) : std::round(run0 + (run1 - run0) * (step + 1) / request.steps);
		cells.push_back(alongX ? QRectF(QPointF(near, y0), QPointF(far, y1)) : QRectF(QPointF(x0, near), QPointF(x1, far)));
	}
	int done = 0;
	QVector<int> ids;
	if (!drawRectangles(document, cells, &ids, &done, error)) {
		return false;
	}
	// Each step rises above the floor the first one was drawn with.
	int base = 0;
	for (const LevelMapDoomSector& sector : std::as_const(document->doomSectors)) {
		base = sector.id == ids.first() ? sector.floorHeight : base;
	}
	for (int step = 0; step < ids.size(); ++step) {
		if (!setLevelMapSectorProperty(document, ids.at(step), QStringLiteral("floorHeight"), QString::number(base + (step + 1) * request.stepHeight),
				error)) {
			undoSteps(document, done);
			return false;
		}
		++done;
	}
	const int count = static_cast<int>(ids.size());
	if (!collapseLevelMapUndoSteps(document, done,
			QCoreApplication::translate("VibeStudioLevelDoomShapes", "Draw stairs of %n step(s)", nullptr, count),
			QCoreApplication::translate("VibeStudioLevelDoomShapes", "Remove the stairs of %n step(s)", nullptr, count), error)) {
		undoSteps(document, done);
		return false;
	}
	QVector<LevelMapSelectionRef> selection;
	for (const int id : std::as_const(ids)) {
		selection.push_back({LevelMapSelectionKind::DoomSector, id});
	}
	setLevelMapSelection(document, selection);
	if (sectorIds) {
		*sectorIds = ids;
	}
	return true;
}

bool drawLevelMapDoomGrid(LevelMapDocument* document, double minX, double minY, double maxX, double maxY, int columns, int rows,
	QVector<int>* sectorIds, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!validFootprint(document, minX, minY, maxX, maxY, error)) {
		return false;
	}
	if (columns < 1 || rows < 1 || columns * rows < 2 || columns > 32 || rows > 32) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelDoomShapes", "A grid has 1 to 32 columns and rows, and at least two cells."));
	}
	const double x0 = std::round(minX);
	const double y0 = std::round(minY);
	const double x1 = std::round(maxX);
	const double y1 = std::round(maxY);
	if ((x1 - x0) < 8.0 * columns || (y1 - y0) < 8.0 * rows) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelDoomShapes", "Each cell needs at least 8 units each way; use fewer cells or a bigger footprint."));
	}
	QVector<QRectF> cells;
	for (int row = 0; row < rows; ++row) {
		for (int column = 0; column < columns; ++column) {
			cells.push_back(QRectF(QPointF(std::round(x0 + (x1 - x0) * column / columns), std::round(y0 + (y1 - y0) * row / rows)),
				QPointF(std::round(x0 + (x1 - x0) * (column + 1) / columns), std::round(y0 + (y1 - y0) * (row + 1) / rows))));
		}
	}
	int done = 0;
	QVector<int> ids;
	if (!drawRectangles(document, cells, &ids, &done, error)) {
		return false;
	}
	const int count = static_cast<int>(ids.size());
	if (!collapseLevelMapUndoSteps(document, done,
			QCoreApplication::translate("VibeStudioLevelDoomShapes", "Draw a grid of %n sector(s)", nullptr, count),
			QCoreApplication::translate("VibeStudioLevelDoomShapes", "Remove the grid of %n sector(s)", nullptr, count), error)) {
		undoSteps(document, done);
		return false;
	}
	QVector<LevelMapSelectionRef> selection;
	for (const int id : std::as_const(ids)) {
		selection.push_back({LevelMapSelectionKind::DoomSector, id});
	}
	setLevelMapSelection(document, selection);
	if (sectorIds) {
		*sectorIds = ids;
	}
	return true;
}

} // namespace vibestudio
