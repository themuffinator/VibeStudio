#pragma once

// Headless map rendering.
//
// Produces a deterministic SVG document from a parsed map so the same picture
// is available from the CLI, from generated documentation, and from CI, without
// requiring a GUI session or a display. The interactive viewport in the shell
// paints the same geometry with QPainter; both read from `map_geometry`, so the
// two stay consistent.

#include "core/level_map.h"
#include "core/map_geometry.h"

#include <QString>
#include <QStringList>

namespace vibestudio {

enum class MapRenderProjection {
	TopXY,
	FrontXZ,
	SideZY,
};

struct MapRenderOptions {
	MapRenderProjection projection = MapRenderProjection::TopXY;
	int width = 1024;
	int height = 1024;
	int margin = 24;
	int gridSize = 64;
	bool showGrid = true;
	bool showThings = true;
	bool showEntities = true;
	bool showVertices = false;
	bool showSectorFill = true;
	bool showLabels = false;
	bool highContrast = false;
	bool darkBackground = true;
	LevelMapSelectionKind highlightKind = LevelMapSelectionKind::None;
	int highlightObjectId = -1;
};

struct MapRenderReport {
	bool rendered = false;
	QString outputPath;
	int width = 0;
	int height = 0;
	int drawnLinedefCount = 0;
	int drawnThingCount = 0;
	int drawnBrushCount = 0;
	int drawnPatchCount = 0;
	int drawnEntityCount = 0;
	int drawnSectorCount = 0;
	double unitsPerPixel = 0.0;
	QStringList warnings;
	QString error;

	[[nodiscard]] bool succeeded() const;
};

QString mapRenderProjectionId(MapRenderProjection projection);
bool mapRenderProjectionFromId(const QString& id, MapRenderProjection* out = nullptr);
QStringList mapRenderProjectionIds();

// Returns an SVG document. `report` receives per-element counts and warnings.
QString renderLevelMapSvg(const LevelMapDocument& document, const MapRenderOptions& options, MapRenderReport* report = nullptr);

// Writes the SVG to `outputPath`. Honours dry-run by reporting without writing.
MapRenderReport writeLevelMapSvg(const LevelMapDocument& document, const MapRenderOptions& options, const QString& outputPath, bool dryRun = false, bool overwriteExisting = false);

QString mapRenderReportText(const MapRenderReport& report);

} // namespace vibestudio
