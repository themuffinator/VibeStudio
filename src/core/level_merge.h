#pragma once

#include "core/level_map.h"
#include <QJsonObject>
#include <functional>

namespace vibestudio
{
struct LevelBrushMergeSource {
	int brushId = -1;
	int faceIndex = -1; // Zero-based source face.
	bool operator==(const LevelBrushMergeSource &) const = default;
};
struct LevelBrushMergeRequest {
	// Output faces are stable for a given selection, ordered by source brush ID
	// and source face. A choice resolves material, UV and flag conflicts together.
	QMap<int, LevelBrushMergeSource> faceSources;
};
struct LevelBrushMergeFace {
	QVector<LevelBrushMergeSource> sources;
	LevelBrushMergeSource chosen;
	bool conflict = false;
	bool resolved = true;
};
struct LevelBrushMergeGeometry {
	LevelMapBrush brush;
	QVector<LevelBrushMergeFace> faces;
	double volume = 0;
	int sourceFaceCount = 0;
	int unresolvedCount() const;
};

// Exact convex union only: never fills a gap, hole or concavity. Original
// exterior planes survive verbatim. Coverage is checked by bounded convex
// subtraction, not by adding overlapping brush volumes. Up to 64 brushes,
// 2,048 source faces, 4,096 vertices and 4,096 coverage fragments per request.
// Numerical classification tolerance is 1e-7 map units.
bool solveLevelBrushMerge(const QVector<LevelMapBrush> &brushes, const LevelBrushMergeRequest &request, LevelBrushMergeGeometry *result,
						  QString *error = nullptr, const std::function<bool()> &cancelled = {});

class LevelBrushMergePlan
{
  public:
	bool prepared() const { return m_prepared; }
	bool ready() const { return m_prepared && m_geometry.unresolvedCount() == 0; }
	int brushCount() const { return static_cast<int>(m_before.size()); }
	const LevelBrushMergeGeometry &geometry() const { return m_geometry; }
	const LevelMapBrush &brush() const { return m_geometry.brush; }
	// Source definitions at the precision used by Save and by the merge proof.
	const QVector<LevelMapBrush> &sourceBrushes() const { return m_canonical; }

  private:
	friend bool prepareLevelBrushMerge(const LevelMapDocument &, const LevelBrushMergeRequest &, LevelBrushMergePlan *, QString *,
									   const std::function<bool()> &);
	friend bool commitLevelBrushMerge(LevelMapDocument *, const LevelBrushMergePlan &, QString *);
	quint64 m_revision = 0;
	QString m_sourcePath;
	QByteArray m_sourceHash;
	LevelMapFormat m_format = LevelMapFormat::Unknown;
	QVector<LevelMapBrush> m_before;
	QVector<LevelMapBrush> m_canonical;
	QVector<QStringList> m_sources;
	QVector<LevelMapSelectionRef> m_selection;
	LevelBrushMergeGeometry m_geometry;
	bool m_prepared = false;
};

// Selected brushes (or all brushes in selected entities), one owner and one
// face dialect. All selected objects must be supported. Preparation is read
// only and cancellable; unresolved conflicts still return a reviewable draft.
bool prepareLevelBrushMerge(const LevelMapDocument &document, const LevelBrushMergeRequest &request, LevelBrushMergePlan *plan,
							QString *error = nullptr, const std::function<bool()> &cancelled = {});
// Source-validated, atomic replacement with one undo step and selection restore.
bool commitLevelBrushMerge(LevelMapDocument *document, const LevelBrushMergePlan &plan, QString *error = nullptr);
QJsonObject levelBrushMergeReportJson(const LevelBrushMergePlan &plan);
} // namespace vibestudio
