// Delivery targets beyond the preflight: the Return to Castle Wolfenstein,
// Enemy Territory, Q2PRO and GZDoom targets, the faces and vertices each issue
// names, Prepare for target (core/model_target_prepare.h) adopted as one undo
// step, the choices a project saves (core/model_delivery.h), and the CLI
// (model prepare, model targets --project, and the defaults model build and
// model lod take from a project).
#include "core/model_delivery.h"
#include "core/model_document.h"
#include "core/model_lod.h"
#include "core/model_skeleton.h"
#include "core/model_skin_bindings.h"
#include "core/model_skin_variants.h"
#include "core/model_target_prepare.h"
#include "core/model_targets.h"
#include "core/project_manifest.h"
#include "core/studio_settings.h"
#include "tests/model_targets_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
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
ModelTargetReport assess(const ModelMesh &mesh, const QString &format, const QString &id)
{
	ModelTargetReport report;
	QString error;
	const auto errors = validateEditableModel(mesh);
	if (!expect(errors.isEmpty(), "the fixture is a valid editable model"))
		std::cerr << "  " << qPrintable(errors.join(QStringLiteral("; "))) << '\n';
	expect(assessModelTarget(mesh, profile(format, id), &report, &error), "the assessment runs");
	return report;
}
const ModelTargetIssue *issueOf(const ModelTargetReport &report, const QString &limit)
{
	for (const auto &issue : report.issues)
	{
		if (issue.limit == limit)
			return &issue;
	}
	return nullptr;
}
ModelTargetPreparation prepare(const ModelMesh &mesh, const QString &format, const QString &id, const ModelTargetPrepareOptions &options = {})
{
	ModelTargetPreparation result;
	QString error;
	if (!expect(prepareModelForTarget(mesh, profile(format, id), options, &result, &error), "the preparation runs"))
		std::cerr << "  " << qPrintable(error) << '\n';
	return result;
}
int triangles(const ModelMesh &mesh)
{
	int count = 0;
	for (const auto &surface : mesh.surfaces)
		count += surface.triangles.size();
	return count;
}
// A grid moved by `dx` along X.
ModelMesh shiftedGrid(int columns, int rows, float dx)
{
	auto mesh = targetMesh({targetGridSurface(QStringLiteral("body"), columns, rows)});
	for (auto &p : mesh.surfaces[0].frames[0].positions)
		p.x += dx;
	updateEditableModelMetadata(&mesh);
	return mesh;
}
// One joint per weight along +X, all influencing vertex 0 of the arm's strip
// (as model_skeletal_export_smoke_test's fixture).
ModelMesh influenceFixture(const QVector<float> &weights)
{
	ModelMesh mesh = exportArmFixture();
	auto &skeleton = mesh.skeleton;
	skeleton.tags.clear();
	skeleton.joints.clear();
	QVector<ModelJointMatrix> local;
	for (int joint = 0; joint < weights.size(); ++joint)
	{
		skeleton.joints.append({QStringLiteral("j%1").arg(joint), joint - 1, {}});
		local.append(modelJointMatrix({}, {joint == 0 ? 0.0f : 4.0f, 0, 0}));
	}
	const auto bind = exportPose(skeleton, local);
	for (int joint = 0; joint < bind.size(); ++joint)
		skeleton.joints[joint].bind = bind[joint];
	ModelSkeletalClip clip;
	clip.name = QStringLiteral("sway");
	clip.framesPerSecond = 20;
	clip.frames = {bind, bind};
	skeleton.clips = {clip};
	auto &surface = mesh.surfaces[0];
	QVector<int> joints;
	QVector<float> perVertex;
	for (int vertex = 0; vertex < surface.vertexCount; ++vertex)
		for (int slot = 0; slot < weights.size(); ++slot)
		{
			joints << slot;
			perVertex << (vertex == 0 ? weights[slot] : (slot == 0 ? 1.0f : 0.0f));
		}
	QString error;
	if (!modelSkinningFromBindPose(skeleton, surface.frames[0].positions, surface.frames[0].normals, joints, perVertex, int(weights.size()),
								   &surface.skinning, &error) ||
		!bakeModelSkeleton(&mesh, {}, &error))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}

void checkNewTargets(const QString &directory)
{
	for (const auto &id : {QStringLiteral("rtcw"), QStringLiteral("rtcw-mp"), QStringLiteral("et"), QStringLiteral("q2pro"), QStringLiteral("gzdoom")})
	{
		const auto entry = profile(QStringLiteral("md3"), id);
		expect(!entry.isDefault && !entry.description.isEmpty() && !entry.sources.isEmpty() && entry.sources.first().startsWith(QStringLiteral("https://")),
			   "each new MD3 target is documented with its sources");
	}
	expect(profile(QStringLiteral("md3"), QStringLiteral("et")).limits.detailLevels == 3 &&
			   profile(QStringLiteral("md3"), QStringLiteral("rtcw")).limits.detailLevels == 2 &&
			   profile(QStringLiteral("md3"), QStringLiteral("rtcw-mp")).limits.detailLevels == 0 &&
			   profile(QStringLiteral("md3"), QStringLiteral("q2pro")).limits.detailLevels == 0 &&
			   profile(QStringLiteral("md3"), QStringLiteral("gzdoom")).limits.detailLevels == 0,
		   "the detail levels each engine loads: three for ET, two for RTCW, none for RTCW multiplayer, Q2PRO or GZDoom");
	// Enemy Territory works to 1,024 vertices and 2,049 triangles per surface.
	const auto et1024 = targetMesh({targetGridSurface(QStringLiteral("body"), 32, 32)});
	const auto et1056 = targetMesh({targetGridSurface(QStringLiteral("body"), 33, 32)});
	expect(assess(et1024, QStringLiteral("md3"), QStringLiteral("et")).ready() && !assess(et1024, QStringLiteral("md3"), QStringLiteral("quake3")).ready(),
		   "1,024 vertices fit Enemy Territory and not Quake III");
	expect(issueOf(assess(et1056, QStringLiteral("md3"), QStringLiteral("et")), QStringLiteral("vertices")), "1,056 vertices are over Enemy Territory's limit");
	// RTCW single player: 3,999 vertices; multiplayer: Quake III's 999.
	const auto rtcw = targetMesh({targetGridSurface(QStringLiteral("body"), 60, 60)});
	expect(assess(rtcw, QStringLiteral("md3"), QStringLiteral("rtcw")).ready() && !assess(rtcw, QStringLiteral("md3"), QStringLiteral("rtcw-mp")).ready(),
		   "3,600 vertices fit RTCW single player and not its multiplayer");
	// Q2PRO: 6,144 triangles per surface; GZDoom sets no vertex limit.
	const auto q2proFits = targetMesh({targetGridSurface(QStringLiteral("body"), 55, 55)});
	const auto q2proOver = targetMesh({targetGridSurface(QStringLiteral("body"), 70, 70)});
	expect(assess(q2proFits, QStringLiteral("md3"), QStringLiteral("q2pro")).ready() &&
			   issueOf(assess(q2proOver, QStringLiteral("md3"), QStringLiteral("q2pro")), QStringLiteral("triangles")),
		   "Q2PRO's MD3 loader holds 6,144 triangles per surface");
	const auto large = targetMesh({targetGridSurface(QStringLiteral("body"), 100, 100)});
	QString error;
	expect(assess(large, QStringLiteral("md3"), QStringLiteral("gzdoom")).ready() &&
			   !exportEditableModel(large, QStringLiteral("md3"), [] { ModelExportOptions o; o.target = QStringLiteral("gzdoom"); return o; }(), &error).isEmpty(),
		   "GZDoom loads and the writer writes a 10,000-vertex MD3 surface");
	// Detail levels per target.
	ModelLodSet set;
	const auto base = QDir(directory).filePath(QStringLiteral("et/prop.md3"));
	QDir(directory).mkpath(QStringLiteral("et"));
	const auto prop = targetMesh({targetGridSurface(QStringLiteral("prop"), 12, 12)});
	expect(prepareModelLodSet(prop, 3, 0.5, base, QStringLiteral("et"), {}, false, &set, &error) && set.files.size() == 4 &&
			   set.files.last().path.endsWith(QStringLiteral("prop_3.md3")),
		   "Enemy Territory takes a third detail level");
	expect(!prepareModelLodSet(prop, 1, 0.5, base, QStringLiteral("rtcw-mp"), {}, false, &set, &error) && error.contains(QStringLiteral("no detail levels")),
		   "RTCW multiplayer loads no detail levels, so none are written");
	// Q2PRO MD5 replacements and GZDoom IQM.
	const auto q2md5 = profile(QStringLiteral("md5mesh"), QStringLiteral("q2pro")).limits;
	expect(q2md5.joints == 256 && q2md5.surfaces == 32 && q2md5.vertices == 6144 && q2md5.weights == 8192 &&
			   profile(QStringLiteral("md5anim"), QStringLiteral("q2pro")).limits.frames == 1024,
		   "Q2PRO's MD5 limits come from MD5_MAX_* and TESS_MAX_VERTICES");
	expect(assess(exportArmFixture(), QStringLiteral("md5mesh"), QStringLiteral("q2pro")).ready() &&
			   issueOf(assess(targetArmWithJoints(300), QStringLiteral("md5mesh"), QStringLiteral("q2pro")), QStringLiteral("joints")),
		   "Q2PRO refuses more than 256 MD5 joints");
	auto longClip = exportArmFixture();
	auto &clip = longClip.skeleton.clips.first();
	while (clip.frames.size() < 1025)
		clip.frames.append(clip.frames.last());
	ModelTargetReport report;
	expect(assessModelTarget(longClip, profile(QStringLiteral("md5anim"), QStringLiteral("q2pro")), &report, &error) && issueOf(report, QStringLiteral("frames")) &&
			   issueOf(report, QStringLiteral("frames"))->element == clip.name,
		   "a clip longer than Q2PRO's 1,024 frames is named");
	const auto six = influenceFixture({0.3f, 0.25f, 0.2f, 0.1f, 0.1f, 0.05f});
	const auto iqm = assess(six, QStringLiteral("iqm"), QStringLiteral("gzdoom"));
	const bool measured = std::any_of(iqm.measures.cbegin(), iqm.measures.cend(), [](const ModelTargetMeasure &measure)
									  { return measure.limit == QStringLiteral("influences") && measure.value == 6 && measure.maximum == 4; });
	expect(iqm.ready() && measured, "six influences on a vertex are measured against IQM's four, without blocking");
}

void checkExactElements()
{
	QString error;
	// The faces past Quake III's budget are those a split moves away.
	const auto over = targetMesh({targetGridSurface(QStringLiteral("body"), 40, 25)});
	const auto report = assess(over, QStringLiteral("md3"), QStringLiteral("quake3"));
	const auto *vertices = issueOf(report, QStringLiteral("vertices"));
	if (expect(vertices && !vertices->faces.isEmpty() && vertices->faces.size() < over.surfaces[0].triangles.size(), "the vertex issue names the faces past the budget"))
	{
		ModelEdit edit;
		edit.kind = ModelEditKind::SeparateFaces;
		edit.selection.surface = 0;
		edit.selection.faces = QSet<int>(vertices->faces.cbegin(), vertices->faces.cend());
		edit.text = QStringLiteral("body2");
		auto split = over;
		expect(applyModelEdit(&split, edit, nullptr, &error) && split.surfaces[0].vertexCount <= 999 && split.surfaces[1].vertexCount <= 999 &&
				   assess(split, QStringLiteral("md3"), QStringLiteral("quake3")).ready(),
			   "separating exactly those faces fits both surfaces");
		const auto json = modelTargetIssueJson(*vertices);
		expect(json.value(QStringLiteral("faces")).toArray().size() == vertices->faces.size(), "the issue's JSON lists its faces");
	}
	// UVs outside the tile name their vertices.
	auto quad = targetMd2Quad(1);
	quad.surfaces[0].texCoords[2] = {1.5f, 0.5f};
	// Each report outlives the issue pointer taken from it.
	const auto uvReport = assess(quad, QStringLiteral("md2"), QStringLiteral("quake2"));
	const auto *uv = issueOf(uvReport, QStringLiteral("uv-range"));
	expect(uv && uv->vertices == QVector<int>{2}, "a UV outside the tile names its vertex");
	// Positions outside MD3's range name their vertices; collapsing triangles their faces.
	const auto shifted = shiftedGrid(4, 2, 500);
	QVector<int> outside;
	for (int v = 0; v < shifted.surfaces[0].vertexCount; ++v)
	{
		if (shifted.surfaces[0].frames[0].positions[v].x > 511.984375f)
			outside << v;
	}
	const auto rangeReport = assess(shifted, QStringLiteral("md3"), QStringLiteral("quake3"));
	const auto *range = issueOf(rangeReport, QStringLiteral("coordinate-range"));
	expect(range && !outside.isEmpty() && range->vertices == outside && range->value == outside.size(),
		   "positions outside MD3's range name exactly their vertices");
	auto tiny = targetMesh({targetGridSurface(QStringLiteral("body"), 2, 2)});
	for (auto &p : tiny.surfaces[0].frames[0].positions)
		p = {p.x / 1024.0f, p.y / 1024.0f, 0};
	updateEditableModelMetadata(&tiny);
	const auto precisionReport = assess(tiny, QStringLiteral("md3"), QStringLiteral("quake3"));
	const auto *precision = issueOf(precisionReport, QStringLiteral("precision"));
	expect(precision && precision->faces == QVector<int>({0, 1}), "triangles that collapse at 1/64 units name their faces");
	// A non-manifold book: every page shares the spine's two vertices, the
	// case that scans one vertex's faces for every part.
	ModelSurface book;
	book.name = QStringLiteral("book");
	ModelFrameGeometry pages;
	pages.positions = {{0, 0, 0}, {0, 0, 8}};
	for (int page = 0; page < 6000; ++page)
	{
		pages.positions.append({float(std::cos(page * 0.001)) * 8, float(std::sin(page * 0.001)) * 8, 4});
		book.triangles.append({0, 1, page + 2});
	}
	pages.normals = QVector<ModelVec3>(pages.positions.size(), ModelVec3{0, 0, 1});
	book.vertexCount = int(pages.positions.size());
	book.frames = {pages};
	QVector<int> partOfFace;
	int parts = 0;
	expect(modelTargetSurfaceParts(book, 999, 0, &partOfFace, &parts, &error) && parts == 7, "a 6,002-vertex book splits into seven parts of 999 vertices");
	QVector<QSet<int>> partVertices(std::max(parts, 1));
	bool assignedAll = partOfFace.size() == book.triangles.size();
	for (int face = 0; assignedAll && face < partOfFace.size(); ++face)
	{
		assignedAll = partOfFace[face] >= 0 && partOfFace[face] < parts;
		if (assignedAll)
			partVertices[partOfFace[face]] += QSet<int>{book.triangles[face].a, book.triangles[face].b, book.triangles[face].c};
	}
	expect(assignedAll && std::all_of(partVertices.cbegin(), partVertices.cend(), [](const QSet<int> &set) { return set.size() <= 999; }),
		   "every page is in one part, and each part keeps within the vertex budget");
}

void checkPrepare()
{
	QString error;
	// Split: every vertex and triangle kept, every surface in budget.
	const auto big = targetMesh({targetGridSurface(QStringLiteral("body"), 60, 40)});
	const auto split = prepare(big, QStringLiteral("md3"), QStringLiteral("quake3"));
	bool inBudget = true;
	for (const auto &surface : split.mesh.surfaces)
		inBudget &= surface.vertexCount <= 999 && surface.triangles.size() <= 1999;
	expect(!split.before.ready() && split.after.ready() && split.changed() && split.fixes.first().kind == ModelTargetFixKind::Split &&
			   split.mesh.surfaces.size() >= 3 && triangles(split.mesh) == triangles(big) && inBudget &&
			   !exportEditableModel(split.mesh, QStringLiteral("md3"), 0, &error).isEmpty(),
		   "a surface over Quake III's budget splits into surfaces that fit, keeping every triangle");
	QSet<QString> names;
	for (const auto &surface : split.mesh.surfaces)
	{
		auto name = surface.name.toLower();
		if (name.size() > 2 && name[name.size() - 2] == QLatin1Char('_'))
			name.chop(2);
		names.insert(name);
	}
	expect(names.size() == split.mesh.surfaces.size() && !split.selection.surfaces.isEmpty(), "split surfaces keep distinct names after Quake III's normalisation");
	// One undo step through the document; a stale review is refused.
	ModelDocument document;
	expect(document.setMesh(big, &error), "open the big grid");
	const auto base = document.revisionFingerprint();
	expect(document.adopt(split.mesh, split.selection, base, &error) && document.canUndo() && document.mesh().surfaces.size() == split.mesh.surfaces.size(),
		   "the prepared model is adopted");
	const auto prepared = document.revisionFingerprint();
	expect(document.undo() && document.revisionFingerprint() == base && !document.canUndo() && document.redo() && document.revisionFingerprint() == prepared,
		   "one undo restores the model and one redo prepares it again");
	expect(document.undo() && !document.adopt(split.mesh, split.selection, prepared, &error) && document.revisionFingerprint() == base &&
			   error.contains(QStringLiteral("changed")),
		   "a preparation of another revision is refused, changing nothing");
	// Without splitting, the surface is reduced instead; with neither, nothing changes.
	ModelTargetPrepareOptions noSplit;
	noSplit.split = false;
	const auto reduced = prepare(big, QStringLiteral("md3"), QStringLiteral("quake3"), noSplit);
	expect(reduced.after.ready() && reduced.mesh.surfaces.size() == 1 && reduced.fixes.first().kind == ModelTargetFixKind::Decimate &&
			   triangles(reduced.mesh) < triangles(big),
		   "without splits the surface is decimated to the budget");
	ModelTargetPrepareOptions none;
	expect(skipModelTargetFixes(QStringLiteral("split,decimate"), &none, &error) && !none.split && !none.decimate && none.fit, "fix kinds can be skipped");
	expect(!skipModelTargetFixes(QStringLiteral("split,shrink"), &none, &error) && error.contains(QStringLiteral("shrink")), "an unknown fix kind is refused");
	none = {};
	none.split = none.decimate = false;
	const auto untouched = prepare(big, QStringLiteral("md3"), QStringLiteral("quake3"), none);
	expect(!untouched.changed() && !untouched.after.ready() &&
			   modelTargetPreparationLines(untouched).join(QLatin1Char('\n')).contains(QStringLiteral("Still blocking")),
		   "with no fix allowed nothing changes and the blockers are listed");
	// MDL holds one surface: the second is joined into the first.
	auto twoMdl = targetMdlGrid(6, 6);
	auto second = twoMdl.surfaces[0];
	second.name = QStringLiteral("other");
	twoMdl.surfaces.append(second);
	updateEditableModelMetadata(&twoMdl);
	const auto joined = prepare(twoMdl, QStringLiteral("mdl"), QStringLiteral("quake"));
	expect(issueOf(joined.before, QStringLiteral("single-surface")) && joined.after.ready() && joined.mesh.surfaces.size() == 1 &&
			   joined.fixes.first().kind == ModelTargetFixKind::Join && triangles(joined.mesh) == triangles(twoMdl),
		   "the surfaces of a model bound for MDL are joined into one");
	// MD2 over its positions and triangles is decimated until both fit.
	auto md2 = targetMesh({targetGridSurface(QStringLiteral("body"), 50, 50)});
	md2.surfaces[0].skinPaths = {QStringLiteral("models/test/body.pcx")};
	md2.md2SkinSize = {256, 256};
	updateEditableModelMetadata(&md2);
	const auto md2Ready = prepare(md2, QStringLiteral("md2"), QStringLiteral("quake2"));
	expect(!md2Ready.before.ready() && md2Ready.after.ready() && md2Ready.fixes.first().kind == ModelTargetFixKind::Decimate &&
			   md2Ready.fixes.first().before == triangles(md2),
		   "a model over MD2's limits is reduced until it fits");
	// Outside MD3's range: moved, with its tag.
	auto far = shiftedGrid(10, 10, 600);
	ModelTag tag;
	tag.name = QStringLiteral("tag_light");
	tag.origin = {610, 0, 0};
	far.tags = {tag};
	updateEditableModelMetadata(&far);
	const auto moved = prepare(far, QStringLiteral("md3"), QStringLiteral("quake3"));
	const float before = far.tags[0].origin.x - far.surfaces[0].frames[0].positions[0].x;
	const float after = moved.mesh.tags[0].origin.x - moved.mesh.surfaces[0].frames[0].positions[0].x;
	expect(issueOf(moved.before, QStringLiteral("coordinate-range")) && moved.after.ready() && moved.fixes.first().kind == ModelTargetFixKind::Fit &&
			   std::abs(before - after) < 1e-3f && !moved.notes.isEmpty(),
		   "a model outside MD3's range moves into it with its tags, and the review says the origin moved");
	const auto wide = targetMesh({targetGridSurface(QStringLiteral("body"), 300, 2)});
	const auto scaled = prepare(wide, QStringLiteral("md3"), QStringLiteral("quake3"));
	expect(scaled.after.ready() && scaled.fixes.first().kind == ModelTargetFixKind::Fit && scaled.fixes.first().summary.contains(QStringLiteral("Scaled")),
		   "a model too long for MD3's range is scaled down to fit");
	// IQM keeps four influences: preparing keeps the strongest four in the source.
	const auto six = influenceFixture({0.3f, 0.25f, 0.2f, 0.1f, 0.1f, 0.05f});
	const auto pruned = prepare(six, QStringLiteral("iqm"), QStringLiteral("iqm"));
	ModelExportReport report;
	expect(pruned.changed() && pruned.fixes.first().kind == ModelTargetFixKind::Influences && pruned.fixes.first().after == 4 &&
			   !exportEditableModel(pruned.mesh, QStringLiteral("iqm"), ModelExportOptions{}, &error, {}, &report).isEmpty() && report.reducedInfluenceVertices == 0,
		   "joint influences past IQM's four are reduced in the source, so the export drops none");
	const auto json = modelTargetPreparationJson(split);
	expect(json.value(QStringLiteral("ready")).toBool() && json.value(QStringLiteral("fixes")).toArray().first().toObject().value(QStringLiteral("kind")) ==
																	 QStringLiteral("split") &&
			   json.value(QStringLiteral("before")).toObject().value(QStringLiteral("ready")) == false,
		   "the preparation's JSON lists its fixes and both reports");
}

// What the source owns beside its geometry follows Prepare's changes: removed
// unused vertices count as a change, skin variants cover split surfaces and
// drop joined ones, and stored hand-made detail levels are noted.
void checkPrepareKeepsSourceData()
{
	QString error;
	// 900 used vertices and 200 no face uses: removing them is the whole fix.
	auto padded = targetMesh({targetGridSurface(QStringLiteral("body"), 30, 30)});
	auto &surface = padded.surfaces[0];
	for (int extra = 0; extra < 200; ++extra)
	{
		surface.frames[0].positions.append({float(extra), -8, 0});
		surface.frames[0].normals.append({0, 0, 1});
		surface.texCoords.append({0.5f, 0.5f});
	}
	surface.vertexCount += 200;
	updateEditableModelMetadata(&padded);
	const auto trimmed = prepare(padded, QStringLiteral("md3"), QStringLiteral("quake3"));
	expect(!trimmed.before.ready() && trimmed.after.ready() && trimmed.changed() && trimmed.fixes.size() == 1 &&
			   trimmed.fixes.first().kind == ModelTargetFixKind::Split && trimmed.fixes.first().before == 1100 && trimmed.fixes.first().after == 900 &&
			   trimmed.mesh.surfaces[0].vertexCount == 900 && trimmed.fixes.first().summary.contains(QStringLiteral("unused")),
		   "removing unused vertices is a listed change, so the review applies it");
	expect(modelTargetPreparationJson(trimmed).value(QStringLiteral("changed")).toBool() &&
			   !modelTargetPreparationLines(trimmed).first().startsWith(QStringLiteral("Nothing")),
		   "the review's text and JSON report the change");
	ModelDocument document;
	expect(document.setMesh(padded, &error) && document.adopt(trimmed.mesh, trimmed.selection, document.revisionFingerprint(), &error) &&
			   document.mesh().surfaces[0].vertexCount == 900 && document.canUndo(),
		   "the editor adopts the trimmed surface as one undo step");

	// A split surface's parts keep its material in every skin variant.
	auto big = targetMesh({targetGridSurface(QStringLiteral("body"), 60, 40), targetGridSurface(QStringLiteral("head"), 4, 4, 8)});
	big.skinVariants = {{QStringLiteral("red"), {{QStringLiteral("body"), QStringLiteral("models/test/body_red")}}},
						{QStringLiteral("blue"), {{QStringLiteral("head"), QStringLiteral("models/test/head_blue")}}}};
	ModelLodRecipeLevel handMade;
	handMade.generated = false;
	handMade.source = QStringLiteral("body_low.mesh.json");
	big.detailLevels = {handMade};
	updateEditableModelMetadata(&big);
	const auto split = prepare(big, QStringLiteral("md3"), QStringLiteral("quake3"));
	const auto &red = split.mesh.skinVariants[0];
	bool covered = split.mesh.surfaces.size() > 3;
	for (const auto &part : split.mesh.surfaces)
	{
		if (part.name == QStringLiteral("head"))
			continue;
		covered &= std::any_of(red.bindings.cbegin(), red.bindings.cend(), [&](const ModelSkinVariantBinding &binding)
							   { return binding.surface == part.name && binding.material == QStringLiteral("models/test/body_red"); });
	}
	expect(split.after.ready() && covered && split.mesh.skinVariants[1].bindings.size() == 1 && validateEditableModel(split.mesh).isEmpty(),
		   "every part of the split surface wears its red material, and blue still binds the head alone");
	const auto notes = split.notes.join(QLatin1Char('\n'));
	expect(notes.contains(QStringLiteral("Skin variants red")) && notes.contains(QStringLiteral("hand-made detail level")),
		   "the review says the variants cover the new surfaces and the hand-made level needs rebuilding");
	ModelMesh applied;
	QStringList inherited;
	expect(applyModelSkinVariant(split.mesh, red, &applied, &error, &inherited) && inherited == QStringList{QStringLiteral("head")},
		   "the red skin of the prepared model leaves only the head on its primary material");
	// The written .skin names every part with the red material.
	QByteArray skin;
	bool partsRed = writeModelQ3Skin(applied, &skin, &error);
	for (const auto &part : split.mesh.surfaces)
	{
		if (part.name != QStringLiteral("head"))
			partsRed &= skin.contains(QStringLiteral("\"%1\",\"models/test/body_red\"").arg(modelQ3SkinSurfaceName(part.name)).toUtf8());
	}
	expect(partsRed && skin.contains("\"body2\",\"models/test/body_red\""), "the red .skin binds every split part to the red material");

	// MD2 joins into one surface; bindings to the joined surfaces go, with a note.
	auto legs = targetGridSurface(QStringLiteral("legs"), 6, 6, 4);
	auto body = targetGridSurface(QStringLiteral("body"), 6, 6);
	body.skinPaths = legs.skinPaths = {QStringLiteral("models/test/body.pcx")};
	auto md2 = targetMesh({body, legs});
	md2.md2SkinSize = {256, 256};
	md2.skinVariants = {{QStringLiteral("red"), {{QStringLiteral("body"), QStringLiteral("models/test/body_red.pcx")},
												 {QStringLiteral("legs"), QStringLiteral("models/test/legs_red.pcx")}}}};
	updateEditableModelMetadata(&md2);
	const auto joined = prepare(md2, QStringLiteral("md2"), QStringLiteral("quake2"));
	expect(joined.mesh.surfaces.size() == 1 && joined.mesh.skinVariants[0].bindings.size() == 1 &&
			   joined.mesh.skinVariants[0].bindings[0].surface == QStringLiteral("body") && modelSkinVariantMissingSurfaces(joined.mesh, joined.mesh.skinVariants[0]).isEmpty() &&
			   joined.notes.join(QLatin1Char('\n')).contains(QStringLiteral("red (legs)")),
		   "joining drops the variant's binding of the joined surface and says so");

	// Separating or duplicating by hand gives the new surface its origin's
	// bindings, replacing one an earlier surface of that name left.
	auto parted = targetMesh({targetGridSurface(QStringLiteral("body"), 4, 4)});
	parted.skinVariants = {{QStringLiteral("red"), {{QStringLiteral("body"), QStringLiteral("models/test/body_red")}}},
						   {QStringLiteral("blue"), {{QStringLiteral("panel"), QStringLiteral("models/test/old_panel")}}}};
	updateEditableModelMetadata(&parted);
	ModelEdit separate;
	separate.kind = ModelEditKind::SeparateFaces;
	separate.selection.surface = 0;
	separate.selection.faces = {0, 1};
	separate.text = QStringLiteral("panel");
	ModelSelection selected;
	expect(applyModelEdit(&parted, separate, &selected, &error) && parted.surfaces.size() == 2 &&
			   parted.skinVariants[0].bindings == QVector<ModelSkinVariantBinding>{{QStringLiteral("body"), QStringLiteral("models/test/body_red")},
																				  {QStringLiteral("panel"), QStringLiteral("models/test/body_red")}} &&
			   parted.skinVariants[1].bindings.isEmpty(),
		   "a separated surface wears its origin's red material, and blue's old binding of that name goes");
	ModelEdit duplicate;
	duplicate.kind = ModelEditKind::DuplicateSurface;
	duplicate.selection.surface = 1;
	duplicate.text = QStringLiteral("panel_copy");
	expect(applyModelEdit(&parted, duplicate, &selected, &error) && parted.skinVariants[0].bindings.size() == 3 &&
			   parted.skinVariants[0].bindings.last().surface == QStringLiteral("panel_copy"),
		   "a duplicated surface wears its origin's variant materials");
}

void checkDelivery(const QString &directory)
{
	QString error;
	const auto root = QDir(directory).filePath(QStringLiteral("project"));
	QDir().mkpath(QDir(root).filePath(QStringLiteral("models/props")));
	auto manifest = defaultProjectManifest(root);
	ModelDeliveryPreferences saved;
	saved.targets.insert(QStringLiteral("md3"), QStringLiteral("et"));
	saved.strictSet = true;
	saved.strict = true;
	setProjectModelDeliveryPreferences(&manifest, saved);
	expect(saveProjectManifest(manifest, &error), "save a project with delivery targets");
	ProjectManifest loaded;
	expect(loadProjectManifest(root, &loaded, &error) && projectModelDeliveryPreferences(loaded) == saved, "the project's delivery targets round-trip");
	QFile file(projectManifestPath(root));
	expect(file.open(QIODevice::ReadOnly) && file.readAll().contains("\"modelDelivery\""), "they live under modelDelivery in the manifest");
	expect(findModelProjectRoot(QDir(root).filePath(QStringLiteral("models/props/lamp.mesh.json"))) == normalizedProjectRootPath(root) &&
			   findModelProjectRoot(QDir(directory).filePath(QStringLiteral("elsewhere/lamp.mesh.json"))).isEmpty(),
		   "a model inside the project finds it, and one outside does not");
	ModelDeliveryPreferences parsed;
	expect(!parseModelDeliveryPreferences(QJsonObject{{QStringLiteral("targets"), QJsonObject{{QStringLiteral("md3"), QStringLiteral("nonsense")}}}}, &parsed, &error) &&
			   !parseModelDeliveryPreferences(QJsonObject{{QStringLiteral("targets"), QJsonObject{{QStringLiteral("obj"), QStringLiteral("x")}}}}, &parsed, &error) &&
			   !parseModelDeliveryPreferences(QJsonObject{{QStringLiteral("strict"), QStringLiteral("yes")}}, &parsed, &error),
		   "unknown targets, formats without targets and a non-boolean strict setting are refused");
	auto stale = loaded;
	stale.extraFields.insert(modelDeliveryManifestKey(), QJsonObject{{QStringLiteral("targets"), QJsonObject{{QStringLiteral("md3"), QStringLiteral("gone")},
																												{QStringLiteral("mdl"), QStringLiteral("fte")}}}});
	QStringList warnings;
	const auto partial = projectModelDeliveryPreferences(stale, &warnings);
	expect(partial.targets.value(QStringLiteral("mdl")) == QStringLiteral("fte") && !partial.targets.contains(QStringLiteral("md3")) && warnings.size() == 1,
		   "a stale saved target is dropped with a warning, keeping the rest");
	setProjectModelDeliveryPreferences(&stale, {});
	expect(!stale.extraFields.contains(modelDeliveryManifestKey()), "empty preferences remove the key");
	const auto suggested = [](const QString &executable)
	{
		GameInstallationProfile installation;
		installation.executablePath = executable;
		return suggestedModelDeliveryPreferences(installation);
	};
	expect(suggested(QStringLiteral("C:/Games/Quake2/q2pro.exe")).targets.value(QStringLiteral("md2")) == QStringLiteral("q2pro") &&
			   suggested(QStringLiteral("/opt/quake/quakespasm")).targets.value(QStringLiteral("mdl")) == QStringLiteral("quakespasm") &&
			   suggested(QStringLiteral("/opt/q3/ioquake3.x86_64")).targets.value(QStringLiteral("md3")) == QStringLiteral("ioquake3") &&
			   suggested(QStringLiteral("D:/RTCW/WolfSP.exe")).targets.value(QStringLiteral("md3")) == QStringLiteral("rtcw") &&
			   suggested(QStringLiteral("D:/ET/ET.exe")).targets.value(QStringLiteral("md3")) == QStringLiteral("et") &&
			   suggested(QStringLiteral("D:/GZDoom/gzdoom.exe")).targets.value(QStringLiteral("iqm")) == QStringLiteral("gzdoom") &&
			   suggested(QStringLiteral("D:/Quake3/quake3.exe")).isEmpty(),
		   "installations suggest the targets of their engines, and the original games keep the defaults");
	ModelDeliveryPreferences installation;
	installation.targets.insert(QStringLiteral("md3"), QStringLiteral("ioquake3"));
	installation.targets.insert(QStringLiteral("mdl"), QStringLiteral("quakespasm"));
	expect(resolveModelDeliveryTarget(QStringLiteral("md3"), QStringLiteral("rtcw"), saved, installation).origin == ModelDeliveryOrigin::Explicit &&
			   resolveModelDeliveryTarget(QStringLiteral("md3"), {}, saved, installation).target == QStringLiteral("et") &&
			   resolveModelDeliveryTarget(QStringLiteral("mdl"), {}, saved, installation).origin == ModelDeliveryOrigin::Installation &&
			   resolveModelDeliveryTarget(QStringLiteral("md2"), {}, saved, installation).target == QStringLiteral("quake2") &&
			   resolveModelDeliveryTarget(QStringLiteral("md2"), {}, saved, installation).origin == ModelDeliveryOrigin::Default &&
			   resolveModelDeliveryTarget(QStringLiteral("obj"), {}, saved, installation).target.isEmpty(),
		   "a target is the one named, else the project's, else the installation's, else the format's default");
}

int run(const QString &executable, const QString &settings, const QStringList &arguments, QJsonObject *json)
{
	QProcess process;
	process.start(executable, QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), settings} + arguments + QStringList{QStringLiteral("--json")});
	if (!process.waitForFinished(180000))
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
	const auto save = [&](const ModelMesh &mesh, const QString &path)
	{
		ModelDocument document;
		QDir().mkpath(QFileInfo(path).absolutePath());
		expect(document.setMesh(mesh, &error) && document.save(path, true, &error), "save a CLI source");
		return path;
	};
	const auto big = save(targetMesh({targetGridSurface(QStringLiteral("body"), 60, 40)}), QDir(directory).filePath(QStringLiteral("cli/big.mesh.json")));
	const auto prepared = QDir(directory).filePath(QStringLiteral("cli/big.prepared.mesh.json"));
	QJsonObject json;
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("prepare"), big}, &json) == 2, "model prepare needs --format");
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("prepare"), big, QStringLiteral("--format"), QStringLiteral("md3"), QStringLiteral("--dry-run")}, &json) == 0 &&
			   json.value(QStringLiteral("changed")).toBool() && json.value(QStringLiteral("ready")).toBool() && !json.value(QStringLiteral("written")).toBool() &&
			   !QFile::exists(prepared),
		   "a dry run reviews the preparation and writes nothing");
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("prepare"), big, QStringLiteral("--format"), QStringLiteral("md3"), QStringLiteral("--output"), prepared},
			   &json) == 0 &&
			   json.value(QStringLiteral("written")).toBool() &&
			   json.value(QStringLiteral("fixes")).toArray().first().toObject().value(QStringLiteral("kind")) == QStringLiteral("split") &&
			   json.value(QStringLiteral("targetOrigin")) == QStringLiteral("default"),
		   "model prepare writes the prepared source");
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("targets"), prepared, QStringLiteral("--format"), QStringLiteral("md3"), QStringLiteral("--target"),
				QStringLiteral("quake3")},
			   &json) == 0,
		   "the prepared source is ready for Quake III");
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("prepare"), big, QStringLiteral("--format"), QStringLiteral("md3"), QStringLiteral("--skip"),
				QStringLiteral("split,decimate"), QStringLiteral("--dry-run")},
			   &json) == 4 &&
			   !json.value(QStringLiteral("ready")).toBool() && !json.value(QStringLiteral("changed")).toBool(),
		   "a preparation that leaves blockers exits 4");
	// A project's saved target is what model build and model lod use there.
	const auto project = QDir(directory).filePath(QStringLiteral("cliproject"));
	QDir().mkpath(project);
	expect(saveProjectManifest(defaultProjectManifest(project), &error), "create a CLI project");
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("targets"), QStringLiteral("--project"), project, QStringLiteral("--save"), QStringLiteral("--format"),
				QStringLiteral("md3"), QStringLiteral("--target"), QStringLiteral("et"), QStringLiteral("--strict")},
			   &json) == 0 &&
			   json.value(QStringLiteral("saved")).toObject().value(QStringLiteral("targets")).toObject().value(QStringLiteral("md3")) == QStringLiteral("et"),
		   "model targets --save keeps a project's target");
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("targets"), QStringLiteral("--project"), project, QStringLiteral("--format"), QStringLiteral("md3")},
			   &json) == 0 &&
			   json.value(QStringLiteral("delivery")).toArray().first().toObject().value(QStringLiteral("targetOrigin")) == QStringLiteral("project") &&
			   json.value(QStringLiteral("delivery")).toArray().first().toObject().value(QStringLiteral("strict")).toBool(),
		   "the project's view says where its target and strict setting come from");
	const auto inside = save(targetMesh({targetGridSurface(QStringLiteral("body"), 32, 32)}), QDir(project).filePath(QStringLiteral("models/prop.mesh.json")));
	const auto outside = save(targetMesh({targetGridSurface(QStringLiteral("body"), 32, 32)}), QDir(directory).filePath(QStringLiteral("cli/prop.mesh.json")));
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("build"), inside, QStringLiteral("--output"), QDir(project).filePath(QStringLiteral("out/prop.md3")),
				QStringLiteral("--dry-run")},
			   &json) == 0 &&
			   json.value(QStringLiteral("target")).toObject().value(QStringLiteral("target")) == QStringLiteral("et") &&
			   json.value(QStringLiteral("targetOrigin")) == QStringLiteral("project") && json.value(QStringLiteral("strict")).toBool() &&
			   json.value(QStringLiteral("strictOrigin")) == QStringLiteral("project"),
		   "model build inside the project uses its target and strict setting");
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("build"), outside, QStringLiteral("--output"), QDir(directory).filePath(QStringLiteral("cli/prop.md3")),
				QStringLiteral("--dry-run")},
			   &json) == 4,
		   "the same model outside the project is over Quake III's limit");
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("build"), inside, QStringLiteral("--output"), QDir(project).filePath(QStringLiteral("out/prop.md3")),
				QStringLiteral("--dry-run"), QStringLiteral("--no-strict")},
			   &json) == 0 &&
			   !json.value(QStringLiteral("strict")).toBool() && json.value(QStringLiteral("strictOrigin")) == QStringLiteral("explicit"),
		   "--no-strict overrides the project's strict setting");
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("lod"), inside, QStringLiteral("--output"), QDir(project).filePath(QStringLiteral("out/lod.md3")),
				QStringLiteral("--levels"), QStringLiteral("3"), QStringLiteral("--dry-run"), QStringLiteral("--no-strict")},
			   &json) == 0 &&
			   json.value(QStringLiteral("target")) == QStringLiteral("et") && json.value(QStringLiteral("outputs")).toArray().size() == 4,
		   "model lod in the project uses Enemy Territory's three levels");
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("targets"), QStringLiteral("--project"), project, QStringLiteral("--clear")}, &json) == 0 &&
			   json.value(QStringLiteral("saved")).toObject().isEmpty(),
		   "model targets --clear removes the saved choices");
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("targets"), QStringLiteral("--save"), QStringLiteral("--format"), QStringLiteral("md3")},
			   &json) == 2,
		   "saving needs a project");
	// A manifest that exists but cannot be read is never replaced.
	const auto broken = QDir(directory).filePath(QStringLiteral("brokenproject"));
	QDir().mkpath(QFileInfo(projectManifestPath(broken)).absolutePath());
	const QByteArray brokenText = "{\"displayName\": \"My Mod\", \"sourceFolders\": [\"src\", \"maps\"],}";
	{
		QFile file(projectManifestPath(broken));
		expect(file.open(QIODevice::WriteOnly) && file.write(brokenText) == brokenText.size(), "write a manifest with a trailing comma");
	}
	const auto manifestBytes = [&]
	{
		QFile file(projectManifestPath(broken));
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	};
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("targets"), QStringLiteral("--project"), broken, QStringLiteral("--save"), QStringLiteral("--format"),
				QStringLiteral("md3"), QStringLiteral("--target"), QStringLiteral("et")},
			   &json) == 1 &&
			   json.value(QStringLiteral("message")).toString().contains(QStringLiteral("could not be read")) && manifestBytes() == brokenText,
		   "saving into an unreadable manifest is refused and leaves it as it was");
	// The installation in use suggests targets by the same rule as the editor
	// and releases: a project of Quake II takes the selected Q2PRO installation.
	const auto q2Settings = QDir(directory).filePath(QStringLiteral("q2pro.ini"));
	GameInstallationProfile q2pro;
	q2pro.id = QStringLiteral("q2pro-test");
	q2pro.gameKey = QStringLiteral("quake2");
	q2pro.displayName = QStringLiteral("Q2PRO test");
	q2pro.rootPath = QDir(directory).filePath(QStringLiteral("q2pro"));
	q2pro.executablePath = QDir(q2pro.rootPath).filePath(QStringLiteral("q2pro.exe"));
	QDir().mkpath(q2pro.rootPath);
	{
		QFile file(q2pro.executablePath);
		expect(file.open(QIODevice::WriteOnly) && file.write("x") == 1, "write a stand-in engine");
	}
	{
		StudioSettings studio(q2Settings);
		studio.upsertGameInstallation(q2pro);
		studio.setSelectedGameInstallation(q2pro.id);
		studio.sync();
	}
	const auto q2Project = QDir(directory).filePath(QStringLiteral("q2project"));
	QDir().mkpath(q2Project);
	auto q2Manifest = defaultProjectManifest(q2Project);
	q2Manifest.gameKey = QStringLiteral("quake2");
	q2Manifest.selectedInstallationId.clear();
	expect(saveProjectManifest(q2Manifest, &error), "save a Quake II project that links no installation");
	expect(run(executable, q2Settings, {QStringLiteral("model"), QStringLiteral("targets"), QStringLiteral("--project"), q2Project, QStringLiteral("--format"), QStringLiteral("md2")},
			   &json) == 0 &&
			   json.value(QStringLiteral("delivery")).toArray().first().toObject().value(QStringLiteral("target")) == QStringLiteral("q2pro") &&
			   json.value(QStringLiteral("delivery")).toArray().first().toObject().value(QStringLiteral("targetOrigin")) == QStringLiteral("installation"),
		   "the CLI takes the selected installation of the project's game, as the editor does");
	QString name;
	expect(installationModelDeliveryPreferences(q2Manifest, {q2pro}, q2pro.id, &name).targets.value(QStringLiteral("md2")) == QStringLiteral("q2pro") &&
			   name == q2pro.displayName && installationModelDeliveryPreferences(ProjectManifest{}, {q2pro}, q2pro.id).targets.contains(QStringLiteral("md3")) &&
			   installationModelDeliveryPreferences(q2Manifest, {q2pro}, {}).targets.value(QStringLiteral("md2")) == QStringLiteral("q2pro") &&
			   installationModelDeliveryPreferences(ProjectManifest{}, {q2pro}, {}).isEmpty(),
		   "the shared rule: the project's game, else the selected installation, else none");
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir directory;
	if (!expect(directory.isValid(), "a temporary folder"))
		return EXIT_FAILURE;
	checkNewTargets(directory.path());
	checkExactElements();
	checkPrepare();
	checkPrepareKeepsSourceData();
	checkDelivery(directory.path());
	if (argc > 1)
		checkCli(QString::fromLocal8Bit(argv[1]), directory.path());
	if (failures > 0)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "model target preparation checks passed\n";
	return EXIT_SUCCESS;
}
