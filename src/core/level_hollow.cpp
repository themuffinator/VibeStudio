#include "core/level_hollow.h"

#include "core/entity_builtin_catalogue.h"
#include "core/level_brush.h"
#include "core/level_clipper.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include "core/map_geometry.h"

#include <QCoreApplication>
#include <QHash>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {

// Workflow reference: NetRadiant Custom GetCommonShader("caulk") and its
// Quake caulk filter (textures/skip) at 449778b3 (GPL-2.0-or-later). Original
// Qt implementation; see docs/CREDITS.md.
QString levelMapCaulkTexture(const LevelMapDocument& document)
{
	if (document.format == LevelMapFormat::Quake3Map) {
		// idTech4's brushDef3 faces name their materials in full.
		const bool brushDef3 = std::any_of(document.brushes.cbegin(), document.brushes.cend(), [](const LevelMapBrush& brush) {
			return brush.primitiveKind == QLatin1String("brushDef3");
		});
		return brushDef3 ? QStringLiteral("textures/common/caulk") : QStringLiteral("common/caulk");
	}
	if (document.format == LevelMapFormat::QuakeMap && builtinEntityGameForMap(document) == BuiltinEntityGame::Quake) {
		return QStringLiteral("skip");
	}
	return {};
}

QString levelRoomCaulkTexture(const LevelMapDocument& document, const LevelRoomPreferences& preferences)
{
	if (!preferences.caulk) {
		return {};
	}
	const QString custom = preferences.caulkTexture.trimmed();
	return custom.isEmpty() ? levelMapCaulkTexture(document) : custom;
}

QString levelRoomPreferencesProblem(const LevelRoomPreferences& preferences)
{
	const QString texture = preferences.caulkTexture.trimmed();
	if (texture.size() > 1024) {
		return QCoreApplication::translate("VibeStudioLevelMap", "The caulk material name is longer than 1024 characters.");
	}
	if (!texture.isEmpty() && !validLevelClipTexture(texture)) {
		return QCoreApplication::translate("VibeStudioLevelMap",
			"A material name cannot contain spaces, quotes, braces, brackets, parentheses, backslashes or comments.");
	}
	return {};
}

QJsonObject levelRoomPreferencesJson(const LevelRoomPreferences& preferences)
{
	return {
		{QStringLiteral("caulk"), preferences.caulk},
		{QStringLiteral("caulkTexture"), preferences.caulkTexture.trimmed()},
		{QStringLiteral("openPickedFaces"), preferences.openPickedFaces},
	};
}

bool updateLevelRoomPreferences(const QJsonObject& values, LevelRoomPreferences* preferences, QString* error)
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
	if (!preferences) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "No Make Room options to change."));
	}
	LevelRoomPreferences next = *preferences;
	for (auto it = values.begin(); it != values.end(); ++it) {
		const QString& field = it.key();
		if (field == QLatin1String("caulk") || field == QLatin1String("openPickedFaces")) {
			if (!it.value().isBool()) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "%1 takes on or off.").arg(field));
			}
			bool& option = field == QLatin1String("caulk") ? next.caulk : next.openPickedFaces;
			option = it.value().toBool();
		} else if (field == QLatin1String("caulkTexture")) {
			if (!it.value().isString()) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "caulkTexture takes a material name, or nothing for the game's own."));
			}
			next.caulkTexture = it.value().toString().trimmed();
		} else {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Unknown Make Room option %1: use caulk, caulkTexture or openPickedFaces.").arg(field));
		}
	}
	const QString problem = levelRoomPreferencesProblem(next);
	if (!problem.isEmpty()) {
		return fail(problem);
	}
	*preferences = next;
	return true;
}

// ---------------------------------------------------------------------------
// The CSG Tool: face policies, Shrink and Expand, and its saved options.
// Workflow reference: NetRadiant Custom HollowSettings::faceExcluded,
// CSGdlg_getSettings, CSGdlg_BrushShrink/Expand and FaceOffset
// (radiant/csg.cpp) at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

namespace {

QString unitsText(double value)
{
	return QString::number(value, 'g', 12);
}

LevelMapSelectionRef brushRef(int id)
{
	return {LevelMapSelectionKind::QuakeBrush, id};
}

} // namespace

QString levelCsgFacePolicyId(LevelCsgFacePolicy policy)
{
	switch (policy) {
	case LevelCsgFacePolicy::OnlyPicked:
		return QStringLiteral("only-picked");
	case LevelCsgFacePolicy::ExcludeFacingView:
		return QStringLiteral("exclude-view");
	case LevelCsgFacePolicy::ExcludeFacingCamera:
		return QStringLiteral("exclude-camera");
	case LevelCsgFacePolicy::ExcludePicked:
		break;
	}
	return QStringLiteral("exclude-picked");
}

bool parseLevelCsgFacePolicy(const QString& id, LevelCsgFacePolicy* policy)
{
	const QString key = id.trimmed().toLower();
	for (const LevelCsgFacePolicy candidate : {LevelCsgFacePolicy::ExcludePicked, LevelCsgFacePolicy::OnlyPicked,
			 LevelCsgFacePolicy::ExcludeFacingView, LevelCsgFacePolicy::ExcludeFacingCamera}) {
		if (levelCsgFacePolicyId(candidate) == key) {
			if (policy) {
				*policy = candidate;
			}
			return true;
		}
	}
	return false;
}

QVector<int> levelCsgReachedBrushIds(const LevelMapDocument& document)
{
	QSet<int> entities;
	QSet<int> brushes;
	for (const LevelMapSelectionRef& ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushes.insert(ref.objectId);
		} else if (ref.kind == LevelMapSelectionKind::Entity) {
			entities.insert(ref.objectId);
		}
	}
	QVector<int> reached;
	for (const LevelMapBrush& brush : document.brushes) {
		if (brushes.contains(brush.id) || entities.contains(brush.entityId)) {
			reached << brush.id;
		}
	}
	return reached;
}

QVector<LevelSurfaceFace> levelMapFacesFacingAxis(const LevelMapDocument& document, const LevelMapVec3& axis)
{
	const double length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
	if (!std::isfinite(length) || length < 1e-9) {
		return {};
	}
	const QVector<int> reached = levelCsgReachedBrushIds(document);
	const QSet<int> wanted(reached.cbegin(), reached.cend());
	QVector<LevelSurfaceFace> faces;
	for (const LevelMapBrush& brush : document.brushes) {
		if (!wanted.contains(brush.id)) {
			continue;
		}
		const MapBrushGeometry geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
		if (!geometry.solved) {
			continue;
		}
		// Both ends: the faces nearest the axis and nearest its opposite, as
		// NetRadiant Custom's -proj and -cam leave out.
		QVector<double> along(geometry.faces.size(), 0.0);
		double lowest = std::numeric_limits<double>::max();
		double highest = std::numeric_limits<double>::lowest();
		for (int face = 0; face < geometry.faces.size(); ++face) {
			const MapFacePolygon& polygon = geometry.faces.at(face);
			if (!polygon.isValid()) {
				continue;
			}
			along[face] = (polygon.plane.normalX * axis.x + polygon.plane.normalY * axis.y + polygon.plane.normalZ * axis.z) / length;
			lowest = std::min(lowest, along.at(face));
			highest = std::max(highest, along.at(face));
		}
		for (int face = 0; face < geometry.faces.size() && face < brush.faces.size(); ++face) {
			if (geometry.faces.at(face).isValid() && (along.at(face) < lowest + 0.001 || along.at(face) > highest - 0.001)) {
				faces << LevelSurfaceFace {brush.id, face};
			}
		}
	}
	return faces;
}

QVector<LevelSurfaceFace> levelCsgExcludedFaces(const LevelMapDocument& document, LevelCsgFacePolicy policy,
	const QVector<LevelSurfaceFace>& picked, const LevelMapVec3& axis)
{
	if (policy == LevelCsgFacePolicy::ExcludeFacingView || policy == LevelCsgFacePolicy::ExcludeFacingCamera) {
		return levelMapFacesFacingAxis(document, axis);
	}
	const QVector<int> reached = levelCsgReachedBrushIds(document);
	const QSet<int> wanted(reached.cbegin(), reached.cend());
	QHash<int, int> faceCounts;
	for (const LevelMapBrush& brush : document.brushes) {
		if (wanted.contains(brush.id)) {
			faceCounts.insert(brush.id, static_cast<int>(brush.faces.size()));
		}
	}
	// Picks on brushes the edit does not reach, or past a brush's faces, are
	// ignored.
	QVector<LevelSurfaceFace> picks;
	for (const LevelSurfaceFace& face : picked) {
		if (face.faceIndex >= 0 && face.faceIndex < faceCounts.value(face.brushId, 0) && !picks.contains(face)) {
			picks << face;
		}
	}
	if (policy == LevelCsgFacePolicy::ExcludePicked) {
		return picks;
	}
	QVector<LevelSurfaceFace> excluded;
	for (const LevelMapBrush& brush : document.brushes) {
		if (!wanted.contains(brush.id)) {
			continue;
		}
		for (int face = 0; face < brush.faces.size(); ++face) {
			const LevelSurfaceFace candidate {brush.id, face};
			if (!picks.contains(candidate)) {
				excluded << candidate;
			}
		}
	}
	return excluded;
}

bool offsetLevelMapSelectionFaces(LevelMapDocument* document, double offset, const QVector<LevelSurfaceFace>& excluded,
	LevelMapBrushOffsetReport* report, QString* error)
{
	LevelMapBrushOffsetReport staged;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return offsetLevelMapSelectionFaces(candidate, offset, excluded, &staged, error);
	}); guarded.has_value()) {
		if (*guarded && report) {
			*report = std::move(staged);
		}
		return *guarded;
	}
	if (error) {
		error->clear();
	}
	if (report) {
		*report = {};
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || (document->format != LevelMapFormat::QuakeMap && document->format != LevelMapFormat::Quake3Map)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brushes can be shrunk or expanded in Quake-family .map files only."));
	}
	if (!std::isfinite(offset) || offset == 0.0 || std::abs(offset) > kLevelCsgMaximumThickness) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Shrink or expand by more than 0 and at most 4096 units."));
	}
	const QVector<int> reached = levelCsgReachedBrushIds(*document);
	if (reached.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Select brushes, or brush entities, to shrink or expand."));
	}
	const QSet<int> wanted(reached.cbegin(), reached.cend());
	QHash<int, QSet<int>> leftAlone;
	for (const LevelSurfaceFace& face : excluded) {
		leftAlone[face.brushId].insert(face.faceIndex);
	}
	const QSet<QString> hidden = levelMapHiddenObjects(*document);
	const QString amount = unitsText(std::abs(offset));
	QMap<int, LevelMapBrush> replacements;
	QStringList skipped;
	int movedFaces = 0;
	for (const LevelMapBrush& brush : document->brushes) {
		if (!wanted.contains(brush.id)) {
			continue;
		}
		if (hidden.contains(levelMapSelectionRefId(brushRef(brush.id)))) {
			skipped << QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 is hidden, so it was left as it was.").arg(brush.id);
			continue;
		}
		const MapBrushGeometry before = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
		if (!before.solved) {
			skipped << QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 is not a closed brush, so it cannot be shrunk or expanded.").arg(brush.id);
			continue;
		}
		// Each face moves along its own outward normal, as NetRadiant Custom's
		// FaceOffset moves the plane; its points move with it, so the
		// texture stays where it is in the world.
		LevelMapBrush moved = brush;
		const QSet<int> keep = leftAlone.value(brush.id);
		int faces = 0;
		for (int index = 0; index < moved.faces.size(); ++index) {
			if (keep.contains(index)) {
				continue;
			}
			LevelMapBrushFace& face = moved.faces[index];
			const MapPlane plane = planeFromPoints(face.p0, face.p1, face.p2, MapGeometryPrecision::PreserveCoordinates);
			if (!plane.valid) {
				continue;
			}
			const double dx = plane.normalX * offset;
			const double dy = plane.normalY * offset;
			const double dz = plane.normalZ * offset;
			for (LevelMapVec3* point : {&face.p0, &face.p1, &face.p2}) {
				point->x += dx;
				point->y += dy;
				point->z += dz;
			}
			if (face.explicitPlane && face.planeNormal.valid) {
				// n.p + d = 0, so moving the plane by delta moves d by -n.delta.
				face.planeDistance -= face.planeNormal.x * dx + face.planeNormal.y * dy + face.planeNormal.z * dz;
			}
			++faces;
		}
		if (faces == 0) {
			skipped << QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 has every face left alone, so it was not changed.").arg(brush.id);
			continue;
		}
		// Every face that bounded the brush must still bound it.
		const MapBrushGeometry after = solveBrushGeometry(moved.faces, brush.id, brush.entityId);
		bool whole = after.solved && after.faces.size() == before.faces.size();
		for (int index = 0; whole && index < before.faces.size(); ++index) {
			whole = !before.faces.at(index).isValid() || after.faces.at(index).isValid();
		}
		LevelBrushTopology topology;
		QString problem;
		if (!whole || !levelBrushTopology(moved, &topology, &problem)) {
			skipped << (offset < 0.0
					? QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 is too thin to shrink by %2 units.").arg(brush.id).arg(amount)
					: QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 would lose a face if expanded by %2 units.").arg(brush.id).arg(amount));
			continue;
		}
		replacements.insert(brush.id, moved);
		movedFaces += faces;
	}
	if (replacements.isEmpty()) {
		return fail(skipped.isEmpty() ? QCoreApplication::translate("VibeStudioLevelMap", "Select brushes, or brush entities, to shrink or expand.")
									  : skipped.first());
	}
	const int count = static_cast<int>(replacements.size());
	const QString description = offset < 0.0
		? QCoreApplication::translate("VibeStudioLevelMap", "Shrink %n brush(es) by %1 units", nullptr, count).arg(amount)
		: QCoreApplication::translate("VibeStudioLevelMap", "Expand %n brush(es) by %1 units", nullptr, count).arg(amount);
	if (!replaceLevelMapComponentGeometry(document, replacements, {}, description, error)) {
		return false;
	}
	if (report) {
		report->changed = count;
		report->movedFaces = movedFaces;
		report->skipped = skipped;
	}
	return true;
}

QString levelCsgToolPreferencesProblem(const LevelCsgToolPreferences& preferences)
{
	if (!std::isfinite(preferences.thickness) || preferences.thickness < kLevelCsgMinimumThickness
		|| preferences.thickness > kLevelCsgMaximumThickness) {
		return QCoreApplication::translate("VibeStudioLevelMap", "The CSG Tool's thickness must be from 0.001 to 4096 units.");
	}
	return {};
}

} // namespace vibestudio
