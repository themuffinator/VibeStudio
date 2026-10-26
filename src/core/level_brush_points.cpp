// Create Brush from Points and Copy Vertex Coordinates. Workflow reference:
// TrenchBroom MapWindow::debugCreateBrush and MapWindow::debugPrintVertices
// (lib/TbUiLib/src/MapWindow.cpp at 4c756be1, GPL-3.0-or-later): a typed list
// of at least four points becomes the convex brush enclosing them in the
// current material, and the selected faces' or brushes' corners are written
// out at full precision. Original implementation over VibeStudio's shared
// envelope builder and hull insertion; see docs/CREDITS.md.

#include "core/level_brush_points.h"
#include "core/level_clipper.h"
#include "core/level_primitive.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {
// The hull solver's own tolerance (core/level_brush.cpp): points closer than
// this to a line or plane do not give it a volume.
constexpr double kSpread = 0.02;
constexpr double kLimit = 32768.0;
constexpr int kMaximumListings = 4096;

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

bool finite(const LevelMapVec3& p)
{
	return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= kLimit && std::abs(p.y) <= kLimit
		&& std::abs(p.z) <= kLimit;
}

LevelMapVec3 minus(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z, true};
}

double dot(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

LevelMapVec3 cross(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}

// Appends one point from exactly three number tokens.
bool appendPoint(const QStringList& parts, QVector<LevelMapVec3>* points)
{
	if (parts.size() != 3) {
		return false;
	}
	double values[3] {};
	for (int i = 0; i < 3; ++i) {
		bool valid = false;
		values[i] = parts[i].toDouble(&valid);
		if (!valid || !std::isfinite(values[i])) {
			return false;
		}
	}
	points->push_back({values[0], values[1], values[2], true});
	return true;
}

QString componentList(const QVector<int>& ids)
{
	QStringList parts;
	for (const int id : ids) {
		parts << QString::number(id);
	}
	return parts.join(QLatin1Char(','));
}
} // namespace

bool parseLevelBrushPoints(const QString& text, QVector<LevelMapVec3>* points, QString* error)
{
	if (points) {
		points->clear();
	}
	if (error) {
		error->clear();
	}
	QVector<LevelMapVec3> parsed;
	static const QRegularExpression separators(QStringLiteral("[\\s,]+"));
	static const QRegularExpression filler(QStringLiteral("^[\\s,;]*$"));
	static const QRegularExpression group(QStringLiteral("\\(([^()]*)\\)"));
	static const QRegularExpression spacedCommas(QStringLiteral("\\s*,\\s*"));
	static const QRegularExpression spaces(QStringLiteral("\\s+"));
	// Each line takes one form, so pasted and typed lines may differ.
	for (QString line : text.split(QLatin1Char('\n'))) {
		// Comments run from // to the end of the line.
		const qsizetype comment = line.indexOf(QStringLiteral("//"));
		if (comment >= 0) {
			line.truncate(comment);
		}
		if (line.contains(QLatin1Char('(')) || line.contains(QLatin1Char(')'))) {
			// TrenchBroom's (x y z) (x y z) form; commas may separate numbers too.
			qsizetype at = 0;
			auto matches = group.globalMatch(line);
			while (matches.hasNext()) {
				const QRegularExpressionMatch match = matches.next();
				if (!filler.match(line.mid(at, match.capturedStart() - at)).hasMatch()) {
					return fail(error, QCoreApplication::translate("LevelBrushPoints", "Write every point on a line in brackets as (x y z), or none of them."));
				}
				if (!appendPoint(match.captured(1).split(separators, Qt::SkipEmptyParts), &parsed)) {
					return fail(error, QCoreApplication::translate("LevelBrushPoints", "Point %1 needs three numbers: (%2).")
											.arg(parsed.size() + 1)
											.arg(match.captured(1).trimmed()));
				}
				at = match.capturedEnd();
			}
			if (!filler.match(line.mid(at)).hasMatch()) {
				return fail(error, QCoreApplication::translate("LevelBrushPoints", "Write every point on a line in brackets as (x y z), or none of them."));
			}
			continue;
		}
		for (const QString& chunk : line.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
			QString part = chunk.trimmed();
			part.replace(spacedCommas, QStringLiteral(","));
			if (part.isEmpty()) {
				continue;
			}
			const QStringList tokens = part.split(spaces, Qt::SkipEmptyParts);
			if (part.contains(QLatin1Char(','))) {
				// x,y,z groups separated by spaces.
				for (const QString& token : tokens) {
					if (!appendPoint(token.split(QLatin1Char(',')), &parsed)) {
						return fail(error, QCoreApplication::translate("LevelBrushPoints", "Point %1 needs three numbers: %2.")
												.arg(parsed.size() + 1)
												.arg(token));
					}
				}
			} else if (tokens.size() % 3 != 0) {
				return fail(error, QCoreApplication::translate("LevelBrushPoints", "Point %1 needs three numbers: %2.")
										.arg(parsed.size() + 1)
										.arg(part));
			} else {
				for (qsizetype i = 0; i < tokens.size(); i += 3) {
					if (!appendPoint(tokens.mid(i, 3), &parsed)) {
						return fail(error, QCoreApplication::translate("LevelBrushPoints", "Point %1 needs three numbers: %2.")
												.arg(parsed.size() + 1)
												.arg(tokens.mid(i, 3).join(QLatin1Char(' '))));
					}
				}
			}
		}
	}
	if (parsed.size() > kLevelBrushPointsMaximum) {
		return fail(error, QCoreApplication::translate("LevelBrushPoints", "Type at most %1 points.").arg(kLevelBrushPointsMaximum));
	}
	if (points) {
		*points = std::move(parsed);
	}
	return true;
}

bool levelBrushPointsHull(const QVector<LevelMapVec3>& points, const QString& texture, LevelBrushTopology* topology, QString* error)
{
	if (error) {
		error->clear();
	}
	const int count = static_cast<int>(points.size());
	if (count < kLevelBrushPointsMinimum || count > kLevelBrushPointsMaximum) {
		return fail(error, QCoreApplication::translate("LevelBrushPoints", "A brush from points needs %1 to %2 points. Given: %n point(s).", nullptr, count)
								.arg(kLevelBrushPointsMinimum)
								.arg(kLevelBrushPointsMaximum));
	}
	for (int i = 0; i < count; ++i) {
		if (!finite(points[i])) {
			return fail(error, QCoreApplication::translate("LevelBrushPoints", "Point %1 is not a finite position within ±32768 units.").arg(i + 1));
		}
	}
	// Name a flat input before the hull solver does: the farthest point from
	// the first, the farthest from that line and the farthest from that plane.
	const LevelMapVec3& a = points.first();
	int b = -1;
	double best = kSpread * kSpread;
	for (int i = 0; i < count; ++i) {
		const auto d = minus(points[i], a);
		if (dot(d, d) > best) {
			best = dot(d, d);
			b = i;
		}
	}
	if (b < 0) {
		return fail(error, QCoreApplication::translate("LevelBrushPoints", "The points all lie in one place. A brush needs points that enclose a volume."));
	}
	const auto line = minus(points[b], a);
	int c = -1;
	best = kSpread * kSpread;
	for (int i = 0; i < count; ++i) {
		const auto normal = cross(minus(points[i], a), line);
		const double distance = dot(normal, normal) / dot(line, line);
		if (distance > best) {
			best = distance;
			c = i;
		}
	}
	if (c < 0) {
		return fail(error, QCoreApplication::translate("LevelBrushPoints", "The points all lie on one line. A brush needs points that enclose a volume."));
	}
	const auto normal = cross(line, minus(points[c], a));
	const double length = std::sqrt(dot(normal, normal));
	if (std::none_of(points.cbegin(), points.cend(), [&](const LevelMapVec3& p) { return std::abs(dot(normal, minus(p, a))) / length > kSpread; })) {
		return fail(error, QCoreApplication::translate("LevelBrushPoints", "The points all lie on one plane. A brush needs points that enclose a volume."));
	}
	LevelMapBrush draft;
	LevelBrushTopology solved;
	if (!createLevelBrushEnvelope(points, texture, &draft, error) || !levelBrushTopology(draft, &solved, error)) {
		return false;
	}
	if (topology) {
		*topology = std::move(solved);
	}
	return true;
}

bool addLevelMapBrushFromPoints(LevelMapDocument* document, const LevelBrushPointsRequest& request, LevelBrushPointsReport* report, QString* error)
{
	if (report) {
		*report = {};
	}
	if (error) {
		error->clear();
	}
	if (!document || (document->format != LevelMapFormat::QuakeMap && document->format != LevelMapFormat::Quake3Map)) {
		return fail(error, QCoreApplication::translate("LevelBrushPoints", "Brushes can be built from points in Quake-family .map files only."));
	}
	const QString texture = request.texture.trimmed();
	if (!validLevelClipTexture(texture)) {
		return fail(error, QCoreApplication::translate("LevelBrushPoints", "Choose a single material name for the new brush."));
	}
	LevelBrushTopology topology;
	if (!levelBrushPointsHull(request.points, texture, &topology, error)) {
		return false;
	}
	QVector<int> ids;
	const int count = static_cast<int>(request.points.size());
	if (!addLevelMapBrushHulls(document, {topology.vertices}, texture,
			QCoreApplication::translate("LevelBrushPoints", "Create brush from %n point(s)", nullptr, count),
			QCoreApplication::translate("LevelBrushPoints", "Remove brush created from points"), &ids, error)
		|| ids.isEmpty()) {
		return false;
	}
	if (report) {
		report->brushId = ids.first();
		report->inputPoints = static_cast<int>(request.points.size());
		report->corners = static_cast<int>(topology.vertices.size());
		report->unusedPoints = report->inputPoints - report->corners;
		report->faces = static_cast<int>(topology.faces.size());
		for (const LevelMapBrush& brush : document->brushes) {
			if (brush.id == report->brushId) {
				report->faces = brush.faceCount;
				report->faceDialect = brush.primitiveKind;
				break;
			}
		}
	}
	return true;
}

QJsonObject levelBrushPointsReportJson(const LevelBrushPointsReport& report)
{
	return {{QStringLiteral("brushId"), report.brushId}, {QStringLiteral("inputPoints"), report.inputPoints},
		{QStringLiteral("corners"), report.corners}, {QStringLiteral("unusedPoints"), report.unusedPoints}, {QStringLiteral("faces"), report.faces},
		{QStringLiteral("faceDialect"), report.faceDialect}};
}

QString levelMapCoordinateText(double value)
{
	if (value == 0.0) {
		return QStringLiteral("0");
	}
	return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

QString levelMapPointText(const LevelMapVec3& point)
{
	return QStringLiteral("(%1 %2 %3)").arg(levelMapCoordinateText(point.x), levelMapCoordinateText(point.y), levelMapCoordinateText(point.z));
}

bool levelMapVertexListings(const LevelMapDocument& document, const QVector<int>& brushIds, const QVector<LevelMapComponentSet>& components,
	QVector<LevelVertexListing>* listings, QString* error)
{
	if (error) {
		error->clear();
	}
	QHash<int, LevelBrushTopology> solved;
	const auto topologyFor = [&](int id, const LevelBrushTopology** topology, const LevelMapBrush** found) {
		const auto brush = std::find_if(document.brushes.cbegin(), document.brushes.cend(), [id](const LevelMapBrush& b) { return b.id == id; });
		if (brush == document.brushes.cend()) {
			return fail(error, QCoreApplication::translate("LevelBrushPoints", "Brush %1 does not exist.").arg(id));
		}
		if (!solved.contains(id)) {
			LevelBrushTopology made;
			QString reason;
			if (!levelBrushTopology(*brush, &made, &reason)) {
				return fail(error, QCoreApplication::translate("LevelBrushPoints", "Brush %1: %2").arg(id).arg(reason));
			}
			solved.insert(id, std::move(made));
		}
		*topology = &solved[id];
		*found = &*brush;
		return true;
	};
	QVector<LevelVertexListing> made;
	// True, with the error set, once the listing limit is reached.
	const auto full = [&] {
		if (made.size() < kMaximumListings) {
			return false;
		}
		fail(error, QCoreApplication::translate("LevelBrushPoints", "List at most %1 brushes and components at a time.").arg(kMaximumListings));
		return true;
	};
	for (const int id : brushIds) {
		const LevelBrushTopology* topology = nullptr;
		const LevelMapBrush* brush = nullptr;
		if (full() || !topologyFor(id, &topology, &brush)) {
			return false;
		}
		LevelVertexListing listing;
		listing.object = {LevelMapSelectionKind::QuakeBrush, id};
		listing.points = topology->vertices;
		made << listing;
	}
	for (const LevelMapComponentSet& set : components) {
		if (set.object.kind == LevelMapSelectionKind::QuakePatch && set.kind == LevelBrushComponent::Vertex) {
			const auto patch = std::find_if(document.patches.cbegin(), document.patches.cend(),
				[&](const LevelMapPatch& p) { return p.id == set.object.objectId; });
			if (patch == document.patches.cend()) {
				return fail(error, QCoreApplication::translate("LevelBrushPoints", "Patch %1 does not exist.").arg(set.object.objectId));
			}
			if (full()) {
				return false;
			}
			LevelVertexListing listing;
			listing.object = set.object;
			listing.whole = false;
			listing.components = set.components;
			for (const int point : set.components) {
				if (point < 0 || point >= patch->controlPoints.size()) {
					return fail(error, QCoreApplication::translate("LevelBrushPoints", "Patch %1 has no control point %2.").arg(set.object.objectId).arg(point));
				}
				listing.points << patch->controlPoints[point];
			}
			made << listing;
			continue;
		}
		if (set.object.kind != LevelMapSelectionKind::QuakeBrush) {
			return fail(error, QCoreApplication::translate("LevelBrushPoints", "Pick brush vertices, edges or faces, or patch control points."));
		}
		const LevelBrushTopology* topology = nullptr;
		const LevelMapBrush* brush = nullptr;
		if (!topologyFor(set.object.objectId, &topology, &brush)) {
			return false;
		}
		const auto missing = [&](int component) {
			return fail(error, QCoreApplication::translate("LevelBrushPoints", "Brush %1 has no component %2 of this kind.").arg(set.object.objectId).arg(component));
		};
		if (set.kind == LevelBrushComponent::Vertex) {
			if (full()) {
				return false;
			}
			LevelVertexListing listing;
			listing.object = set.object;
			listing.whole = false;
			listing.components = set.components;
			for (const int vertex : set.components) {
				if (vertex < 0 || vertex >= topology->vertices.size()) {
					return missing(vertex);
				}
				listing.points << topology->vertices[vertex];
			}
			made << listing;
			continue;
		}
		for (const int component : set.components) {
			const bool face = set.kind == LevelBrushComponent::Face;
			if (component < 0 || component >= (face ? topology->faces.size() : topology->edges.size())) {
				return missing(component);
			}
			if (full()) {
				return false;
			}
			LevelVertexListing listing;
			listing.object = set.object;
			listing.whole = false;
			listing.kind = set.kind;
			listing.components = {component};
			if (face) {
				for (const int vertex : topology->faces[component]) {
					listing.points << topology->vertices[vertex];
				}
				listing.texture = brush->faces.value(component).textureName;
			} else {
				listing.points << topology->vertices[topology->edges[component][0]] << topology->vertices[topology->edges[component][1]];
			}
			made << listing;
		}
	}
	if (listings) {
		*listings = std::move(made);
	}
	return true;
}

QString levelVertexListingText(const QVector<LevelVertexListing>& listings)
{
	QString text;
	for (const LevelVertexListing& listing : listings) {
		QString header = QStringLiteral("// ") + levelMapSelectionRefId(listing.object);
		if (!listing.whole) {
			if (listing.object.kind == LevelMapSelectionKind::QuakePatch) {
				header += QStringLiteral(" control:") + componentList(listing.components);
			} else if (listing.kind == LevelBrushComponent::Face) {
				header += QStringLiteral(" face:") + componentList(listing.components);
				if (!listing.texture.isEmpty()) {
					header += QLatin1Char(' ') + listing.texture;
				}
			} else if (listing.kind == LevelBrushComponent::Edge) {
				header += QStringLiteral(" edge:") + componentList(listing.components);
			} else {
				header += QStringLiteral(" vertex:") + componentList(listing.components);
			}
		}
		QStringList points;
		for (const LevelMapVec3& point : listing.points) {
			points << levelMapPointText(point);
		}
		text += header + QLatin1Char('\n') + points.join(QLatin1Char(' ')) + QLatin1Char('\n');
	}
	return text;
}

int levelVertexListingPointCount(const QVector<LevelVertexListing>& listings)
{
	int count = 0;
	for (const LevelVertexListing& listing : listings) {
		count += static_cast<int>(listing.points.size());
	}
	return count;
}
} // namespace vibestudio
