#include "core/level_scene.h"
#include "core/map_render.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPointF>
#include <QPolygonF>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>

// The output document follows the SVG 1.1 Second Edition recommendation
// (https://www.w3.org/TR/SVG11/). Only the static subset is used: no scripting,
// no external references and no embedded raster data, so the result renders in
// any conforming viewer and can be diffed as text.
//
// Doom conventions used below come from the Doom Wiki map format pages:
//   - thing angles are degrees counter-clockwise with 0 pointing east
//     (https://doomwiki.org/wiki/Thing)
//   - the two-sided linedef flag is bit 2 / value 4
//     (https://doomwiki.org/wiki/Linedef)
//   - sector light levels run 0..255
//     (https://doomwiki.org/wiki/Sector)
// Quake-family map coordinates come from the `.map` text format as parsed by
// `level_map`; brush polygons are solved in `map_geometry`.

namespace vibestudio {

namespace {

// Fixed-precision, locale-independent formatting. QString::number(double, char,
// int) always formats in the C locale, unlike QString::arg() or QLocale, so the
// document is byte-identical on every machine.
QString svgNumber(double value)
{
	if (!std::isfinite(value)) {
		value = 0.0;
	}
	if (value == 0.0) {
		value = 0.0; // normalise -0.0 so the text output is stable
	}
	return QString::number(value, 'f', 3);
}

QString svgInt(int value)
{
	return QString::number(value);
}

QString escapeXml(const QString& text)
{
	QString out;
	out.reserve(text.size() + 16);
	for (const QChar ch : text) {
		const char16_t code = ch.unicode();
		switch (code) {
		case u'&':
			out += QStringLiteral("&amp;");
			break;
		case u'<':
			out += QStringLiteral("&lt;");
			break;
		case u'>':
			out += QStringLiteral("&gt;");
			break;
		case u'"':
			out += QStringLiteral("&quot;");
			break;
		case u'\'':
			out += QStringLiteral("&apos;");
			break;
		default:
			if (code < 0x20 && code != u'\t' && code != u'\n' && code != u'\r') {
				out += QLatin1Char(' ');
			} else {
				out += ch;
			}
			break;
		}
	}
	return out;
}

struct RenderPalette {
	QString background;
	QString grid;
	QString axis;
	QString oneSidedLine;
	QString twoSidedLine;
	QString vertex;
	QString thing;
	QString thingTick;
	QString worldBrush;
	QString entityBrush;
	QString patch;
	QString entity;
	QString label;
	QString highlight;
	QString leak;
	QString link;
	quint32 sectorDim = 0x000000u;
	quint32 sectorLit = 0xffffffu;
};

RenderPalette paletteFor(bool darkBackground, bool highContrast)
{
	RenderPalette palette;
	if (darkBackground && highContrast) {
		palette.background = QStringLiteral("#000000");
		palette.grid = QStringLiteral("#2a2a2a");
		palette.axis = QStringLiteral("#5f5f5f");
		palette.oneSidedLine = QStringLiteral("#ffffff");
		palette.twoSidedLine = QStringLiteral("#c8c8c8");
		palette.vertex = QStringLiteral("#ffd400");
		palette.thing = QStringLiteral("#00ff88");
		palette.thingTick = QStringLiteral("#ffffff");
		palette.worldBrush = QStringLiteral("#00d7ff");
		palette.entityBrush = QStringLiteral("#ffa500");
		palette.patch = QStringLiteral("#00ff00");
		palette.entity = QStringLiteral("#ff4fa3");
		palette.label = QStringLiteral("#ffffff");
		palette.highlight = QStringLiteral("#ffff00");
		palette.leak = QStringLiteral("#ff3030");
		palette.link = QStringLiteral("#8cb4ff");
		palette.sectorDim = 0x000000u;
		palette.sectorLit = 0x8c8c8cu;
		return palette;
	}
	if (darkBackground) {
		palette.background = QStringLiteral("#12151a");
		palette.grid = QStringLiteral("#232a33");
		palette.axis = QStringLiteral("#3c4756");
		palette.oneSidedLine = QStringLiteral("#e6e9ee");
		palette.twoSidedLine = QStringLiteral("#8b97a8");
		palette.vertex = QStringLiteral("#f0a860");
		palette.thing = QStringLiteral("#6fd36f");
		palette.thingTick = QStringLiteral("#bdf0bd");
		palette.worldBrush = QStringLiteral("#7fb3ff");
		palette.entityBrush = QStringLiteral("#ffb066");
		palette.patch = QStringLiteral("#9ad0a0");
		palette.entity = QStringLiteral("#ff7f9a");
		palette.label = QStringLiteral("#dfe4ec");
		palette.highlight = QStringLiteral("#ffd54a");
		palette.leak = QStringLiteral("#ff5656");
		palette.link = QStringLiteral("#d68ce0");
		palette.sectorDim = 0x161b22u;
		palette.sectorLit = 0x59667au;
		return palette;
	}
	if (highContrast) {
		palette.background = QStringLiteral("#ffffff");
		palette.grid = QStringLiteral("#d0d0d0");
		palette.axis = QStringLiteral("#808080");
		palette.oneSidedLine = QStringLiteral("#000000");
		palette.twoSidedLine = QStringLiteral("#404040");
		palette.vertex = QStringLiteral("#7a3a00");
		palette.thing = QStringLiteral("#006400");
		palette.thingTick = QStringLiteral("#000000");
		palette.worldBrush = QStringLiteral("#00008b");
		palette.entityBrush = QStringLiteral("#8b4500");
		palette.patch = QStringLiteral("#005f00");
		palette.entity = QStringLiteral("#8b0000");
		palette.label = QStringLiteral("#000000");
		palette.highlight = QStringLiteral("#c60000");
		palette.leak = QStringLiteral("#e00000");
		palette.link = QStringLiteral("#4b0082");
		palette.sectorDim = 0x9a9a9au;
		palette.sectorLit = 0xffffffu;
		return palette;
	}
	palette.background = QStringLiteral("#f7f8fa");
	palette.grid = QStringLiteral("#e2e6ec");
	palette.axis = QStringLiteral("#b6bfcc");
	palette.oneSidedLine = QStringLiteral("#1d2430");
	palette.twoSidedLine = QStringLiteral("#6b7688");
	palette.vertex = QStringLiteral("#b4620c");
	palette.thing = QStringLiteral("#1c7d3c");
	palette.thingTick = QStringLiteral("#0d4a22");
	palette.worldBrush = QStringLiteral("#1d5fbf");
	palette.entityBrush = QStringLiteral("#b45a00");
	palette.patch = QStringLiteral("#1f7a4d");
	palette.entity = QStringLiteral("#b3123c");
	palette.label = QStringLiteral("#1d2430");
	palette.highlight = QStringLiteral("#d92b00");
	palette.leak = QStringLiteral("#d61f1f");
	palette.link = QStringLiteral("#8e3fa0");
	palette.sectorDim = 0xd7dce4u;
	palette.sectorLit = 0xffffffu;
	return palette;
}

QString hexColor(quint32 rgb)
{
	QString text = QStringLiteral("#");
	for (int shift = 16; shift >= 0; shift -= 8) {
		const int component = static_cast<int>((rgb >> shift) & 0xffu);
		const QString part = QString::number(component, 16);
		if (part.size() < 2) {
			text += QLatin1Char('0');
		}
		text += part;
	}
	return text;
}

// A sector's tint is derived only from its light level, so the picture carries
// real map information rather than an arbitrary per-sector colour.
QString sectorTint(const RenderPalette& palette, int lightLevel)
{
	const int light = std::clamp(lightLevel, 0, 255);
	quint32 rgb = 0u;
	for (int shift = 16; shift >= 0; shift -= 8) {
		const int dim = static_cast<int>((palette.sectorDim >> shift) & 0xffu);
		const int lit = static_cast<int>((palette.sectorLit >> shift) & 0xffu);
		const int value = dim + ((lit - dim) * light) / 255;
		rgb |= static_cast<quint32>(std::clamp(value, 0, 255)) << shift;
	}
	return hexColor(rgb);
}

QPointF projectVec3(const LevelMapVec3& point, MapRenderProjection projection)
{
	switch (projection) {
	case MapRenderProjection::TopXY:
		return QPointF(point.x, point.y);
	case MapRenderProjection::FrontXZ:
		return QPointF(point.x, point.z);
	case MapRenderProjection::SideZY:
		return QPointF(point.y, point.z);
	}
	return QPointF(point.x, point.y);
}

QPointF projectXY(double x, double y, MapRenderProjection projection)
{
	LevelMapVec3 point;
	point.x = x;
	point.y = y;
	point.z = 0.0;
	point.valid = true;
	return projectVec3(point, projection);
}

bool finitePoint(const QPointF& point)
{
	return std::isfinite(point.x()) && std::isfinite(point.y());
}

struct Bounds {
	double minX = 0.0;
	double minY = 0.0;
	double maxX = 0.0;
	double maxY = 0.0;
	bool valid = false;

	void add(const QPointF& point)
	{
		if (!finitePoint(point)) {
			return;
		}
		if (!valid) {
			minX = maxX = point.x();
			minY = maxY = point.y();
			valid = true;
			return;
		}
		minX = std::min(minX, point.x());
		maxX = std::max(maxX, point.x());
		minY = std::min(minY, point.y());
		maxY = std::max(maxY, point.y());
	}
};

// World-to-pixel mapping. SVG Y grows downwards while Doom and Quake world Y
// (and Z, for the elevation projections) grow upwards, so the vertical axis is
// flipped here and nowhere else.
struct ViewTransform {
	double centerX = 0.0;
	double centerY = 0.0;
	double scale = 1.0;
	double pixelWidth = 0.0;
	double pixelHeight = 0.0;

	[[nodiscard]] QPointF toPixel(const QPointF& world) const
	{
		return QPointF(pixelWidth * 0.5 + (world.x() - centerX) * scale,
			pixelHeight * 0.5 - (world.y() - centerY) * scale);
	}
};

struct SectorShape {
	int sectorId = -1;
	int lightLevel = 0;
	int openEdgeCount = 0;
	QVector<QVector<QPointF>> loops;
};

struct LineShape {
	int linedefId = -1;
	QPointF a;
	QPointF b;
	bool twoSided = false;
	int special = 0;
	int tag = 0;
};

struct VertexShape {
	int vertexId = -1;
	QPointF position;
};

struct ThingShape {
	int thingId = -1;
	int type = 0;
	int angle = 0;
	QPointF position;
	QPointF direction;
};

struct BrushShape {
	int brushId = -1;
	int entityId = -1;
	bool brushEntity = false;
	QString className;
	QString textureName;
	QVector<QPointF> polygon;
};

struct PatchShape {
	int patchId = -1;
	int entityId = -1;
	QString textureName;
	QVector<QVector<QPointF>> polylines;
};

struct EntityShape {
	int entityId = -1;
	QString className;
	QPointF position;
};

struct RenderScene {
	QVector<SectorShape> sectors;
	QVector<LineShape> lines;
	QVector<VertexShape> vertices;
	QVector<ThingShape> things;
	QVector<BrushShape> brushes;
	QVector<PatchShape> patches;
	QVector<EntityShape> entities;
	Bounds bounds;
	int unprojectedBrushCount = 0;
};

// Andrew's monotone chain convex hull. The footprint of a convex brush under an
// orthographic projection is the convex hull of its projected vertices, which
// keeps every projection consistent without special-casing the axis pair.
double crossProduct(const QPointF& o, const QPointF& a, const QPointF& b)
{
	return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

QVector<QPointF> convexHull(QVector<QPointF> points)
{
	std::sort(points.begin(), points.end(), [](const QPointF& lhs, const QPointF& rhs) {
		if (lhs.x() != rhs.x()) {
			return lhs.x() < rhs.x();
		}
		return lhs.y() < rhs.y();
	});
	points.erase(std::unique(points.begin(), points.end(), [](const QPointF& lhs, const QPointF& rhs) {
		return std::abs(lhs.x() - rhs.x()) < 1e-6 && std::abs(lhs.y() - rhs.y()) < 1e-6;
	}),
		points.end());
	if (points.size() < 3) {
		return points;
	}
	QVector<QPointF> hull;
	hull.reserve(points.size() * 2);
	for (const QPointF& point : points) {
		while (hull.size() >= 2 && crossProduct(hull.at(hull.size() - 2), hull.at(hull.size() - 1), point) <= 0.0) {
			hull.removeLast();
		}
		hull.push_back(point);
	}
	const qsizetype lowerSize = hull.size() + 1;
	for (qsizetype index = points.size() - 2; index >= 0; --index) {
		const QPointF& point = points.at(index);
		while (hull.size() >= lowerSize && crossProduct(hull.at(hull.size() - 2), hull.at(hull.size() - 1), point) <= 0.0) {
			hull.removeLast();
		}
		hull.push_back(point);
	}
	if (!hull.isEmpty()) {
		hull.removeLast();
	}
	return hull;
}

bool documentHasDoomGeometry(const LevelMapDocument& document)
{
	return !document.doomLinedefs.isEmpty() || !document.doomVertices.isEmpty() || !document.doomThings.isEmpty() || !document.doomSectors.isEmpty();
}

QHash<int, int> entityIndexById(const LevelMapDocument& document)
{
	QHash<int, int> byId;
	byId.reserve(document.entities.size());
	for (int index = 0; index < document.entities.size(); ++index) {
		byId.insert(document.entities.at(index).id, index);
	}
	return byId;
}

void collectDoomScene(const LevelMapDocument& document, const MapRenderOptions& options, RenderScene* scene)
{
	const auto hidden = levelSceneHiddenObjects(document);
	if (options.showSectorFill && !document.doomSectors.isEmpty() && !document.doomLinedefs.isEmpty()) {
		const QVector<DoomSectorOutline> outlines = buildDoomSectorOutlines(document);
		for (const DoomSectorOutline& outline : outlines) {
		if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::DoomSector, outline.sectorId}))) { continue; }
			SectorShape shape;
			shape.sectorId = outline.sectorId;
			shape.openEdgeCount = outline.openEdgeCount;
			if (outline.sectorId >= 0 && outline.sectorId < document.doomSectors.size()) {
				shape.lightLevel = document.doomSectors.at(outline.sectorId).lightLevel;
			}
			for (const QPolygonF& loop : outline.loops) {
				if (loop.size() < 3) {
					continue;
				}
				QVector<QPointF> projected;
				projected.reserve(loop.size());
				for (const QPointF& point : loop) {
					const QPointF mapped = projectXY(point.x(), point.y(), options.projection);
					if (!finitePoint(mapped)) {
						continue;
					}
					projected.push_back(mapped);
					scene->bounds.add(mapped);
				}
				if (projected.size() >= 3) {
					shape.loops.push_back(projected);
				}
			}
			if (!shape.loops.isEmpty()) {
				scene->sectors.push_back(shape);
			}
		}
	}

	for (const LevelMapDoomLinedef& linedef : document.doomLinedefs) {
		if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::DoomLinedef, linedef.id}))) { continue; }
		if (linedef.startVertex < 0 || linedef.startVertex >= document.doomVertices.size()) {
			continue;
		}
		if (linedef.endVertex < 0 || linedef.endVertex >= document.doomVertices.size()) {
			continue;
		}
		const LevelMapDoomVertex& start = document.doomVertices.at(linedef.startVertex);
		const LevelMapDoomVertex& end = document.doomVertices.at(linedef.endVertex);
		LineShape shape;
		shape.linedefId = linedef.id;
		shape.a = projectXY(start.x, start.y, options.projection);
		shape.b = projectXY(end.x, end.y, options.projection);
		if (!finitePoint(shape.a) || !finitePoint(shape.b)) {
			continue;
		}
		// Doom linedef flag 0x0004 marks a two-sided line; a valid back sidedef
		// means the same thing for maps whose flags were not written.
		shape.twoSided = linedef.backSidedef >= 0 || (linedef.flags & 0x0004) != 0;
		shape.special = linedef.special;
		shape.tag = linedef.tag;
		scene->bounds.add(shape.a);
		scene->bounds.add(shape.b);
		scene->lines.push_back(shape);
	}

	for (const LevelMapDoomVertex& vertex : document.doomVertices) {
		if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::DoomVertex, vertex.id}))) { continue; }
		VertexShape shape;
		shape.vertexId = vertex.id;
		shape.position = projectXY(vertex.x, vertex.y, options.projection);
		if (!finitePoint(shape.position)) {
			continue;
		}
		scene->bounds.add(shape.position);
		if (options.showVertices) {
			scene->vertices.push_back(shape);
		}
	}

	if (options.showThings) {
		for (const LevelMapDoomThing& thing : document.doomThings) {
		if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::DoomThing, thing.id}))) { continue; }
			ThingShape shape;
			shape.thingId = thing.id;
			shape.type = thing.type;
			shape.angle = thing.angle;
			shape.position = projectXY(thing.x, thing.y, options.projection);
			if (!finitePoint(shape.position)) {
				continue;
			}
			const double radians = static_cast<double>(thing.angle) * 3.14159265358979323846 / 180.0;
			LevelMapVec3 direction;
			direction.x = std::cos(radians);
			direction.y = std::sin(radians);
			direction.z = 0.0;
			direction.valid = true;
			shape.direction = projectVec3(direction, options.projection);
			scene->bounds.add(shape.position);
			scene->things.push_back(shape);
		}
	}
}

void collectQuakeScene(const LevelMapDocument& document, const MapRenderOptions& options, RenderScene* scene)
{
	const auto hidden = levelSceneHiddenObjects(document);
	const QHash<int, int> entityById = entityIndexById(document);

	if (!document.brushes.isEmpty()) {
		const QVector<MapBrushGeometry> geometry = buildLevelMapBrushGeometry(document);
		for (const MapBrushGeometry& brush : geometry) {
		if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, brush.brushId}))) { continue; }
			QVector<QPointF> projected;
			QString textureName;
			for (const MapFacePolygon& face : brush.faces) {
				if (textureName.isEmpty()) {
					textureName = face.textureName;
				}
				for (const LevelMapVec3& point : face.points) {
					const QPointF mapped = projectVec3(point, options.projection);
					if (finitePoint(mapped)) {
						projected.push_back(mapped);
					}
				}
			}
			BrushShape shape;
			shape.brushId = brush.brushId;
			shape.entityId = brush.entityId;
			shape.textureName = textureName;
			const auto entityIt = entityById.constFind(brush.entityId);
			if (entityIt != entityById.constEnd()) {
				shape.className = document.entities.at(entityIt.value()).className;
			}
			shape.brushEntity = !shape.className.isEmpty() && shape.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) != 0;
			shape.polygon = convexHull(projected);
			if (shape.polygon.size() < 3) {
				++scene->unprojectedBrushCount;
				continue;
			}
			for (const QPointF& point : shape.polygon) {
				scene->bounds.add(point);
			}
			scene->brushes.push_back(shape);
		}
	}

	for (const LevelMapPatch& patch : document.patches) {
		if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch, patch.id}))) { continue; }
		const QVector<QVector<LevelMapVec3>> mesh = tessellatePatchMesh(patch, 4);
		if (mesh.isEmpty()) {
			continue;
		}
		PatchShape shape;
		shape.patchId = patch.id;
		shape.entityId = patch.entityId;
		shape.textureName = patch.textureName;
		int columnCount = 0;
		for (const QVector<LevelMapVec3>& row : mesh) {
			columnCount = std::max(columnCount, static_cast<int>(row.size()));
			QVector<QPointF> polyline;
			polyline.reserve(row.size());
			for (const LevelMapVec3& point : row) {
				const QPointF mapped = projectVec3(point, options.projection);
				if (!finitePoint(mapped)) {
					continue;
				}
				polyline.push_back(mapped);
				scene->bounds.add(mapped);
			}
			if (polyline.size() >= 2) {
				shape.polylines.push_back(polyline);
			}
		}
		for (int column = 0; column < columnCount; ++column) {
			QVector<QPointF> polyline;
			polyline.reserve(mesh.size());
			for (const QVector<LevelMapVec3>& row : mesh) {
				if (column >= row.size()) {
					continue;
				}
				const QPointF mapped = projectVec3(row.at(column), options.projection);
				if (!finitePoint(mapped)) {
					continue;
				}
				polyline.push_back(mapped);
			}
			if (polyline.size() >= 2) {
				shape.polylines.push_back(polyline);
			}
		}
		if (!shape.polylines.isEmpty()) {
			scene->patches.push_back(shape);
		}
	}

	if (options.showEntities) {
		QHash<int, bool> ownsGeometry;
		for (const LevelMapBrush& brush : document.brushes) {
			ownsGeometry.insert(brush.entityId, true);
		}
		for (const LevelMapPatch& patch : document.patches) {
		if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch, patch.id}))) { continue; }
			ownsGeometry.insert(patch.entityId, true);
		}
		for (const LevelMapEntity& entity : document.entities) {
		if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::Entity, entity.id}))) { continue; }
			if (!entity.origin.valid) {
				continue;
			}
			if (ownsGeometry.value(entity.id, false)) {
				continue;
			}
			EntityShape shape;
			shape.entityId = entity.id;
			shape.className = entity.className;
			shape.position = projectVec3(entity.origin, options.projection);
			if (!finitePoint(shape.position)) {
				continue;
			}
			scene->bounds.add(shape.position);
			scene->entities.push_back(shape);
		}
	}
}

QString titleElement(const QString& text)
{
	if (text.isEmpty()) {
		return QString();
	}
	return QStringLiteral("<title>") + escapeXml(text) + QStringLiteral("</title>");
}

void appendLineElement(QString* out, const QPointF& a, const QPointF& b, const QString& color, double strokeWidth, const QString& extraAttributes, const QString& title)
{
	*out += QStringLiteral("   <line x1=\"") + svgNumber(a.x()) + QStringLiteral("\" y1=\"") + svgNumber(a.y())
		+ QStringLiteral("\" x2=\"") + svgNumber(b.x()) + QStringLiteral("\" y2=\"") + svgNumber(b.y())
		+ QStringLiteral("\" stroke=\"") + color + QStringLiteral("\" stroke-width=\"") + svgNumber(strokeWidth)
		+ QStringLiteral("\"") + extraAttributes;
	if (title.isEmpty()) {
		*out += QStringLiteral("/>\n");
		return;
	}
	*out += QStringLiteral(">") + titleElement(title) + QStringLiteral("</line>\n");
}

QString pointListText(const QVector<QPointF>& points)
{
	QString text;
	text.reserve(points.size() * 18);
	for (int index = 0; index < points.size(); ++index) {
		if (index > 0) {
			text += QLatin1Char(' ');
		}
		text += svgNumber(points.at(index).x()) + QLatin1Char(',') + svgNumber(points.at(index).y());
	}
	return text;
}

void appendPolygonElement(QString* out, const QVector<QPointF>& points, const QString& attributes, const QString& title)
{
	if (points.size() < 3) {
		return;
	}
	*out += QStringLiteral("   <polygon points=\"") + pointListText(points) + QStringLiteral("\"") + attributes;
	if (title.isEmpty()) {
		*out += QStringLiteral("/>\n");
		return;
	}
	*out += QStringLiteral(">") + titleElement(title) + QStringLiteral("</polygon>\n");
}

void appendPolylineElement(QString* out, const QVector<QPointF>& points, const QString& attributes, const QString& title)
{
	if (points.size() < 2) {
		return;
	}
	*out += QStringLiteral("   <polyline points=\"") + pointListText(points) + QStringLiteral("\"") + attributes;
	if (title.isEmpty()) {
		*out += QStringLiteral("/>\n");
		return;
	}
	*out += QStringLiteral(">") + titleElement(title) + QStringLiteral("</polyline>\n");
}

void appendCircleElement(QString* out, const QPointF& center, double radius, const QString& attributes, const QString& title)
{
	*out += QStringLiteral("   <circle cx=\"") + svgNumber(center.x()) + QStringLiteral("\" cy=\"") + svgNumber(center.y())
		+ QStringLiteral("\" r=\"") + svgNumber(radius) + QStringLiteral("\"") + attributes;
	if (title.isEmpty()) {
		*out += QStringLiteral("/>\n");
		return;
	}
	*out += QStringLiteral(">") + titleElement(title) + QStringLiteral("</circle>\n");
}

void appendRectElement(QString* out, const QPointF& center, double halfSize, const QString& attributes, const QString& title)
{
	*out += QStringLiteral("   <rect x=\"") + svgNumber(center.x() - halfSize) + QStringLiteral("\" y=\"") + svgNumber(center.y() - halfSize)
		+ QStringLiteral("\" width=\"") + svgNumber(halfSize * 2.0) + QStringLiteral("\" height=\"") + svgNumber(halfSize * 2.0)
		+ QStringLiteral("\"") + attributes;
	if (title.isEmpty()) {
		*out += QStringLiteral("/>\n");
		return;
	}
	*out += QStringLiteral(">") + titleElement(title) + QStringLiteral("</rect>\n");
}

// Entities use a diamond marker while Doom things use a circle, so the two are
// distinguishable without relying on colour.
QVector<QPointF> diamondPoints(const QPointF& center, double radius)
{
	QVector<QPointF> points;
	points.reserve(4);
	points.push_back(QPointF(center.x(), center.y() - radius));
	points.push_back(QPointF(center.x() + radius, center.y()));
	points.push_back(QPointF(center.x(), center.y() + radius));
	points.push_back(QPointF(center.x() - radius, center.y()));
	return points;
}

QString strokeAttributes(const QString& color, double width, bool dashed = false)
{
	QString attributes = QStringLiteral(" fill=\"none\" stroke=\"") + color + QStringLiteral("\" stroke-width=\"") + svgNumber(width) + QStringLiteral("\"");
	if (dashed) {
		attributes += QStringLiteral(" stroke-dasharray=\"5 3\"");
	}
	return attributes;
}

QString projectionDisplayName(MapRenderProjection projection)
{
	switch (projection) {
	case MapRenderProjection::TopXY:
		return QCoreApplication::translate("VibeStudioMapRender", "Top (X/Y)");
	case MapRenderProjection::FrontXZ:
		return QCoreApplication::translate("VibeStudioMapRender", "Front (X/Z)");
	case MapRenderProjection::SideZY:
		return QCoreApplication::translate("VibeStudioMapRender", "Side (Y/Z)");
	}
	return QCoreApplication::translate("VibeStudioMapRender", "Top (X/Y)");
}

QString selectionKindLabel(LevelMapSelectionKind kind)
{
	return levelMapSelectionKindId(kind);
}

constexpr int kMaxGridLines = 512;

} // namespace

bool MapRenderReport::succeeded() const
{
	return error.isEmpty();
}

QString mapRenderProjectionId(MapRenderProjection projection)
{
	switch (projection) {
	case MapRenderProjection::TopXY:
		return QStringLiteral("top-xy");
	case MapRenderProjection::FrontXZ:
		return QStringLiteral("front-xz");
	case MapRenderProjection::SideZY:
		return QStringLiteral("side-zy");
	}
	return QStringLiteral("top-xy");
}

bool mapRenderProjectionFromId(const QString& id, MapRenderProjection* out)
{
	QString normalized = id.trimmed().toLower();
	normalized.replace(QLatin1Char('_'), QLatin1Char('-'));
	MapRenderProjection projection = MapRenderProjection::TopXY;
	if (normalized == QStringLiteral("top-xy") || normalized == QStringLiteral("top") || normalized == QStringLiteral("topxy") || normalized == QStringLiteral("xy")) {
		projection = MapRenderProjection::TopXY;
	} else if (normalized == QStringLiteral("front-xz") || normalized == QStringLiteral("front") || normalized == QStringLiteral("frontxz") || normalized == QStringLiteral("xz")) {
		projection = MapRenderProjection::FrontXZ;
	} else if (normalized == QStringLiteral("side-zy") || normalized == QStringLiteral("side") || normalized == QStringLiteral("sidezy") || normalized == QStringLiteral("zy") || normalized == QStringLiteral("yz")) {
		projection = MapRenderProjection::SideZY;
	} else {
		return false;
	}
	if (out != nullptr) {
		*out = projection;
	}
	return true;
}

QStringList mapRenderProjectionIds()
{
	return {QStringLiteral("top-xy"), QStringLiteral("front-xz"), QStringLiteral("side-zy")};
}

QString renderLevelMapSvg(const LevelMapDocument& document, const MapRenderOptions& options, MapRenderReport* report)
{
	MapRenderReport local;
	local.outputPath = document.outputPath;

	const int width = std::clamp(options.width, 64, 16384);
	const int height = std::clamp(options.height, 64, 16384);
	if (width != options.width || height != options.height) {
		local.warnings << QCoreApplication::translate("VibeStudioMapRender", "Requested image size was clamped to the supported range.");
	}
	const int maxMargin = std::max(0, std::min(width, height) / 2 - 8);
	const int margin = std::clamp(options.margin, 0, maxMargin);
	if (margin != options.margin) {
		local.warnings << QCoreApplication::translate("VibeStudioMapRender", "Requested margin was clamped to fit the image.");
	}
	local.width = width;
	local.height = height;

	const RenderPalette palette = paletteFor(options.darkBackground, options.highContrast);

	RenderScene scene;
	if (documentHasDoomGeometry(document)) {
		collectDoomScene(document, options, &scene);
	}
	if (!document.brushes.isEmpty() || !document.patches.isEmpty() || !document.entities.isEmpty()) {
		collectQuakeScene(document, options, &scene);
	}
	if (scene.unprojectedBrushCount > 0) {
		local.warnings << QCoreApplication::translate("VibeStudioMapRender", "Some brushes could not be solved and were omitted: %1").arg(svgInt(scene.unprojectedBrushCount));
	}
	// A leak trail runs out into the void, so the picture is framed to include
	// it rather than cutting it off at the map's edge.
	for (const LevelMapVec3& point : options.leakTrail) {
		scene.bounds.add(projectVec3(point, options.projection));
	}

	ViewTransform view;
	view.pixelWidth = static_cast<double>(width);
	view.pixelHeight = static_cast<double>(height);
	if (!scene.bounds.valid) {
		local.warnings << QCoreApplication::translate("VibeStudioMapRender", "Document contains no renderable geometry.");
		scene.bounds.minX = -1.0;
		scene.bounds.maxX = 1.0;
		scene.bounds.minY = -1.0;
		scene.bounds.maxY = 1.0;
		scene.bounds.valid = true;
	}
	double spanX = scene.bounds.maxX - scene.bounds.minX;
	double spanY = scene.bounds.maxY - scene.bounds.minY;
	if (spanX < 1e-6 || spanY < 1e-6) {
		local.warnings << QCoreApplication::translate("VibeStudioMapRender", "Projected geometry is flat on one axis for this projection.");
		spanX = std::max(spanX, 1e-6);
		spanY = std::max(spanY, 1e-6);
	}
	view.centerX = (scene.bounds.minX + scene.bounds.maxX) * 0.5;
	view.centerY = (scene.bounds.minY + scene.bounds.maxY) * 0.5;
	const double usableWidth = std::max(1.0, static_cast<double>(width - 2 * margin));
	const double usableHeight = std::max(1.0, static_cast<double>(height - 2 * margin));
	view.scale = std::min(usableWidth / spanX, usableHeight / spanY);
	if (!std::isfinite(view.scale) || view.scale <= 0.0) {
		view.scale = 1.0;
	}
	local.unitsPerPixel = 1.0 / view.scale;

	const double halfWorldWidth = (view.pixelWidth * 0.5) / view.scale;
	const double halfWorldHeight = (view.pixelHeight * 0.5) / view.scale;
	const double worldLeft = view.centerX - halfWorldWidth;
	const double worldRight = view.centerX + halfWorldWidth;
	const double worldBottom = view.centerY - halfWorldHeight;
	const double worldTop = view.centerY + halfWorldHeight;

	const double contrastBoost = options.highContrast ? 1.6 : 1.0;

	QString body;
	body.reserve(65536);

	// Grid.
	if (options.showGrid) {
		if (options.gridSize <= 0) {
			local.warnings << QCoreApplication::translate("VibeStudioMapRender", "Grid size must be greater than zero; the grid was skipped.");
		} else {
			const double grid = static_cast<double>(options.gridSize);
			const double estimated = ((worldRight - worldLeft) / grid) + ((worldTop - worldBottom) / grid) + 4.0;
			if (estimated > static_cast<double>(kMaxGridLines)) {
				local.warnings << QCoreApplication::translate("VibeStudioMapRender", "Grid skipped: %1 world units per pixel would need too many lines.").arg(svgNumber(local.unitsPerPixel));
			} else {
				body += QStringLiteral("  <g id=\"vs-grid\" shape-rendering=\"crispEdges\">\n");
				const long long firstX = static_cast<long long>(std::floor(worldLeft / grid));
				const long long lastX = static_cast<long long>(std::ceil(worldRight / grid));
				for (long long index = firstX; index <= lastX; ++index) {
					const double worldX = static_cast<double>(index) * grid;
					const QPointF top = view.toPixel(QPointF(worldX, worldTop));
					const QPointF bottom = view.toPixel(QPointF(worldX, worldBottom));
					const bool axis = index == 0;
					appendLineElement(&body, top, bottom, axis ? palette.axis : palette.grid, axis ? 1.4 : 0.5, QString(), QString());
				}
				const long long firstY = static_cast<long long>(std::floor(worldBottom / grid));
				const long long lastY = static_cast<long long>(std::ceil(worldTop / grid));
				for (long long index = firstY; index <= lastY; ++index) {
					const double worldY = static_cast<double>(index) * grid;
					const QPointF left = view.toPixel(QPointF(worldLeft, worldY));
					const QPointF right = view.toPixel(QPointF(worldRight, worldY));
					const bool axis = index == 0;
					appendLineElement(&body, left, right, axis ? palette.axis : palette.grid, axis ? 1.4 : 0.5, QString(), QString());
				}
				body += QStringLiteral("  </g>\n");
			}
		}
	}

	// Doom sector fills.
	if (!scene.sectors.isEmpty()) {
		body += QStringLiteral("  <g id=\"vs-sectors\">\n");
		for (const SectorShape& sector : scene.sectors) {
			const QString fill = sectorTint(palette, sector.lightLevel);
			QString title = QCoreApplication::translate("VibeStudioMapRender", "Sector %1 light %2").arg(svgInt(sector.sectorId), svgInt(sector.lightLevel));
			if (sector.openEdgeCount > 0) {
				title += QStringLiteral(" ") + QCoreApplication::translate("VibeStudioMapRender", "(open edges: %1)").arg(svgInt(sector.openEdgeCount));
			}
			for (const QVector<QPointF>& loop : sector.loops) {
				QVector<QPointF> pixels;
				pixels.reserve(loop.size());
				for (const QPointF& point : loop) {
					pixels.push_back(view.toPixel(point));
				}
				const QString attributes = QStringLiteral(" fill=\"") + fill + QStringLiteral("\" fill-opacity=\"")
					+ svgNumber(options.highContrast ? 0.55 : 0.35) + QStringLiteral("\" stroke=\"none\"");
				appendPolygonElement(&body, pixels, attributes, title);
			}
			++local.drawnSectorCount;
		}
		body += QStringLiteral("  </g>\n");
	}

	// Doom linedefs. One-sided lines are drawn solid and thicker than two-sided
	// lines so the two never depend on colour alone.
	if (!scene.lines.isEmpty()) {
		body += QStringLiteral("  <g id=\"vs-linedefs\" stroke-linecap=\"round\">\n");
		for (const LineShape& line : scene.lines) {
			const QPointF a = view.toPixel(line.a);
			const QPointF b = view.toPixel(line.b);
			const double strokeWidth = (line.twoSided ? 0.8 : 1.8) * contrastBoost;
			const QString color = line.twoSided ? palette.twoSidedLine : palette.oneSidedLine;
			QString title = QCoreApplication::translate("VibeStudioMapRender", "Linedef %1 (%2)").arg(svgInt(line.linedefId), line.twoSided ? QCoreApplication::translate("VibeStudioMapRender", "two-sided") : QCoreApplication::translate("VibeStudioMapRender", "one-sided"));
			if (line.special != 0 || line.tag != 0) {
				title += QStringLiteral(" ") + QCoreApplication::translate("VibeStudioMapRender", "special %1 tag %2").arg(svgInt(line.special), svgInt(line.tag));
			}
			appendLineElement(&body, a, b, color, strokeWidth, QString(), title);
			++local.drawnLinedefCount;
		}
		body += QStringLiteral("  </g>\n");
	}

	// Doom vertices.
	if (!scene.vertices.isEmpty()) {
		body += QStringLiteral("  <g id=\"vs-vertices\">\n");
		const QString attributes = QStringLiteral(" fill=\"") + palette.vertex + QStringLiteral("\" stroke=\"none\"");
		for (const VertexShape& vertex : scene.vertices) {
			appendRectElement(&body, view.toPixel(vertex.position), options.highContrast ? 2.0 : 1.5, attributes,
				QCoreApplication::translate("VibeStudioMapRender", "Vertex %1").arg(svgInt(vertex.vertexId)));
		}
		body += QStringLiteral("  </g>\n");
	}

	// Doom things: a circular marker plus a direction tick taken from the thing
	// angle (degrees counter-clockwise, 0 = east).
	if (!scene.things.isEmpty()) {
		body += QStringLiteral("  <g id=\"vs-things\">\n");
		const double radius = options.highContrast ? 4.0 : 3.0;
		const QString markerAttributes = QStringLiteral(" fill=\"none\" stroke=\"") + palette.thing + QStringLiteral("\" stroke-width=\"") + svgNumber(1.2 * contrastBoost) + QStringLiteral("\"");
		for (const ThingShape& thing : scene.things) {
			const QPointF center = view.toPixel(thing.position);
			const QString title = QCoreApplication::translate("VibeStudioMapRender", "Thing %1 type %2 angle %3").arg(svgInt(thing.thingId), svgInt(thing.type), svgInt(thing.angle));
			appendCircleElement(&body, center, radius, markerAttributes, title);
			const double dirX = thing.direction.x();
			const double dirY = -thing.direction.y();
			const double length = std::sqrt(dirX * dirX + dirY * dirY);
			if (length > 1e-6) {
				const double tick = radius + 4.0;
				const QPointF tip(center.x() + (dirX / length) * tick, center.y() + (dirY / length) * tick);
				appendLineElement(&body, center, tip, palette.thingTick, 1.2 * contrastBoost, QString(), QString());
			}
			++local.drawnThingCount;
		}
		body += QStringLiteral("  </g>\n");
	}

	// Quake-family brush footprints. Brush entities are dashed so they read
	// differently from worldspawn geometry without relying on colour.
	if (!scene.brushes.isEmpty()) {
		body += QStringLiteral("  <g id=\"vs-brushes\">\n");
		for (const BrushShape& brush : scene.brushes) {
			QVector<QPointF> pixels;
			pixels.reserve(brush.polygon.size());
			for (const QPointF& point : brush.polygon) {
				pixels.push_back(view.toPixel(point));
			}
			const QString attributes = strokeAttributes(brush.brushEntity ? palette.entityBrush : palette.worldBrush, 1.1 * contrastBoost, brush.brushEntity);
			QString title = QCoreApplication::translate("VibeStudioMapRender", "Brush %1").arg(svgInt(brush.brushId));
			if (!brush.className.isEmpty()) {
				title += QStringLiteral(" ") + QCoreApplication::translate("VibeStudioMapRender", "in %1").arg(brush.className);
			}
			if (!brush.textureName.isEmpty()) {
				title += QStringLiteral(" ") + QCoreApplication::translate("VibeStudioMapRender", "texture %1").arg(brush.textureName);
			}
			appendPolygonElement(&body, pixels, attributes, title);
			++local.drawnBrushCount;
		}
		body += QStringLiteral("  </g>\n");
	}

	// Quake III patch meshes, drawn as tessellated outlines.
	if (!scene.patches.isEmpty()) {
		body += QStringLiteral("  <g id=\"vs-patches\">\n");
		const QString attributes = strokeAttributes(palette.patch, 0.9 * contrastBoost);
		for (const PatchShape& patch : scene.patches) {
			QString title = QCoreApplication::translate("VibeStudioMapRender", "Patch %1").arg(svgInt(patch.patchId));
			if (!patch.textureName.isEmpty()) {
				title += QStringLiteral(" ") + QCoreApplication::translate("VibeStudioMapRender", "texture %1").arg(patch.textureName);
			}
			bool titled = false;
			for (const QVector<QPointF>& polyline : patch.polylines) {
				QVector<QPointF> pixels;
				pixels.reserve(polyline.size());
				for (const QPointF& point : polyline) {
					pixels.push_back(view.toPixel(point));
				}
				appendPolylineElement(&body, pixels, attributes, titled ? QString() : title);
				titled = true;
			}
			++local.drawnPatchCount;
		}
		body += QStringLiteral("  </g>\n");
	}

	// Target links, under the entity markers so each arrowhead stops at one.
	// A dashed line removes its target (killtarget); a solid one fires it.
	if (options.showTargetLinks) {
		QString links;
		const double linkWidth = 1.4 * contrastBoost;
		for (const LevelMapTargetLink& link : levelMapTargetLinks(document)) {
			const QPointF from = view.toPixel(projectVec3(link.from, options.projection));
			const QPointF to = view.toPixel(projectVec3(link.to, options.projection));
			const QPointF span = to - from;
			const double length = std::hypot(span.x(), span.y());
			if (length < 12.0) {
				continue;
			}
			const QPointF direction = span / length;
			const QPointF normal(-direction.y(), direction.x());
			const QPointF tip = to - direction * 7.0;
			appendPolylineElement(&links, {from, tip}, strokeAttributes(palette.link, linkWidth, link.key == QStringLiteral("killtarget")),
				QCoreApplication::translate("VibeStudioMapRender", "Entity %1 %2 %3, entity %4").arg(svgInt(link.sourceEntityId), link.key, link.name, svgInt(link.targetEntityId)));
			appendPolygonElement(&links, {tip, tip - direction * 8.0 + normal * 4.0, tip - direction * 8.0 - normal * 4.0},
				QStringLiteral(" fill=\"") + palette.link + QStringLiteral("\""), QString());
			++local.drawnTargetLinkCount;
		}
		if (!links.isEmpty()) {
			body += QStringLiteral("  <g id=\"vs-links\">\n") + links + QStringLiteral("  </g>\n");
		}
	}

	// Point entities.
	if (!scene.entities.isEmpty()) {
		body += QStringLiteral("  <g id=\"vs-entities\">\n");
		const double radius = options.highContrast ? 5.0 : 4.0;
		const QString attributes = QStringLiteral(" fill=\"none\" stroke=\"") + palette.entity + QStringLiteral("\" stroke-width=\"") + svgNumber(1.2 * contrastBoost) + QStringLiteral("\"");
		for (const EntityShape& entity : scene.entities) {
			const QPointF center = view.toPixel(entity.position);
			const QString title = QCoreApplication::translate("VibeStudioMapRender", "Entity %1 %2").arg(svgInt(entity.entityId), entity.className);
			appendPolygonElement(&body, diamondPoints(center, radius), attributes, title);
			++local.drawnEntityCount;
		}
		body += QStringLiteral("  </g>\n");
	}

	// Labels.
	if (options.showLabels && !scene.entities.isEmpty()) {
		body += QStringLiteral("  <g id=\"vs-labels\" font-family=\"sans-serif\" font-size=\"")
			+ svgNumber(options.highContrast ? 11.0 : 9.0) + QStringLiteral("\" fill=\"") + palette.label + QStringLiteral("\">\n");
		for (const EntityShape& entity : scene.entities) {
			if (entity.className.trimmed().isEmpty()) {
				continue;
			}
			const QPointF center = view.toPixel(entity.position);
			if (center.x() < -32.0 || center.y() < -32.0 || center.x() > view.pixelWidth + 32.0 || center.y() > view.pixelHeight + 32.0) {
				continue;
			}
			body += QStringLiteral("   <text x=\"") + svgNumber(center.x() + 6.0) + QStringLiteral("\" y=\"") + svgNumber(center.y() - 6.0)
				+ QStringLiteral("\">") + escapeXml(entity.className) + QStringLiteral("</text>\n");
		}
		body += QStringLiteral("  </g>\n");
	}

	// Highlight overlay.
	if (options.highlightKind != LevelMapSelectionKind::None && options.highlightObjectId >= 0) {
		QString overlay;
		const double highlightWidth = 2.6 * contrastBoost;
		const QString lineAttributes = strokeAttributes(palette.highlight, highlightWidth);
		switch (options.highlightKind) {
		case LevelMapSelectionKind::DoomLinedef:
			for (const LineShape& line : scene.lines) {
				if (line.linedefId != options.highlightObjectId) {
					continue;
				}
				appendLineElement(&overlay, view.toPixel(line.a), view.toPixel(line.b), palette.highlight, highlightWidth, QString(),
					QCoreApplication::translate("VibeStudioMapRender", "Highlighted linedef %1").arg(svgInt(line.linedefId)));
			}
			break;
		case LevelMapSelectionKind::DoomVertex:
			for (const VertexShape& vertex : scene.vertices) {
				if (vertex.vertexId != options.highlightObjectId) {
					continue;
				}
				appendRectElement(&overlay, view.toPixel(vertex.position), 3.0, lineAttributes,
					QCoreApplication::translate("VibeStudioMapRender", "Highlighted vertex %1").arg(svgInt(vertex.vertexId)));
			}
			break;
		case LevelMapSelectionKind::DoomThing:
			for (const ThingShape& thing : scene.things) {
				if (thing.thingId != options.highlightObjectId) {
					continue;
				}
				appendCircleElement(&overlay, view.toPixel(thing.position), 6.0, lineAttributes,
					QCoreApplication::translate("VibeStudioMapRender", "Highlighted thing %1").arg(svgInt(thing.thingId)));
			}
			break;
		case LevelMapSelectionKind::DoomSector:
			for (const SectorShape& sector : scene.sectors) {
				if (sector.sectorId != options.highlightObjectId) {
					continue;
				}
				for (const QVector<QPointF>& loop : sector.loops) {
					QVector<QPointF> pixels;
					pixels.reserve(loop.size());
					for (const QPointF& point : loop) {
						pixels.push_back(view.toPixel(point));
					}
					appendPolygonElement(&overlay, pixels, lineAttributes, QCoreApplication::translate("VibeStudioMapRender", "Highlighted sector %1").arg(svgInt(sector.sectorId)));
				}
			}
			break;
		case LevelMapSelectionKind::QuakeBrush:
			for (const BrushShape& brush : scene.brushes) {
				if (brush.brushId != options.highlightObjectId) {
					continue;
				}
				QVector<QPointF> pixels;
				pixels.reserve(brush.polygon.size());
				for (const QPointF& point : brush.polygon) {
					pixels.push_back(view.toPixel(point));
				}
				appendPolygonElement(&overlay, pixels, lineAttributes, QCoreApplication::translate("VibeStudioMapRender", "Highlighted brush %1").arg(svgInt(brush.brushId)));
			}
			break;
		case LevelMapSelectionKind::QuakePatch:
			for (const PatchShape& patch : scene.patches) {
				if (patch.patchId != options.highlightObjectId) {
					continue;
				}
				bool titled = false;
				for (const QVector<QPointF>& polyline : patch.polylines) {
					QVector<QPointF> pixels;
					pixels.reserve(polyline.size());
					for (const QPointF& point : polyline) {
						pixels.push_back(view.toPixel(point));
					}
					appendPolylineElement(&overlay, pixels, lineAttributes, titled ? QString() : QCoreApplication::translate("VibeStudioMapRender", "Highlighted patch %1").arg(svgInt(patch.patchId)));
					titled = true;
				}
			}
			break;
		case LevelMapSelectionKind::Entity:
			for (const EntityShape& entity : scene.entities) {
				if (entity.entityId != options.highlightObjectId) {
					continue;
				}
				appendPolygonElement(&overlay, diamondPoints(view.toPixel(entity.position), 7.0), lineAttributes,
					QCoreApplication::translate("VibeStudioMapRender", "Highlighted entity %1").arg(svgInt(entity.entityId)));
			}
			for (const BrushShape& brush : scene.brushes) {
				if (brush.entityId != options.highlightObjectId) {
					continue;
				}
				QVector<QPointF> pixels;
				pixels.reserve(brush.polygon.size());
				for (const QPointF& point : brush.polygon) {
					pixels.push_back(view.toPixel(point));
				}
				appendPolygonElement(&overlay, pixels, lineAttributes, QCoreApplication::translate("VibeStudioMapRender", "Highlighted brush %1").arg(svgInt(brush.brushId)));
			}
			break;
		case LevelMapSelectionKind::None:
			break;
		}
		if (overlay.isEmpty()) {
			local.warnings << QCoreApplication::translate("VibeStudioMapRender", "Highlight target %1 %2 was not found in the rendered geometry.")
								  .arg(selectionKindLabel(options.highlightKind), svgInt(options.highlightObjectId));
		} else {
			body += QStringLiteral("  <g id=\"vs-highlight\">\n") + overlay + QStringLiteral("  </g>\n");
		}
	}

	// Leak trail, drawn last so it reads over everything. Shapes as well as
	// colour mark its ends: a filled circle at the start, a hollow square at
	// the end, as in the viewport.
	if (!options.leakTrail.isEmpty()) {
		QVector<QPointF> pixels;
		pixels.reserve(options.leakTrail.size());
		for (const LevelMapVec3& point : options.leakTrail) {
			pixels.push_back(view.toPixel(projectVec3(point, options.projection)));
		}
		const double leakWidth = 2.4 * contrastBoost;
		QString leak;
		appendPolylineElement(&leak, pixels, strokeAttributes(palette.leak, leakWidth),
			QCoreApplication::translate("VibeStudioMapRender", "Leak trail, %1 points").arg(svgInt(pixels.size())));
		appendCircleElement(&leak, pixels.first(), 5.0, QStringLiteral(" fill=\"") + palette.leak + QStringLiteral("\""), QCoreApplication::translate("VibeStudioMapRender", "Leak trail start"));
		appendRectElement(&leak, pixels.last(), 5.0, strokeAttributes(palette.leak, leakWidth), QCoreApplication::translate("VibeStudioMapRender", "Leak trail end"));
		body += QStringLiteral("  <g id=\"vs-leak\">\n") + leak + QStringLiteral("  </g>\n");
		local.drawnLeakPointCount = static_cast<int>(pixels.size());
	}

	const QString mapName = document.mapName.trimmed().isEmpty() ? QCoreApplication::translate("VibeStudioMapRender", "Level map") : document.mapName;
	QString description = QCoreApplication::translate("VibeStudioMapRender", "Projection %1, %2 world units per pixel.").arg(projectionDisplayName(options.projection), svgNumber(local.unitsPerPixel));

	QString svg;
	svg.reserve(body.size() + 2048);
	svg += QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"") + svgInt(width)
		+ QStringLiteral("\" height=\"") + svgInt(height) + QStringLiteral("\" viewBox=\"0 0 ") + svgInt(width)
		+ QStringLiteral(" ") + svgInt(height) + QStringLiteral("\" role=\"img\" aria-label=\"") + escapeXml(mapName)
		+ QStringLiteral("\">\n");
	svg += QStringLiteral(" <title>") + escapeXml(mapName) + QStringLiteral("</title>\n");
	svg += QStringLiteral(" <desc>") + escapeXml(description) + QStringLiteral("</desc>\n");
	svg += QStringLiteral(" <defs><clipPath id=\"vs-viewport-clip\"><rect x=\"0\" y=\"0\" width=\"") + svgInt(width)
		+ QStringLiteral("\" height=\"") + svgInt(height) + QStringLiteral("\"/></clipPath></defs>\n");
	svg += QStringLiteral(" <rect id=\"vs-background\" x=\"0\" y=\"0\" width=\"") + svgInt(width) + QStringLiteral("\" height=\"")
		+ svgInt(height) + QStringLiteral("\" fill=\"") + palette.background + QStringLiteral("\"/>\n");
	svg += QStringLiteral(" <g id=\"vs-scene\" clip-path=\"url(#vs-viewport-clip)\">\n");
	svg += body;
	svg += QStringLiteral(" </g>\n");
	svg += QStringLiteral("</svg>\n");

	local.rendered = true;
	if (report != nullptr) {
		*report = local;
	}
	return svg;
}

MapRenderReport writeLevelMapSvg(const LevelMapDocument& document, const MapRenderOptions& options, const QString& outputPath, bool dryRun, bool overwriteExisting)
{
	MapRenderReport report;
	const QString svg = renderLevelMapSvg(document, options, &report);
	// `rendered` reports whether the file reached disk, matching the `written`
	// convention used by saveLevelMapAs; a dry run leaves it false.
	report.rendered = false;
	if (outputPath.trimmed().isEmpty()) {
		report.error = QCoreApplication::translate("VibeStudioMapRender", "Output path is required.");
		return report;
	}
	report.outputPath = QFileInfo(outputPath).absoluteFilePath();
	const QFileInfo outputInfo(report.outputPath);
	if (outputInfo.exists() && !overwriteExisting && !dryRun) {
		report.error = QCoreApplication::translate("VibeStudioMapRender", "Output already exists. Use overwrite to replace it.");
		return report;
	}
	if (dryRun) {
		report.warnings << (outputInfo.exists() ? QCoreApplication::translate("VibeStudioMapRender", "Would overwrite SVG output.") : QCoreApplication::translate("VibeStudioMapRender", "Would write SVG output."));
		return report;
	}
	if (!QDir().mkpath(outputInfo.absolutePath())) {
		report.error = QCoreApplication::translate("VibeStudioMapRender", "Unable to create output directory.");
		return report;
	}
	QSaveFile file(report.outputPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		report.error = QCoreApplication::translate("VibeStudioMapRender", "Unable to open the SVG output for writing.");
		return report;
	}
	const QByteArray bytes = svg.toUtf8();
	if (file.write(bytes) != bytes.size() || !file.commit()) {
		report.error = QCoreApplication::translate("VibeStudioMapRender", "Unable to write the SVG output.");
		return report;
	}
	report.rendered = true;
	return report;
}

QString mapRenderReportText(const MapRenderReport& report)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioMapRender", "Map render");
	if (!report.outputPath.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioMapRender", "Output: %1").arg(report.outputPath);
	}
	lines << QCoreApplication::translate("VibeStudioMapRender", "Rendered: %1").arg(report.rendered ? QCoreApplication::translate("VibeStudioMapRender", "yes") : QCoreApplication::translate("VibeStudioMapRender", "no"));
	lines << QCoreApplication::translate("VibeStudioMapRender", "Size: %1 x %2").arg(svgInt(report.width), svgInt(report.height));
	lines << QCoreApplication::translate("VibeStudioMapRender", "Units per pixel: %1").arg(svgNumber(report.unitsPerPixel));
	lines << QCoreApplication::translate("VibeStudioMapRender", "Linedefs: %1").arg(svgInt(report.drawnLinedefCount));
	lines << QCoreApplication::translate("VibeStudioMapRender", "Things: %1").arg(svgInt(report.drawnThingCount));
	lines << QCoreApplication::translate("VibeStudioMapRender", "Sectors: %1").arg(svgInt(report.drawnSectorCount));
	lines << QCoreApplication::translate("VibeStudioMapRender", "Brushes: %1").arg(svgInt(report.drawnBrushCount));
	lines << QCoreApplication::translate("VibeStudioMapRender", "Patches: %1").arg(svgInt(report.drawnPatchCount));
	lines << QCoreApplication::translate("VibeStudioMapRender", "Entities: %1").arg(svgInt(report.drawnEntityCount));
	if (report.drawnTargetLinkCount > 0) {
		lines << QCoreApplication::translate("VibeStudioMapRender", "Target links: %1").arg(svgInt(report.drawnTargetLinkCount));
	}
	if (report.drawnLeakPointCount > 0) {
		lines << QCoreApplication::translate("VibeStudioMapRender", "Leak trail points: %1").arg(svgInt(report.drawnLeakPointCount));
	}
	for (const QString& warning : report.warnings) {
		lines << QCoreApplication::translate("VibeStudioMapRender", "Warning: %1").arg(warning);
	}
	if (!report.error.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioMapRender", "Error: %1").arg(report.error);
	}
	return lines.join('\n');
}

} // namespace vibestudio
