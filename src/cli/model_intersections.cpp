#include "cli/model_intersections.h"
#include "core/model_document.h"
#include "core/model_intersections.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelIntersectionsCli)
};
ModelIntersectionsCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
bool scopeIndex(const QString &text, int *value)
{
	if (text == "all")
	{
		*value = -1;
		return true;
	}
	bool ok = false;
	*value = text.toInt(&ok);
	return ok && *value >= 0 && QString::number(*value) == text;
}
} // namespace
ModelIntersectionsCliResult runModelIntersections(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose"};
	const QSet<QString> options{"--input", "--surface", "--frame", "--settings-file", "--locale", "--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto argument = arguments[i];
		if (!argument.startsWith('-'))
		{
			positional.append(argument);
			continue;
		}
		const int equal = argument.indexOf('=');
		const auto key = equal < 0 ? argument : argument.left(equal);
		if (seen.contains(key))
			return fail(2, Text::tr("Repeated option: %1.").arg(key));
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key))
			return fail(2, Text::tr("Unknown or invalid option: %1.").arg(key));
		const auto value = equal < 0 ? arguments.value(++i) : argument.mid(equal + 1);
		if (value.isEmpty() || value.startsWith("--"))
			return fail(2, Text::tr("Option %1 requires a value.").arg(key));
		values.insert(key, value);
	}
	const bool inputOption = seen.contains("--input");
	if (positional.size() != (inputOption ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "intersections")
		return fail(2, Text::tr("Provide one model source, positionally or with --input."));
	ModelIntersectionOptions request;
	if (!scopeIndex(values.value("--surface", "all"), &request.surface) || !scopeIndex(values.value("--frame", "all"), &request.frame))
		return fail(2, Text::tr("Surface and frame require all or a nonnegative zero-based index."));
	const auto source = inputOption ? values.value("--input") : positional[2];
	QByteArray bytes;
	QString error;
	ModelMesh mesh;
	if (!readModelFile(source, &bytes, &error))
		return fail(1, error);
	if (!importEditableModel(source, bytes, &mesh, &error))
		return fail(4, error);
	if (request.frame >= mesh.frames.size() || request.surface >= mesh.surfaces.size())
		return fail(2, Text::tr("The selected surface or frame does not exist."));
	ModelIntersectionReport report;
	if (!inspectModelIntersections(mesh, request, &report, &error))
		return fail(4, error);
	ModelIntersectionsCliResult result;
	QJsonArray findings;
	for (const auto &finding : report.findings)
	{
		findings.append(QJsonObject{{"frame", finding.frame},
									{"firstSurface", finding.firstSurface},
									{"firstFace", finding.firstFace},
									{"secondSurface", finding.secondSurface},
									{"secondFace", finding.secondFace},
									{"kind", modelIntersectionKindId(finding.kind)}});
		result.lines << Text::tr("Pose %1: surface %2 face %3 / surface %4 face %5 · %6")
							.arg(finding.frame)
							.arg(finding.firstSurface)
							.arg(finding.firstFace)
							.arg(finding.secondSurface)
							.arg(finding.secondFace)
							.arg(finding.kind == ModelTriangleContact::CoplanarOverlap ? Text::tr("coplanar overlap")
																					   : Text::tr("crossing"));
	}
	result.lines.prepend(Text::tr("%1 face-pair findings in %2 stored poses. Shared boundaries and isolated point contacts are allowed; "
								  "geometry was not changed.")
							 .arg(report.findings.size())
							 .arg(report.framesScanned));
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"complete", true},
					  {"frame", request.frame},
					  {"surface", request.surface},
					  {"framesScanned", report.framesScanned},
					  {"facePoses", report.facePoses},
					  {"candidatePairs", report.candidatePairs},
					  {"nodeChecks", report.nodeChecks},
					  {"findings", findings}};
	return result;
}
} // namespace vibestudio::cli
