#include "core/model_lod.h"

#include "core/model_document.h"
#include "core/model_targets.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

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
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
// R_LoadMD3 lowercases surface names and strips a final underscore and
// character (a q3data crutch), and custom skins compare the result.
QString engineSurfaceName(const QString &name)
{
	auto result = name.toLower();
	if (result.size() > 2 && result[result.size() - 2] == QLatin1Char('_'))
		result.chop(2);
	return result;
}
// One generated level: each surface decimated by `ratio`, or the whole model
// towards `targetTriangles` shared between surfaces by their triangles.
bool reduceLevel(const ModelMesh &previous, int level, double ratio, int targetTriangles, bool preserveBoundary, ModelLodLevel *next, QString *error,
				 const ModelWorkControl &control)
{
	next->mesh = previous;
	const int total = triangleCount(previous);
	if (targetTriangles > 0 && targetTriangles >= total)
	{
		next->notes << Text::tr("Level %1 keeps every triangle: its target of %2 is not below the %3 of the level above.").arg(level).arg(targetTriangles).arg(total);
		next->triangles = total;
		return modelWorkCheckpoint(control, ModelWorkPhase::Editing, 0, 0, error);
	}
	for (int surface = 0; surface < next->mesh.surfaces.size(); ++surface)
	{
		const int faces = int(next->mesh.surfaces[surface].triangles.size());
		if (faces == 0)
			continue;
		ModelEdit edit;
		edit.kind = ModelEditKind::Decimate;
		edit.selection.surface = surface;
		for (int face = 0; face < faces; ++face)
			edit.selection.faces.insert(face);
		if (targetTriangles > 0)
			edit.tool.targetTriangles = std::max(1, int(std::lround(double(faces) * targetTriangles / std::max(1, total))));
		else
			edit.tool.ratio = ratio;
		edit.tool.preserveBoundary = preserveBoundary;
		QString failure;
		auto candidate = next->mesh;
		if (applyModelEdit(&candidate, edit, nullptr, &failure, control))
			next->mesh = std::move(candidate);
		else
		{
			if (!modelWorkCheckpoint(control, ModelWorkPhase::Editing, 0, 0, error))
				return false;
			next->notes << Text::tr("Level %1 keeps surface %2 (%3): %4").arg(level).arg(surface).arg(next->mesh.surfaces[surface].name, failure);
		}
	}
	next->triangles = triangleCount(next->mesh);
	return modelWorkCheckpoint(control, ModelWorkPhase::Editing, 0, 0, error);
}
QString severityName(ModelLodSeverity severity)
{
	return severity == ModelLodSeverity::Error	   ? Text::tr("Error")
		   : severity == ModelLodSeverity::Warning ? Text::tr("Warning")
												   : Text::tr("Note");
}
QString severityId(ModelLodSeverity severity)
{
	return severity == ModelLodSeverity::Error ? QStringLiteral("error") : severity == ModelLodSeverity::Warning ? QStringLiteral("warning") : QStringLiteral("note");
}
} // namespace

// RTCW and Enemy Territory load name.mdc before name.md3, for the base and for
// every level (R_RegisterModel in their src/renderer/tr_model.c changes the
// name's last character to 'c' first, then back to '3').
bool modelLodTargetLoadsMdcFirst(const ModelTargetProfile &target)
{
	return target.id == QStringLiteral("rtcw") || target.id == QStringLiteral("rtcw-mp") || target.id == QStringLiteral("et");
}

QString modelLodMdcPath(const QString &md3Path)
{
	auto path = md3Path;
	if (!path.isEmpty())
		path[path.size() - 1] = QLatin1Char('c');
	return path;
}

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
	if (!result || levels < 1 || levels > modelLodMaxLevels || !std::isfinite(ratio) || ratio < 0.05 || ratio > 0.95)
	{
		if (error)
			*error = Text::tr("Choose 1 to %1 detail levels and a ratio from 0.05 to 0.95. Quake III loads name_1.md3 and name_2.md3; Enemy Territory also loads name_3.md3.")
						 .arg(modelLodMaxLevels);
		return false;
	}
	return buildModelLodsFromRecipe(source, modelLodGeneratedRecipe(levels, ratio), {}, result, error, control);
}

QVector<ModelLodRecipeLevel> modelLodGeneratedRecipe(int levels, double ratio)
{
	QVector<ModelLodRecipeLevel> recipe;
	for (int level = 1; level <= levels; ++level)
	{
		ModelLodRecipeLevel entry;
		entry.level = level;
		entry.ratio = ratio;
		// Lower levels may give up borders that the first keeps.
		entry.preserveBoundary = level == 1;
		recipe.append(entry);
	}
	return recipe;
}

bool validateModelLodRecipe(const QVector<ModelLodRecipeLevel> &recipe, int maxLevels, QString *error)
{
	const int limit = maxLevels < 0 ? modelLodMaxLevels : std::min(maxLevels, modelLodMaxLevels);
	if (recipe.size() > limit)
	{
		const int count = int(recipe.size());
		return fail(error, Text::tr("The detail-level recipe has %n level(s); at most %1 can load beside the model.", nullptr, count).arg(limit));
	}
	for (int index = 0; index < recipe.size(); ++index)
	{
		const auto &level = recipe.at(index);
		if (level.level != index + 1)
			return fail(error, Text::tr("Detail levels must be numbered 1, 2 and so on in order; entry %1 is level %2.").arg(index + 1).arg(level.level));
		if (level.generated)
		{
			const bool ratioOk = std::isfinite(level.ratio) && level.ratio >= 0.05 && level.ratio <= 0.95;
			if (level.targetTriangles < 0 || (level.targetTriangles == 0 && !ratioOk))
				return fail(error, Text::tr("Generated detail level %1 needs a ratio from 0.05 to 0.95 or a positive triangle target.").arg(level.level));
			if (!level.source.isEmpty())
				return fail(error, Text::tr("Generated detail level %1 cannot also name a source model.").arg(level.level));
		}
		else if (level.source.trimmed().isEmpty() || level.source.size() > 4096)
			return fail(error, Text::tr("Hand-made detail level %1 needs the path of its model.").arg(level.level));
	}
	return true;
}

QJsonArray modelLodRecipeJson(const QVector<ModelLodRecipeLevel> &recipe)
{
	QJsonArray levels;
	for (const auto &level : recipe)
	{
		QJsonObject entry{{QStringLiteral("level"), level.level},
						  {QStringLiteral("mode"), level.generated ? QStringLiteral("generated") : QStringLiteral("source")}};
		if (level.generated)
		{
			if (level.targetTriangles > 0)
				entry.insert(QStringLiteral("targetTriangles"), level.targetTriangles);
			else
				entry.insert(QStringLiteral("ratio"), level.ratio);
			entry.insert(QStringLiteral("preserveBoundary"), level.preserveBoundary);
		}
		else
			entry.insert(QStringLiteral("source"), level.source);
		levels.append(entry);
	}
	return levels;
}

bool parseModelLodRecipe(const QJsonValue &value, QVector<ModelLodRecipeLevel> *recipe, QString *error)
{
	const auto malformed = [&] { return fail(error, Text::tr("The detail-level recipe has malformed, missing or unknown fields.")); };
	if (!value.isArray())
		return malformed();
	QVector<ModelLodRecipeLevel> result;
	for (const auto &item : value.toArray())
	{
		if (!item.isObject())
			return malformed();
		const auto object = item.toObject();
		static const QSet<QString> known{QStringLiteral("level"), QStringLiteral("mode"), QStringLiteral("ratio"), QStringLiteral("targetTriangles"),
										 QStringLiteral("preserveBoundary"), QStringLiteral("source")};
		for (auto it = object.begin(); it != object.end(); ++it)
		{
			if (!known.contains(it.key()))
				return malformed();
		}
		ModelLodRecipeLevel level;
		const auto number = object.value(QStringLiteral("level"));
		const auto mode = object.value(QStringLiteral("mode")).toString();
		if (!number.isDouble() || number.toDouble() != number.toInt(-1) || (mode != QStringLiteral("generated") && mode != QStringLiteral("source")))
			return malformed();
		level.level = number.toInt();
		level.generated = mode == QStringLiteral("generated");
		if (level.generated)
		{
			const auto ratio = object.value(QStringLiteral("ratio")), target = object.value(QStringLiteral("targetTriangles"));
			const auto boundary = object.value(QStringLiteral("preserveBoundary"));
			if (object.contains(QStringLiteral("source")) || ratio.isDouble() == target.isDouble() || (!boundary.isUndefined() && !boundary.isBool()) ||
				(target.isDouble() && target.toDouble() != target.toInt(-1)))
				return malformed();
			level.ratio = ratio.isDouble() ? ratio.toDouble() : 0.5;
			level.targetTriangles = target.isDouble() ? target.toInt() : 0;
			level.preserveBoundary = boundary.isBool() ? boundary.toBool() : level.level == 1;
		}
		else
		{
			if (!object.value(QStringLiteral("source")).isString() || object.contains(QStringLiteral("ratio")) ||
				object.contains(QStringLiteral("targetTriangles")) || object.contains(QStringLiteral("preserveBoundary")))
				return malformed();
			level.source = object.value(QStringLiteral("source")).toString();
		}
		result.append(level);
	}
	if (!validateModelLodRecipe(result, -1, error))
		return false;
	if (recipe)
		*recipe = std::move(result);
	return true;
}

QString modelLodRecipeSourcePath(const ModelLodRecipeLevel &level, const QString &baseDirectory)
{
	if (level.generated || level.source.isEmpty())
		return {};
	if (QFileInfo(level.source).isAbsolute() || baseDirectory.isEmpty())
		return QDir::cleanPath(level.source);
	return QDir::cleanPath(QDir(baseDirectory).filePath(level.source));
}

void absoluteModelLodSources(QVector<ModelLodRecipeLevel> *recipe, const QString &directory)
{
	if (!recipe || directory.isEmpty())
		return;
	for (auto &level : *recipe)
	{
		if (!level.generated && !level.source.isEmpty())
			level.source = modelLodRecipeSourcePath(level, directory);
	}
}

void relativeModelLodSources(QVector<ModelLodRecipeLevel> *recipe, const QString &directory)
{
	if (!recipe || directory.isEmpty())
		return;
	const QDir folder(directory);
	for (auto &level : *recipe)
	{
		if (!level.generated && QFileInfo(level.source).isAbsolute())
			level.source = folder.relativeFilePath(QDir::cleanPath(level.source));
	}
}

bool loadModelLodSource(const QString &path, ModelMesh *mesh, QString *error, const ModelWorkControl &control)
{
	QByteArray bytes;
	if (!readModelFile(path, &bytes, error, control))
		return false;
	ModelMesh result;
	const bool editable = path.endsWith(QStringLiteral(".mesh.json"), Qt::CaseInsensitive);
	if (!(editable ? parseEditableModel(bytes, &result, error, control) : importEditableModel(path, bytes, &result, error, nullptr, control)))
		return false;
	if (mesh)
		*mesh = std::move(result);
	return true;
}

bool buildModelLodsFromRecipe(const ModelMesh &base, const QVector<ModelLodRecipeLevel> &recipe, const QString &baseDirectory,
							  QVector<ModelLodLevel> *result, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	if (!result)
		return fail(error, Text::tr("No detail levels were requested."));
	if (recipe.isEmpty())
		return fail(error, Text::tr("The detail-level recipe has no levels."));
	if (!validateModelLodRecipe(recipe, -1, error))
		return false;
	QVector<ModelLodLevel> levels;
	const ModelMesh *previous = &base;
	for (const auto &entry : recipe)
	{
		ModelLodLevel next;
		if (entry.generated)
		{
			if (!reduceLevel(*previous, entry.level, entry.ratio, entry.targetTriangles, entry.preserveBoundary, &next, error, control))
				return false;
		}
		else
		{
			next.source = modelLodRecipeSourcePath(entry, baseDirectory);
			QString problem;
			if (!loadModelLodSource(next.source, &next.mesh, &problem, control))
			{
				if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error))
					return false;
				return fail(error, Text::tr("Detail level %1 (%2): %3").arg(entry.level).arg(next.source, problem));
			}
			next.triangles = triangleCount(next.mesh);
		}
		levels.append(std::move(next));
		previous = &levels.last().mesh;
	}
	*result = std::move(levels);
	return modelWorkCheckpoint(control, ModelWorkPhase::Editing, 0, 0, error);
}

int ModelLodValidation::count(ModelLodSeverity severity) const
{
	return int(std::count_if(findings.cbegin(), findings.cend(), [severity](const ModelLodFinding &finding) { return finding.severity == severity; }));
}

bool validateModelLodSet(const ModelMesh &base, const QVector<ModelMesh> &levels, const ModelTargetProfile &target, ModelLodValidation *validation,
						 QString *error, const ModelWorkControl &control, const QVector<int> &levelNumbers)
{
	if (error)
		error->clear();
	if (!levelNumbers.isEmpty() && levelNumbers.size() != levels.size())
		return fail(error, Text::tr("Each detail level needs its level number."));
	ModelLodValidation result;
	const auto add = [&](ModelLodSeverity severity, int level, const char *code, const QString &message)
	{ result.findings.append({severity, level, QString::fromLatin1(code), message}); };
	const auto numberOf = [&](int index) { return levelNumbers.isEmpty() ? index + 1 : levelNumbers.at(index); };
	const int loaded = std::max(0, target.limits.detailLevels);
	const int highest = levels.isEmpty() ? 0 : numberOf(int(levels.size()) - 1);
	if (highest > loaded)
	{
		const int count = int(levels.size());
		add(ModelLodSeverity::Error, 0, "target",
			Text::tr("%1 loads at most %2 detail level(s) beside the model; the set has %n.", nullptr, count).arg(target.label).arg(loaded));
	}
	QHash<QString, int> baseSurfaces;
	for (int s = 0; s < base.surfaces.size(); ++s)
		baseSurfaces.insert(engineSurfaceName(base.surfaces[s].name), s);
	int above = triangleCount(base);
	for (int index = 0; index < levels.size(); ++index)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, index, levels.size(), error))
			return false;
		const auto &mesh = levels.at(index);
		const int level = numberOf(index);
		// R_AddMD3Surfaces checks the frame against the base model only, then
		// reads this level's vertices for it.
		if (mesh.frames.size() < base.frames.size())
			add(ModelLodSeverity::Error, level, "frames",
				Text::tr("Level %1 has %2 frames and the base model %3. The game checks frames against the base model only, so it would read past "
						 "the end of this level's frames.")
					.arg(level)
					.arg(mesh.frames.size())
					.arg(base.frames.size()));
		else if (mesh.frames.size() > base.frames.size())
			add(ModelLodSeverity::Error, level, "frames",
				Text::tr("Level %1 has %2 frames and the base model %3. The game plays this level's frames by the base model's numbers, so its extra "
						 "frames never show; give every level the base model's frames.")
					.arg(level)
					.arg(mesh.frames.size())
					.arg(base.frames.size()));
		else
		{
			QStringList renamed;
			for (int f = 0; f < base.frames.size(); ++f)
			{
				if (mesh.frames[f].name != base.frames[f].name)
					renamed << QString::number(f);
			}
			if (!renamed.isEmpty())
			{
				const int count = int(renamed.size());
				add(ModelLodSeverity::Warning, level, "frame-names",
					Text::tr("Level %1 names frames differently from the base model (%n): %2. The game plays frames by number, so check they hold "
							 "the same poses.",
							 nullptr, count)
						.arg(level)
						.arg(renamed.mid(0, 8).join(QStringLiteral(", "))));
			}
		}
		QSet<QString> present;
		for (const auto &surface : mesh.surfaces)
		{
			const auto name = engineSurfaceName(surface.name);
			present.insert(name);
			const auto found = baseSurfaces.constFind(name);
			if (found == baseSurfaces.cend())
			{
				add(ModelLodSeverity::Error, level, "surface-names",
					Text::tr("Level %1 has surface \"%2\", which the base model lacks, so a .skin file for the model cannot bind it.").arg(level).arg(surface.name));
				continue;
			}
			const auto &baseSurface = base.surfaces.at(found.value());
			if (surface.skinPaths.value(0) != baseSurface.skinPaths.value(0))
				add(ModelLodSeverity::Note, level, "materials",
					Text::tr("Level %1 surface \"%2\" uses %3 where the base model uses %4; the level's own shader shows unless a .skin names the surface.")
						.arg(level)
						.arg(surface.name, surface.skinPaths.value(0, Text::tr("no shader")), baseSurface.skinPaths.value(0, Text::tr("no shader"))));
		}
		for (auto it = baseSurfaces.cbegin(); it != baseSurfaces.cend(); ++it)
		{
			if (!present.contains(it.key()))
				add(ModelLodSeverity::Warning, level, "surfaces",
					Text::tr("Level %1 has no surface \"%2\", so it disappears at this distance.").arg(level).arg(base.surfaces.at(it.value()).name));
		}
		const int triangles = triangleCount(mesh);
		if (triangles >= above)
			add(ModelLodSeverity::Warning, level, "triangles",
				Text::tr("Level %1 has %2 triangles, no fewer than the %3 of the level above it.").arg(level).arg(triangles).arg(above));
		above = triangles;
		// R_LerpTag reads the base model's tags only.
		bool sameTags = mesh.tags.size() == base.tags.size();
		for (int t = 0; sameTags && t < base.tags.size(); ++t)
			sameTags = mesh.tags[t].name == base.tags[t].name && mesh.tags[t].frameIndex == base.tags[t].frameIndex;
		if (!sameTags)
			add(ModelLodSeverity::Note, level, "tags",
				Text::tr("Level %1's attachment tags differ from the base model's; the game reads tags from the base model only.").arg(level));
		if (validateEditableModel(mesh, control).isEmpty())
		{
			ModelTargetReport report;
			if (!assessModelTarget(mesh, target, &report, error, control))
				return false;
			for (const auto &issue : std::as_const(report.issues))
				add(ModelLodSeverity::Error, level, "target", Text::tr("Level %1: %2 %3").arg(level).arg(issue.message, issue.fix));
		}
		else
			add(ModelLodSeverity::Error, level, "target", Text::tr("Level %1 is not a complete model the studio can check.").arg(level));
	}
	std::stable_sort(result.findings.begin(), result.findings.end(),
					 [](const ModelLodFinding &a, const ModelLodFinding &b) { return int(a.severity) < int(b.severity); });
	if (validation)
		*validation = std::move(result);
	return modelWorkCheckpoint(control, ModelWorkPhase::Validating, 1, 1, error);
}

QStringList modelLodValidationLines(const ModelLodValidation &validation)
{
	QStringList lines;
	for (const auto &finding : validation.findings)
		lines << Text::tr("%1: %2").arg(severityName(finding.severity), finding.message);
	return lines;
}

QJsonArray modelLodValidationJson(const ModelLodValidation &validation)
{
	QJsonArray findings;
	for (const auto &finding : validation.findings)
		findings.append(QJsonObject{{QStringLiteral("severity"), severityId(finding.severity)},
									{QStringLiteral("level"), finding.level},
									{QStringLiteral("code"), finding.code},
									{QStringLiteral("message"), finding.message}});
	return findings;
}

QVector<ModelLodSurfaceRow> modelLodSurfaceTable(const ModelMesh &base, const QVector<ModelMesh> &levels, const QVector<int> &levelNumbers)
{
	QVector<ModelLodSurfaceRow> rows;
	QHash<QString, int> rowOf;
	const bool numbered = !levelNumbers.isEmpty() && levelNumbers.size() == levels.size();
	const auto columnOf = [&](int index) { return numbered ? levelNumbers.at(index) : index + 1; };
	int columns = 1;
	for (int index = 0; index < levels.size(); ++index)
		columns = std::max(columns, columnOf(index) + 1);
	for (const auto &surface : base.surfaces)
	{
		ModelLodSurfaceRow row{surface.name, QVector<int>(columns, -1)};
		row.triangles[0] = int(surface.triangles.size());
		rowOf.insert(engineSurfaceName(surface.name), int(rows.size()));
		rows.append(row);
	}
	for (int level = 0; level < levels.size(); ++level)
	{
		for (const auto &surface : levels.at(level).surfaces)
		{
			const auto key = engineSurfaceName(surface.name);
			if (!rowOf.contains(key))
			{
				rowOf.insert(key, int(rows.size()));
				rows.append({surface.name, QVector<int>(columns, -1)});
			}
			auto &cell = rows[rowOf.value(key)].triangles[columnOf(level)];
			cell = std::max(cell, 0) + int(surface.triangles.size());
		}
	}
	return rows;
}

ModelLodFileReader modelLodDiskReader()
{
	return [](const QString &path, QByteArray *bytes, QString *error)
	{
		if (error)
			error->clear();
		const QFileInfo info(path);
		if (!info.exists())
			return false;
		return readModelFile(path, bytes, error);
	};
}

int ModelLodInspection::levelsPresent() const
{
	// Only the levels the target loads count.
	return int(std::count_if(files.cbegin(), files.cend(), [this](const ModelLodInspectedFile &file)
							 { return file.level > 0 && file.level <= targetLevels && file.present && file.error.isEmpty(); }));
}

bool inspectModelLodSet(const QString &basePath, const ModelLodFileReader &read, const ModelTargetProfile &target, ModelLodInspection *inspection,
						QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	if (!read)
		return fail(error, Text::tr("Nothing can read the detail set."));
	if (!basePath.endsWith(QStringLiteral(".md3"), Qt::CaseInsensitive))
		return fail(error, Text::tr("Detail levels sit beside an MD3 model; choose a name ending in .md3."));
	ModelLodInspection result;
	result.target = target.id;
	result.targetLabel = target.label;
	result.targetLevels = std::max(0, target.limits.detailLevels);
	const bool mdcFirst = modelLodTargetLoadsMdcFirst(target);
	ModelMesh base;
	QVector<ModelMesh> levels;
	QVector<int> levelNumbers;
	int missing = 0, mdcOnly = 0;
	for (int level = 0; level <= modelLodMaxLevels; ++level)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, level, modelLodMaxLevels + 1, error))
			return false;
		ModelLodInspectedFile file;
		file.level = level;
		file.path = level == 0 ? basePath : modelLodPath(basePath, level);
		QByteArray bytes;
		QString problem;
		file.present = read(file.path, &bytes, &problem);
		if (!file.present && !problem.isEmpty())
		{
			file.present = true;
			file.error = problem;
		}
		// RTCW and Enemy Territory try each level's MDC before its MD3
		// (R_RegisterModel), so an MDC loads in its place.
		bool mdc = false;
		if (mdcFirst && level <= result.targetLevels)
		{
			const auto mdcPath = modelLodMdcPath(file.path);
			QByteArray mdcBytes;
			QString mdcProblem;
			mdc = read(mdcPath, &mdcBytes, &mdcProblem) || !mdcProblem.isEmpty();
			mdcOnly += mdc && !file.present && level > 0;
			if (mdc)
				result.validation.findings.append(
					{ModelLodSeverity::Warning, level, QStringLiteral("target"),
					 file.present ? Text::tr("%1 exists, and %2 loads it before %3; remove it or rebuild it from this model.").arg(mdcPath, target.label, file.path)
								  : Text::tr("Level %1 is only %2, which %3 loads; this review checks MD3 files, so check it in the game or rebuild it as %4.")
										.arg(level)
										.arg(mdcPath, target.label, file.path)});
		}
		// Levels the target does not load are listed only when present.
		if (level > result.targetLevels && !file.present)
			continue;
		ModelMesh mesh;
		if (file.present && file.error.isEmpty())
		{
			if (importEditableModel(file.path, bytes, &mesh, &problem, nullptr, control))
			{
				file.frames = int(mesh.frames.size());
				file.surfaces = int(mesh.surfaces.size());
				file.triangles = triangleCount(mesh);
			}
			else
			{
				if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error))
					return false;
				file.error = problem;
			}
		}
		result.files.append(file);
		if (level == 0)
		{
			if (!file.present || !file.error.isEmpty())
				return fail(error, file.present ? Text::tr("%1: %2").arg(file.path, file.error) : Text::tr("%1 does not exist.").arg(file.path));
			base = mesh;
			continue;
		}
		if (!file.present)
		{
			// A level present as an MDC only still loads, so it leaves no gap.
			missing += !mdc;
			continue;
		}
		if (level > result.targetLevels)
		{
			result.validation.findings.append({ModelLodSeverity::Warning, level, QStringLiteral("target"),
											   Text::tr("%1 is never loaded: %2 loads %n detail level(s) beside the model.", nullptr, result.targetLevels)
												   .arg(file.path, target.label)});
			continue;
		}
		if (!file.error.isEmpty())
		{
			result.validation.findings.append({ModelLodSeverity::Error, level, QStringLiteral("target"), Text::tr("%1: %2").arg(file.path, file.error)});
			continue;
		}
		// R_RegisterModel skips a missing level but counts the ones it loads, so
		// a gap leaves an empty slot the renderer can pick.
		if (missing > 0)
			result.validation.findings.append({ModelLodSeverity::Error, level, QStringLiteral("surfaces"),
											   Text::tr("%1 exists but a lower level is missing; the game would count this level and leave the missing "
														"one's slot empty. Add the missing level or remove this one.")
												   .arg(file.path)});
		levels.append(mesh);
		levelNumbers.append(level);
	}
	// Findings name each level by its own number, also after a gap.
	ModelLodValidation consistency;
	if (!validateModelLodSet(base, levels, target, &consistency, error, control, levelNumbers))
		return false;
	result.validation.findings += consistency.findings;
	std::stable_sort(result.validation.findings.begin(), result.validation.findings.end(),
					 [](const ModelLodFinding &a, const ModelLodFinding &b) { return int(a.severity) < int(b.severity); });
	result.surfaces = modelLodSurfaceTable(base, levels, levelNumbers);
	result.switchDistances = modelQuake3LodSwitchDistances(modelLodRadius(base), int(levels.size()) + mdcOnly + 1);
	if (inspection)
		*inspection = std::move(result);
	return modelWorkCheckpoint(control, ModelWorkPhase::Validating, 1, 1, error);
}

QStringList modelLodSiblingPaths(const QString &basePath)
{
	QStringList paths{modelLodMdcPath(basePath)};
	for (int level = 1; level <= modelLodMaxLevels; ++level)
	{
		const auto path = modelLodPath(basePath, level);
		paths << path << modelLodMdcPath(path);
	}
	return paths;
}

ModelLodFileReader modelLodMemoryReader(const QHash<QString, QByteArray> &files)
{
	QHash<QString, QByteArray> keyed;
	for (auto it = files.cbegin(); it != files.cend(); ++it)
		keyed.insert(QString(it.key()).replace(QLatin1Char('\\'), QLatin1Char('/')).toLower(), it.value());
	return [keyed](const QString &path, QByteArray *bytes, QString *error)
	{
		if (error)
			error->clear();
		const auto found = keyed.constFind(QString(path).replace(QLatin1Char('\\'), QLatin1Char('/')).toLower());
		if (found == keyed.cend())
			return false;
		if (bytes)
			*bytes = found.value();
		return true;
	};
}

bool reviewModelLodSiblings(const ModelMesh &base, const QString &basePath, int writtenLevels, const ModelLodFileReader &read,
							const ModelTargetProfile &target, ModelLodSiblingReview *review, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelLodSiblingReview result;
	result.target = target.id;
	result.targetLabel = target.label;
	const int loaded = std::max(0, std::min(target.limits.detailLevels, modelLodMaxLevels));
	if (!read || !basePath.endsWith(QStringLiteral(".md3"), Qt::CaseInsensitive))
	{
		if (review)
			*review = std::move(result);
		return modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error);
	}
	const bool mdcFirst = modelLodTargetLoadsMdcFirst(target);
	QVector<ModelMesh> stale;
	QVector<int> staleLevels;
	for (int level = 0; level <= loaded; ++level)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, level, loaded + 1, error))
			return false;
		const auto md3 = level == 0 ? basePath : modelLodPath(basePath, level);
		// An MDC loads in place of the MD3 of its level, written or not.
		if (mdcFirst)
		{
			ModelLodInspectedFile file;
			file.level = level;
			file.path = modelLodMdcPath(md3);
			QByteArray bytes;
			QString problem;
			file.present = read(file.path, &bytes, &problem) || !problem.isEmpty();
			file.error = problem;
			if (file.present)
			{
				result.files.append(file);
				result.validation.findings.append({ModelLodSeverity::Error, level, QStringLiteral("mdc"),
												   Text::tr("%1 stays, and %2 loads it in place of %3; remove it or rebuild it from this model.")
													   .arg(file.path, target.label, md3)});
			}
		}
		if (level == 0 || level <= writtenLevels)
			continue;
		ModelLodInspectedFile file;
		file.level = level;
		file.path = md3;
		QByteArray bytes;
		QString problem;
		file.present = read(file.path, &bytes, &problem);
		if (!file.present && problem.isEmpty())
			continue;
		file.present = true;
		file.error = problem;
		ModelMesh mesh;
		if (file.error.isEmpty())
		{
			if (importEditableModel(file.path, bytes, &mesh, &problem, nullptr, control))
			{
				file.frames = int(mesh.frames.size());
				file.surfaces = int(mesh.surfaces.size());
				file.triangles = triangleCount(mesh);
			}
			else
			{
				if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error))
					return false;
				file.error = problem;
			}
		}
		result.files.append(file);
		result.validation.findings.append({ModelLodSeverity::Warning, level, QStringLiteral("stale"),
										   Text::tr("%1 stays beside the model, and %2 loads it as detail level %3; it was not written with this model. "
													"Write the detail levels again (Detail Levels…, or model lod) or remove it.")
											   .arg(file.path, target.label)
											   .arg(level)});
		if (!file.error.isEmpty())
		{
			result.validation.findings.append({ModelLodSeverity::Error, level, QStringLiteral("target"), Text::tr("%1: %2").arg(file.path, file.error)});
			continue;
		}
		stale.append(mesh);
		staleLevels.append(level);
	}
	if (!stale.isEmpty())
	{
		ModelLodValidation consistency;
		if (!validateModelLodSet(base, stale, target, &consistency, error, control, staleLevels))
			return false;
		// What matters here is whether the old files still fit the new model;
		// the target's own limits on them were checked when they were written.
		for (const auto &finding : std::as_const(consistency.findings))
		{
			if (finding.code != QStringLiteral("triangles"))
				result.validation.findings.append(finding);
		}
	}
	std::stable_sort(result.validation.findings.begin(), result.validation.findings.end(),
					 [](const ModelLodFinding &a, const ModelLodFinding &b) { return int(a.severity) < int(b.severity); });
	if (review)
		*review = std::move(result);
	return modelWorkCheckpoint(control, ModelWorkPhase::Validating, 1, 1, error);
}

QStringList modelLodSiblingLines(const ModelLodSiblingReview &review)
{
	QStringList lines;
	if (review.isEmpty())
		return lines;
	const int files = int(review.files.size());
	lines << (review.validation.ok()
				  ? Text::tr("%n detail file(s) beside the model stay as they were, and %1 loads them with it:", nullptr, files).arg(review.targetLabel)
				  : Text::tr("%n detail file(s) beside the model stay as they were, and %1 would load them with it wrongly:", nullptr, files)
						.arg(review.targetLabel));
	for (const auto &finding : review.validation.findings)
		lines << Text::tr("%1: %2").arg(severityName(finding.severity), finding.message);
	return lines;
}

QString modelLodSiblingSummary(const ModelLodSiblingReview &review)
{
	if (review.isEmpty())
		return {};
	const int files = int(review.files.size());
	return review.validation.ok() ? Text::tr("%n detail file(s) beside the model stay as they were, and %1 loads them with it.", nullptr, files)
										.arg(review.targetLabel)
								  : Text::tr("%n detail file(s) beside the model stay as they were, and %1 would load them with it wrongly.", nullptr, files)
										.arg(review.targetLabel);
}

QJsonObject modelLodSiblingJson(const ModelLodSiblingReview &review)
{
	QJsonArray files;
	for (const auto &file : review.files)
	{
		QJsonObject entry{{QStringLiteral("level"), file.level},
						  {QStringLiteral("path"), file.path},
						  {QStringLiteral("mdc"), file.path.endsWith(QStringLiteral(".mdc"), Qt::CaseInsensitive)}};
		if (file.error.isEmpty() && !entry.value(QStringLiteral("mdc")).toBool())
		{
			entry.insert(QStringLiteral("frames"), file.frames);
			entry.insert(QStringLiteral("surfaces"), file.surfaces);
			entry.insert(QStringLiteral("triangles"), file.triangles);
		}
		if (!file.error.isEmpty())
			entry.insert(QStringLiteral("error"), file.error);
		files.append(entry);
	}
	return {{QStringLiteral("target"), review.target},
			{QStringLiteral("files"), files},
			{QStringLiteral("findings"), modelLodValidationJson(review.validation)},
			{QStringLiteral("consistent"), review.validation.ok()}};
}

QJsonObject modelLodInspectionJson(const ModelLodInspection &inspection)
{
	QJsonArray files, surfaces, distances;
	for (const auto &file : inspection.files)
	{
		QJsonObject entry{{QStringLiteral("level"), file.level}, {QStringLiteral("path"), file.path}, {QStringLiteral("present"), file.present}};
		if (file.present && file.error.isEmpty())
		{
			entry.insert(QStringLiteral("frames"), file.frames);
			entry.insert(QStringLiteral("surfaces"), file.surfaces);
			entry.insert(QStringLiteral("triangles"), file.triangles);
		}
		if (!file.error.isEmpty())
			entry.insert(QStringLiteral("error"), file.error);
		files.append(entry);
	}
	for (const auto &row : inspection.surfaces)
	{
		QJsonArray triangles;
		for (int count : row.triangles)
			triangles.append(count);
		surfaces.append(QJsonObject{{QStringLiteral("surface"), row.surface}, {QStringLiteral("triangles"), triangles}});
	}
	for (double distance : inspection.switchDistances)
		distances.append(std::round(distance * 10) / 10);
	return {{QStringLiteral("target"), inspection.target},
			{QStringLiteral("targetLevels"), inspection.targetLevels},
			{QStringLiteral("levelsPresent"), inspection.levelsPresent()},
			{QStringLiteral("files"), files},
			{QStringLiteral("surfaces"), surfaces},
			{QStringLiteral("switchDistances"), distances},
			{QStringLiteral("ready"), inspection.validation.ok()},
			{QStringLiteral("findings"), modelLodValidationJson(inspection.validation)}};
}

QStringList modelLodInspectionLines(const ModelLodInspection &inspection)
{
	QStringList lines;
	lines << Text::tr("%1 loads %n detail level(s) beside the model.", nullptr, inspection.targetLevels).arg(inspection.targetLabel);
	for (const auto &file : inspection.files)
	{
		if (!file.present)
			lines << Text::tr("Level %1: %2 · missing").arg(file.level).arg(file.path);
		else if (!file.error.isEmpty())
			lines << Text::tr("Level %1: %2 · unreadable: %3").arg(file.level).arg(file.path, file.error);
		else
			lines << Text::tr("Level %1: %2 · %3 frames, %4 surfaces, %5 triangles")
						 .arg(file.level)
						 .arg(file.path)
						 .arg(file.frames)
						 .arg(file.surfaces)
						 .arg(file.triangles);
	}
	for (int level = 1; level < inspection.switchDistances.size(); ++level)
	{
		const double distance = inspection.switchDistances.at(level);
		lines << (distance < 0 ? Text::tr("Level %1 never shows with the default view.").arg(level)
							   : Text::tr("Level %1 shows from %2 units away (cg_fov 90, r_lodscale 5, r_lodbias 0).").arg(level).arg(distance, 0, 'f', 1));
	}
	lines += modelLodValidationLines(inspection.validation);
	if (inspection.validation.findings.isEmpty())
		lines << Text::tr("The detail set is consistent.");
	return lines;
}

double modelLodRadius(const ModelMesh &mesh, int frame)
{
	// RadiusFromBounds: the length of the larger magnitude of each axis.
	double extent[3]{0, 0, 0};
	bool any = false;
	for (const auto &surface : mesh.surfaces)
	{
		if (frame < 0 || frame >= surface.frames.size())
			continue;
		for (const auto &p : surface.frames[frame].positions)
		{
			any = true;
			extent[0] = std::max(extent[0], double(std::abs(p.x)));
			extent[1] = std::max(extent[1], double(std::abs(p.y)));
			extent[2] = std::max(extent[2], double(std::abs(p.z)));
		}
	}
	return any ? std::sqrt(extent[0] * extent[0] + extent[1] * extent[1] + extent[2] * extent[2]) : 0;
}

QVector<double> modelQuake3LodSwitchDistances(double radius, int loadedModels, const ModelLodView &view)
{
	// R_ComputeLOD (code/renderer/tr_mesh.c): with n models loaded, the level is
	// (1 - projectedRadius * r_lodscale) * n truncated (ioquake3's ftol), clamped
	// to 0..n-1, plus r_lodbias, clamped again. ProjectRadius with the
	// R_SetupProjection matrix gives radius / (depth * tan(fov_y / 2)), and
	// CG_CalcFov's fov_y has tan(fov_y / 2) = tan(fov_x / 2) / aspect. Level k
	// therefore starts at depth radius * lodscale / (tan(fov_y / 2) * (1 - k / n)).
	const int n = std::max(1, loadedModels);
	QVector<double> distances(n, 0.0);
	if (n < 2 || radius <= 0)
		return distances;
	const double pi = 3.14159265358979323846;
	const double tanHalfY = std::tan(std::clamp(view.fovX, 1.0, 179.0) * pi / 360.0) / std::max(0.01, view.aspect);
	const double lodScale = std::min(view.lodScale, 20.0);
	for (int level = 1; level < n; ++level)
	{
		// The unbiased level that shows as `level` once r_lodbias is added.
		const int raw = level - view.lodBias;
		if (raw <= 0)
			distances[level] = 0;
		else if (raw >= n || lodScale <= 0)
			// Unbiased levels stop at n - 1, and with no scale every model shows
			// the last level: this one shows only through the clamp.
			distances[level] = level == n - 1 && lodScale <= 0 ? 0 : -1;
		else
			distances[level] = radius * lodScale / (tanHalfY * (1.0 - double(raw) / n));
	}
	return distances;
}

QStringList ModelLodSet::existing() const
{
	QStringList paths;
	for (const auto &file : files)
	{
		if (file.destination.existed)
			paths << file.path;
	}
	return paths;
}

int ModelLodSet::written() const
{
	return int(std::count_if(files.cbegin(), files.cend(), [](const ModelLodFile &file) { return file.commit == ModelLodCommit::Written; }));
}

bool prepareModelLodSet(const ModelMesh &mesh, int levels, double ratio, const QString &basePath, const QString &target,
						const QStringList &protectedInputs, bool strict, ModelLodSet *set, QString *error, const ModelWorkControl &control)
{
	// More levels than any target loads still get the target's own refusal.
	const auto recipe = modelLodGeneratedRecipe(std::clamp(levels, 0, modelLodMaxLevels + 1), ratio);
	if (!prepareModelLodSet(mesh, recipe, {}, basePath, target, protectedInputs, strict, set, error, control))
		return false;
	if (set)
		set->ratio = ratio;
	return true;
}

bool prepareModelLodSet(const ModelMesh &mesh, const QVector<ModelLodRecipeLevel> &recipe, const QString &baseDirectory, const QString &basePath,
						const QString &target, const QStringList &protectedInputs, bool strict, ModelLodSet *set, QString *error,
						const ModelWorkControl &control)
{
	ModelLodSet result;
	if (!prepareModelLodFiles(mesh, recipe, baseDirectory, basePath, target, strict, &result, error, control))
		return false;
	// Review every destination before any is written; hand-made sources are
	// inputs too.
	QStringList inputs = protectedInputs;
	for (const auto &file : std::as_const(result.files))
	{
		if (!file.source.isEmpty())
			inputs << file.source;
	}
	for (int index = 0; index < result.files.size(); ++index)
	{
		auto &file = result.files[index];
		for (const auto &input : std::as_const(inputs))
		{
			if (!input.isEmpty() && modelPathsReferToSameFile(file.path, input))
				return fail(error, Text::tr("%1 is an input of this export. Choose an output name separate from the source.").arg(file.path));
		}
		for (int other = 0; other < index; ++other)
		{
			if (modelPathsReferToSameFile(file.path, result.files[other].path))
				return fail(error, Text::tr("%1 and %2 are the same file. Choose a base name whose detail files do not collide.")
									   .arg(result.files[other].path, file.path));
		}
		file.destination = inspectModelWriteTarget(file.path, control);
		if (!file.destination.isValid())
			return fail(error, Text::tr("%1: %2").arg(file.path, file.destination.error));
	}
	// Detail files the set does not replace still load with it.
	ModelTargetProfile profile;
	if (findModelTargetProfile(QStringLiteral("md3"), result.target, &profile) &&
		!reviewModelLodSiblings(mesh, basePath, result.levels, modelLodDiskReader(), profile, &result.stale, error, control))
		return false;
	if (set)
		*set = std::move(result);
	return modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, error);
}

bool prepareModelLodFiles(const ModelMesh &mesh, const QVector<ModelLodRecipeLevel> &recipe, const QString &baseDirectory, const QString &basePath,
						  const QString &target, bool strict, ModelLodSet *set, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelTargetProfile profile;
	if (!findModelTargetProfile(QStringLiteral("md3"), target, &profile))
	{
		return fail(error, Text::tr("There is no MD3 delivery target called \"%1\". Choose one of: %2.")
							   .arg(target.trimmed(), modelTargetChoices(QStringLiteral("md3"))));
	}
	const int loaded = std::max(0, profile.limits.detailLevels);
	const int levels = int(recipe.size());
	if (levels < 1 || levels > loaded)
	{
		return fail(error, loaded == 0 ? Text::tr("%1 loads no detail levels beside the model, so there is nothing to write.").arg(profile.label)
									   : Text::tr("%1 loads at most %n detail level(s) beside the model (name_1.md3 to name_%2.md3); choose a number from 1 to %n.",
												  nullptr, loaded)
											 .arg(profile.label)
											 .arg(loaded));
	}
	if (!basePath.endsWith(QStringLiteral(".md3"), Qt::CaseInsensitive))
	{
		return fail(error, Text::tr("Detail levels are written beside an MD3 model; choose a name ending in .md3."));
	}
	if (!validateModelLodRecipe(recipe, loaded, error))
		return false;
	QVector<ModelLodLevel> lods;
	if (!buildModelLodsFromRecipe(mesh, recipe, baseDirectory, &lods, error, control))
		return false;
	QVector<ModelMesh> meshes;
	for (const auto &lod : std::as_const(lods))
		meshes.append(lod.mesh);
	ModelLodSet result;
	if (!validateModelLodSet(mesh, meshes, profile, &result.validation, error, control))
		return false;
	if (!result.validation.ok())
	{
		QStringList problems;
		for (const auto &finding : std::as_const(result.validation.findings))
		{
			if (finding.severity == ModelLodSeverity::Error)
				problems << finding.message;
		}
		const int count = int(problems.size());
		return fail(error, Text::tr("The detail set is not consistent (%n problem(s)): %1", nullptr, count).arg(problems.join(QLatin1Char(' '))));
	}
	result.target = profile.id;
	result.targetLabel = profile.label;
	result.targetLevels = loaded;
	result.levels = levels;
	result.ratio = recipe.first().generated && recipe.first().targetTriangles == 0 ? recipe.first().ratio : 0;
	result.recipe = recipe;
	result.files.append({0, basePath, {}, triangleCount(mesh), {}});
	for (int level = 0; level < lods.size(); ++level)
	{
		ModelLodFile file{level + 1, modelLodPath(basePath, level + 1), {}, lods[level].triangles, lods[level].notes};
		file.source = lods[level].source;
		result.files.append(file);
	}
	for (int index = 0; index < result.files.size(); ++index)
	{
		auto &file = result.files[index];
		const auto &source = index == 0 ? mesh : lods[index - 1].mesh;
		ModelExportOptions options;
		options.target = profile.id;
		options.strict = strict;
		QString problem;
		ModelExportReport report;
		file.bytes = exportEditableModel(source, QStringLiteral("md3"), options, &problem, control, &report);
		if (file.bytes.isEmpty())
		{
			if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 0, 0, error))
				return false;
			return fail(error, index == 0 ? problem : Text::tr("Detail level %1 (%2): %3").arg(index).arg(file.path, problem));
		}
		file.exportNotes = report.notes;
		file.exportOmissions = report.omissions;
	}
	if (set)
		*set = std::move(result);
	return modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 1, 1, error);
}

bool publishModelLodSet(ModelLodSet *set, bool overwrite, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	if (!set || set->files.isEmpty())
		return fail(error, Text::tr("Prepare the detail levels before writing them."));
	const auto existing = set->existing();
	if (!existing.isEmpty() && !overwrite)
	{
		const int count = int(existing.size());
		return fail(error, Text::tr("Nothing was written: files of the detail set already exist (%n): %1. Allow replacing them to continue.",
									nullptr, count)
							   .arg(existing.join(QStringLiteral(", "))));
	}
	for (auto &file : set->files)
	{
		file.commit = ModelLodCommit::Pending;
		file.error.clear();
		QString problem;
		if (!validateModelWriteTarget(file.destination, &problem, control))
			return fail(error, Text::tr("%1: %2 Nothing was written.").arg(file.path, problem));
	}
	QStringList written;
	for (auto &file : set->files)
	{
		QString problem;
		if (!writeModelFile(file.destination, file.bytes, &problem, control))
		{
			file.commit = ModelLodCommit::Failed;
			file.error = problem;
			const int count = int(written.size());
			return fail(error, written.isEmpty() ? Text::tr("%1: %2 Nothing was written.").arg(file.path, problem)
												 : Text::tr("%1: %2 The detail set is incomplete. Files already written (%n): %3.", nullptr,
															count)
													   .arg(file.path, problem, written.join(QStringLiteral(", "))));
		}
		file.commit = ModelLodCommit::Written;
		written << file.path;
	}
	return true;
}

QJsonArray modelLodSetJson(const ModelLodSet &set)
{
	QJsonArray files;
	for (const auto &file : set.files)
	{
		const auto notes = QJsonArray::fromStringList(file.notes);
		QJsonObject entry{{QStringLiteral("path"), QFileInfo(file.path).absoluteFilePath()},
						  {QStringLiteral("level"), file.level},
						  {QStringLiteral("triangles"), file.triangles},
						  {QStringLiteral("bytes"), qint64(file.bytes.size())},
						  {QStringLiteral("existed"), file.destination.existed},
						  {QStringLiteral("committed"), file.commit == ModelLodCommit::Written  ? QStringLiteral("written")
														: file.commit == ModelLodCommit::Failed ? QStringLiteral("failed")
																								: QStringLiteral("pending")},
						  {QStringLiteral("notes"), notes},
						  {QStringLiteral("exportNotes"), QJsonArray::fromStringList(file.exportNotes)},
						  {QStringLiteral("exportOmissions"), QJsonArray::fromStringList(file.exportOmissions)}};
		if (!file.error.isEmpty())
			entry.insert(QStringLiteral("error"), file.error);
		if (!file.source.isEmpty())
			entry.insert(QStringLiteral("source"), QFileInfo(file.source).absoluteFilePath());
		files.append(entry);
	}
	return files;
}
} // namespace vibestudio
