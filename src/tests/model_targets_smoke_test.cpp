// Delivery targets (core/model_targets.h): the data-driven table, the
// preflight that reports every limit at once, the writers reading their limits
// from the chosen target, the LOD set's level cap and output safety, and the
// CLI (model targets, model build --target, model lod).
#include "core/model_document.h"
#include "core/model_file_io.h"
#include "core/model_lod.h"
#include "core/model_targets.h"
#include "tests/model_targets_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QtEndian>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
int failures = 0;
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}
ModelTargetProfile profile(const QString &format, const QString &id)
{
	ModelTargetProfile result;
	expect(findModelTargetProfile(format, id, &result), "the target exists");
	return result;
}
ModelTargetReport assess(const ModelMesh &mesh, const QString &format, const QString &id, ModelTargetDepth depth = ModelTargetDepth::Full)
{
	ModelTargetReport report;
	QString error;
	const auto errors = validateEditableModel(mesh);
	if (!expect(errors.isEmpty(), "the fixture is a valid editable model"))
		std::cerr << "  " << errors.join(QStringLiteral("; ")).toStdString() << '\n';
	expect(assessModelTarget(mesh, profile(format, id), &report, &error, {}, depth), "the assessment runs");
	return report;
}
bool hasIssue(const ModelTargetReport &report, const QString &limit, const QString &element = {})
{
	return std::any_of(report.issues.cbegin(), report.issues.cend(),
					   [&](const ModelTargetIssue &issue) { return issue.limit == limit && (element.isEmpty() || issue.element == element); });
}
ModelExportOptions target(const QString &id)
{
	ModelExportOptions options;
	options.target = id;
	return options;
}
QByteArray bytesOf(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void checkTable()
{
	const QStringList formats{QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("iqm"), QStringLiteral("md5mesh"),
							  QStringLiteral("md5anim")};
	for (const auto &format : formats)
	{
		const auto targets = modelTargetProfiles(format);
		QSet<QString> ids;
		int defaults = 0;
		bool documented = true;
		for (const auto &entry : targets)
		{
			ids.insert(entry.id);
			defaults += entry.isDefault;
			documented &= !entry.label.isEmpty() && !entry.description.isEmpty() && !entry.sources.isEmpty() &&
						  entry.sources.first().startsWith(QStringLiteral("https://"));
		}
		expect(!targets.isEmpty() && defaults == 1 && ids.size() == targets.size(), "each format has unique targets and one default");
		expect(documented, "every target has a label, a description and its sources");
	}
	expect(modelTargetProfiles(QStringLiteral("obj")).isEmpty() && modelTargetProfiles(QStringLiteral("ase")).isEmpty() &&
			   !findModelTargetProfile(QStringLiteral("md3"), QStringLiteral("nonsense"), nullptr),
		   "OBJ and ASE have no targets, and an unknown target is not found");
	expect(profile(QStringLiteral("mdl"), {}).id == QStringLiteral("quake") && profile(QStringLiteral("md2"), {}).id == QStringLiteral("quake2") &&
			   profile(QStringLiteral("md3"), {}).id == QStringLiteral("quake3") && profile(QStringLiteral("iqm"), {}).id == QStringLiteral("iqm"),
		   "the defaults are the original engines and the IQM format");
	const auto quake3 = profile(QStringLiteral("md3"), QStringLiteral("quake3")).limits;
	const auto spasm = profile(QStringLiteral("mdl"), QStringLiteral("quakespasm")).limits;
	expect(quake3.vertices == 999 && quake3.triangles == 1999 && quake3.detailLevels == 2, "Quake III works to 999 vertices, 1,999 triangles and two levels");
	expect(spasm.vertices == 2400 && spasm.triangles == 4096 && spasm.frames == 1024 && spasm.skinWidthMultiple == 0,
		   "QuakeSpasm's MDL limits differ from the original's");
	expect(profile(QStringLiteral("iqm"), QStringLiteral("ioquake3")).limits.joints == 128, "ioquake3 loads at most 128 IQM joints");
	const auto json = modelTargetProfileJson(profile(QStringLiteral("md2"), QStringLiteral("q2pro")));
	expect(json.value(QStringLiteral("limits")).toObject().value(QStringLiteral("frames")).toInt() == 1024 &&
			   json.value(QStringLiteral("limits")).toObject().value(QStringLiteral("renderVertices")).toInt() == 6144,
		   "profile JSON carries the limits");
}

void checkMd3()
{
	QString error;
	// 37 x 27 = 999 vertices, 1,872 triangles: the largest grid Quake III draws.
	const auto fits = targetMesh({targetGridSurface(QStringLiteral("body"), 37, 27)});
	const auto ready = assess(fits, QStringLiteral("md3"), {});
	expect(ready.ready() && ready.target == QStringLiteral("quake3"), "999 vertices fit Quake III");
	expect(!exportEditableModel(fits, QStringLiteral("md3"), ModelExportOptions{}, &error).isEmpty(), "and the MD3 writes");
	// 40 x 25 = 1,000 vertices; 41 x 26 has 1,066 vertices and 2,000 triangles.
	const auto over = targetMesh({targetGridSurface(QStringLiteral("body"), 40, 25), targetGridSurface(QStringLiteral("cape"), 41, 26, 4)});
	const auto blocked = assess(over, QStringLiteral("md3"), QStringLiteral("ioquake3"));
	expect(blocked.issues.size() == 3 && hasIssue(blocked, QStringLiteral("vertices"), QStringLiteral("body")) &&
			   hasIssue(blocked, QStringLiteral("vertices"), QStringLiteral("cape")) && hasIssue(blocked, QStringLiteral("triangles"), QStringLiteral("cape")),
		   "every limit is reported, each naming its surface");
	const auto &first = blocked.issues.first();
	expect(first.value == 1000 && first.maximum == 999 && first.surface == 0 && !first.fix.isEmpty() && first.message.contains(QStringLiteral("body")),
		   "an issue carries the value, the limit, the surface and a fix");
	expect(first.fix.contains(QStringLiteral("Manage Surfaces… > Separate selected faces on the Surface page")) &&
			   first.fix.contains(QStringLiteral("Mesh > Clean Up > Decimate")),
		   "the fix names the commands and the page where they are");
	ModelExportReport report;
	error.clear();
	expect(exportEditableModel(over, QStringLiteral("md3"), target(QStringLiteral("quake3")), &error, {}, &report).isEmpty() &&
			   error.contains(QStringLiteral("body")) && error.contains(QStringLiteral("cape")) && error.contains(QStringLiteral("3 limit")) &&
			   report.target.issues.size() == 3 && report.target.target == QStringLiteral("quake3"),
		   "the export is refused before writing, listing every blocker, and keeps the report");
	// Out-of-range positions and collapsed triangles are found across frames.
	auto far = fits;
	far.surfaces[0].frames[0].positions[5].x = 600;
	far.surfaces[0].frames[0].positions[6] = far.surfaces[0].frames[0].positions[7];
	far.surfaces[0].frames[0].positions[6].x += 1.0f / 512;
	const auto ranged = assess(far, QStringLiteral("md3"), {});
	expect(hasIssue(ranged, QStringLiteral("coordinate-range")) && hasIssue(ranged, QStringLiteral("precision")) &&
			   !hasIssue(assess(far, QStringLiteral("md3"), {}, ModelTargetDepth::Counts), QStringLiteral("coordinate-range")),
		   "the full preflight finds the range and precision blockers; counts alone skip the frame scan");
	const auto counted = assess(far, QStringLiteral("md3"), {}, ModelTargetDepth::Counts);
	expect(counted.unchecked == QStringList{QStringLiteral("coordinate-range"), QStringLiteral("precision")} && ranged.unchecked.isEmpty() &&
			   modelTargetReportJson(counted).value(QStringLiteral("unchecked")).toArray().size() == 2,
		   "a counts-only check lists the limits it left unchecked, so ready is not claimed for them");
	const auto json = modelTargetReportJson(blocked);
	expect(!json.value(QStringLiteral("ready")).toBool() && json.value(QStringLiteral("issues")).toArray().size() == 3 &&
			   json.value(QStringLiteral("issues")).toArray().first().toObject().value(QStringLiteral("element")).toString() == QStringLiteral("body"),
		   "report JSON lists each issue");
	expect(exportEditableModel(fits, QStringLiteral("md3"), target(QStringLiteral("quake2")), &error).isEmpty() && error.contains(QStringLiteral("ioquake3")),
		   "an unknown target is refused, listing the choices");
	expect(exportEditableModel(fits, QStringLiteral("obj"), target(QStringLiteral("quake3")), &error).isEmpty(), "OBJ takes no target");
}

void checkMdl()
{
	QString error;
	// 33 x 32 = 1,056 distinct vertices: over the original 1,024, under QuakeSpasm's 2,400.
	const auto mesh = targetMdlGrid(33, 32);
	const auto original = assess(mesh, QStringLiteral("mdl"), QStringLiteral("quake"));
	expect(original.issues.size() == 1 && hasIssue(original, QStringLiteral("vertices")) && original.issues.first().value == 1056,
		   "the original Quake refuses 1,056 stored vertices");
	ModelExportReport report;
	expect(exportEditableModel(mesh, QStringLiteral("mdl"), ModelExportOptions{}, &error, {}, &report).isEmpty() && report.target.issues.size() == 1,
		   "the default MDL export is refused by the preflight");
	const auto bytes = exportEditableModel(mesh, QStringLiteral("mdl"), target(QStringLiteral("quakespasm")), &error, {}, &report);
	expect(!bytes.isEmpty() && qFromLittleEndian<qint32>(bytes.data() + 60) == 1056 && report.target.ready() &&
			   report.target.label == QStringLiteral("QuakeSpasm"),
		   "QuakeSpasm takes the same model, and the file stores every vertex");
	expect(assess(mesh, QStringLiteral("mdl"), QStringLiteral("darkplaces")).ready() && assess(mesh, QStringLiteral("mdl"), QStringLiteral("fte")).ready(),
		   "DarkPlaces and FTE take it too");
	const auto counts = assess(mesh, QStringLiteral("mdl"), QStringLiteral("quake"), ModelTargetDepth::Counts);
	bool bounded = false;
	for (const auto &measure : counts.measures)
		bounded |= measure.limit == QStringLiteral("vertices") && measure.upperBound && measure.value == 1056;
	expect(counts.ready() && bounded, "counts alone measure source vertices as an upper bound rather than a blocker");
	// Several blockers at once: a tag MDL cannot hold and the vertex count.
	auto tagged = mesh;
	ModelTag tag;
	tag.name = QStringLiteral("tag_weapon");
	tagged.tags = {tag};
	const auto both = assess(tagged, QStringLiteral("mdl"), QStringLiteral("quake"));
	expect(both.issues.size() == 2 && hasIssue(both, QStringLiteral("tags")) && hasIssue(both, QStringLiteral("vertices")),
		   "the tag and the vertex count are both reported");
}

void checkMd2AndIqm()
{
	QString error;
	const auto quad = targetMd2Quad(513);
	const auto original = assess(quad, QStringLiteral("md2"), QStringLiteral("quake2"));
	expect(original.issues.size() == 1 && hasIssue(original, QStringLiteral("frames")), "Quake II refuses 513 frames");
	expect(assess(quad, QStringLiteral("md2"), QStringLiteral("q2pro")).ready() &&
			   !exportEditableModel(quad, QStringLiteral("md2"), target(QStringLiteral("q2pro")), &error).isEmpty(),
		   "Q2PRO loads 1,024 frames, so the MD2 writes");
	expect(exportEditableModel(quad, QStringLiteral("md2"), ModelExportOptions{}, &error).isEmpty(), "the default MD2 export is refused");
	auto large = quad;
	large.md2SkinSize = {700, 500};
	const auto sized = assess(large, QStringLiteral("md2"), QStringLiteral("q2pro"));
	expect(sized.issues.size() == 2 && hasIssue(sized, QStringLiteral("skin-width")) && hasIssue(sized, QStringLiteral("skin-height")) &&
			   assess(large, QStringLiteral("md2"), QStringLiteral("quake2")).issues.size() == 3,
		   "skin size and frames are reported together");

	const auto rig = targetArmWithJoints(126);
	expect(rig.skeleton.joints.size() == 129, "the rig has 129 joints");
	const auto io = assess(rig, QStringLiteral("iqm"), QStringLiteral("ioquake3"));
	expect(io.issues.size() == 1 && hasIssue(io, QStringLiteral("joints")) && io.issues.first().maximum == 128, "ioquake3 refuses 129 joints");
	expect(assess(rig, QStringLiteral("iqm"), {}).ready() && assess(rig, QStringLiteral("iqm"), QStringLiteral("fte")).ready(),
		   "the IQM format and FTE take 129 joints");
	ModelExportReport report;
	expect(exportEditableModel(rig, QStringLiteral("iqm"), target(QStringLiteral("ioquake3")), &error, {}, &report).isEmpty() &&
			   report.target.issues.size() == 1 && !exportEditableModel(rig, QStringLiteral("iqm"), ModelExportOptions{}, &error).isEmpty(),
		   "the ioquake3 IQM export is refused while the default writes");
	expect(assess(exportArmFixture(), QStringLiteral("md5mesh"), {}).ready() &&
			   !exportEditableModel(exportArmFixture(), QStringLiteral("md5mesh"), target(QStringLiteral("doom3")), &error).isEmpty(),
		   "Doom 3 sets no MD5 count limits");
}

void checkLods(const QString &directory)
{
	QString error;
	const auto mesh = targetMesh({targetGridSurface(QStringLiteral("prop"), 9, 9)});
	const auto base = QDir(directory).filePath(QStringLiteral("lod/prop.md3"));
	QDir(directory).mkpath(QStringLiteral("lod"));
	ModelLodSet set;
	expect(!prepareModelLodSet(mesh, 3, 0.5, base, {}, {}, false, &set, &error) && error.contains(QStringLiteral("name_2.md3")),
		   "a third level is refused: Quake III loads name_1 and name_2 only");
	QVector<ModelLodLevel> levels;
	expect(!buildModelLods(mesh, modelLodMaxLevels + 1, 0.5, &levels, &error), "the builder refuses more levels than any target loads");
	// The source sits where level 1 would go.
	const auto source = QDir(directory).filePath(QStringLiteral("lod/prop_1.md3"));
	expect(!prepareModelLodSet(mesh, 2, 0.5, base, {}, {source}, false, &set, &error) && error.contains(QStringLiteral("prop_1.md3")),
		   "a level that would overwrite an input is refused");
	expect(prepareModelLodSet(mesh, 2, 0.5, base, QStringLiteral("ioquake3"), {}, false, &set, &error) && set.files.size() == 3 &&
			   set.existing().isEmpty() && set.target == QStringLiteral("ioquake3") && set.files[2].path.endsWith(QStringLiteral("prop_2.md3")),
		   "a set prepares the base and two levels for the target");
	// A hand-made level 1 appears after review: nothing is written.
	QFile handMade(source);
	expect(handMade.open(QIODevice::WriteOnly) && handMade.write("hand-made level") > 0, "write a hand-made level");
	handMade.close();
	expect(!publishModelLodSet(&set, false, &error) && !QFile::exists(base) && bytesOf(source) == "hand-made level" &&
			   set.written() == 0,
		   "a destination that changed after review stops the set before any write");
	expect(prepareModelLodSet(mesh, 2, 0.5, base, {}, {}, false, &set, &error) && set.existing() == QStringList{source}, "the review lists the existing level");
	expect(!publishModelLodSet(&set, false, &error) && error.contains(QStringLiteral("prop_1.md3")) && !QFile::exists(base),
		   "an existing level is not replaced without overwrite, and nothing is written");
	// A failure after the first file leaves an accurate commit state.
	bool committed = false;
	ModelWorkControl control;
	control.progress = [&](ModelWorkPhase phase, qint64 done, qint64 total) { committed |= phase == ModelWorkPhase::Committing && done == total && total > 0; };
	control.cancelled = [&] { return committed; };
	expect(!publishModelLodSet(&set, true, &error, control), "publication stops when the second file fails");
	expect(set.files[0].commit == ModelLodCommit::Written && set.files[1].commit == ModelLodCommit::Failed &&
			   set.files[2].commit == ModelLodCommit::Pending && set.written() == 1 && error.contains(QStringLiteral("prop.md3")) &&
			   QFile::exists(base) && bytesOf(source) == "hand-made level",
		   "the report says which file was written, which failed and which was not reached");
	const auto json = modelLodSetJson(set);
	expect(json.size() == 3 && json[0].toObject().value(QStringLiteral("committed")).toString() == QStringLiteral("written") &&
			   json[1].toObject().value(QStringLiteral("committed")).toString() == QStringLiteral("failed") &&
			   json[1].toObject().value(QStringLiteral("existed")).toBool(),
		   "the set's JSON carries each commit state");
	expect(prepareModelLodSet(mesh, 2, 0.5, base, {}, {}, false, &set, &error) && publishModelLodSet(&set, true, &error) && set.written() == 3 &&
			   bytesOf(source).startsWith("IDP3"),
		   "with overwrite allowed the whole set is written");
	// Every file keeps its export's notes; a strict set refuses what MD3 leaves out.
	auto timed = mesh;
	timed.animations = {{QStringLiteral("spin"), 0, 1, 12}};
	const auto timedBase = QDir(directory).filePath(QStringLiteral("lod/timed.md3"));
	expect(prepareModelLodSet(timed, 2, 0.5, timedBase, {}, {}, false, &set, &error) && set.files.size() == 3 &&
			   set.files[0].exportOmissions.size() == 1 && set.files[2].exportOmissions == set.files[0].exportOmissions &&
			   set.files[0].exportNotes.contains(set.files[0].exportOmissions.first()) &&
			   modelLodSetJson(set)[1].toObject().value(QStringLiteral("exportOmissions")).toArray().size() == 1,
		   "every detail file keeps the export's notes and omissions");
	error.clear();
	expect(!prepareModelLodSet(timed, 2, 0.5, timedBase, {}, {}, true, &set, &error) && error.contains(QStringLiteral("spin")) &&
			   !QFile::exists(timedBase),
		   "a strict detail set refuses a clip rate MD3 cannot hold");
}

int run(const QString &executable, const QString &settings, const QStringList &arguments, QJsonObject *json)
{
	QProcess process;
	process.start(executable, QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), settings} + arguments + QStringList{QStringLiteral("--json")});
	if (!process.waitForFinished(120000))
	{
		process.kill();
		return -1;
	}
	if (json)
		*json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
	return process.exitCode();
}

void checkCli(const QString &executable, const QString &directory)
{
	QString error;
	const auto settings = QDir(directory).filePath(QStringLiteral("cli.ini"));
	const auto save = [&](const ModelMesh &mesh, const QString &name)
	{
		ModelDocument document;
		const auto path = QDir(directory).filePath(name);
		expect(document.setMesh(mesh, &error) && document.save(path, true, &error), "save a CLI source");
		return path;
	};
	const auto over = save(targetMesh({targetGridSurface(QStringLiteral("body"), 40, 25)}), QStringLiteral("over.mesh.json"));
	const auto fits = save(targetMesh({targetGridSurface(QStringLiteral("body"), 37, 27)}), QStringLiteral("fits.mesh.json"));
	const auto mdl = save(targetMdlGrid(33, 32), QStringLiteral("grid.mesh.json"));
	QJsonObject json;
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("targets")}, &json) == 0 &&
			   json.value(QStringLiteral("targets")).toArray().size() == modelTargetProfiles().size(),
		   "model targets lists every target");
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("targets"), over, QStringLiteral("--format"), QStringLiteral("md3"),
									  QStringLiteral("--target"), QStringLiteral("quake3")},
			   &json) == 4 &&
			   !json.value(QStringLiteral("ready")).toBool() &&
			   json.value(QStringLiteral("reports")).toArray().first().toObject().value(QStringLiteral("issues")).toArray().first().toObject().value(
				   QStringLiteral("limit")) == QStringLiteral("vertices"),
		   "model targets exits 4 for a named target the model exceeds, with each issue");
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("targets"), fits, QStringLiteral("--format"), QStringLiteral("md3")}, &json) == 0 &&
			   json.value(QStringLiteral("reports")).toArray().size() == modelTargetProfiles(QStringLiteral("md3")).size() && json.value(QStringLiteral("ready")).toBool(),
		   "a survey of the MD3 targets succeeds");
	// ioquake3 names an MD3 and an IQM target: the check needs the format.
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("targets"), fits, QStringLiteral("--target"), QStringLiteral("ioquake3")}, &json) ==
				   2 &&
			   json.value(QStringLiteral("message")).toString().contains(QStringLiteral("--format")),
		   "a target id shared by several formats asks for --format");
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("targets"), QStringLiteral("--target"), QStringLiteral("ioquake3")}, &json) == 0 &&
			   json.value(QStringLiteral("targets")).toArray().size() == 2,
		   "listing a shared id shows each format's target");
	const auto out = QDir(directory).filePath(QStringLiteral("out.md3"));
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("build"), over, QStringLiteral("--output"), out}, &json) == 4 &&
			   json.value(QStringLiteral("targetIssues")).toArray().size() == 1 && !QFile::exists(out),
		   "model build checks the default target and writes nothing");
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("build"), fits, QStringLiteral("--output"), out, QStringLiteral("--target"),
									  QStringLiteral("ioquake3")},
			   &json) == 0 &&
			   json.value(QStringLiteral("target")).toObject().value(QStringLiteral("target")).toString() == QStringLiteral("ioquake3") && QFile::exists(out),
		   "model build --target names the target it checked");
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("build"), fits, QStringLiteral("--output"), out, QStringLiteral("--target"),
									  QStringLiteral("quakespasm"), QStringLiteral("--overwrite")},
			   nullptr) == 2,
		   "a target of another format is a usage error");
	const auto mdlOut = QDir(directory).filePath(QStringLiteral("grid.mdl"));
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("build"), mdl, QStringLiteral("--output"), mdlOut}, nullptr) == 4 &&
			   run(executable, settings, {QStringLiteral("model"), QStringLiteral("build"), mdl, QStringLiteral("--output"), mdlOut, QStringLiteral("--target"),
										  QStringLiteral("quakespasm")},
				   &json) == 0 &&
			   json.value(QStringLiteral("storedVertices")).toInt() == 1056,
		   "the MDL the original refuses builds for QuakeSpasm");
	// Detail levels.
	const auto lod = QDir(directory).filePath(QStringLiteral("cli-lod/prop.md3"));
	QDir(directory).mkpath(QStringLiteral("cli-lod"));
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("lod"), fits, QStringLiteral("--output"), lod, QStringLiteral("--levels"),
									  QStringLiteral("3")},
			   nullptr) == 2,
		   "model lod --levels 3 is a usage error");
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("lod"), fits, QStringLiteral("--output"), lod}, &json) == 0 &&
			   json.value(QStringLiteral("committed")).toInt() == 3 && json.value(QStringLiteral("existing")).toArray().isEmpty() &&
			   json.value(QStringLiteral("outputs")).toArray()[2].toObject().value(QStringLiteral("committed")).toString() == QStringLiteral("written") &&
			   json.value(QStringLiteral("target")).toString() == QStringLiteral("quake3"),
		   "model lod writes the set and reports each file committed");
	const auto level1 = QDir(directory).filePath(QStringLiteral("cli-lod/prop_1.md3"));
	const auto before = bytesOf(level1);
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("lod"), fits, QStringLiteral("--output"), lod}, nullptr) == 1 &&
			   bytesOf(level1) == before,
		   "existing levels need --overwrite");
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("lod"), fits, QStringLiteral("--output"), lod, QStringLiteral("--overwrite"),
									  QStringLiteral("--dry-run")},
			   &json) == 0 &&
			   json.value(QStringLiteral("existing")).toArray().size() == 3 && json.value(QStringLiteral("committed")).toInt() == 0 &&
			   !json.value(QStringLiteral("written")).toBool(),
		   "a dry run lists the existing files and commits nothing");
	// A clip rate MD3 cannot hold: reported per file, and refused with --strict.
	auto timedMesh = targetMesh({targetGridSurface(QStringLiteral("prop"), 9, 9)});
	timedMesh.animations = {{QStringLiteral("spin"), 0, 1, 12}};
	const auto timed = save(timedMesh, QStringLiteral("timed.mesh.json"));
	const auto timedLod = QDir(directory).filePath(QStringLiteral("cli-lod/timed.md3"));
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("lod"), timed, QStringLiteral("--output"), timedLod, QStringLiteral("--dry-run")},
			   &json) == 0 &&
			   json.value(QStringLiteral("exportOmissions")).toArray().size() == 1 &&
			   json.value(QStringLiteral("outputs")).toArray()[2].toObject().value(QStringLiteral("exportOmissions")).toArray().size() == 1 &&
			   !json.value(QStringLiteral("strict")).toBool(),
		   "model lod reports what each file leaves out");
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("lod"), timed, QStringLiteral("--output"), timedLod, QStringLiteral("--strict")},
			   &json) == 4 &&
			   !QFile::exists(timedLod),
		   "model lod --strict refuses and writes nothing");
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir directory;
	if (!expect(directory.isValid(), "a temporary folder"))
		return EXIT_FAILURE;
	checkTable();
	checkMd3();
	checkMdl();
	checkMd2AndIqm();
	checkLods(directory.path());
	if (argc > 1)
		checkCli(QString::fromLocal8Bit(argv[1]), directory.path());
	if (failures > 0)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "model delivery target checks passed\n";
	return EXIT_SUCCESS;
}
