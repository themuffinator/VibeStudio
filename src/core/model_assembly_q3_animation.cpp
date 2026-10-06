#include "core/model_assembly.h"
#include "core/package_archive.h"

#include <QCoreApplication>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
} // namespace
bool validateModelAssemblyQ3Animation(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, QString *error)
{
	if (!assembly.q3Animation)
		return true;
	if (!validateModelAssembly(assembly, error))
		return false;
	int lower = -1, upper = -1;
	for (const auto &input : resolved.inputs)
	{
		if (input.part == assembly.q3Animation->lowerPart)
			lower = int(input.mesh.frames.size());
		if (input.part == assembly.q3Animation->upperPart)
			upper = int(input.mesh.frames.size());
	}
	if (lower < 0 || upper < 0)
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
													   "Resolve both native animation model inputs before validation or export."));
	return validateModelQ3Animation(assembly.q3Animation->config, error, lower, upper);
}
bool exportModelAssemblyQ3Animation(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, const QString &path,
									const QString &sourcePath, bool overwrite, bool dryRun, QString *error, const ModelWorkControl &control,
									const std::shared_ptr<const PackageArchiveReader> &archive)
{
	if (error)
		error->clear();
	if (!assembly.q3Animation || resolved.recipeSha256 != modelAssemblyFingerprint(assembly))
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
													   "Configure native animation and reload its model inputs before export."));
	if (!validateModelAssemblyQ3Animation(assembly, resolved, error))
		return false;
	if (!path.endsWith(QStringLiteral(".cfg"), Qt::CaseInsensitive))
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly", "Native animation outputs must use the .cfg extension."));
	const auto target = inspectModelWriteTarget(path, control);
	if (!target.isValid())
		return fail(error, target.error);
	bool protectedInput = modelPathsReferToSameFile(path, sourcePath) ||
						  (archive && (archive->protectsInputPath(target.path) || archive->protectsInputPath(target.resolvedPath)));
	for (const auto &input : resolved.inputs)
		protectedInput |= modelAssemblyInputProtectsPath(input, path);
	if (protectedInput || (target.existed && !overwrite))
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
													   "Choose a separate configuration output; model, assembly and package inputs are "
													   "protected. Replacing an existing output requires overwrite."));
	const auto bytes = exportModelQ3Animation(assembly.q3Animation->config, error);
	if (bytes.isEmpty())
		return false;
	return dryRun ? modelWorkCheckpoint(control, ModelWorkPhase::Writing, bytes.size(), bytes.size(), error)
				  : writeModelFile(target, bytes, error, control);
}
} // namespace vibestudio
