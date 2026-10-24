#include "cli/level_face_transform.h"
#include "core/level_material_paint.h"
#include "core/level_scene.h"
#include <QCoreApplication>
#include <QJsonArray>

namespace vibestudio::cli {
namespace { struct Text { Q_DECLARE_TR_FUNCTIONS(LevelFaceTransformCli) }; }
bool parseLevelFaceTransformPicks(const QStringList& selectors, const QString& context, LevelMapDocument* document,
	LevelSurfaceFacePicks* picks, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return false; };
	if (!document || !picks) { return fail(Text::tr("Load a map before reading --face picks.")); }
	if (selectors.isEmpty()) { return fail(Text::tr("--context applies only to --face picks.")); }
	if (selectors.size() > kLevelTexturedSelectionLimit) { return fail(Text::tr("At most 16,384 --face picks are allowed.")); }
	LevelSurfaceFacePicks result;
	for (const auto& selector : selectors) {
		LevelMaterialTarget target; QString problem;
		if (!parseLevelMaterialTarget(selector,&target,&problem) || target.kind != LevelMaterialKind::BrushFace) {
			return fail(Text::tr("--face requires face:brushId:faceNumber with a one-based face number, as map select-textured prints it."));
		}
		const LevelSurfaceFace face{target.objectId,target.faceIndex};
		if (!result.faces.contains(face)) { result.faces.append(face); }
	}
	if (!context.isEmpty()) {
		QString problem;
		if (!openLevelSceneGroup(document,context,&problem)) { return fail(problem); }
	}
	*picks = std::move(result); return true;
}

void addLevelFaceTransformDetails(const LevelSurfaceFaceTransformReport& report, QJsonObject* details)
{
	if (!details) { return; }
	QJsonArray faces; QJsonObject geometry;
	for (const auto& face : report.faces) { faces.append(levelMaterialTargetId({LevelMaterialKind::BrushFace,face.brushId,face.faceIndex})); }
	for (auto it = report.brushGeometry.cbegin(); it != report.brushGeometry.cend(); ++it) {
		geometry.insert(QString::number(it.key()),QString::fromLatin1(it.value().toHex()));
	}
	details->insert(QStringLiteral("faces"),faces);
	details->insert(QStringLiteral("brushGeometrySha256"),geometry);
	details->insert(QStringLiteral("retiredFaces"),report.retiredFaces);
	details->insert(QStringLiteral("parentBrushesSelected"),false);
}
} // namespace vibestudio::cli
