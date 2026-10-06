#pragma once

#include "core/level_map.h"
#include <QHash>
#include <QSize>
#include <functional>

namespace vibestudio
{
class LevelSurfaceClipboard;
struct LevelSurfacePasteOptions;
struct LevelMaterialTarget;
struct LevelSurfaceFace {
	int brushId = -1;
	int faceIndex = -1; // Zero-based source face, as used by camera picking.
	bool operator==(const LevelSurfaceFace &) const = default;
};
enum class LevelSurfaceOperation { Shift, Scale, Rotate, Fit, Align };
enum class LevelSurfaceAlignment { Keep, Minimum, Center, Maximum };
struct LevelSurfaceRequest {
	LevelSurfaceOperation operation = LevelSurfaceOperation::Fit;
	double x = 1, y = 1; // Texels for shift, size multipliers for scale, repeats for fit.
	double degrees = 0;
	LevelSurfaceAlignment alignU = LevelSurfaceAlignment::Center;
	LevelSurfaceAlignment alignV = LevelSurfaceAlignment::Center;
};

// Selected brushes, including brushes owned by selected entities. Patches use
// their control-point editor and are intentionally not included in this list.
QVector<LevelSurfaceFace> levelMapSelectedSurfaces(const LevelMapDocument &document);

class LevelSurfaceEditPlan
{
  public:
	int faceCount() const { return m_faceCount; }
	int patchCount() const { return static_cast<int>(m_patchAfter.size()); }
	int convertedFaceCount() const { return m_convertedFaces; }
	int edgeOnFaceCount() const { return m_edgeOnFaces; }
	int brushCount() const { return static_cast<int>(m_after.size()); }
	bool ready() const { return m_ready; }

  private:
	friend bool prepareLevelSurfaceEdits(const LevelMapDocument &, const QVector<LevelSurfaceFace> &, const QVector<LevelSurfaceRequest> &,
										 const QHash<QString, QSize> &, LevelSurfaceEditPlan *, QString *, const std::function<bool()> &);
	friend bool prepareLevelSurfaceEdit(const LevelMapDocument &, const QVector<LevelSurfaceFace> &, const LevelSurfaceRequest &,
										const QHash<QString, QSize> &, LevelSurfaceEditPlan *, QString *, const std::function<bool()> &);
	friend bool commitLevelSurfaceEdit(LevelMapDocument *, const LevelSurfaceEditPlan &, QString *);
	friend class LevelSurfaceStroke;
	friend bool prepareLevelSurfacePaste(const LevelMapDocument&, const QVector<LevelSurfaceFace>&, const LevelSurfaceClipboard&,
		const LevelSurfacePasteOptions&, LevelSurfaceEditPlan*, QString*, const std::function<bool()>&);
	friend bool prepareLevelSurfaceTransfer(const LevelMapDocument&, const QVector<LevelMaterialTarget>&, const LevelSurfaceClipboard&,
		const LevelSurfacePasteOptions&, LevelSurfaceEditPlan*, QString*, const std::function<bool()>&);
	quint64 m_revision = 0;
	QString m_sourcePath;
	QByteArray m_sourceHash;
	LevelMapFormat m_format = LevelMapFormat::Unknown;
	QVector<LevelMapBrush> m_before, m_after;
	QVector<LevelMapPatch> m_patchBefore, m_patchAfter;
	QVector<LevelMapSelectionRef> m_selection;
	bool m_bindSelection = false;
	int m_faceCount = 0;
	int m_convertedFaces = 0;
	int m_edgeOnFaces = 0;
	bool m_ready = false;
	bool m_paste = false;
};

// Work on an immutable document, normally on a worker. Each face keeps its
// dialect, geometry, material and flags. Required image sizes must be explicit
// or come from the package material resolver; there is no guessed 64x64 size.
// Keys are trimmed, slash-normalized and case-folded virtual material names.
bool prepareLevelSurfaceEdit(const LevelMapDocument &document, const QVector<LevelSurfaceFace> &faces, const LevelSurfaceRequest &request,
							 const QHash<QString, QSize> &textureSizes, LevelSurfaceEditPlan *plan, QString *error = nullptr,
							 const std::function<bool()> &cancelled = {});
// Ordered quick adjustments share one geometry solve and one atomic undo step.
// At most 64 adjustments per batch; any invalid intermediate result rejects all.
bool prepareLevelSurfaceEdits(const LevelMapDocument &document, const QVector<LevelSurfaceFace> &faces,
							  const QVector<LevelSurfaceRequest> &requests, const QHash<QString, QSize> &textureSizes,
							  LevelSurfaceEditPlan *plan, QString *error = nullptr, const std::function<bool()> &cancelled = {});
// Lightweight, source-validated commit. All changed faces are one undo step.
// A no-op leaves revision, undo, redo and the saved state unchanged.
bool commitLevelSurfaceEdit(LevelMapDocument *document, const LevelSurfaceEditPlan &plan, QString *error = nullptr);
} // namespace vibestudio
