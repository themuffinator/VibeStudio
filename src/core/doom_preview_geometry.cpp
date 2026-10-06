#include "core/doom_preview_geometry.h"
#include "core/level_scene.h"

#include <QCoreApplication>
#include <QMap>
#include <algorithm>
#include <cmath>

namespace vibestudio {

QString doomPreviewMaterialKey(const QString& name, bool flat)
{
	return (flat ? QStringLiteral("flats/") : QStringLiteral("textures/")) + name.trimmed().toCaseFolded();
}

namespace {
struct Edge { QPointF a, b; };
struct Boundary {
	QVector<Edge> edges;
	QHash<int, int> balance;
	bool invalid = false;
};
bool finite(const LevelMapDoomVertex& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::abs(v.x) <= 1e7 && std::abs(v.y) <= 1e7; }
bool named(const QString& name) { return !name.trimmed().isEmpty() && name != QStringLiteral("-"); }
double xAt(const Edge& edge, double y) { return edge.a.x() + (edge.b.x() - edge.a.x()) * ((y - edge.a.y()) / (edge.b.y() - edge.a.y())); }
}

DoomPreviewGeometry buildDoomPreviewGeometry(const LevelMapDocument& document, const LevelMapPreviewMeshOptions& options)
{
	DoomPreviewGeometry result;
	const auto hidden = levelSceneHiddenObjects(document);
	QHash<int, const LevelMapDoomVertex*> vertices;
	QHash<int, const LevelMapDoomSidedef*> sides;
	QHash<int, const LevelMapDoomSector*> sectors;
	for (const auto& v : document.doomVertices) { vertices.insert(v.id, &v); }
	for (const auto& s : document.doomSidedefs) { sides.insert(s.id, &s); }
	for (const auto& s : document.doomSectors) { sectors.insert(s.id, &s); }
	QHash<int, Boundary> boundaries;
	int triangles = 0;
	const int limit = std::max(0, options.triangleLimit);
	const auto cancelled = [&] { if (options.isCancelled && options.isCancelled()) { result.cancelled = true; } return result.cancelled; };
	const auto add = [&](DoomPreviewPolygon polygon) {
		const int count = static_cast<int>(polygon.points.size()) - 2;
		if (triangles + count > limit) { result.truncated = true; return false; }
		triangles += count;
		result.polygons << std::move(polygon);
		return true;
	};
	for (const auto& line : document.doomLinedefs) {
		if (cancelled()) { return result; }
		const auto* a = vertices.value(line.startVertex);
		const auto* b = vertices.value(line.endVertex);
		const auto* front = sides.value(line.frontSidedef);
		const auto* back = sides.value(line.backSidedef);
		const auto* fs = front ? sectors.value(front->sector) : nullptr;
		const auto* bs = back ? sectors.value(back->sector) : nullptr;
		const bool valid = a && b && finite(*a) && finite(*b) && (a->x != b->x || a->y != b->y);
		for (int pass = 0; pass < 2; ++pass) {
			const auto* own = pass ? bs : fs;
			if (!own || (fs && bs && fs->id == bs->id)) { continue; }
			auto& boundary = boundaries[own->id];
			if (!valid) { boundary.invalid = true; continue; }
			const auto* from = pass ? b : a; const auto* to = pass ? a : b;
			boundary.edges << Edge {{from->x, from->y}, {to->x, to->y}};
			++boundary.balance[from->id]; --boundary.balance[to->id];
		}
		if (!valid || !fs) { continue; }
		for (int pass = 0; pass < (bs ? 2 : 1); ++pass) {
			const auto& side = *(pass ? back : front);
			const auto& own = *(pass ? bs : fs);
			if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::DoomLinedef, line.id}))
				|| hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::DoomSector, own.id}))) { continue; }
			const auto* other = pass ? fs : bs;
			const auto* from = pass ? b : a; const auto* to = pass ? a : b;
			const double length = std::hypot(to->x - from->x, to->y - from->y);
			const auto wall = [&](LevelMaterialKind kind, double low, double high, const QString& texture) {
				low = std::max(low, double(own.floorHeight)); high = std::min(high, double(own.ceilingHeight));
				if (high <= low || (other && !named(texture))) { return; }
				const auto key = doomPreviewMaterialKey(texture, false);
				const auto size = options.textureSizes.value(key, QSize(64, 128));
				const double width = std::max(1, size.width()), height = std::max(1, size.height());
				// Doom wall anchors, adapted from Chocolate Doom 3.1.0 r_segs.c,
				// R_StoreWallRange/R_RenderMaskedSegRange (GPL-2.0-or-later).
				// Copyright (C) 1993-1996 Id Software, Inc.; 2005-2014 Simon Howard.
				// https://github.com/chocolate-doom/chocolate-doom/blob/chocolate-doom-3.1.0/src/doom/r_segs.c
				// This adaptation is distributed under GPL-3.0-or-later; no warranty.
				double anchor = own.ceilingHeight;
				if (kind == LevelMaterialKind::WallUpper && other) { anchor = line.flags & 8 ? own.ceilingHeight : other->ceilingHeight + height; }
				else if (kind == LevelMaterialKind::WallLower && other) { anchor = line.flags & 16 ? own.ceilingHeight : other->floorHeight; }
				else if (kind == LevelMaterialKind::WallMiddle) {
					anchor = line.flags & 16 ? (other ? std::max(own.floorHeight, other->floorHeight) : own.floorHeight) + height
						: (other ? std::min(own.ceilingHeight, other->ceilingHeight) : own.ceilingHeight);
				}
				anchor += side.offsetY;
				if (kind == LevelMaterialKind::WallMiddle && other) { low = std::max(low, anchor - height); high = std::min(high, anchor); }
				if (high <= low) { return; }
				DoomPreviewPolygon polygon;
				polygon.material = key;
				polygon.points = {{from->x, from->y, low, true}, {to->x, to->y, low, true}, {to->x, to->y, high, true}, {from->x, from->y, high, true}};
				polygon.uv = {{side.offsetX / width, (anchor - low) / height}, {(side.offsetX + length) / width, (anchor - low) / height},
					{(side.offsetX + length) / width, (anchor - high) / height}, {side.offsetX / width, (anchor - high) / height}};
				polygon.normal = {(to->y - from->y) / length, -(to->x - from->x) / length, 0, true};
				polygon.owner = {LevelMapSelectionKind::DoomLinedef, line.id}; polygon.target = {kind, side.id};
				if (add(std::move(polygon))) { ++result.walls; }
			};
			if (!other) { wall(LevelMaterialKind::WallMiddle, own.floorHeight, own.ceilingHeight, side.middleTexture); }
			else {
				wall(LevelMaterialKind::WallLower, own.floorHeight, other->floorHeight, side.lowerTexture);
				wall(LevelMaterialKind::WallUpper, other->ceilingHeight, own.ceilingHeight, side.upperTexture);
				wall(LevelMaterialKind::WallMiddle, std::max(own.floorHeight, other->floorHeight), std::min(own.ceilingHeight, other->ceilingHeight), side.middleTexture);
			}
		}
	}
	// Work is bounded independently of output: a malformed comb may have many
	// active edges without producing any useful triangles. No GUI work occurs here.
	qint64 work = 0;
	for (const auto& sector : document.doomSectors) {
		if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::DoomSector, sector.id}))) { continue; }
		if (cancelled()) { return result; }
		const auto boundary = boundaries.value(sector.id);
		if (boundary.edges.isEmpty()) { continue; }
		bool valid = !boundary.invalid && sector.floorHeight <= sector.ceilingHeight;
		for (int balance : boundary.balance) { valid &= balance == 0; }
		QMap<double, QVector<int>> starts;
		QMap<double, QVector<Edge>> horizontals;
		QVector<double> levels;
		for (int i = 0; i < boundary.edges.size(); ++i) {
			const auto& edge = boundary.edges[i];
			levels << edge.a.y() << edge.b.y();
			if (edge.a.y() != edge.b.y()) { starts[std::min(edge.a.y(), edge.b.y())] << i; }
			else { horizontals[edge.a.y()] << edge; }
		}
		std::sort(levels.begin(), levels.end()); levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
		QVector<int> active;
		QVector<QVector<QPointF>> pieces;
		int needed = 0;
		for (int band = 0; valid && band + 1 < levels.size(); ++band) {
			if (cancelled()) { return result; }
			const double y0 = levels[band], y1 = levels[band + 1], mid = (y0 + y1) * 0.5;
			active.removeIf([&](int index) { const auto& e = boundary.edges[index]; return std::max(e.a.y(), e.b.y()) <= y0; });
			active += starts.value(y0);
			work += active.size();
			if (work > 4000000) { result.truncated = true; valid = false; break; }
			std::sort(active.begin(), active.end(), [&](int a, int b) { return xAt(boundary.edges[a], mid) < xAt(boundary.edges[b], mid); });
			if (active.size() % 2) { valid = false; break; }
			// All edge order, including across holes, must remain consistent in
			// this slab. Crossings are invalid topology, not a licence to fill it.
			for (int i = 1; i < active.size(); ++i) {
				const auto& left = boundary.edges[active[i-1]]; const auto& right = boundary.edges[active[i]];
				if (xAt(left, y0) > xAt(right, y0) + 1e-8 || xAt(left, y1) > xAt(right, y1) + 1e-8
					|| std::abs(xAt(left, mid) - xAt(right, mid)) < 1e-8) { valid = false; break; }
				for (double y : {y0, y1}) {
					if (std::abs(xAt(left, y) - xAt(right, y)) < 1e-8
						&& !((left.a.y() == y || left.b.y() == y) && (right.a.y() == y || right.b.y() == y))) { valid = false; break; }
				}
			}
			for (double y : {y0, y1}) {
				if (!valid) { break; }
				auto rows = horizontals.value(y);
				std::sort(rows.begin(), rows.end(), [](const Edge& a, const Edge& b) { return std::min(a.a.x(), a.b.x()) < std::min(b.a.x(), b.b.x()); });
				double previous = -1e100;
				for (const auto& horizontal : rows) {
					const double left = std::min(horizontal.a.x(), horizontal.b.x()), right = std::max(horizontal.a.x(), horizontal.b.x());
					if (left < previous - 1e-8) { valid = false; break; } previous = right;
					const auto first = std::lower_bound(active.cbegin(), active.cend(), left - 1e-8,
						[&](int index, double x) { return xAt(boundary.edges[index], y) < x; });
					for (auto it = first; it != active.cend(); ++it) {
						const auto& edge = boundary.edges[*it]; const double x = xAt(edge, y);
						if (x > right + 1e-8) { break; }
						if (++work > 4000000) { result.truncated = true; valid = false; break; }
						if ((x > left + 1e-8 && x < right - 1e-8) || (edge.a.y() != y && edge.b.y() != y)) { valid = false; break; }
					}
				}
			}
			for (int i = 0; valid && i < active.size(); i += 2) {
				const auto& left = boundary.edges[active[i]]; const auto& right = boundary.edges[active[i+1]];
				QVector<QPointF> polygon {{xAt(left, y0), y0}, {xAt(right, y0), y0}, {xAt(right, y1), y1}, {xAt(left, y1), y1}};
				for (int c = int(polygon.size()) - 1; c >= 0; --c) { if (polygon[c] == polygon[(c + 1) % polygon.size()]) { polygon.removeAt(c); } }
				if (polygon.size() < 3) { continue; }
				needed += 2 * (int(polygon.size()) - 2);
				if (needed > limit - triangles) { result.truncated = true; valid = false; break; }
				pieces << std::move(polygon);
			}
		}
		if (!valid || pieces.isEmpty()) {
			result.warnings << QCoreApplication::translate("DoomPreview", "Sector %1: floor/ceiling preview omitted because its boundary is open, intersecting, degenerate, or exceeds the preview budget.").arg(sector.id);
			continue;
		}
		for (int ceiling = 0; ceiling < 2; ++ceiling) {
			const auto key = doomPreviewMaterialKey(ceiling ? sector.ceilingTexture : sector.floorTexture, true);
			const auto size = options.textureSizes.value(key, QSize(64, 64));
			for (auto piece : pieces) {
				if (ceiling) { std::reverse(piece.begin(), piece.end()); }
				DoomPreviewPolygon polygon;
				polygon.material = key;
				for (const auto& p : piece) {
					polygon.points << LevelMapVec3 {p.x(), p.y(), double(ceiling ? sector.ceilingHeight : sector.floorHeight), true};
					polygon.uv << QPointF(p.x() / std::max(1, size.width()), -p.y() / std::max(1, size.height()));
				}
				polygon.normal = {0, 0, ceiling ? -1.0 : 1.0, true};
				polygon.owner = {LevelMapSelectionKind::DoomSector, sector.id};
				polygon.target = {ceiling ? LevelMaterialKind::SectorCeiling : LevelMaterialKind::SectorFloor, sector.id};
				add(std::move(polygon));
			}
			if (ceiling) { ++result.ceilings; } else { ++result.floors; }
		}
	}
	return result;
}

} // namespace vibestudio
