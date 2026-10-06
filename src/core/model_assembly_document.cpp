#include "core/model_assembly.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

#include <algorithm>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}
ModelAssembly absoluteReferences(ModelAssembly assembly, const QString &directory)
{
	for (auto &part : assembly.parts)
	{
		if (part.sourceKind == ModelAssemblySource::File)
		{
			part.source = QDir::cleanPath(QDir(directory).absoluteFilePath(part.source));
		}
		if (part.skin && part.skin->sourceKind == ModelAssemblySource::File)
			part.skin->source = QDir::cleanPath(QDir(directory).absoluteFilePath(part.skin->source));
	}
	return assembly;
}
} // namespace

bool ModelAssemblyDocument::isModified() const
{
	return m_recovered ||
		   (m_savedHash.isEmpty() ? !m_state.assembly.parts.isEmpty() : modelAssemblyFingerprint(m_state.assembly) != m_savedHash);
}
bool ModelAssemblyDocument::setAssembly(const ModelAssembly &assembly, const QString &directory, QString *error)
{
	if (!validateModelAssembly(assembly, error))
	{
		return false;
	}
	const auto base = QDir(directory).absolutePath();
	auto candidate = absoluteReferences(assembly, base);
	if (!validateModelAssembly(candidate, error))
	{
		return false;
	}
	m_state = {std::move(candidate), assembly.parts.isEmpty() ? QString() : assembly.parts.first().id};
	m_directory = base;
	m_path.clear();
	m_resolvedPath.clear();
	m_savedHash.clear();
	m_sourceHash.clear();
	m_recoverySource.clear();
	m_recoveryResolvedSource.clear();
	m_recovered = false;
	m_undo.clear();
	m_redo.clear();
	return true;
}
bool ModelAssemblyDocument::restoreDraft(const ModelAssembly &assembly, const QString &directory, const QString &selected,
										 const QString &sourcePath, QString *error)
{
	ModelAssemblyDocument candidate;
	if (!candidate.setAssembly(assembly, directory, error))
	{
		return false;
	}
	if (!selected.isEmpty() &&
		std::none_of(assembly.parts.cbegin(), assembly.parts.cend(), [&](const auto &part) { return part.id == selected; }))
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelAssembly", "The recovered selection does not name an assembly part."));
	}
	candidate.m_state.selected = selected;
	candidate.m_recovered = true;
	if (!sourcePath.isEmpty())
	{
		candidate.m_recoverySource = QFileInfo(sourcePath).absoluteFilePath();
		candidate.m_recoveryResolvedSource = QFileInfo(sourcePath).canonicalFilePath();
	}
	*this = std::move(candidate);
	return true;
}
bool ModelAssemblyDocument::load(const QString &path, QString *error, const ModelWorkControl &control)
{
	QByteArray bytes;
	ModelAssembly assembly;
	if (QFileInfo(path).size() > modelAssemblyMaxSourceBytes)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly", "The assembly source exceeds 1 MiB."));
	}
	if (!readModelFile(path, &bytes, error, control) || !parseModelAssembly(bytes, &assembly, error))
	{
		return false;
	}
	ModelAssemblyDocument candidate;
	if (!candidate.setAssembly(assembly, QFileInfo(path).absolutePath(), error))
	{
		return false;
	}
	candidate.m_path = QFileInfo(path).absoluteFilePath();
	candidate.m_resolvedPath = QFileInfo(path).canonicalFilePath();
	candidate.m_sourceHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	candidate.m_savedHash = modelAssemblyFingerprint(candidate.m_state.assembly);
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, bytes.size(), bytes.size(), error))
	{
		return false;
	}
	*this = std::move(candidate);
	return true;
}
bool ModelAssemblyDocument::save(const QString &path, bool overwrite, QString *error, const ModelWorkControl &control, bool dryRun,
								 const std::shared_ptr<const PackageArchiveReader> &archive)
{
	if (!path.endsWith(QStringLiteral(".assembly.json"), Qt::CaseInsensitive))
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelAssembly", "Assembly sources must use the .assembly.json extension."));
	}
	const auto target = inspectModelWriteTarget(path, control);
	if (!target.isValid())
	{
		return fail(error, target.error);
	}
	if (modelPathsReferToSameFile(path, m_recoverySource) || modelPathsReferToSameFile(target.resolvedPath, m_recoveryResolvedSource))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
													   "Save the recovered assembly to a different path from its original source."));
	}
	if (archive && (archive->protectsInputPath(target.path) || archive->protectsInputPath(target.resolvedPath)))
	{
		return fail(
			error, QCoreApplication::translate("VibeStudioModelAssembly", "Write outside the assembly's input package or portable draft."));
	}
	const bool current = modelPathsReferToSameFile(path, m_path);
	if (current && (!target.existed || target.sha256 != m_sourceHash || !modelPathsReferToSameFile(target.resolvedPath, m_resolvedPath)))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
													   "The assembly source changed on disk. Reload it or save to a different path."));
	}
	if (target.existed && !current && !overwrite)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
													   "The assembly output exists. Choose another path or explicitly enable overwrite."));
	}
	if (!validateModelAssembly(m_state.assembly, error))
	{
		return false;
	}
	auto portable = m_state.assembly;
	const QDir directory(QFileInfo(path).absolutePath());
	for (auto &part : portable.parts)
	{
		if (part.sourceKind == ModelAssemblySource::File)
		{
			if (modelPathsReferToSameFile(part.source, path))
			{
				return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
															   "An assembly source cannot replace one of its model inputs."));
			}
			part.source = directory.relativeFilePath(part.source);
		}
		if (part.skin && part.skin->sourceKind == ModelAssemblySource::File)
		{
			if (modelPathsReferToSameFile(part.skin->source, path))
				return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
															   "An assembly source cannot replace one of its linked skin inputs."));
			part.skin->source = directory.relativeFilePath(part.skin->source);
		}
	}
	if (!validateModelAssembly(portable, error))
	{
		return false;
	}
	const auto bytes = QJsonDocument(modelAssemblyJson(portable)).toJson(QJsonDocument::Indented);
	if (bytes.size() > modelAssemblyMaxSourceBytes)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly", "The assembly source exceeds 1 MiB."));
	}
	const auto savedHash = modelAssemblyFingerprint(m_state.assembly);
	const auto sourceHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	if (dryRun)
	{
		return modelWorkCheckpoint(control, ModelWorkPhase::Writing, bytes.size(), bytes.size(), error);
	}
	if (!writeModelFile(target, bytes, error, control))
	{
		return false;
	}
	m_path = target.path;
	m_resolvedPath = target.resolvedPath;
	m_directory = directory.absolutePath();
	m_savedHash = savedHash;
	m_sourceHash = sourceHash;
	m_recovered = false;
	return true;
}
void ModelAssemblyDocument::commit(State next)
{
	if (modelAssemblyFingerprint(next.assembly) == modelAssemblyFingerprint(m_state.assembly))
	{
		m_state.selected = next.selected;
		return;
	}
	m_undo.append(m_state);
	m_redo.clear();
	m_state = std::move(next);
	qint64 bytes = 0;
	for (const auto &state : m_undo)
	{
		bytes += 2 * QJsonDocument(modelAssemblyJson(state.assembly)).toJson(QJsonDocument::Compact).size() + 1024;
	}
	while (!m_undo.isEmpty() && (m_undo.size() > 64 || bytes > 16 * 1024 * 1024))
	{
		bytes -= 2 * QJsonDocument(modelAssemblyJson(m_undo.first().assembly)).toJson(QJsonDocument::Compact).size() + 1024;
		m_undo.removeFirst();
	}
}
bool ModelAssemblyDocument::setPart(const QString &previousId, const ModelAssemblyPart &part, QString *error)
{
	auto candidate = m_state;
	if (previousId.isEmpty())
	{
		candidate.assembly.parts.append(part);
	}
	else
	{
		const auto found = std::find_if(candidate.assembly.parts.begin(), candidate.assembly.parts.end(),
										[&](const auto &p) { return p.id == previousId; });
		if (found == candidate.assembly.parts.end())
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelAssembly", "Select an existing assembly part."));
		}
		*found = part;
		if (candidate.assembly.q3Animation)
		{
			auto &binding = *candidate.assembly.q3Animation;
			if (binding.lowerPart == previousId)
				binding.lowerPart = part.id;
			if (binding.upperPart == previousId)
				binding.upperPart = part.id;
		}
		for (auto &child : candidate.assembly.parts)
		{
			if (child.parent == previousId)
			{
				child.parent = part.id;
			}
		}
	}
	if (!validateModelAssembly(candidate.assembly, error))
	{
		return false;
	}
	candidate.assembly = absoluteReferences(std::move(candidate.assembly), m_directory);
	if (!validateModelAssembly(candidate.assembly, error))
	{
		return false;
	}
	candidate.selected = part.id;
	commit(std::move(candidate));
	return true;
}
bool ModelAssemblyDocument::removeBranch(const QString &id, QString *error)
{
	QSet<QString> removed{id};
	auto candidate = m_state;
	const auto found =
		std::find_if(candidate.assembly.parts.cbegin(), candidate.assembly.parts.cend(), [&](const auto &p) { return p.id == id; });
	if (found == candidate.assembly.parts.cend())
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly", "Select an existing assembly part."));
	}
	for (int pass = 0; pass < candidate.assembly.parts.size(); ++pass)
	{
		for (const auto &part : candidate.assembly.parts)
		{
			if (removed.contains(part.parent))
			{
				removed.insert(part.id);
			}
		}
	}
	if (candidate.assembly.q3Animation &&
		(removed.contains(candidate.assembly.q3Animation->lowerPart) || removed.contains(candidate.assembly.q3Animation->upperPart)))
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
													   "Remove or rebind native animation before deleting one of its model parts."));
	candidate.selected = found->parent;
	candidate.assembly.parts.removeIf([&](const auto &part) { return removed.contains(part.id); });
	commit(std::move(candidate));
	return true;
}
bool ModelAssemblyDocument::setQ3Animation(const std::optional<ModelAssemblyQ3Animation> &animation, QString *error)
{
	auto candidate = m_state;
	candidate.assembly.q3Animation = animation;
	if (!validateModelAssembly(candidate.assembly, error))
		return false;
	commit(std::move(candidate));
	return true;
}
void ModelAssemblyDocument::selectPart(const QString &id)
{
	if (std::any_of(m_state.assembly.parts.cbegin(), m_state.assembly.parts.cend(), [&](const auto &p) { return p.id == id; }))
	{
		m_state.selected = id;
	}
}
bool ModelAssemblyDocument::undo()
{
	if (m_undo.isEmpty())
	{
		return false;
	}
	m_redo.append(m_state);
	m_state = m_undo.takeLast();
	return true;
}
bool ModelAssemblyDocument::redo()
{
	if (m_redo.isEmpty())
	{
		return false;
	}
	m_undo.append(m_state);
	m_state = m_redo.takeLast();
	return true;
}
} // namespace vibestudio
