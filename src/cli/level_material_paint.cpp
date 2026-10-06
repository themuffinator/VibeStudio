#include "cli/level_material_paint.h"
#include "core/level_document.h"
#include "core/level_material_paint.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>

namespace vibestudio::cli {
LevelMaterialPaintCliResult runLevelMaterialPaint(const QStringList& arguments)
{
	const auto failure = [](int code, const QString& message) { return LevelMaterialPaintCliResult{code, message, {}, {}}; };
	const QSet<QString> globals {"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> flags {"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run"};
	const QSet<QString> options {"--map-name", "--target", "--texture", "--output"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional, selectors;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i];
		if (!arg.startsWith('-')) { positional << arg; continue; }
		const auto equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key) && key != QStringLiteral("--target")) { return failure(2, QCoreApplication::translate("LevelMaterialPaintCli", "Repeated option: %1").arg(key)); }
		seen.insert(key);
		if (flags.contains(key) && equal < 0) { continue; }
		if (!options.contains(key) && !globals.contains(key)) { return failure(2, QCoreApplication::translate("LevelMaterialPaintCli", "Unexpected option: %1").arg(arg)); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal + 1); }
		else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith('-')) { value = arguments[++i]; }
		if (value.trimmed().isEmpty()) { return failure(2, QCoreApplication::translate("LevelMaterialPaintCli", "Missing value for %1.").arg(key)); }
		if (key == QStringLiteral("--target")) { selectors << value; }
		else { values.insert(key, value); }
	}
	if (positional.size() != 3 || (positional[1] != QStringLiteral("paint-material") && positional[1] != QStringLiteral("sample-material"))) {
		return failure(2, QCoreApplication::translate("LevelMaterialPaintCli", "Expected map paint-material|sample-material <map> with --target selectors."));
	}
	const bool paint = positional[1] == QStringLiteral("paint-material");
	if (selectors.isEmpty() || selectors.size() > 16384 || (!paint && selectors.size() != 1)) {
		return failure(2, QCoreApplication::translate("LevelMaterialPaintCli", "Painting requires 1–16384 targets; sampling requires exactly one."));
	}
	if (paint && (!values.contains(QStringLiteral("--texture")) || !values.contains(QStringLiteral("--output")))) {
		return failure(2, QCoreApplication::translate("LevelMaterialPaintCli", "Painting requires --texture and --output, including a dry run."));
	}
	if (!paint && (seen.contains(QStringLiteral("--texture")) || seen.contains(QStringLiteral("--output")) || seen.contains(QStringLiteral("--overwrite")) || seen.contains(QStringLiteral("--dry-run")))) {
		return failure(2, QCoreApplication::translate("LevelMaterialPaintCli", "Sampling does not accept texture, output or write options."));
	}
	const auto path = positional[2];
	if (!QFileInfo(path).isFile()) { return failure(3, QCoreApplication::translate("LevelMaterialPaintCli", "The map file does not exist.")); }
	const bool wad = QFileInfo(path).suffix().compare(QStringLiteral("wad"), Qt::CaseInsensitive) == 0;
	if (wad != values.contains(QStringLiteral("--map-name"))) {
		return failure(2, QCoreApplication::translate("LevelMaterialPaintCli", "WAD files require --map-name; text maps do not accept it."));
	}
	QString error;
	QVector<LevelMaterialTarget> targets;
	for (const auto& selector : selectors) {
		LevelMaterialTarget target;
		if (!parseLevelMaterialTarget(selector, &target, &error)) { return failure(2, error); }
		targets << target;
	}
	LevelMapDocument document;
	if (!loadLevelMap({path, values.value(QStringLiteral("--map-name")), {}}, &document, &error)) { return failure(4, error); }
	LevelMaterialPaintCliResult result;
	result.payload = {{"map", document.sourcePath}, {"mapName", wad ? document.mapName : QString()}, {"targets", QJsonArray::fromStringList(selectors)}};
	if (!paint) {
		QString material;
		if (!sampleLevelMaterial(document, targets.first(), &material, &error)) { return failure(4, error); }
		result.payload.insert(QStringLiteral("material"), material);
		result.lines << QStringLiteral("%1: %2").arg(selectors.first(), material);
		return result;
	}
	LevelMaterialPaintPlan plan;
	if (!prepareLevelMaterialPaint(document, targets, values.value(QStringLiteral("--texture")), &plan, &error) || !commitLevelMaterialPaint(&document, plan, &error)) { return failure(4, error); }
	LevelDocumentSaveRequest save;
	save.path = values.value(QStringLiteral("--output"));
	save.overwrite = seen.contains(QStringLiteral("--overwrite"));
	save.dryRun = seen.contains(QStringLiteral("--dry-run"));
	const auto report = writeLevelDocument(document, save);
	if (!report.succeeded()) { return failure(4, report.errors.join(QLatin1Char('\n'))); }
	result.payload.insert(QStringLiteral("material"), plan.material());
	result.payload.insert(QStringLiteral("targetCount"), plan.targetCount());
	result.payload.insert(QStringLiteral("changedCount"), plan.changedCount());
	result.payload.insert(QStringLiteral("output"), report.outputPath);
	result.payload.insert(QStringLiteral("backup"), report.backupPath);
	result.payload.insert(QStringLiteral("dryRun"), save.dryRun);
	result.payload.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(report.warnings));
	result.payload.insert(QStringLiteral("staleLumps"), QJsonArray::fromStringList(report.staleLumps));
	result.lines << (save.dryRun ? QCoreApplication::translate("LevelMaterialPaintCli", "Would paint %n surface(s) with %1.", nullptr, plan.changedCount())
		: QCoreApplication::translate("LevelMaterialPaintCli", "Painted %n surface(s) with %1.", nullptr, plan.changedCount())).arg(plan.material());
	result.lines << report.warnings;
	return result;
}
} // namespace vibestudio::cli
