#include "core/model_lod.h"

#include "core/model_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelMeshTools)
};
int triangleCount(const ModelMesh &mesh)
{
	int count = 0;
	for (const auto &surface : mesh.surfaces)
		count += surface.triangles.size();
	return count;
}
} // namespace

QString modelLodPath(const QString &basePath, int level)
{
	const QFileInfo info(basePath);
	const auto suffix = info.suffix().isEmpty() ? QStringLiteral("md3") : info.suffix();
	const auto name = info.completeBaseName() + QStringLiteral("_%1.").arg(level) + suffix;
	return info.path() == QStringLiteral(".") && !basePath.startsWith(QStringLiteral("./")) ? name : info.dir().filePath(name);
}

bool buildModelLods(const ModelMesh &source, int levels, double ratio, QVector<ModelLodLevel> *result, QString *error,
					const ModelWorkControl &control)
{
	if (error)
		error->clear();
	if (!result || levels < 1 || levels > 3 || !std::isfinite(ratio) || ratio < 0.05 || ratio > 0.95)
	{
		if (error)
			*error = Text::tr("Choose 1 to 3 detail levels and a ratio from 0.05 to 0.95.");
		return false;
	}
	result->clear();
	ModelMesh current = source;
	for (int level = 1; level <= levels; ++level)
	{
		ModelLodLevel next;
		next.mesh = current;
		for (int surface = 0; surface < next.mesh.surfaces.size(); ++surface)
		{
			ModelEdit edit;
			edit.kind = ModelEditKind::Decimate;
			edit.selection.surface = surface;
			for (int face = 0; face < next.mesh.surfaces[surface].triangles.size(); ++face)
				edit.selection.faces.insert(face);
			edit.tool.ratio = ratio;
			// Lower levels may give up borders that the base level keeps.
			edit.tool.preserveBoundary = level == 1;
			QString failure;
			auto candidate = next.mesh;
			if (applyModelEdit(&candidate, edit, nullptr, &failure, control))
				next.mesh = std::move(candidate);
			else
			{
				if (!modelWorkCheckpoint(control, ModelWorkPhase::Editing, 0, 0, error))
					return false;
				next.notes << Text::tr("Level %1 keeps surface %2 (%3): %4").arg(level).arg(surface).arg(next.mesh.surfaces[surface].name, failure);
			}
		}
		next.triangles = triangleCount(next.mesh);
		current = next.mesh;
		result->append(std::move(next));
	}
	return modelWorkCheckpoint(control, ModelWorkPhase::Editing, 0, 0, error);
}
} // namespace vibestudio
