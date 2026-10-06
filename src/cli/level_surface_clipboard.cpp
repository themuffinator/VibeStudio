#include "cli/level_surface_clipboard.h"
#include "core/level_document.h"
#include "core/level_material_paint.h"
#include "core/level_surface_clipboard.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace vibestudio::cli {
namespace { struct Text { Q_DECLARE_TR_FUNCTIONS(LevelSurfaceClipboardCli) }; }
LevelSurfaceClipboardCliResult runLevelSurfaceClipboard(const QStringList& arguments)
{
	const auto failure = [](int code, const QString& message) { return LevelSurfaceClipboardCliResult{code, message, {}, {}}; };
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--allow-valve220", "--mapping-only", "--stroke"};
	const QSet<QString> options{"--target", "--clipboard", "--output", "--mode", "--texture-size", "--material-size", "--object"};
	const QSet<QString> repeated{"--target", "--material-size", "--object"};
	QSet<QString> seen; QHash<QString, QString> values; QStringList positional, selectors, objects, materialSizes;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i];
		if (!arg.startsWith('-')) { positional << arg; continue; }
		const auto equal = arg.indexOf('='); const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key) && !repeated.contains(key)) { return failure(2, Text::tr("Repeated option: %1").arg(key)); }
		seen.insert(key);
		if (flags.contains(key) && equal < 0) { continue; }
		if (!options.contains(key) && !globals.contains(key)) { return failure(2, Text::tr("Unexpected option: %1").arg(arg)); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal + 1); }
		else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith('-')) { value = arguments[++i]; }
		if (value.trimmed().isEmpty()) { return failure(2, Text::tr("Missing value for %1.").arg(key)); }
		if (key == QLatin1String("--target")) { selectors << value; }
		else if (key == QLatin1String("--object")) { objects << value; }
		else if (key == QLatin1String("--material-size")) { materialSizes << value; }
		else { values.insert(key, value); }
	}
	if (positional.size() != 3 || !QStringList{"map", "maps", "level"}.contains(positional[0])
		|| (positional[1] != QLatin1String("copy-surface") && positional[1] != QLatin1String("paste-surface"))) {
		return failure(2, Text::tr("Expected map copy-surface|paste-surface <map> with --target and --output."));
	}
	const bool paste = positional[1] == QLatin1String("paste-surface");
	if (selectors.isEmpty() || selectors.size() > 16384 || (!paste && selectors.size() != 1) || !values.contains("--output")) {
		return failure(2, Text::tr("Copy needs one face target; paste needs 1–16,384 targets. Both require --output, including dry runs."));
	}
	if ((!paste && (seen.contains("--clipboard") || seen.contains("--mode") || seen.contains("--texture-size") || seen.contains("--allow-valve220")
		|| seen.contains("--mapping-only") || seen.contains("--material-size") || seen.contains("--object") || seen.contains("--stroke")))
		|| (paste && !values.contains("--clipboard"))) { return failure(2, Text::tr("Only paste accepts clipboard, mapping, dimension and selection options; --clipboard is required.")); }
	const auto path = positional[2], output = values.value("--output");
	if (!QFileInfo(path).isFile()) { return failure(3, Text::tr("The source map does not exist.")); }
	if ((!paste && QFileInfo(path) == QFileInfo(output)) || (paste && QFileInfo(values.value("--clipboard")) == QFileInfo(output))) {
		return failure(2, Text::tr("The output would overwrite an input of a different file type. Choose a separate output file."));
	}
	LevelSurfacePasteOptions request;
	const auto mode = values.value("--mode", "parameters");
	if (mode != QLatin1String("parameters") && mode != QLatin1String("project") && mode != QLatin1String("seamless") && mode != QLatin1String("radiant-values") && mode != QLatin1String("radiant-project")) {
		return failure(2, Text::tr("Surface mode must be parameters, project, seamless, radiant-values or radiant-project."));
	}
	request.mode = mode == QLatin1String("radiant-project") ? LevelSurfacePasteMode::RadiantProject : mode == QLatin1String("radiant-values") ? LevelSurfacePasteMode::RadiantValues : mode == QLatin1String("seamless") ? LevelSurfacePasteMode::Seamless
		: mode == QLatin1String("project") ? LevelSurfacePasteMode::Project : LevelSurfacePasteMode::Parameters;
	request.allowValve220 = seen.contains("--allow-valve220");
	request.mappingOnly = seen.contains("--mapping-only"); request.includeSelection = !objects.isEmpty();
	const bool parameters = request.mode == LevelSurfacePasteMode::Parameters || request.mode == LevelSurfacePasteMode::RadiantValues || request.mode == LevelSurfacePasteMode::RadiantProject;
	if ((parameters && request.allowValve220) || (!parameters && request.includeSelection)) {
		return failure(2, Text::tr("Valve conversion requires project or seamless mode; --object requires parameters, radiant-values or radiant-project."));
	}
	const auto dimensions = [](const QString& value, QSize* result) {
		const auto size = value.split(','); bool x = false, y = false;
		if (size.size() == 2) { *result = {size[0].toInt(&x), size[1].toInt(&y)}; }
		return x && y && result->width() >= 1 && result->height() >= 1 && result->width() <= 65536 && result->height() <= 65536;
	};
	if (seen.contains("--texture-size")) {
		if (!dimensions(values.value("--texture-size"), &request.textureSize)) {
			return failure(2, Text::tr("Use --texture-size width,height with dimensions from 1 to 65,536."));
		}
	}
	if (materialSizes.size() > 16384 || objects.size() > 16384) { return failure(2, Text::tr("Surface transfer exceeds 16,384 dimension or selection entries.")); }
	for (const auto& entry : materialSizes) {
		const int split = entry.lastIndexOf('='); QSize size;
		const auto name = entry.left(split).trimmed().replace('\\', '/').toCaseFolded();
		if (split <= 0 || name.isEmpty() || request.materialSizes.contains(name) || !dimensions(entry.mid(split + 1), &size)) {
			return failure(2, Text::tr("Use unique --material-size material=width,height entries with dimensions from 1 to 65,536."));
		}
		request.materialSizes.insert(name, size);
	}
	QString error; LevelMapDocument document;
	if (!loadLevelMap({path, {}, {}}, &document, &error)) { return failure(4, error); }
	QVector<LevelMapSelectionRef> selection;
	for (const auto& selector : objects) {
		if ((!selector.startsWith("brush:") && !selector.startsWith("patch:") && !selector.startsWith("entity:"))
			|| !selectLevelMapObject(&document, selector, &error)) { return failure(2, Text::tr("Invalid selected object %1: %2").arg(selector, error)); }
		for (const auto& ref : document.selection) { if (!selection.contains(ref)) { selection << ref; } }
	}
	if (!objects.isEmpty() && !setLevelMapSelection(&document, selection, &error)) { return failure(2, error); }
	QVector<LevelMaterialTarget> targets;
	const bool stroke = seen.contains("--stroke");
	if (stroke && selectors.size() > 4096) { return failure(2, Text::tr("A surface stroke supports at most 4,096 ordered targets.")); }
	for (const auto& selector : selectors) {
		if (paste && selector.startsWith("brush:")) {
			if (stroke) { return failure(2, Text::tr("Stroke targets must be individual faces or patches, in traversal order.")); }
			bool number = false; const auto id = selector.mid(6).toInt(&number); bool found = false;
			if (number && id >= 0) {
				for (const auto& brush : document.brushes) { if (brush.id == id) { found = true; for (int i = 0; i < brush.faces.size(); ++i) { targets << LevelMaterialTarget{LevelMaterialKind::BrushFace, id, i}; } break; } }
			}
			if (!found) { return failure(2, Text::tr("Unknown brush target: %1").arg(selector)); }
		} else {
			LevelMaterialTarget target;
			if (!parseLevelMaterialTarget(selector, &target, &error) || (target.kind != LevelMaterialKind::BrushFace && (!paste || target.kind != LevelMaterialKind::Patch))) {
				return failure(2, Text::tr("Use face:brushId:faceNumber (one-based); paste also accepts brush:brushId and patch:patchId."));
			}
			targets << target;
		}
		if (targets.size() > 16384) { return failure(2, Text::tr("Surface paste exceeds 16,384 surfaces.")); }
	}
	LevelSurfaceClipboard clipboard; LevelSurfaceClipboardCliResult result;
	const bool dryRun = seen.contains("--dry-run"), overwrite = seen.contains("--overwrite");
	result.payload = {{"map", document.sourcePath}, {"output", QFileInfo(output).absoluteFilePath()}, {"dryRun", dryRun}, {"targets", QJsonArray::fromStringList(selectors)}};
	if (!paste) {
		if (!copyLevelSurface(document, {targets.first().objectId, targets.first().faceIndex}, &clipboard, &error)) { return failure(4, error); }
		const QFileInfo destination(output);
		if (destination.exists() && (!destination.isFile() || !overwrite)) { return failure(4, Text::tr("The surface output exists. Choose another file or use --overwrite.")); }
		if (!destination.dir().exists()) { return failure(4, Text::tr("The output directory does not exist.")); }
		const auto bytes = serializeLevelSurfaceClipboard(clipboard);
		if (!dryRun) {
			QSaveFile file(output);
			if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { return failure(4, file.errorString()); }
		}
		result.payload.insert("mapping", clipboard.mappingKind()); result.payload.insert("material", clipboard.material());
		result.lines << (dryRun ? Text::tr("Would copy surface settings to %1.") : Text::tr("Copied surface settings to %1.")).arg(output);
		return result;
	}
	QFile file(values.value("--clipboard"));
	if (!file.open(QIODevice::ReadOnly)) { return failure(3, file.errorString()); }
	if (!parseLevelSurfaceClipboard(file.read(16385), &clipboard, &error)) { return failure(4, error); }
	LevelSurfaceEditPlan plan;
	if (stroke) {
		LevelSurfaceStroke transaction(document, clipboard);
		for (const auto& target : targets) { if (!transaction.append({target}, request, &error)) { return failure(4, error); } }
		if (!transaction.commit(&document, &error)) { return failure(4, error); }
		plan = transaction.plan();
		result.payload.insert("strokeHits", transaction.stepCount());
		result.payload.insert("sourceAdvanced", transaction.clipboardFromDocument());
		result.payload.insert("finalSource", QJsonDocument::fromJson(serializeLevelSurfaceClipboard(transaction.clipboard())).object());
	} else if (!prepareLevelSurfaceTransfer(document, targets, clipboard, request, &plan, &error) || !commitLevelSurfaceEdit(&document, plan, &error)) { return failure(4, error); }
	LevelDocumentSaveRequest save; save.path = output; save.overwrite = overwrite; save.dryRun = dryRun;
	const auto report = writeLevelDocument(document, save);
	if (!report.succeeded()) { return failure(4, report.errors.join('\n')); }
	result.payload.insert("mode", mode); result.payload.insert("changedFaces", plan.faceCount()); result.payload.insert("material", clipboard.material());
	result.payload.insert("convertedFaces", plan.convertedFaceCount());
	result.payload.insert("edgeOnFaces", plan.edgeOnFaceCount());
	result.payload.insert("changedPatches", plan.patchCount()); result.payload.insert("mappingOnly", request.mappingOnly);
	result.payload.insert("selectedObjects", QJsonArray::fromStringList(objects));
	result.payload.insert("backup", report.backupPath); result.payload.insert("warnings", QJsonArray::fromStringList(report.warnings));
	result.lines << (dryRun ? Text::tr("Would paste %n surface(s).", nullptr, plan.faceCount() + plan.patchCount()) : Text::tr("Pasted %n surface(s).", nullptr, plan.faceCount() + plan.patchCount()));
	if (plan.convertedFaceCount() > 0) {
		result.lines << Text::tr("Map-wide Valve 220 conversion: %n classic face(s). Unpasted materials and UVs are preserved.", nullptr, plan.convertedFaceCount());
	}
	if (plan.edgeOnFaceCount() > 0) { result.lines << Text::tr("Radiant projection left %n face(s) edge-on to the copied mapping.", nullptr, plan.edgeOnFaceCount()); }
	result.lines << report.warnings; return result;
}
} // namespace vibestudio::cli
